#include "../internal/NativeServices.h"
#if defined(LUI_CAPTURED_ONLY) || defined(__EMSCRIPTEN__) || defined(__ANDROID__) || (!defined(_WIN32) && !defined(__linux__))
uint32_t InstallWorkers(LuiRuntime*) { return LUI_NATIVE_UNSUPPORTED; }
#else
#include "../internal/Modules.h"
#include "../../native/ffi/Packets.h"
#include "lua.h"
#include "lualib.h"
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <future>
#include <nlohmann/json.hpp>

namespace {
using Clock=std::chrono::steady_clock;
constexpr LuiNativeIdV2 None{};
constexpr unsigned MiB=1024*1024;
std::atomic<unsigned> ProcessWorkers{0};

struct Message {
    LuiNativeIdV2 Request{};
    std::string Packet;
    LuiNativeIdV2 Output{};
    uint32_t OutputStatus=0;
    std::atomic<bool> Canceled{false};
    Clock::time_point Deadline;
};
struct Workers;
struct Actor {
    Workers& Service;
    const std::string Entry;
    const unsigned Memory, Interrupts, Milliseconds;
    const bool NativeFfi;
    std::condition_variable Wake;
    std::deque<std::shared_ptr<Message>> Queue;
    std::shared_ptr<Message> Current;
    size_t Bytes=0;
    std::atomic<bool> Closing{false};
    LuiNativeIdV2 Handle{};
    std::thread Thread;
    std::promise<void> Exit;
    std::future<void> Exited=Exit.get_future();
    Actor(Workers& Owner,std::string Id,unsigned Limit,unsigned Budget,unsigned Timeout,bool NativeFfi):
        Service(Owner),Entry(std::move(Id)),Memory(Limit),Interrupts(Budget),Milliseconds(Timeout),NativeFfi(NativeFfi) {}
    void Run() noexcept;
};
struct Workers {
    LuiNativeHostV2 Host{sizeof(LuiNativeHostV2),2};
    std::shared_ptr<const Lui::Project::CompiledSnapshot> Snapshot;
    WorkerFfiBinding Ffi;
    uint64_t ParentBytes, ParentInterrupts;
    std::mutex Mutex;
    std::vector<std::unique_ptr<Actor>> Actors;
    bool Closing=false;
    void Collect() {
        for(auto I=Actors.begin();I!=Actors.end();) {
            if((*I)->Exited.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
                (*I)->Thread.join();
                I=Actors.erase(I);
            } else ++I;
        }
    }
};
struct WorkerNativeSession {
    const WorkerFfiBinding& Binding;
    LuiExtensionMethodV1 Dispatch=nullptr;
    void* MethodContext=nullptr;
    void* Context=nullptr;
    bool InitializationAttempted=false;
    ~WorkerNativeSession() {
        if(InitializationAttempted) try {Binding.Shutdown(Context);} catch(...) {}
    }
    static int LUI_EXTENSION_CALL Register(void* Self,const char* Service,const char* Method,const char* Type,
        LuiExtensionMethodV1 Call,void* Value) {
        auto& Session=*static_cast<WorkerNativeSession*>(Self);
        if(Session.Dispatch||!Service||!Method||!Type||!Call||std::strcmp(Service,"Ffi")||
            std::strcmp(Method,"Dispatch")||std::strcmp(Type,"(Packet: string) -> string")) return 0;
        Session.Dispatch=Call;Session.MethodContext=Value;return 1;
    }
    bool Initialize() {
        LuiHostApiV1 Host{sizeof(Host),LUI_EXTENSION_ABI_VERSION,this,Register,
            [](void*,const char*){},[](void*,const char*){},[](void*,LuiUiCompletionV1,void*){return 0;},
            [](void*,const char*,const char*,const char*){return 0;},
            [](void*,const char*,const char*,const LuiValueV1*,uint32_t){return 0;}};
        InitializationAttempted=true;
        try {return Binding.Init(&Host,&Context)&&Dispatch;} catch(...) {return false;}
    }
};
struct Machine : LuiRuntime {
    Actor& Worker;
    Message* Active=nullptr;
    const char* Failure=nullptr;
    bool AllocationDenied=false;
    WorkerNativeSession Ffi;
    explicit Machine(Actor& Owner):Worker(Owner),Ffi{Owner.Service.Ffi} {}
    ~Machine() {if(State) lua_close(State);}
};
int DispatchFfi(lua_State* State) {
    CheckModuleEffect(State);
    auto& Vm=*static_cast<Machine*>(lua_callbacks(State)->userdata);
    size_t Size=0; const char* Packet=luaL_checklstring(State,2,&Size);
    if(Size>131072) luaL_error(State,"[LUI:Workers] PacketLimit");
    LuiValueV1 Input{sizeof(Input),LUI_VALUE_STRING,0,Packet,Size,0,0}, Output{sizeof(Output)};
    bool Dispatched=false;
    try {
        // Worker callbacks keep their own future contract. In particular,
        // notification polling cannot rely on catchable worker VM interrupts.
        bool Allowed=Lui::Ffi::IsWorkerBinaryPacket({Packet,Size});
        if(!Allowed) {
            auto Header=nlohmann::json::parse(Packet,Packet+Size,[&](int Depth,nlohmann::json::parse_event_t,nlohmann::json&) {
                if(Depth>24) throw std::invalid_argument("PacketDepth");
                return true;
            });
            if(Header.is_object()&&Header.contains("Op")&&Header["Op"].is_string()) {
                const auto& Method=Header["Op"].get_ref<const std::string&>();
                Allowed=Method!="Callback"&&Method!="Poll";
            }
        }
        if(Allowed) Dispatched=Vm.Ffi.Dispatch(Vm.Ffi.MethodContext,&Input,1,&Output)!=0;
    } catch(...) {}
    if(!Dispatched||Output.StructSize<sizeof(Output)||Output.Type!=LUI_VALUE_STRING||!Output.Text||Output.TextLength>131072)
        luaL_error(State,"[LUI:Workers] NativeDispatchFailed");
    lua_pushlstring(State,Output.Text,static_cast<size_t>(Output.TextLength)); return 1;
}
int WorkerService(lua_State* State) {
    CheckModuleEffect(State);
    size_t Size=0; const char* Name=luaL_checklstring(State,2,&Size);
    if(std::string_view(Name,Size)!="Ffi") luaL_error(State,"[LUI:Workers] CapabilityDenied");
    lua_pushvalue(State,lua_upvalueindex(1));return 1;
}
void* Allocate(void* Context,void* Pointer,size_t Old,size_t Size) {
    auto* Vm=static_cast<Machine*>(Context);
    auto* Result=VmAllocate(Vm,Pointer,Old,Size);
    if(Size && !Result) Vm->AllocationDenied=true;
    return Result;
}
void Interrupt(lua_State* State,int Gc) {
    if(Gc>=0) return;
    auto& Vm=*static_cast<Machine*>(lua_callbacks(State)->userdata);
    if(Vm.Worker.Closing || Vm.Active->Canceled) Vm.Failure="Canceled";
    else if(++Vm.InterruptCount>Vm.MaxInterrupts) Vm.Failure="LimitExceeded";
    else if(Clock::now()>=Vm.Active->Deadline) Vm.Failure="DeadlineExceeded";
    if(Vm.Failure) luaL_error(State,"[LUI:Workers] %s",Vm.Failure);
}
int Configure(lua_State* State) {
    auto& Vm=*static_cast<Machine*>(lua_callbacks(State)->userdata);
    luaL_openlibs(State);
    RegisterModules(State);
    // Remove ambient libraries, catch/yield paths and indirect access to the
    // original globals before freezing the single private VM's core tables.
    for(const char* Key:{"app","Instance","os","debug","coroutine","pcall","xpcall","getfenv","setfenv",
        "loadstring","newproxy","collectgarbage","gcinfo","print","warn","require","_G"}) {
        lua_pushnil(State);lua_setglobal(State,Key);
    }
    // Pattern backtracking inside the C library has no VM interrupt safepoint.
    lua_getglobal(State,"string");
    for(const char* Key:{"find","match","gmatch","gsub","split"}) {lua_pushnil(State);lua_setfield(State,-2,Key);}
    lua_pop(State,1);
    lua_getglobal(State,"table");lua_pushnil(State);lua_setfield(State,-2,"move");lua_pop(State,1);
    if(Vm.Worker.NativeFfi) {
        if(!Vm.Ffi.Initialize()) luaL_error(State,"[LUI:Workers] NativeInitializationFailed");
        lua_newtable(State);
        lua_newtable(State);
        lua_pushcfunction(State,DispatchFfi,"Dispatch");lua_setfield(State,-2,"Dispatch");
        lua_pushboolean(State,1);lua_setfield(State,-2,"CompactAggregates");
        lua_pushboolean(State,1);lua_setfield(State,-2,"CompactHandles");
        lua_setreadonly(State,-1,true);
        lua_pushcclosure(State,WorkerService,"GetService",1);lua_setfield(State,-2,"GetService");
        lua_setreadonly(State,-1,true);lua_setglobal(State,"app");
    }
    luaL_sandbox(State);
    luaL_sandboxthread(State);
    auto& Entry=Vm.Modules->Records.at(Vm.Worker.Entry);
    lua_pushlightuserdata(State,&Entry);
    // WorkerModuleEntry expects the sole argument at stack index one.
    lua_remove(State,1);
    return WorkerModuleEntry(State);
}
int Dispatch(lua_State* State) {
    auto& Vm=*static_cast<Machine*>(lua_callbacks(State)->userdata);
    auto& Call=*Vm.Active;
    lua_getref(State,Vm.Modules->Records.at(Vm.Worker.Entry).ExportReference);
    lua_pushlstring(State,Call.Packet.data(),Call.Packet.size());
    lua_call(State,1,1);
    if(lua_type(State,-1)!=LUA_TSTRING) luaL_error(State,"[LUI:Workers] InvalidResult");
    size_t Count=0;const char* Data=lua_tolstring(State,-1,&Count);
    if(Count<5 || Count>65536 || (std::memcmp(Data,"LUM1B",5) && std::memcmp(Data,"LUM1J",5)))
        luaL_error(State,"[LUI:Workers] InvalidPacket");
    auto& Host=Vm.Worker.Service.Host;
    Call.OutputStatus=Host.CopyBytes(Host.Session,Call.Request,Data,Count,&Call.Output);
    return 0;
}
void Actor::Run() noexcept {
    auto& Host=Service.Host;
    auto Complete=[&](const std::shared_ptr<Message>& Job,uint32_t Status,bool Start) {
        LuiNativeIdV2 Created{};
        if(!Status && !Start) Status=Job->OutputStatus;
        if(!Status && Start) {
            Status=Host.CreateHandle(Host.Session,1,&Created);
            if(!Status) {std::lock_guard<std::mutex> Lock(Service.Mutex);Handle=Created;}
        }
        const auto Settled=Host.Complete(Host.Session,Job->Request,Status,Status?None:Job->Output,Created);
        if(Job->Output.Value) {
            Host.ReleaseBytes(Host.Session,Job->Output);
            Job->Output=None;
        }
        if(Created.Value) Host.ReleaseHandle(Host.Session,Created);
        return Status ? Status : Settled;
    };
    try {
        Machine Vm(*this);
        Vm.VmLimitBytes=Memory;Vm.MaxInterrupts=Interrupts;
        Vm.Modules=std::make_shared<ModuleContext>();
        Vm.Modules->Snapshot=Service.Snapshot;
        Vm.Modules->Stack.reserve(64);
        for(const auto& Pair:Service.Snapshot->GetArtifacts()) {
            ModuleRecord Record;Record.Id=Pair.first;Record.Artifact=&Pair.second;
            Vm.Modules->Records.emplace(Pair.first,std::move(Record));
        }
        Vm.State=lua_newstate(Allocate,&Vm);
        bool Starting=true;
        for(;;) {
            std::shared_ptr<Message> Job;
            {
                std::unique_lock<std::mutex> Lock(Service.Mutex);
                Wake.wait(Lock,[&]{return Closing || !Queue.empty();});
                if(Closing) break;
                Job=Queue.front();Queue.pop_front();Current=Job;
            }
            uint32_t Status=0;
            Vm.Active=Job.get();Vm.Failure=nullptr;Vm.AllocationDenied=false;Vm.InterruptCount=0;
            if(!Vm.State) Status=LUI_NATIVE_LIMIT_EXCEEDED;
            else if(Job->Canceled) Status=LUI_NATIVE_CANCELED;
            else if(Clock::now()>=Job->Deadline) Status=LUI_NATIVE_DEADLINE_EXCEEDED;
            else {
                lua_callbacks(Vm.State)->userdata=&Vm;
                lua_callbacks(Vm.State)->interrupt=Interrupt;
                int Code=lua_cpcall(Vm.State,Starting?Configure:Dispatch,nullptr);
                lua_settop(Vm.State,0);
                if(Closing || Job->Canceled) Status=LUI_NATIVE_CANCELED;
                else if(Clock::now()>=Job->Deadline) Status=LUI_NATIVE_DEADLINE_EXCEEDED;
                else if(Code==LUA_ERRMEM || Vm.AllocationDenied || Vm.InterruptCount>Vm.MaxInterrupts) Status=LUI_NATIVE_LIMIT_EXCEEDED;
                else if(Code!=LUA_OK) Status=LUI_NATIVE_PROVIDER_FAILURE;
            }
            Status=Complete(Job,Status,Starting);
            {
                std::lock_guard<std::mutex> Lock(Service.Mutex);
                Bytes-=Job->Packet.size();Current.reset();
                if(Status) Closing=true;
            }
            if(Status) break;
            Starting=false;
        }
    } catch(...) {Closing=true;}
    std::deque<std::shared_ptr<Message>> Remaining;
    std::shared_ptr<Message> Interrupted;
    {
        std::lock_guard<std::mutex> Lock(Service.Mutex);
        Closing=true;Remaining.swap(Queue);Interrupted=std::move(Current);
    }
    if(Interrupted) Complete(Interrupted,LUI_NATIVE_PROVIDER_FAILURE,false);
    for(auto& Job:Remaining) Complete(Job,LUI_NATIVE_CLOSED,false);
    --ProcessWorkers;
    Exit.set_value_at_thread_exit();
}
unsigned Word(const unsigned char* Data) {
    return unsigned(Data[0])|(unsigned(Data[1])<<8)|(unsigned(Data[2])<<16)|(unsigned(Data[3])<<24);
}
uint32_t Admit(Workers& Owner,LuiNativeIdV2 Request,unsigned Method,const void* Input,size_t Size,LuiNativeIdV2 Handle) {
    auto Job=std::make_shared<Message>();Job->Request=Request;
    std::unique_lock<std::mutex> Lock(Owner.Mutex);
    if(Owner.Closing) return LUI_NATIVE_CLOSED;
    Owner.Collect();
    if(Method==0||Method==2) {
        if(Method==2&&!Owner.Ffi.Image) return LUI_NATIVE_CAPABILITY_DENIED;
        if(Size<=12 || Size>268) return LUI_NATIVE_INVALID_ARGUMENT;
        const auto* Data=static_cast<const unsigned char*>(Input);
        unsigned Memory=Word(Data),Budget=Word(Data+4),Timeout=Word(Data+8);
        if(Memory<MiB || Memory>16*MiB || !Budget || Budget>100000000 || !Timeout || Timeout>30000) return LUI_NATIVE_INVALID_ARGUMENT;
        Memory=static_cast<unsigned>(std::min<uint64_t>(Memory,Owner.ParentBytes));
        Budget=static_cast<unsigned>(std::min<uint64_t>(Budget,Owner.ParentInterrupts));
        const std::string_view Path(reinterpret_cast<const char*>(Data+12),Size-12);
        if(Path.size()<3 || Path.substr(0,2)!="./") return LUI_NATIVE_INVALID_ARGUMENT;
        const auto& Sources=Owner.Snapshot->GetSources();
        std::string Error;
        const auto* Source=Sources.Resolve(Sources.GetEntry(),Path,Error);
        if(!Source || Source->Owner!=Sources.GetSource(Sources.GetEntry())->Owner) return LUI_NATIVE_INVALID_ARGUMENT;
        if(Owner.Actors.size()>=4) return LUI_NATIVE_LIMIT_EXCEEDED;
        auto ActorPtr=std::make_unique<Actor>(Owner,Source->Id,Memory,Budget,Timeout,Method==2);
        Job->Deadline=Clock::now()+std::chrono::milliseconds(Timeout);ActorPtr->Queue.push_back(Job);
        if(ProcessWorkers.fetch_add(1)>=8) {--ProcessWorkers;return LUI_NATIVE_LIMIT_EXCEEDED;}
        try {ActorPtr->Thread=std::thread(&Actor::Run,ActorPtr.get());}
        catch(...) {--ProcessWorkers;throw;}
        Owner.Actors.push_back(std::move(ActorPtr)); // capacity reserved before registration
    } else {
        auto I=std::find_if(Owner.Actors.begin(),Owner.Actors.end(),[&](auto& A){return A->Handle.Value==Handle.Value;});
        if(I==Owner.Actors.end() || (*I)->Closing) return LUI_NATIVE_CLOSED;
        auto& A=**I;
        if(Size<5 || Size>65536) return LUI_NATIVE_INVALID_ARGUMENT;
        if(A.Queue.size()+(A.Current?1:0)>=8 || Size>262144-A.Bytes) return LUI_NATIVE_WOULD_BLOCK;
        Job->Packet.assign(static_cast<const char*>(Input),Size);
        Job->Deadline=Clock::now()+std::chrono::milliseconds(A.Milliseconds);
        A.Queue.push_back(Job);A.Bytes+=Size;A.Wake.notify_one();
    }
    return 0;
}
void LUI_CALL Start(void* Context,LuiNativeIdV2 Request,uint32_t Method,LuiNativeIdV2 Input,LuiNativeIdV2 Handle) {
    auto& Owner=*static_cast<Workers*>(Context);
    const void* Data=nullptr;uint64_t Size=0;
    auto Status=Owner.Host.ViewBytes(Owner.Host.Session,Input,&Data,&Size);
    if(!Status) try {Status=Admit(Owner,Request,Method,Data,static_cast<size_t>(Size),Handle);}
    catch(...) {Status=LUI_NATIVE_LIMIT_EXCEEDED;}
    if(Status) Owner.Host.Complete(Owner.Host.Session,Request,Status,None,None);
}
void LUI_CALL Cancel(void* Context,LuiNativeIdV2 Request) {
    auto& Owner=*static_cast<Workers*>(Context);
    std::lock_guard<std::mutex> Lock(Owner.Mutex);
    for(auto& A:Owner.Actors) {
        if(A->Current && A->Current->Request.Value==Request.Value) A->Current->Canceled=true;
        for(auto& Job:A->Queue) if(Job->Request.Value==Request.Value) Job->Canceled=true;
        A->Wake.notify_one();
    }
}
void LUI_CALL Close(void* Context,LuiNativeIdV2 Handle,uint32_t) {
    auto& Owner=*static_cast<Workers*>(Context);std::lock_guard<std::mutex> Lock(Owner.Mutex);
    for(auto& A:Owner.Actors) if(A->Handle.Value==Handle.Value) {A->Closing=true;A->Wake.notify_one();}
}
void LUI_CALL Shutdown(void* Context) {
    auto& Owner=*static_cast<Workers*>(Context);std::lock_guard<std::mutex> Lock(Owner.Mutex);
    Owner.Closing=true;
    for(auto& A:Owner.Actors) {A->Closing=true;A->Wake.notify_one();}
}
uint32_t LUI_CALL Poll(void* Context) {
    auto& Owner=*static_cast<Workers*>(Context);std::lock_guard<std::mutex> Lock(Owner.Mutex);
    Owner.Collect();return Owner.Actors.empty();
}
void LUI_CALL Destroy(void* Context) {delete static_cast<Workers*>(Context);}
const LuiNativeMethodV2 Methods[]={
    {sizeof(LuiNativeMethodV2),2,"Start",268,0,0,1,31000,0},
    {sizeof(LuiNativeMethodV2),2,"Send",65536,65536,1,0,31000,0},
    {sizeof(LuiNativeMethodV2),2,"StartTrusted",268,0,0,1,31000,0}};
const LuiNativeServiceV2 Service{sizeof(LuiNativeServiceV2),2,"WorkerService",Methods,3,1,0,1,0};
const LuiNativeProviderV2 Provider{sizeof(LuiNativeProviderV2),2,Start,Cancel,Close,Shutdown,Poll,Destroy};
}

uint32_t InstallWorkers(LuiRuntime* Runtime) {
    if(!Runtime->Modules) return LUI_NATIVE_CAPABILITY_DENIED;
    try {
        const auto& Sources=Runtime->Modules->Snapshot->GetSources();
        const auto& Requires=Sources.GetOwners().at(Sources.GetSource(Sources.GetEntry())->Owner).Requires;
        if(std::find(Requires.begin(),Requires.end(),"Workers")==Requires.end()) return LUI_NATIVE_CAPABILITY_DENIED;
        auto Owner=std::make_unique<Workers>();
        Owner->Snapshot=Runtime->Modules->Snapshot;
        if(Runtime->ProjectNativeVerified&&Runtime->VerifiedProjectDigest==Owner->Snapshot->GetDigest())
            Owner->Ffi=Runtime->TrustedWorkerFfi;
        Owner->ParentBytes=Runtime->VmLimitBytes;Owner->ParentInterrupts=Runtime->MaxInterrupts;
        Owner->Actors.reserve(4);
        auto Status=RegisterWorkerService(Runtime,&Service,&Provider,Owner.get(),&Owner->Host);
        if(!Status) Owner.release();
        return Status;
    } catch(...) {return LUI_NATIVE_LIMIT_EXCEEDED;}
}
#endif
