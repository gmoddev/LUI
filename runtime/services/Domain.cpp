#include "Domain.h"
#include "LuiOwnerWake.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <map>
#include <mutex>
#include <thread>
#include <string_view>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Lui::Native {
constexpr uint64_t DomainBytes = 64 * 1024 * 1024, SessionBytes = 16 * 1024 * 1024;
constexpr uint64_t ValueBytes = 8 * 1024 * 1024;
using Clock = std::chrono::steady_clock;
struct Buffer { std::vector<unsigned char> Bytes; uint64_t References = 1; };
struct Handle { uint32_t Kind; uint64_t References = 1; bool Closed = false, Notified = false; };
struct Request {
    uint64_t Reservation = 0, Sequence = 0;
    uint32_t Method = 0, Status = 0;
    LuiNativeIdV2 Buffer{}, Handle{};
    Clock::time_point Due;
    bool NotifyCancel = false;
};
struct Event { uint64_t Sequence; LuiNativeIdV2 Request, Buffer; uint64_t Bytes; };
struct WakeState {
    LuiNativeWakeV1 Callbacks{};
    std::atomic<bool> Active{true};
    uint64_t Identity=0, Attempt=0;
    bool Pending=false, OwnsContext=false;
    ~WakeState() { if(OwnsContext)try{Callbacks.Release(Callbacks.Context);}catch(...){} }
};
struct Domain {
    std::thread::id Owner = std::this_thread::get_id();
    std::mutex Mutex;
    std::vector<std::shared_ptr<LuiNativeSessionV2>> Sessions;
    uint64_t Bytes = 0, Reserved = 0, Requests = 0, Handles = 0, Buffers = 0;
    uint64_t EventCount = 0, EventBytes = 0, NextSequence = 1;
    bool Pumping = false;
    std::shared_ptr<WakeState> Wake;
};
}

struct LuiNativeDomainV2 { std::shared_ptr<Lui::Native::Domain> State; };
struct LuiNativeSessionV2 {
    std::shared_ptr<Lui::Native::Domain> Domain;
    uint64_t Identity = 0, NextId = 1, Leases = 0, Bytes = 0, Reserved = 0, EventBytes = 0;
    bool Retired = false, ShutdownStarted = false, Quiescent = false;
    bool Faulted = false;
    std::string Name;
    std::vector<std::string> MethodNames;
    std::vector<LuiNativeMethodV2> Methods;
    uint32_t KindCount = 0;
    LuiNativeProviderV2 Provider{};
    void* Context = nullptr;
    std::shared_ptr<void> Image;
    std::map<uint64_t, Lui::Native::Buffer> Buffers;
    std::map<uint64_t, Lui::Native::Handle> Handles;
    std::map<uint64_t, Lui::Native::Request> Requests;
    std::vector<Lui::Native::Event> Events;
    ~LuiNativeSessionV2() {
        if (Quiescent && Provider.Destroy) try { Provider.Destroy(Context); } catch (...) { }
    }
};

namespace {
using namespace Lui::Native;
constexpr LuiNativeIdV2 None{};
bool Empty(LuiNativeIdV2 Id) { return !Id.Session && !Id.Value; }
bool Own(LuiNativeSessionV2* Session, LuiNativeIdV2 Id) { return Id.Session == Session->Identity && Id.Value; }
bool Owner(LuiNativeSessionV2* Session) { return Session && Session->Domain->Owner == std::this_thread::get_id(); }
bool Identifier(const char* Text) {
    if (!Text || !*Text) return false;
    for (size_t Index = 0; Index <= 64; ++Index) {
        const char Byte = Text[Index];
        if (!Byte) {
            if (!Index) return false;
            for (const auto* Word : {"and","break","do","else","elseif","end","false","for","function","if","in","local","nil","not","or","repeat","return","then","true","until","while"})
                if (std::string_view(Text, Index) == Word) return false;
            return true;
        }
        if (!((Byte >= 'A' && Byte <= 'Z') || (Byte >= 'a' && Byte <= 'z') || Byte == '_' ||
            (Index && Byte >= '0' && Byte <= '9'))) return false;
    }
    return false;
}
bool PinHostCode() {
#if defined(LUI_STATIC_NATIVE_HOST)
    // Statically linked portable qualification executable lives until exit.
    return true;
#else
    // A worker may be returning through one of these host entries after the
    // embedding host drops its original library reference. Keep host code
    // resident for the process after the first v2 session. Provider images
    // still unload independently after their owner quiescence barrier.
    static const bool Pinned = [] {
#if defined(_WIN32)
        HMODULE Module = nullptr;
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&PinHostCode), &Module) != 0;
#else
        Dl_info Info{};
        if (!dladdr(reinterpret_cast<void*>(&PinHostCode), &Info) || !Info.dli_fname) return false;
        void* Module = dlopen(Info.dli_fname, RTLD_NOW | RTLD_NODELETE);
        if (!Module) return false;
        dlclose(Module); return true;
#endif
    }();
    return Pinned;
#endif
}
template<class Function> uint32_t Boundary(Function&& Callback) noexcept {
    try { return Callback(); } catch (...) { return LUI_NATIVE_LIMIT_EXCEEDED; }
}
uint64_t Next(uint64_t& Counter) {
    if (!Counter || Counter == UINT64_MAX) return 0;
    return Counter++;
}
void ReleaseBytesLocked(LuiNativeSessionV2* Session, LuiNativeIdV2 Id) {
    if (Empty(Id)) return;
    auto Found = Session->Buffers.find(Id.Value);
    if (Found != Session->Buffers.end() && --Found->second.References == 0) {
        Session->Domain->Bytes -= Found->second.Bytes.size();
        Session->Bytes -= Found->second.Bytes.size();
        --Session->Domain->Buffers;
        Session->Buffers.erase(Found);
    }
}
void ReleaseHandleLocked(LuiNativeSessionV2* Session, LuiNativeIdV2 Id) {
    if (Empty(Id)) return;
    auto Found = Session->Handles.find(Id.Value);
    if (Found != Session->Handles.end() && Found->second.References && --Found->second.References == 0)
        Found->second.Closed = true;
}
void Unreserve(LuiNativeSessionV2* Session, Request& Value) {
    Session->Reserved -= Value.Reservation;
    Session->Domain->Reserved -= Value.Reservation;
    Value.Reservation = 0;
}
void Settle(LuiNativeSessionV2* Session, Request& Value, uint32_t Status, bool Cancel) {
    Unreserve(Session, Value);
    Value.Status = Status;
    Value.Sequence = Next(Session->Domain->NextSequence);
    Value.NotifyCancel = Cancel;
}
void DropEvent(LuiNativeSessionV2* Session, const Event& Value) {
    ReleaseBytesLocked(Session, Value.Buffer);
    --Session->Domain->EventCount;
    Session->Domain->EventBytes -= Value.Bytes;
    Session->EventBytes -= Value.Bytes;
}
bool SuppressedProgress(const LuiNativeSessionV2* Session, const Event& Event) {
    const auto Found = Session->Requests.find(Event.Request.Value);
    if (Found == Session->Requests.end()) return true;
    const auto& Request = Found->second;
    return Request.Sequence && (Request.Status == LUI_NATIVE_CANCELED ||
        Request.Status == LUI_NATIVE_DEADLINE_EXCEEDED || Request.Status == LUI_NATIVE_CLOSED);
}
uint64_t PeekLocked(const LuiNativeSessionV2* Session) {
    uint64_t Sequence = UINT64_MAX;
    if (Session->Retired) return Sequence;
    for (const auto& Pair : Session->Requests) if (Pair.second.Sequence) Sequence = std::min(Sequence, Pair.second.Sequence);
    for (const auto& Event : Session->Events) if (!SuppressedProgress(Session,Event)) Sequence = std::min(Sequence,Event.Sequence);
    return Sequence;
}
uint32_t NextLocked(LuiNativeSessionV2* Session,LuiNativeDeliveryV2* Delivery) {
    if (Session->Retired) return LUI_NATIVE_CLOSED;
    auto Terminal = Session->Requests.end();
    for (auto Item = Session->Requests.begin(); Item != Session->Requests.end(); ++Item)
        if (Item->second.Sequence && (Terminal == Session->Requests.end() || Item->second.Sequence < Terminal->second.Sequence)) Terminal = Item;
    for (auto Item = Session->Events.begin(); Item != Session->Events.end();)
        if (SuppressedProgress(Session,*Item)) { DropEvent(Session,*Item); Item = Session->Events.erase(Item); }
        else ++Item;
    if (!Session->Events.empty() && (Terminal == Session->Requests.end() || Session->Events.front().Sequence < Terminal->second.Sequence)) {
        const auto Event = Session->Events.front(); Session->Events.erase(Session->Events.begin());
        --Session->Domain->EventCount; Session->Domain->EventBytes -= Event.Bytes; Session->EventBytes -= Event.Bytes;
        *Delivery = {sizeof(*Delivery),2,LUI_NATIVE_PROGRESS,0,Event.Sequence,Event.Request,Event.Buffer,None}; return LUI_NATIVE_OK;
    }
    if (Terminal == Session->Requests.end() || Terminal->second.NotifyCancel) return LUI_NATIVE_WOULD_BLOCK;
    const auto& Value = Terminal->second;
    *Delivery = {sizeof(*Delivery),2,LUI_NATIVE_TERMINAL,Value.Status,Value.Sequence,
        {Session->Identity,Terminal->first},Value.Buffer,Value.Handle};
    Session->Requests.erase(Terminal); --Session->Domain->Requests; return LUI_NATIVE_OK;
}
struct WakeDispatch {
    std::shared_ptr<Domain> Owner;
    std::shared_ptr<WakeState> State;
    uint64_t Attempt=0;
    void Send() noexcept {
        if(!State)return;
        uint32_t Accepted=0;
        if(State->Active.load())try{Accepted=State->Callbacks.Request(State->Callbacks.Context);}catch(...){}
        if(Accepted!=1){
            std::lock_guard<std::mutex> Lock(Owner->Mutex);
            // An owner acknowledgment and newer attempt may overtake this
            // callback's return. A failed older attempt cannot clear that wake.
            if(State->Attempt==Attempt)State->Pending=false;
        }
    }
};
WakeDispatch WakeLocked(const std::shared_ptr<Domain>& Owner) {
    const auto& State=Owner->Wake;
    if(!State||State->Pending||State->Attempt==UINT64_MAX)return {};
    State->Pending=true;++State->Attempt;
    return {Owner,State,State->Attempt};
}
uint32_t LUI_CALL RetainSession(LuiNativeSessionV2* Session) {
    if (!Session) return LUI_NATIVE_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
    if (Session->Retired) return LUI_NATIVE_CLOSED;
    if (Session->Leases == UINT64_MAX) return LUI_NATIVE_LIMIT_EXCEEDED;
    ++Session->Leases; return LUI_NATIVE_OK;
}
uint32_t LUI_CALL ReleaseSession(LuiNativeSessionV2* Session) {
    if (!Session) return LUI_NATIVE_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
    if (!Session->Leases) return LUI_NATIVE_INVALID_ARGUMENT;
    --Session->Leases; return LUI_NATIVE_OK;
}
uint32_t LUI_CALL CopyBytes(LuiNativeSessionV2* Session, LuiNativeIdV2 RequestId,
    const void* Bytes, uint64_t Length, LuiNativeIdV2* Result) {
    if (Result) *Result = None;
    if (!Session || !Result || (Length && !Bytes)) return LUI_NATIVE_INVALID_ARGUMENT;
    return Boundary([&]() -> uint32_t {
        auto& Domain = *Session->Domain;
        std::lock_guard<std::mutex> Lock(Domain.Mutex);
        if (Session->Retired) return LUI_NATIVE_CLOSED;
        if (Length > ValueBytes || Session->Buffers.size() >= 256 || Domain.Buffers >= 1024)
            return LUI_NATIVE_LIMIT_EXCEEDED;
        Request* Reservation = nullptr;
        if (!Empty(RequestId)) {
            if (!Own(Session, RequestId)) return LUI_NATIVE_INVALID_ARGUMENT;
            auto Found = Session->Requests.find(RequestId.Value);
            if (Found == Session->Requests.end()) return LUI_NATIVE_CLOSED;
            if (Found->second.Sequence) return LUI_NATIVE_ALREADY_SETTLED;
            Reservation = &Found->second;
            if (Length > Reservation->Reservation) return LUI_NATIVE_LIMIT_EXCEEDED;
        }
        if (!Reservation && (Length > DomainBytes - Domain.Bytes - Domain.Reserved ||
            Length > SessionBytes - Session->Bytes - Session->Reserved)) return LUI_NATIVE_LIMIT_EXCEEDED;
        const uint64_t Id = Next(Session->NextId);
        if (!Id) return LUI_NATIVE_LIMIT_EXCEEDED;
        Buffer Value;
        if (Length) Value.Bytes.assign(static_cast<const unsigned char*>(Bytes), static_cast<const unsigned char*>(Bytes) + Length);
        Session->Buffers.emplace(Id, std::move(Value));
        if (Reservation) {
            Reservation->Reservation -= Length; Session->Reserved -= Length; Domain.Reserved -= Length;
        }
        Session->Bytes += Length; Domain.Bytes += Length; ++Domain.Buffers;
        *Result = {Session->Identity, Id}; return LUI_NATIVE_OK;
    });
}
uint32_t LUI_CALL RetainBytes(LuiNativeSessionV2* Session, LuiNativeIdV2 Id) {
    if (!Session || !Own(Session, Id)) return LUI_NATIVE_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
    auto Found = Session->Buffers.find(Id.Value);
    if (Found == Session->Buffers.end()) return LUI_NATIVE_INVALID_ARGUMENT;
    if (Found->second.References == UINT64_MAX) return LUI_NATIVE_LIMIT_EXCEEDED;
    ++Found->second.References; return LUI_NATIVE_OK;
}
uint32_t LUI_CALL ReleaseBytes(LuiNativeSessionV2* Session, LuiNativeIdV2 Id) {
    if (!Session || !Own(Session, Id)) return LUI_NATIVE_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
    if (!Session->Buffers.count(Id.Value)) return LUI_NATIVE_INVALID_ARGUMENT;
    ReleaseBytesLocked(Session, Id); return LUI_NATIVE_OK;
}
uint32_t LUI_CALL ViewBytes(LuiNativeSessionV2* Session, LuiNativeIdV2 Id, const void** Bytes, uint64_t* Length) {
    if (Bytes) *Bytes = nullptr;
    if (Length) *Length = 0;
    if (!Session || !Bytes || !Length || !Own(Session, Id)) return LUI_NATIVE_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
    auto Found = Session->Buffers.find(Id.Value);
    if (Found == Session->Buffers.end()) return LUI_NATIVE_INVALID_ARGUMENT;
    *Bytes = Found->second.Bytes.data(); *Length = Found->second.Bytes.size(); return LUI_NATIVE_OK;
}
uint32_t LUI_CALL CreateHandle(LuiNativeSessionV2* Session, uint32_t Kind, LuiNativeIdV2* Result) {
    if (Result) *Result = None;
    if (!Session || !Result || !Kind || Kind > Session->KindCount) return LUI_NATIVE_INVALID_ARGUMENT;
    return Boundary([&]() -> uint32_t {
        std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
        if (Session->Retired) return LUI_NATIVE_CLOSED;
        if (Session->Handles.size() >= 64 || Session->Domain->Handles >= 256) return LUI_NATIVE_LIMIT_EXCEEDED;
        const uint64_t Id = Next(Session->NextId);
        if (!Id) return LUI_NATIVE_LIMIT_EXCEEDED;
        Session->Handles.emplace(Id, Handle{Kind}); ++Session->Domain->Handles;
        *Result = {Session->Identity, Id}; return LUI_NATIVE_OK;
    });
}
uint32_t LUI_CALL RetainHandle(LuiNativeSessionV2* Session, LuiNativeIdV2 Id) {
    if (!Session || !Own(Session, Id)) return LUI_NATIVE_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
    const auto Found = Session->Handles.find(Id.Value);
    if (Found == Session->Handles.end()) return LUI_NATIVE_INVALID_ARGUMENT;
    if (Found->second.Closed || Session->Retired) return LUI_NATIVE_CLOSED;
    if (Found->second.References == UINT64_MAX) return LUI_NATIVE_LIMIT_EXCEEDED;
    ++Found->second.References; return LUI_NATIVE_OK;
}
uint32_t LUI_CALL ReleaseHandle(LuiNativeSessionV2* Session, LuiNativeIdV2 Id) {
    if (!Session || !Own(Session, Id)) return LUI_NATIVE_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
    const auto Found = Session->Handles.find(Id.Value);
    if (Found == Session->Handles.end() || !Found->second.References) return LUI_NATIVE_INVALID_ARGUMENT;
    ReleaseHandleLocked(Session, Id); return LUI_NATIVE_OK;
}
uint32_t LUI_CALL Complete(LuiNativeSessionV2* Session, LuiNativeIdV2 Id, uint32_t Status,
    LuiNativeIdV2 Bytes, LuiNativeIdV2 HandleId) {
    if (!Session || !Own(Session, Id) || Status > LUI_NATIVE_PROVIDER_FAILURE) return LUI_NATIVE_INVALID_ARGUMENT;
    std::unique_lock<std::mutex> Lock(Session->Domain->Mutex);
    if (Session->Retired) return LUI_NATIVE_CLOSED;
    auto Found = Session->Requests.find(Id.Value);
    if (Found == Session->Requests.end()) return LUI_NATIVE_CLOSED;
    auto& Value = Found->second;
    if (Value.Sequence) return LUI_NATIVE_ALREADY_SETTLED;
    if (Session->Domain->NextSequence == UINT64_MAX) return LUI_NATIVE_LIMIT_EXCEEDED;
    if (Status && (!Empty(Bytes) || !Empty(HandleId))) return LUI_NATIVE_INVALID_ARGUMENT;
    Buffer* Output = nullptr; Handle* Resource = nullptr;
    if (!Empty(Bytes)) {
        if (!Own(Session, Bytes)) return LUI_NATIVE_INVALID_ARGUMENT;
        auto Item = Session->Buffers.find(Bytes.Value);
        if (Item == Session->Buffers.end()) return LUI_NATIVE_INVALID_ARGUMENT;
        Output = &Item->second;
        if (Output->Bytes.size() > Session->Methods[Value.Method].MaxResultBytes || Output->References == UINT64_MAX)
            return LUI_NATIVE_LIMIT_EXCEEDED;
    }
    const auto Kind = Session->Methods[Value.Method].ResultKind;
    if (!Empty(HandleId)) {
        if (!Own(Session, HandleId)) return LUI_NATIVE_INVALID_ARGUMENT;
        auto Item = Session->Handles.find(HandleId.Value);
        if (Item == Session->Handles.end()) return LUI_NATIVE_INVALID_ARGUMENT;
        Resource = &Item->second;
        if (!Kind || Resource->Kind != Kind || Resource->Closed) return LUI_NATIVE_INVALID_ARGUMENT;
        if (Resource->References == UINT64_MAX) return LUI_NATIVE_LIMIT_EXCEEDED;
    } else if (!Status && Kind) return LUI_NATIVE_INVALID_ARGUMENT;
    if (Output) ++Output->References;
    if (Resource) ++Resource->References;
    Value.Buffer = Bytes; Value.Handle = HandleId;
    Settle(Session, Value, Status, false);
    auto Wake=WakeLocked(Session->Domain);Lock.unlock();Wake.Send();return LUI_NATIVE_OK;
}
uint32_t LUI_CALL Emit(LuiNativeSessionV2* Session, LuiNativeIdV2 Id, LuiNativeIdV2 Bytes) {
    if (!Session || !Own(Session, Id) || !Own(Session, Bytes)) return LUI_NATIVE_INVALID_ARGUMENT;
    WakeDispatch Wake;
    const auto Status=Boundary([&]() -> uint32_t {
        auto& Domain = *Session->Domain;
        std::lock_guard<std::mutex> Lock(Domain.Mutex);
        if (Session->Retired) return LUI_NATIVE_CLOSED;
        const auto Request = Session->Requests.find(Id.Value);
        if (Request == Session->Requests.end()) return LUI_NATIVE_CLOSED;
        if (Request->second.Sequence) return LUI_NATIVE_ALREADY_SETTLED;
        auto Found = Session->Buffers.find(Bytes.Value);
        if (Found == Session->Buffers.end()) return LUI_NATIVE_INVALID_ARGUMENT;
        const auto Length = Found->second.Bytes.size();
        if (Length > 64 * 1024) return LUI_NATIVE_LIMIT_EXCEEDED;
        if (Domain.EventCount >= 256 || Session->Events.size() >= 64 ||
            Length > 1024 * 1024 - Domain.EventBytes || Length > 256 * 1024 - Session->EventBytes)
            return LUI_NATIVE_WOULD_BLOCK;
        if (Domain.NextSequence >= UINT64_MAX - 128 || Found->second.References == UINT64_MAX) return LUI_NATIVE_LIMIT_EXCEEDED;
        Session->Events.push_back({Domain.NextSequence, Id, Bytes, Length});
        ++Domain.NextSequence; ++Found->second.References;
        ++Domain.EventCount; Domain.EventBytes += Length; Session->EventBytes += Length;
        Wake=WakeLocked(Session->Domain);
        return LUI_NATIVE_OK;
    });
    Wake.Send();return Status;
}
}

namespace Lui::Native {
std::shared_ptr<Domain> GetDomain(LuiNativeDomainV2* Value) { return Value ? Value->State : nullptr; }
std::shared_ptr<Domain> GetDomain() {
    // Avoid a DLL-owned thread_local destructor surviving an ABI v1 host's
    // explicit library unload. V2 session admission separately pins host code.
    static std::mutex Mutex;
    static std::map<std::thread::id,std::weak_ptr<Domain>> Domains;
    std::lock_guard<std::mutex> Lock(Mutex);
    for (auto Item = Domains.begin(); Item != Domains.end();)
        if (Item->second.expired()) Item = Domains.erase(Item); else ++Item;
    auto& Entry = Domains[std::this_thread::get_id()];
    auto Value = Entry.lock();
    if (!Value) { Value = std::make_shared<Domain>(); Entry = Value; }
    return Value;
}
LuiNativeDomainV2* WrapDomain(const std::shared_ptr<Domain>& Value) { return new LuiNativeDomainV2{Value}; }
void SetContext(LuiNativeSessionV2* Session, void* Context) { Session->Context = Context; }
void SetImage(LuiNativeSessionV2* Session, std::shared_ptr<void> Image) { Session->Image = std::move(Image); }
const std::string& Name(LuiNativeSessionV2* Session) { return Session->Name; }
const std::vector<LuiNativeMethodV2>& Methods(LuiNativeSessionV2* Session) { return Session->Methods; }
uint32_t KindCount(LuiNativeSessionV2* Session) { return Session->KindCount; }
uint32_t HandleKind(LuiNativeSessionV2* Session, LuiNativeIdV2 Id) {
    std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
    auto Found = Session->Handles.find(Id.Value);
    return Own(Session, Id) && Found != Session->Handles.end() ? Found->second.Kind : 0;
}
bool HandleOpen(LuiNativeSessionV2* Session, LuiNativeIdV2 Id) {
    std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
    const auto Found = Session->Handles.find(Id.Value);
    return Own(Session, Id) && !Session->Retired && Found != Session->Handles.end() && !Found->second.Closed;
}
uint32_t Take(const std::shared_ptr<Domain>& Domain,LuiNativeSessionV2* const* Sessions,
    uint32_t Count,uint32_t* Selected,LuiNativeDeliveryV2* Delivery) {
    if (!Domain || Domain->Owner != std::this_thread::get_id() || !Sessions || Count > SessionLimit || !Selected ||
        !Delivery || Delivery->StructSize != sizeof(*Delivery) || Delivery->AbiVersion != 2) return LUI_NATIVE_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> Lock(Domain->Mutex);
    uint64_t Sequence = UINT64_MAX; *Selected = Count;
    for (uint32_t Index = 0; Index < Count; ++Index) {
        if (!Sessions[Index] || Sessions[Index]->Domain != Domain) return LUI_NATIVE_INVALID_ARGUMENT;
        const auto Candidate = PeekLocked(Sessions[Index]);
        if (Candidate < Sequence) { Sequence = Candidate; *Selected = Index; }
    }
    // Selection and dequeue share one lock: worker settlement cannot replace
    // a selected progress record with a later terminal between these steps.
    return *Selected == Count ? LUI_NATIVE_WOULD_BLOCK : NextLocked(Sessions[*Selected],Delivery);
}
bool RequestRunning(LuiNativeSessionV2* Session, LuiNativeIdV2 Id) {
    std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
    const auto Found = Session->Requests.find(Id.Value);
    return Own(Session,Id) && !Session->Retired && Found != Session->Requests.end() && !Found->second.Sequence && Clock::now() < Found->second.Due;
}
static void PumpWork(const std::shared_ptr<Domain>& Value) {
    if (!Value || Value->Owner != std::this_thread::get_id() || Value->Pumping) return;
    Value->Pumping = true;
    struct Reset { bool& Value; ~Reset() { Value = false; } } Guard{Value->Pumping};
    // Snapshot pins control blocks while callbacks execute without registry locks.
    std::vector<std::shared_ptr<LuiNativeSessionV2>> Sessions;
    { std::lock_guard<std::mutex> Lock(Value->Mutex); Sessions = Value->Sessions; }
    for (const auto& Current : Sessions) {
        auto* Session = Current.get();
        std::vector<LuiNativeIdV2> Cancel;
        std::vector<std::pair<LuiNativeIdV2, uint32_t>> Close;
        bool Begin = false, Poll = false;
        Cancel.reserve(32); Close.reserve(64);
        {
            std::lock_guard<std::mutex> Lock(Value->Mutex);
            for (auto& Pair : Session->Requests) {
                auto& Request = Pair.second;
                if (!Request.Sequence && Clock::now() >= Request.Due) Settle(Session, Request, LUI_NATIVE_DEADLINE_EXCEEDED, true);
                if (Request.NotifyCancel) { Cancel.push_back({Session->Identity, Pair.first}); Request.NotifyCancel = false; }
            }
            for (auto& Pair : Session->Handles) if (Pair.second.Closed && !Pair.second.Notified) {
                Close.push_back({{Session->Identity, Pair.first}, Pair.second.Kind}); Pair.second.Notified = true;
            }
            if (Session->Retired && !Session->ShutdownStarted) { Session->ShutdownStarted = true; Begin = true; }
            Poll = Session->Retired && !Session->Quiescent;
        }
        for (const auto& Id : Cancel) try { Session->Provider.Cancel(Session->Context, Id); } catch (...) { Session->Faulted = true; }
        for (const auto& Pair : Close) try { Session->Provider.CloseHandle(Session->Context, Pair.first, Pair.second); } catch (...) { Session->Faulted = true; }
        if (Begin) try { Session->Provider.BeginShutdown(Session->Context); } catch (...) { Session->Faulted = true; }
        if (Poll) try { if (Session->Provider.PollShutdown(Session->Context) == 1 && !Session->Faulted) Session->Quiescent = true; }
            catch (...) { Session->Faulted = true; }
        {
            std::lock_guard<std::mutex> Lock(Value->Mutex);
            for (auto Item = Session->Handles.begin(); Item != Session->Handles.end();) {
                if (!Item->second.References && Item->second.Notified) { Item = Session->Handles.erase(Item); --Value->Handles; }
                else ++Item;
            }
            if (Session->Retired) {
                for (auto& Pair : Session->Requests) {
                    Unreserve(Session, Pair.second); ReleaseBytesLocked(Session, Pair.second.Buffer);
                    ReleaseHandleLocked(Session, Pair.second.Handle); --Value->Requests;
                }
                Session->Requests.clear();
            }
        }
    }
    // Image deleters run only here, on the owner, after PollShutdown has returned.
    std::vector<std::shared_ptr<LuiNativeSessionV2>> Reclaimed;
    {
        std::lock_guard<std::mutex> Lock(Value->Mutex);
        for (auto Item = Value->Sessions.begin(); Item != Value->Sessions.end();) {
            const auto& Session = *Item;
            if (Session->Retired && Session->Quiescent && !Session->Leases && Session->Buffers.empty() &&
                Session->Handles.empty() && Session->Requests.empty() && Session->Events.empty()) {
                Reclaimed.push_back(*Item); Item = Value->Sessions.erase(Item);
            } else ++Item;
        }
    }
}
void Pump(const std::shared_ptr<Domain>& Value) noexcept {
    try { PumpWork(Value); } catch (...) { /* Preserve bounded ownership for a later owner pump. */ }
}
void WakePending(const std::shared_ptr<Domain>& Value,LuiNativeSessionV2* const* Sessions,uint32_t Count) noexcept {
    if(!Value)return;
    WakeDispatch Wake;
    {
        std::lock_guard<std::mutex> Lock(Value->Mutex);
        if(!Value->Wake)return;
        if(Sessions){for(uint32_t Index=0;Index<Count;++Index)if(PeekLocked(Sessions[Index])!=UINT64_MAX){Wake=WakeLocked(Value);break;}}
        else for(const auto& Session:Value->Sessions)if(PeekLocked(Session.get())!=UINT64_MAX){Wake=WakeLocked(Value);break;}
    }
    Wake.Send();
}
}

extern "C" {
LUI_API uint32_t LUI_CALL Lui_SetNativeWakeV1(LuiNativeDomainV2* Domain,
    const LuiNativeWakeV1* Callbacks,uint64_t* Registration) {
    if(Registration)*Registration=0;
    if(!Domain||Domain->State->Owner!=std::this_thread::get_id()||
        (Callbacks&&(!Registration||Callbacks->StructSize!=sizeof(*Callbacks)||Callbacks->AbiVersion!=1||
            !Callbacks->Request||!Callbacks->Release)))return LUI_NATIVE_INVALID_ARGUMENT;
    return Boundary([&]() -> uint32_t {
        std::shared_ptr<WakeState> NextWake,Previous;
        if(Callbacks){
            NextWake=std::make_shared<WakeState>();NextWake->Callbacks=*Callbacks;
            static std::atomic<uint64_t> Identity{1};auto Id=Identity.load();
            while(Id!=UINT64_MAX&&!Identity.compare_exchange_weak(Id,Id+1)){}
            if(Id==UINT64_MAX)return LUI_NATIVE_LIMIT_EXCEEDED;
            NextWake->Identity=Id;
        }
        {
            std::lock_guard<std::mutex> Lock(Domain->State->Mutex);
            Previous=std::move(Domain->State->Wake);
            if(Previous)Previous->Active=false;
            Domain->State->Wake=NextWake;
            if(NextWake){NextWake->OwnsContext=true;*Registration=NextWake->Identity;}
        }
        // Release context outside the domain lock; in-flight notifications hold
        // their own reference and may release it later on a worker.
        Previous.reset();
        Lui::Native::WakePending(Domain->State);
        return LUI_NATIVE_OK;
    });
}
LUI_API uint32_t LUI_CALL Lui_AcknowledgeNativeWakeV1(LuiNativeDomainV2* Domain,uint64_t Registration) {
    if(!Domain||!Registration||Domain->State->Owner!=std::this_thread::get_id())return LUI_NATIVE_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> Lock(Domain->State->Mutex);
    if(!Domain->State->Wake||Domain->State->Wake->Identity!=Registration)return LUI_NATIVE_CLOSED;
    Domain->State->Wake->Pending=false;return LUI_NATIVE_OK;
}
LUI_API LuiNativeDomainV2* LUI_CALL Lui_CreateNativeDomainV2(void) {
    try { return new LuiNativeDomainV2{std::make_shared<Lui::Native::Domain>()}; } catch (...) { return nullptr; }
}
LUI_API void LUI_CALL Lui_ReleaseNativeDomainV2(LuiNativeDomainV2* Domain) { delete Domain; }
LUI_API uint32_t LUI_CALL Lui_PumpNativeDomainV2(LuiNativeDomainV2* Domain) {
    if (!Domain || Domain->State->Owner != std::this_thread::get_id()) return LUI_NATIVE_INVALID_ARGUMENT;
    return Boundary([&]() -> uint32_t { Lui::Native::Pump(Domain->State); return LUI_NATIVE_OK; });
}
LUI_API uint32_t LUI_CALL Lui_GetNativeStatsV2(LuiNativeDomainV2* Domain, LuiNativeStatsV2* Stats) {
    if (!Domain || !Stats || Stats->StructSize != sizeof(*Stats) || Stats->AbiVersion != 2) return LUI_NATIVE_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> Lock(Domain->State->Mutex);
    auto& Value = *Domain->State;
    *Stats = {sizeof(*Stats), 2, Value.Bytes, Value.Reserved, Value.Requests, Value.Handles, Value.Sessions.size(), 0, 0, Value.EventCount};
    for (const auto& Session : Value.Sessions) { Stats->RetiringSessions += Session->Retired; Stats->Leases += Session->Leases; }
    return LUI_NATIVE_OK;
}
LUI_API uint32_t LUI_CALL Lui_OpenNativeSessionV2(LuiNativeDomainV2* Domain, const LuiNativeServiceV2* Service,
    const LuiNativeProviderV2* Provider, void* Context, LuiNativeHostV2* Host) {
    if (sizeof(void*) != 8) return LUI_NATIVE_UNSUPPORTED;
    if (!Domain || !Service || !Provider || !Host || Domain->State->Owner != std::this_thread::get_id() ||
        Host->StructSize != sizeof(*Host) || Host->AbiVersion != 2 ||
        Service->StructSize != sizeof(*Service) || Service->AbiVersion != 2 || !Identifier(Service->Name) ||
        !Service->Version || Service->Reserved ||
        !Service->Methods || !Service->MethodCount || Service->MethodCount > 32 || Service->ResourceKindCount > 64 ||
        Provider->StructSize != sizeof(*Provider) || Provider->AbiVersion != 2 || !Provider->Start ||
        !Provider->Cancel || !Provider->CloseHandle || !Provider->BeginShutdown || !Provider->PollShutdown || !Provider->Destroy)
        return LUI_NATIVE_INVALID_ARGUMENT;
    return Boundary([&]() -> uint32_t {
        if (!PinHostCode()) return LUI_NATIVE_UNSUPPORTED;
        auto Session = std::make_shared<LuiNativeSessionV2>();
        Session->Name = Service->Name; Session->Domain = Domain->State; Session->Provider = *Provider;
        Session->Context = Context; Session->KindCount = Service->ResourceKindCount;
        Session->Methods.reserve(Service->MethodCount); Session->MethodNames.reserve(Service->MethodCount);
        for (uint32_t Index = 0; Index < Service->MethodCount; ++Index) {
            const auto& Method = Service->Methods[Index];
            if (Method.StructSize != sizeof(Method) || Method.AbiVersion != 2 || Method.Reserved || !Identifier(Method.Name) ||
                Method.MaxInputBytes > ValueBytes || Method.MaxResultBytes > ValueBytes ||
                Method.InputKind > Session->KindCount || Method.ResultKind > Session->KindCount ||
                !Method.DeadlineMilliseconds || Method.DeadlineMilliseconds > 300000) return LUI_NATIVE_INVALID_ARGUMENT;
            for (const auto& Name : Session->MethodNames) if (Name == Method.Name) return LUI_NATIVE_INVALID_ARGUMENT;
            Session->MethodNames.emplace_back(Method.Name); Session->Methods.push_back(Method);
            Session->Methods.back().Name = Session->MethodNames.back().c_str();
        }
        static std::atomic<uint64_t> Identity{1};
        auto Id = Identity.load();
        while (Id != UINT64_MAX && !Identity.compare_exchange_weak(Id, Id + 1)) { }
        if (Id == UINT64_MAX) return LUI_NATIVE_LIMIT_EXCEEDED;
        Session->Identity = Id;
        std::lock_guard<std::mutex> Lock(Domain->State->Mutex);
        if (Domain->State->Sessions.size() >= Lui::Native::SessionLimit) return LUI_NATIVE_LIMIT_EXCEEDED;
        Domain->State->Sessions.push_back(Session);
        *Host = {sizeof(*Host), 2, Session.get(), RetainSession, ReleaseSession, CopyBytes, RetainBytes, ReleaseBytes,
            ViewBytes, CreateHandle, RetainHandle, ReleaseHandle, Complete, Emit};
        return LUI_NATIVE_OK;
    });
}
LUI_API uint32_t LUI_CALL Lui_StartNativeRequestV2(LuiNativeSessionV2* Session, uint32_t MethodIndex,
    LuiNativeIdV2 Input, LuiNativeIdV2 HandleId, LuiNativeIdV2* Output) {
    if (Output) *Output = None;
    if (!Owner(Session) || !Output || MethodIndex >= Session->Methods.size()) return LUI_NATIVE_INVALID_ARGUMENT;
    return Boundary([&]() -> uint32_t {
        auto& Domain = *Session->Domain;
        const auto& Method = Session->Methods[MethodIndex];
        LuiNativeIdV2 Id{};
        {
            std::lock_guard<std::mutex> Lock(Domain.Mutex);
            if (Session->Retired) return LUI_NATIVE_CLOSED;
            auto Buffer = Session->Buffers.find(Input.Value);
            if (!Own(Session, Input) || Buffer == Session->Buffers.end() || Buffer->second.Bytes.size() > Method.MaxInputBytes)
                return LUI_NATIVE_INVALID_ARGUMENT;
            if (!Empty(HandleId)) {
                const auto Handle = Session->Handles.find(HandleId.Value);
                if (!Own(Session, HandleId) || Handle == Session->Handles.end() || Handle->second.Closed ||
                    Handle->second.Kind != Method.InputKind || Handle->second.References == UINT64_MAX)
                    return LUI_NATIVE_INVALID_ARGUMENT;
            } else if (Method.InputKind) return LUI_NATIVE_INVALID_ARGUMENT;
            if (Domain.Requests >= 128 || Session->Requests.size() >= 32 ||
                Method.MaxResultBytes > DomainBytes - Domain.Bytes - Domain.Reserved ||
                Method.MaxResultBytes > SessionBytes - Session->Bytes - Session->Reserved ||
                Buffer->second.References == UINT64_MAX || Domain.NextSequence >= UINT64_MAX - 128)
                return LUI_NATIVE_LIMIT_EXCEEDED;
            const auto Value = Next(Session->NextId);
            if (!Value) return LUI_NATIVE_LIMIT_EXCEEDED;
            Request Request; Request.Reservation = Method.MaxResultBytes; Request.Method = MethodIndex;
            Request.Due = Clock::now() + std::chrono::milliseconds(Method.DeadlineMilliseconds);
            Session->Requests.emplace(Value, Request);
            ++Buffer->second.References;
            if (!Empty(HandleId)) ++Session->Handles.at(HandleId.Value).References;
            Domain.Reserved += Request.Reservation; Session->Reserved += Request.Reservation; ++Domain.Requests;
            Id = {Session->Identity, Value}; *Output = Id;
        }
        try { Session->Provider.Start(Session->Context, Id, MethodIndex, Input, HandleId); }
        catch (...) { Complete(Session, Id, LUI_NATIVE_PROVIDER_FAILURE, None, None); }
        ReleaseBytes(Session, Input);
        if (!Empty(HandleId)) ReleaseHandle(Session, HandleId);
        return LUI_NATIVE_OK;
    });
}
LUI_API uint32_t LUI_CALL Lui_CancelNativeRequestV2(LuiNativeSessionV2* Session, LuiNativeIdV2 Id) {
    if (!Session || !Own(Session, Id)) return LUI_NATIVE_INVALID_ARGUMENT;
    std::unique_lock<std::mutex> Lock(Session->Domain->Mutex);
    if (Session->Retired) return LUI_NATIVE_CLOSED;
    auto Found = Session->Requests.find(Id.Value);
    if (Found == Session->Requests.end()) return LUI_NATIVE_CLOSED;
    if (Found->second.Sequence) return LUI_NATIVE_ALREADY_SETTLED;
    Settle(Session, Found->second, LUI_NATIVE_CANCELED, true);
    auto Wake=WakeLocked(Session->Domain);Lock.unlock();Wake.Send();return LUI_NATIVE_OK;
}
LUI_API uint32_t LUI_CALL Lui_CloseNativeHandleV2(LuiNativeSessionV2* Session, LuiNativeIdV2 Id) {
    if (!Session || !Own(Session, Id)) return LUI_NATIVE_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
    auto Found = Session->Handles.find(Id.Value);
    if (Found == Session->Handles.end()) return LUI_NATIVE_INVALID_ARGUMENT;
    Found->second.Closed = true; return LUI_NATIVE_OK;
}
LUI_API uint32_t LUI_CALL Lui_RetireNativeSessionV2(LuiNativeSessionV2* Session) {
    if (!Owner(Session)) return LUI_NATIVE_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
    if (Session->Retired) return LUI_NATIVE_OK;
    Session->Retired = true;
    for (auto& Pair : Session->Requests) {
        if (!Pair.second.Sequence) Settle(Session, Pair.second, LUI_NATIVE_CLOSED, true);
    }
    for (auto& Pair : Session->Handles) Pair.second.Closed = true;
    for (const auto& Event : Session->Events) DropEvent(Session, Event);
    Session->Events.clear();
    return LUI_NATIVE_OK;
}
LUI_API uint32_t LUI_CALL Lui_NextNativeDeliveryV2(LuiNativeSessionV2* Session, LuiNativeDeliveryV2* Delivery) {
    if (!Owner(Session) || !Delivery || Delivery->StructSize != sizeof(*Delivery) || Delivery->AbiVersion != 2)
        return LUI_NATIVE_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> Lock(Session->Domain->Mutex);
    return NextLocked(Session,Delivery);
}
}
