#include "SelectedFiles.h"
#include "Domain.h"
#include "LuiFiles.h"
#include "../internal/NativeServices.h"
#include "../internal/Dispatch.h"
#include "../project/Files.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <map>

namespace {
constexpr LuiNativeIdV2 None{};
struct Files;
struct Job {
    Files* Owner=nullptr;
    LuiNativeIdV2 Request{},Input{},Handle{};
    uint32_t Method=0;
    uint64_t Position=0;
    uint32_t Count=0;
    bool SessionHeld=false,InputHeld=false,HandleHeld=false,Waiting=false;
    bool WriteOnly=false,WritableFolder=false;
    std::atomic<bool> Canceled{false};
    std::string Name,Path,Destination;
    std::shared_ptr<Lui::Native::SelectedFile> File;
    std::shared_ptr<Lui::Native::SelectedWriter> Writer;
    ~Job();
};
struct Files {
    LuiNativeHostV2 Host{sizeof(LuiNativeHostV2),2};
    LuiFilePickerCallbacksV1 Picker{};
    std::shared_ptr<Lui::Native::SelectionScope> Scope=std::make_shared<Lui::Native::SelectionScope>();
    std::mutex Mutex;
    std::condition_variable Ready;
    std::map<uint64_t,std::shared_ptr<Lui::Native::SelectedFile>> Handles;
    std::map<uint64_t,std::shared_ptr<Lui::Native::SelectedWriter>> Writers, RetiredWriters;
    std::map<uint64_t,std::shared_ptr<Job>> Requests;
    std::deque<std::shared_ptr<Job>> Queue;
    bool Closing=false;
    std::thread Worker;
    std::promise<void> Exit;
    std::future<void> Exited=Exit.get_future();
    void Work(const std::shared_ptr<Job>& Value) noexcept {
        LuiNativeIdV2 Result{},Handle{}; uint32_t Status=0;
        try {
            if(Value->Canceled.load()) Lui::Project::Internal::Fail("Canceled");
            std::shared_ptr<Lui::Native::SelectedFile> Opened; std::string Bytes;
            auto Canceled=[&] {
                return Value->Canceled.load() || !Lui::Native::RequestRunning(Host.Session,Value->Request) ||
                    !Lui::Native::HandleOpen(Host.Session,Value->Handle);
            };
            if(Value->Method<=2) {
                Opened=std::make_shared<Lui::Native::SelectedFile>(std::filesystem::u8path(Value->Path),Value->Method==2,Value->Method==1 || Value->WritableFolder,Scope);
                if(Value->WriteOnly) Opened->RestrictToWriting();
                Bytes=Opened->Name();
            } else if(Value->Method==3) Bytes=Value->File->Read([&]{return Value->Canceled.load();});
            else if(Value->Method==4) {
                const void* Input=nullptr; uint64_t Length=0;
                Status=Host.ViewBytes(Host.Session,Value->Input,&Input,&Length);
                if(!Status) Value->File->Write(std::string_view(Input?static_cast<const char*>(Input):"",static_cast<size_t>(Length)),[&]{return Value->Canceled.load();});
            } else if(Value->Method==5) {
                for(const auto& Entry:Value->File->List([&]{return Value->Canceled.load();}))
                    Bytes+=Entry.Name+"\t"+(Entry.Directory?"Folder":"File")+"\t"+std::to_string(Entry.Size)+"\n";
            } else if(Value->Method==6) {Opened=Value->File->Open(Value->Name); Bytes=Opened->Name();}
            else if(Value->Method==7) Bytes=Value->File->ReadRange(Value->Position,Value->Count,[&]{return Value->Canceled.load();});
            else if(Value->Method==8) Bytes=std::to_string(Value->File->ByteLength());
            else if(Value->Method==9) {
                auto Writer=Value->File->BeginWriter();
                std::lock_guard<std::mutex> Lock(Mutex);
                if(Closing || Canceled()) Status=LUI_NATIVE_CANCELED;
                else if(Writers.size()>=4 || Writers.size()+Handles.size()>=32) Status=LUI_NATIVE_LIMIT_EXCEEDED;
                else {
                    Status=Host.CreateHandle(Host.Session,3,&Handle);
                    if(!Status) Writers.emplace(Handle.Value,std::move(Writer));
                }
            } else if(Value->Method==10) {
                const void* Input=nullptr; uint64_t Length=0;
                Status=Host.ViewBytes(Host.Session,Value->Input,&Input,&Length);
                if(!Status) Value->Writer->WriteAt(Value->Position,{static_cast<const char*>(Input)+8,static_cast<size_t>(Length)-8},Canceled);
            } else if(Value->Method==11) Value->Writer->Commit(Canceled);
            else if(Value->Method==12) {Opened=Value->File->OpenOutput(Value->Name); Bytes=Opened->Name();}
            else if(Value->Method>=13 && Value->Method<=16) {
                using Change=Lui::Native::DirectoryChange;
                const Change Actions[]={Change::CreateFolder,Change::RemoveFolder,Change::RemoveFile,Change::Rename};
                Value->File->Change(Actions[Value->Method-13],Value->Name,Value->Destination,Canceled);
            } else Status=LUI_NATIVE_INVALID_ARGUMENT;
            if(!Status && Opened) {
                std::lock_guard<std::mutex> Lock(Mutex);
                if(Closing || Value->Canceled.load()) Status=LUI_NATIVE_CANCELED;
                else if(Handles.size()+Writers.size()>=32) Status=LUI_NATIVE_LIMIT_EXCEEDED;
                else {
                    Status=Host.CreateHandle(Host.Session,Value->Method==2?2:1,&Handle);
                    if(!Status) Handles.emplace(Handle.Value,std::move(Opened));
                }
            }
            if(!Status && Value->Method!=4 && (Value->Method<9 || Value->Method==12)) Status=Host.CopyBytes(Host.Session,Value->Request,Bytes.data(),Bytes.size(),&Result);
        } catch(const Lui::Project::Internal::Failure& Error) {
            const std::string_view Code=Error.what();
            Status=Code=="Canceled"?LUI_NATIVE_CANCELED:Code=="LimitExceeded"?LUI_NATIVE_LIMIT_EXCEEDED:
                Code=="Closed"?LUI_NATIVE_CLOSED:Code=="InvalidArgument"?LUI_NATIVE_INVALID_ARGUMENT:Code=="CapabilityDenied"?LUI_NATIVE_CAPABILITY_DENIED:LUI_NATIVE_PROVIDER_FAILURE;
        } catch(const std::bad_alloc&) {Status=LUI_NATIVE_LIMIT_EXCEEDED;}
        catch(...) {Status=LUI_NATIVE_PROVIDER_FAILURE;}
        Host.Complete(Host.Session,Value->Request,Status,Status?None:Result,Status?None:Handle);
        if(Result.Value) Host.ReleaseBytes(Host.Session,Result);
        if(Handle.Value) Host.ReleaseHandle(Host.Session,Handle);
    }
    void Run() noexcept {
        while(true) {
            std::shared_ptr<Job> Value;
            {
                std::unique_lock<std::mutex> Lock(Mutex); Ready.wait(Lock,[&]{return Closing || !Queue.empty() || !RetiredWriters.empty();});
                if(!RetiredWriters.empty()) {
                    decltype(RetiredWriters) Retired;
                    Retired.swap(RetiredWriters);
                    Lock.unlock();
                    continue;
                }
                if(Queue.empty() && Closing) break;
                Value=std::move(Queue.front()); Queue.pop_front();
            }
            Work(Value);
            {std::lock_guard<std::mutex> Lock(Mutex); Requests.erase(Value->Request.Value);}
        }
        Exit.set_value_at_thread_exit();
    }
};
Job::~Job() {
    if(HandleHeld) Owner->Host.ReleaseHandle(Owner->Host.Session,Handle);
    if(InputHeld) Owner->Host.ReleaseBytes(Owner->Host.Session,Input);
    if(SessionHeld) Owner->Host.ReleaseSession(Owner->Host.Session);
}
void LUI_CALL Start(void* Pointer,LuiNativeIdV2 Request,uint32_t Method,LuiNativeIdV2 Input,LuiNativeIdV2 Handle) {
    auto& Owner=*static_cast<Files*>(Pointer); uint32_t Status=0;
    try {
        auto Value=std::make_shared<Job>(); Value->Owner=&Owner; Value->Request=Request; Value->Input=Input; Value->Handle=Handle; Value->Method=Method;
        const void* Bytes=nullptr; uint64_t Length=0;
        Status=Owner.Host.ViewBytes(Owner.Host.Session,Input,&Bytes,&Length);
        if(!Status && (Method==7 || Method==10)) {
            if(Method==7 ? Length!=12 : Length<8 || Length>65544) Status=LUI_NATIVE_INVALID_ARGUMENT;
            else {
                const auto* Data=static_cast<const uint8_t*>(Bytes);
                for(unsigned Index=0;Index<8;++Index) Value->Position |= uint64_t(Data[Index])<<(8*Index);
                if(Method==7) {
                    for(unsigned Index=0;Index<4;++Index) Value->Count |= uint32_t(Data[8+Index])<<(8*Index);
                    if(Value->Position>9007199254740991ULL || !Value->Count || Value->Count>65536) Status=LUI_NATIVE_INVALID_ARGUMENT;
                }
            }
        }
        if(!Status && Method==16) {
            const auto Packet=std::string_view(Bytes?static_cast<const char*>(Bytes):"",static_cast<size_t>(Length));
            const auto End=Packet.find(char(0));
            if(End==Packet.npos || !Lui::Native::SelectedName(Packet.substr(0,End)) || !Lui::Native::SelectedName(Packet.substr(End+1)))
                Status=LUI_NATIVE_INVALID_ARGUMENT;
            else {Value->Name=Packet.substr(0,End); Value->Destination=Packet.substr(End+1);}
        }
        if(!Status && (Method==1 || Method==6 || (Method>=12 && Method<=15))) {
            if(Length) Value->Name.assign(static_cast<const char*>(Bytes),static_cast<size_t>(Length));
            if(!Lui::Native::SelectedName(Value->Name)) Status=LUI_NATIVE_INVALID_ARGUMENT;
        }
        if(!Status && (Method==10 || Method==11)) {
            std::lock_guard<std::mutex> Lock(Owner.Mutex); const auto Found=Owner.Writers.find(Handle.Value);
            if(Found==Owner.Writers.end()) Status=LUI_NATIVE_CLOSED; else Value->Writer=Found->second;
        } else if(!Status && Method>=3) {
            std::lock_guard<std::mutex> Lock(Owner.Mutex); const auto Found=Owner.Handles.find(Handle.Value);
            if(Found==Owner.Handles.end()) Status=LUI_NATIVE_CLOSED; else Value->File=Found->second;
        }
        if(!Status) {Status=Owner.Host.RetainSession(Owner.Host.Session); Value->SessionHeld=!Status;}
        if(!Status) {Status=Owner.Host.RetainBytes(Owner.Host.Session,Input); Value->InputHeld=!Status;}
        if(!Status && Handle.Value) {Status=Owner.Host.RetainHandle(Owner.Host.Session,Handle); Value->HandleHeld=!Status;}
        if(!Status) {
            std::lock_guard<std::mutex> Lock(Owner.Mutex);
            if(Owner.Closing) Status=LUI_NATIVE_CLOSED;
            else if(Owner.Requests.size()>=32) Status=LUI_NATIVE_LIMIT_EXCEEDED;
            else {
                Value->Waiting=Method<=2; Owner.Requests.emplace(Request.Value,Value);
                if(!Value->Waiting) try {Owner.Queue.push_back(Value);} catch(...) {Owner.Requests.erase(Request.Value); throw;}
            }
        }
        if(!Status && Value->Waiting) {
            try {Status=Owner.Picker.StartPicker(Owner.Picker.Context,Request.Value,Method+1,Value->Name.c_str());}
            catch(...) {Status=LUI_NATIVE_PROVIDER_FAILURE;}
            if(Status>LUI_NATIVE_PROVIDER_FAILURE) Status=LUI_NATIVE_PROVIDER_FAILURE;
            if(Status) {std::lock_guard<std::mutex> Lock(Owner.Mutex); Owner.Requests.erase(Request.Value);}
        } else if(!Status) Owner.Ready.notify_one();
    } catch(...) {Status=LUI_NATIVE_LIMIT_EXCEEDED;}
    if(Status) Owner.Host.Complete(Owner.Host.Session,Request,Status,None,None);
}
void LUI_CALL Cancel(void* Pointer,LuiNativeIdV2 Request) {
    auto& Owner=*static_cast<Files*>(Pointer); std::shared_ptr<Job> Waiting;
    {
        std::lock_guard<std::mutex> Lock(Owner.Mutex); const auto Found=Owner.Requests.find(Request.Value);
        if(Found==Owner.Requests.end()) return;
        Found->second->Canceled.store(true);
        if(Found->second->Waiting) {Waiting=Found->second; Owner.Requests.erase(Found);}
    }
    if(Waiting) try {Owner.Picker.CancelPicker(Owner.Picker.Context,Request.Value);} catch(...) {}
}
void LUI_CALL Close(void* Pointer,LuiNativeIdV2 Handle,uint32_t) {
    auto& Owner=*static_cast<Files*>(Pointer); std::lock_guard<std::mutex> Lock(Owner.Mutex); Owner.Handles.erase(Handle.Value);
    auto Writer=Owner.Writers.extract(Handle.Value);
    if(!Writer.empty()) {Owner.RetiredWriters.insert(std::move(Writer)); Owner.Ready.notify_one();}
}
void LUI_CALL Shutdown(void* Pointer) {
    auto& Owner=*static_cast<Files*>(Pointer); std::map<uint64_t,std::shared_ptr<Job>> Pending;
    {
        std::lock_guard<std::mutex> Lock(Owner.Mutex); Owner.Closing=true;
        Owner.RetiredWriters.merge(Owner.Writers);
        Pending.swap(Owner.Requests);
        for(const auto& Pair:Pending) Pair.second->Canceled.store(true);
    }
    for(const auto& Pair:Pending) if(Pair.second->Waiting)
        try {Owner.Picker.CancelPicker(Owner.Picker.Context,Pair.first);} catch(...) {}
    Owner.Ready.notify_one();
}
uint32_t LUI_CALL Poll(void* Pointer) {
    auto& Owner=*static_cast<Files*>(Pointer);
    if(!Owner.Worker.joinable()) return 1;
    if(Owner.Exited.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return 0;
    Owner.Worker.join(); return 1;
}
void LUI_CALL Destroy(void* Pointer) {delete static_cast<Files*>(Pointer);}
const LuiNativeMethodV2 Methods[]={
    {sizeof(LuiNativeMethodV2),2,"OpenFile",0,255,0,1,120000,0},
    {sizeof(LuiNativeMethodV2),2,"SaveFile",255,255,0,1,120000,0},
    {sizeof(LuiNativeMethodV2),2,"OpenFolder",0,255,0,2,120000,0},
    {sizeof(LuiNativeMethodV2),2,"Read",0,4*1024*1024,1,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"Write",4*1024*1024,0,1,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"List",0,128*1024,2,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"Open",255,255,2,1,30000,0},
    {sizeof(LuiNativeMethodV2),2,"ReadRange",12,65536,1,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"ByteLength",0,20,1,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"BeginWriter",0,0,1,3,30000,0},
    {sizeof(LuiNativeMethodV2),2,"WriteAt",65544,0,3,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"CommitWriter",0,0,3,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"OpenOutput",255,255,2,1,30000,0},
    {sizeof(LuiNativeMethodV2),2,"CreateDirectory",255,0,2,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"RemoveDirectory",255,0,2,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"RemoveFile",255,0,2,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"Rename",511,0,2,0,30000,0}
};
const LuiNativeServiceV2 Service{sizeof(LuiNativeServiceV2),2,"FileService",Methods,17,3,LUI_CAPABILITY_DIALOGS,1,0};
const LuiNativeProviderV2 Provider{sizeof(LuiNativeProviderV2),2,Start,Cancel,Close,Shutdown,Poll,Destroy};
}
extern "C" LUI_API uint32_t LUI_CALL Lui_InstallFilesV1(LuiRuntime* Runtime,const LuiFilePickerCallbacksV1* Callbacks) {
    if(!CheckOwner(Runtime) || !Callbacks || Callbacks->StructSize!=sizeof(*Callbacks) || Callbacks->AbiVersion!=1 ||
        !Callbacks->StartPicker || !Callbacks->CancelPicker || Runtime->ApplicationStarted || Runtime->ProjectNativeVerified ||
        Runtime->VmDepth || Runtime->BackendDepth || Runtime->ShuttingDown || Runtime->FileServiceContext) return LUI_NATIVE_INVALID_ARGUMENT;
    if(!(Runtime->GrantedCapabilities&LUI_CAPABILITY_DIALOGS)) return LUI_NATIVE_CAPABILITY_DENIED;
    try {
        auto Owner=std::make_unique<Files>(); Owner->Picker=*Callbacks;
        const auto Status=RegisterPlatformNativeService(Runtime,&Service,&Provider,Owner.get(),&Owner->Host);
        if(Status) return Status;
        auto* Context=Owner.release();
        try {Context->Worker=std::thread([Context]{Context->Run();});}
        catch(...) {
            Lui_RetireNativeSessionV2(Context->Host.Session); Runtime->NativeServices.pop_back(); Lui::Native::Pump(Runtime->NativeDomain); return LUI_NATIVE_LIMIT_EXCEEDED;
        }
        Runtime->FileServiceContext=Context; return 0;
    } catch(...) {return LUI_NATIVE_LIMIT_EXCEEDED;}
}
static uint32_t CompleteFilePicker(LuiRuntime* Runtime,uint64_t Request,const char* Path,uint32_t Status,bool WriteOnly,bool WritableFolder=false) {
    if(!CheckOwner(Runtime) || Runtime->ShuttingDown || !Runtime->FileServiceContext || Status>LUI_NATIVE_PROVIDER_FAILURE) return LUI_NATIVE_INVALID_ARGUMENT;
    auto& Owner=*static_cast<Files*>(Runtime->FileServiceContext); std::shared_ptr<Job> Value;
    try {
        std::lock_guard<std::mutex> Lock(Owner.Mutex); const auto Found=Owner.Requests.find(Request);
        if(Owner.Closing || Found==Owner.Requests.end() || !Found->second->Waiting) return LUI_NATIVE_CLOSED;
        Value=Found->second;
        if((WriteOnly && Value->Method!=1) || (WritableFolder && Value->Method!=2)) return LUI_NATIVE_INVALID_ARGUMENT;
        Value->WriteOnly=WriteOnly;Value->WritableFolder=WritableFolder;
        if(!Status && !Path) Status=LUI_NATIVE_CANCELED;
        if(!Status && Path) {
            size_t Length=0; while(Length<=32768 && Path[Length]) ++Length;
            if(!Length || Length>32768 || !Lui::Project::Internal::Utf8(std::string_view(Path,Length))) Status=LUI_NATIVE_INVALID_ARGUMENT;
            else {
                Value->Path.assign(Path,Length); Owner.Queue.push_back(Value); Value->Waiting=false;
                Owner.Ready.notify_one(); return 0;
            }
        }
        Owner.Requests.erase(Found);
    } catch(...) {
        Status=LUI_NATIVE_LIMIT_EXCEEDED;
        std::lock_guard<std::mutex> Lock(Owner.Mutex); Owner.Requests.erase(Request);
    }
    if(Value) Owner.Host.Complete(Owner.Host.Session,Value->Request,Status,None,None);
    return Status;
}
extern "C" LUI_API uint32_t LUI_CALL Lui_CompleteFilePickerV1(LuiRuntime* Runtime,uint64_t Request,const char* Path,uint32_t Status) {
    return CompleteFilePicker(Runtime,Request,Path,Status,false);
}
extern "C" LUI_API uint32_t LUI_CALL Lui_CompleteWriteOnlyPickerV1(LuiRuntime* Runtime,uint64_t Request,const char* Path) {
    return CompleteFilePicker(Runtime,Request,Path,0,true);
}
extern "C" LUI_API uint32_t LUI_CALL Lui_CompleteWritableFolderV1(LuiRuntime* Runtime,uint64_t Request,const char* Path) {
    return CompleteFilePicker(Runtime,Request,Path,0,false,true);
}
