#include "StorageFiles.h"
#include "Domain.h"
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
struct Job {
    LuiNativeIdV2 Request, Input, Handle;
    uint32_t Method;
    std::string Key;
    std::shared_ptr<std::atomic<bool>> Canceled;
};
struct Storage {
    LuiNativeHostV2 Host{sizeof(LuiNativeHostV2),2};
    Lui::Native::StorageFiles Files;
    std::mutex Mutex;
    std::condition_variable Ready;
    std::map<uint64_t,std::string> Keys;
    std::map<uint64_t,std::shared_ptr<std::atomic<bool>>> Cancels;
    std::deque<Job> Jobs;
    bool Closing = false;
    std::thread Worker;
    std::promise<void> Exit;
    std::future<void> Exited;
    explicit Storage(const std::filesystem::path& Root) : Files(Root), Exited(Exit.get_future()) {}
    void Release(const Job& Value) {
        if (Value.Handle.Value) Host.ReleaseHandle(Host.Session,Value.Handle);
        Host.ReleaseBytes(Host.Session,Value.Input); Host.ReleaseSession(Host.Session);
    }
    void Work(const Job& Value) {
        LuiNativeIdV2 Result{}, Handle{}; uint32_t Status = 0;
        try {
            if (Value.Canceled->load()) Lui::Project::Internal::Fail("Canceled");
            if (Value.Method == 0) {
                Status = Host.CreateHandle(Host.Session,1,&Handle);
                if (!Status) { std::lock_guard<std::mutex> Lock(Mutex); Keys.emplace(Handle.Value,Value.Key); }
            } else if (Value.Method == 1) {
                std::string Bytes;
                if (Files.Read(Value.Key,Bytes)) Status = Host.CopyBytes(Host.Session,Value.Request,Bytes.data(),Bytes.size(),&Result);
            } else if (Value.Method == 2) {
                const void* Bytes = nullptr; uint64_t Length = 0;
                Status = Host.ViewBytes(Host.Session,Value.Input,&Bytes,&Length);
                if (!Status) Files.Write(Value.Key,std::string_view(Bytes ? static_cast<const char*>(Bytes) : "",static_cast<size_t>(Length)),[&] { return Value.Canceled->load(); });
            } else if (Value.Method == 3) Files.Remove(Value.Key);
            else Status = LUI_NATIVE_INVALID_ARGUMENT;
        } catch (const Lui::Project::Internal::Failure& Error) {
            const std::string_view Code = Error.what();
            Status = Code == "Canceled" ? LUI_NATIVE_CANCELED : Code == "LimitExceeded" ? LUI_NATIVE_LIMIT_EXCEEDED :
                Code == "InvalidArgument" ? LUI_NATIVE_INVALID_ARGUMENT : LUI_NATIVE_PROVIDER_FAILURE;
        } catch (const std::bad_alloc&) { Status = LUI_NATIVE_LIMIT_EXCEEDED; }
        catch (...) { Status = LUI_NATIVE_PROVIDER_FAILURE; }
        Host.Complete(Host.Session,Value.Request,Status,Status ? None : Result,Status ? None : Handle);
        if (Result.Value) Host.ReleaseBytes(Host.Session,Result);
        if (Handle.Value) Host.ReleaseHandle(Host.Session,Handle);
    }
    void Run() noexcept {
        while (true) {
            Job Value;
            {
                std::unique_lock<std::mutex> Lock(Mutex);
                Ready.wait(Lock,[&] { return Closing || !Jobs.empty(); });
                if (Jobs.empty() && Closing) break;
                Value = std::move(Jobs.front()); Jobs.pop_front();
            }
            Work(Value);
            { std::lock_guard<std::mutex> Lock(Mutex); Cancels.erase(Value.Request.Value); }
            Release(Value);
        }
        Exit.set_value_at_thread_exit();
    }
};
void LUI_CALL Start(void* Pointer, LuiNativeIdV2 Request, uint32_t Method, LuiNativeIdV2 Input, LuiNativeIdV2 Handle) {
    auto& Value = *static_cast<Storage*>(Pointer);
    uint32_t Status = 0; bool SessionHeld = false, InputHeld = false, HandleHeld = false;
    try {
        Job Work{Request,Input,Handle,Method,{},std::make_shared<std::atomic<bool>>(false)};
        if (Method == 0) {
            const void* Bytes = nullptr; uint64_t Length = 0;
            Status = Value.Host.ViewBytes(Value.Host.Session,Input,&Bytes,&Length);
            if (!Status) {
                if (Length) Work.Key.assign(static_cast<const char*>(Bytes),static_cast<size_t>(Length));
                if (!Lui::Native::StorageKey(Work.Key)) Status = LUI_NATIVE_INVALID_ARGUMENT;
            }
        } else {
            std::lock_guard<std::mutex> Lock(Value.Mutex);
            const auto Found = Value.Keys.find(Handle.Value);
            if (Found == Value.Keys.end()) Status = LUI_NATIVE_CLOSED;
            else Work.Key = Found->second;
        }
        if (!Status) { Status = Value.Host.RetainSession(Value.Host.Session); SessionHeld = !Status; }
        if (!Status) { Status = Value.Host.RetainBytes(Value.Host.Session,Input); InputHeld = !Status; }
        if (!Status && Handle.Value) { Status = Value.Host.RetainHandle(Value.Host.Session,Handle); HandleHeld = !Status; }
        if (!Status) {
            std::lock_guard<std::mutex> Lock(Value.Mutex);
            if (Value.Closing) Status = LUI_NATIVE_CLOSED;
            else {
                Value.Cancels.emplace(Request.Value,Work.Canceled);
                try { Value.Jobs.push_back(std::move(Work)); } catch (...) { Value.Cancels.erase(Request.Value); throw; }
            }
        }
    } catch (...) { Status = LUI_NATIVE_LIMIT_EXCEEDED; }
    if (Status) {
        if (HandleHeld) Value.Host.ReleaseHandle(Value.Host.Session,Handle);
        if (InputHeld) Value.Host.ReleaseBytes(Value.Host.Session,Input);
        if (SessionHeld) Value.Host.ReleaseSession(Value.Host.Session);
        Value.Host.Complete(Value.Host.Session,Request,Status,None,None);
    } else Value.Ready.notify_one();
}
void LUI_CALL Cancel(void* Pointer, LuiNativeIdV2 Request) {
    auto& Value = *static_cast<Storage*>(Pointer); std::lock_guard<std::mutex> Lock(Value.Mutex);
    const auto Found = Value.Cancels.find(Request.Value); if (Found != Value.Cancels.end()) Found->second->store(true);
}
void LUI_CALL Close(void* Pointer, LuiNativeIdV2 Handle, uint32_t) {
    auto& Value = *static_cast<Storage*>(Pointer); std::lock_guard<std::mutex> Lock(Value.Mutex); Value.Keys.erase(Handle.Value);
}
void LUI_CALL Shutdown(void* Pointer) {
    auto& Value = *static_cast<Storage*>(Pointer);
    { std::lock_guard<std::mutex> Lock(Value.Mutex); Value.Closing = true; for (const auto& Pair : Value.Cancels) Pair.second->store(true); }
    Value.Ready.notify_one();
}
uint32_t LUI_CALL Poll(void* Pointer) {
    auto& Value = *static_cast<Storage*>(Pointer);
    if (!Value.Worker.joinable()) return 1;
    if (Value.Exited.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return 0;
    Value.Worker.join(); return 1;
}
void LUI_CALL Destroy(void* Pointer) { delete static_cast<Storage*>(Pointer); }
const LuiNativeMethodV2 Methods[] = {
    {sizeof(LuiNativeMethodV2),2,"Open",64,0,0,1,30000,0},
    {sizeof(LuiNativeMethodV2),2,"Read",0,4*1024*1024,1,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"Write",4*1024*1024,0,1,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"Remove",0,0,1,0,30000,0}
};
const LuiNativeServiceV2 Service{sizeof(LuiNativeServiceV2),2,"StorageService",Methods,4,1,LUI_CAPABILITY_STORAGE,1,0};
const LuiNativeProviderV2 Provider{sizeof(LuiNativeProviderV2),2,Start,Cancel,Close,Shutdown,Poll,Destroy};
}

extern "C" LUI_API uint32_t LUI_CALL Lui_InstallStorageV1(LuiRuntime* Runtime, const char* Root) {
    if (!CheckOwner(Runtime) || !Root || Runtime->ApplicationStarted || Runtime->ProjectNativeVerified || Runtime->VmDepth || Runtime->BackendDepth || Runtime->ShuttingDown)
        return LUI_NATIVE_INVALID_ARGUMENT;
    if (!(Runtime->GrantedCapabilities & LUI_CAPABILITY_STORAGE)) return LUI_NATIVE_CAPABILITY_DENIED;
    if (HasNativeService(Runtime,"StorageService")) return LUI_NATIVE_INVALID_ARGUMENT;
    try {
        size_t Length = 0; while (Length < 4097 && Root[Length]) ++Length;
        if (!Length || Length == 4097) return LUI_NATIVE_INVALID_ARGUMENT;
        auto Value = std::make_unique<Storage>(std::filesystem::u8path(Root,Root+Length));
        const auto Status = RegisterPlatformNativeService(Runtime,&Service,&Provider,Value.get(),&Value->Host);
        if (Status) return Status;
        auto* Context = Value.release();
        try { Context->Worker = std::thread([Context] { Context->Run(); }); }
        catch (...) {
            Lui_RetireNativeSessionV2(Context->Host.Session); Runtime->NativeServices.pop_back();
            Lui::Native::Pump(Runtime->NativeDomain); return LUI_NATIVE_LIMIT_EXCEEDED;
        }
        return 0;
    } catch (const Lui::Project::Internal::Failure& Error) {
        return std::string_view(Error.what()) == "LimitExceeded" ? LUI_NATIVE_LIMIT_EXCEEDED : LUI_NATIVE_PROVIDER_FAILURE;
    } catch (...) { return LUI_NATIVE_PROVIDER_FAILURE; }
}
