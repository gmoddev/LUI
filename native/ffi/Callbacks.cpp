#include "Callbacks.h"
#include <atomic>
#include <cerrno>
#include <cstring>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace Lui::Ffi {
namespace {
thread_local Callbacks::Scope* CurrentScope=nullptr;
struct ComputeGuard {
    Callbacks::Scope& Scope;
    int Error=errno;
#ifdef _WIN32
    DWORD SystemError=GetLastError();
#endif
    explicit ComputeGuard(Callbacks::Scope& Scope):Scope(Scope){Scope.Invoking=true;}
    ~ComputeGuard(){
        Scope.Invoking=false;
        errno=Error;
#ifdef _WIN32
        SetLastError(SystemError);
#endif
    }
};
}
Callbacks::Scope::Scope(Engine& Service,LuiFfiComputeV1 Compute,void* Context)
    :Service(Service),Compute(Compute),Context(Context),Previous(CurrentScope){CurrentScope=this;}
Callbacks::Scope::~Scope(){CurrentScope=Previous;}

struct Callbacks::Entry {
    unsigned Id=0;
    std::shared_ptr<Signature> Sig;
    std::shared_ptr<Ownership> Owner;
    std::weak_ptr<Mailbox> Mail;
    std::atomic<bool> Active{true};
    Engine* ComputedFor=nullptr;
    ffi_closure* Closure=nullptr;
    void* Code=nullptr;
    Slot Fallback;
    ~Entry(){if(Closure)ffi_closure_free(Closure);}
    void Compute(void* Out,void** Args,size_t Size){
        auto* Scope=CurrentScope;
        if(!Scope || &Scope->Service!=ComputedFor || Scope->Invoking)return;
        ComputeGuard Guard(*Scope);
        Json Values=Json::array();
        for(size_t I=0;I<Sig->Parameters.size();++I)
            Values.push_back(Scope->Service.Decode(Sig->Parameters[I],Args[I],{}));
        const auto Packet=Json{{"Id",Id},{"Args",std::move(Values)}}.dump();
        char Reply[128];
        size_t Length=0;
        if(!Scope->Compute(Scope->Context,Packet.data(),Packet.size(),Reply,sizeof(Reply),&Length) ||
            !Length || Length>sizeof(Reply) || !Size)return;
        Slot Result;
        Held Pins;
        Scope->Service.Encode(Sig->Result,Json::parse(Reply,Reply+Length),Result.Data.data(),Pins);
        std::memcpy(Out,Result.Data.data(),Size);
    }
    static void Invoke(ffi_cif*,void* Out,void** Args,void* User) noexcept {
        auto& Self=*static_cast<Entry*>(User);
        size_t Size=0;
        if(Self.Sig->Result->Name!="void") {
            // libffi closures widen narrow integer results to a native word.
            Size=Self.Sig->Result->Abi->size;
            const auto Kind=Self.Sig->Result->Abi->type;
            if(Kind!=FFI_TYPE_FLOAT && Kind!=FFI_TYPE_DOUBLE && Size<sizeof(ffi_arg)) Size=sizeof(ffi_arg);
            std::memcpy(Out,Self.Fallback.Data.data(),Size);
        }
        if(!Self.Active.load())return;
        try{
            if(Self.ComputedFor){Self.Compute(Out,Args,Size);return;}
            auto M=Self.Mail.lock(); if(!M)return;
            std::lock_guard<std::mutex> Guard(M->Mutex);
            if(!Self.Active.load())return;
            if(M->Pending.size()>=64){++M->Dropped;return;}
            Event Item{Self.Id,{}};
            for(size_t I=0;I<Self.Sig->Parameters.size();++I){
                auto Bytes=static_cast<const unsigned char*>(Args[I]);
                Item.Values.emplace_back(Bytes,Bytes+Self.Sig->Parameters[I]->Abi->size);
            }
            M->Pending.push_back(std::move(Item));
        }catch(...){/* No exception may cross a foreign stack. */}
    }
};
Callbacks::~Callbacks(){for(auto& Item:Entries)Item.second->Active=false;}
Json Callbacks::Create(Engine& E,const Json& Req){
    auto Sig=E.Declare(Req.at("Params"),Req.at("Result"));
    size_t Total=0;
    for(auto& P:Sig->Parameters){
        Require(!P->HasPointers,"CallbackPointerUnsupported");
        Total+=P->Abi->size;
    }
    Require(Total<=1024 && !Sig->Result->HasPointers && Sig->Result->Children.empty(),"CallbackTypeUnsupported");
    auto Item=std::make_unique<Entry>(); Item->Sig=Sig;Item->Mail=Mail;
    if(Req.value("Computed",false))Item->ComputedFor=&E;
    if(Sig->Result->Name!="void"){
        Held Pins; E.Encode(Sig->Result,Req.at("Fallback"),Item->Fallback.Data.data(),Pins);
    }
    PinCallbackModule();
    static std::mutex Mutex;
    static unsigned Slots=0;
    std::lock_guard<std::mutex> Lock(Mutex);
    Require(Slots<128,"CallbackLifetimeLimit");
    Item->Closure=static_cast<ffi_closure*>(ffi_closure_alloc(sizeof(ffi_closure),&Item->Code));
    Require(Item->Closure && Item->Code,"ClosureUnavailable");
    Require(ffi_prep_closure_loc(Item->Closure,&Sig->Cif,Entry::Invoke,Item.get(),Item->Code)==FFI_OK,"ClosureUnavailable");
    auto P=E.ManagedPointer(Item->Code,0,{});
    Item->Owner=P->Owner;
    Item->Id=E.Add(P);
    Json Result={{"Ref",Item->Id}};
    Entries.emplace(Item->Id,Item.get());
    ++Slots;
    // A retained C callback may arrive after unregister, VM destruction or module shutdown.
    // Pin its immutable signature, closure and fallback until process exit. Retired slots discard notifications.
    Item.release();
    return Result;
}
bool Callbacks::Retire(unsigned Id){
    auto It=Entries.find(Id);if(It==Entries.end())return false;
    It->second->Active.store(false);
    Entries.erase(It);return true;
}
Json Callbacks::Poll(Engine& E){
    std::deque<Event> Pending;uint64_t Lost;
    {std::lock_guard<std::mutex> Lock(Mail->Mutex);Pending.swap(Mail->Pending);Lost=Mail->Dropped;Mail->Dropped=0;}
    Json Events=Json::array();
    for(const auto& Item:Pending){
        auto It=Entries.find(Item.Id);if(It==Entries.end())continue;
        Json Values=Json::array();
        for(size_t I=0;I<Item.Values.size();++I)Values.push_back(E.Decode(It->second->Sig->Parameters[I],Item.Values[I].data(),{}));
        Events.push_back({{"Id",Item.Id},{"Args",std::move(Values)}});
    }
    return {{"Events",std::move(Events)},{"Dropped",Lost}};
}
}
