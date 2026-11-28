#include "ByteWorker.h"
#if defined(LUI_LIVE_SQLITE)
#include "LiveStore.h"
#endif
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace {
struct Work {
    LuiNativeHostV2 Host;
    LuiNativeIdV2 Request, Input;
    uint32_t Method;
    std::atomic<bool> Canceled{false};
    bool SessionHeld = false, InputHeld = false;
    ~Work() {
        if (InputHeld) Host.ReleaseBytes(Host.Session,Input);
        if (SessionHeld) Host.ReleaseSession(Host.Session);
    }
};
struct Worker {
    LuiNativeHostV2 Host;
    std::mutex Mutex;
    std::condition_variable Changed;
    std::map<uint64_t,std::shared_ptr<Work>> Active;
    std::deque<std::shared_ptr<Work>> Queue;
#if defined(LUI_LIVE_SQLITE)
    std::unique_ptr<Lui::Standard::LiveStore> Store;
#endif
    bool Closed = false;
    std::atomic<bool> Done{false};
    std::thread Thread;
    void Run() noexcept {
        for (;;) {
            std::shared_ptr<Work> Item;
            {
                std::unique_lock<std::mutex> Lock(Mutex);
                Changed.wait(Lock,[&]{return Closed || !Queue.empty();});
                if (Queue.empty()) break;
                Item = std::move(Queue.front());
                Queue.pop_front();
            }
            LuiNativeIdV2 Result{};
            uint32_t Status = LUI_NATIVE_CANCELED;
            try {
                const void* Input = nullptr;
                uint64_t Size = 0;
                std::vector<uint8_t> Bytes;
                if (!Item->Canceled) Status = Host.ViewBytes(Host.Session,Item->Input,&Input,&Size);
#if defined(LUI_LIVE_SQLITE)
                if (!Status && Item->Method) Status = Lui::Standard::ProcessLive(Store.get(),Item->Method,
                    static_cast<const uint8_t*>(Input),static_cast<size_t>(Size),Item->Canceled,Bytes);
                else
#endif
                if (!Status) Status = Lui::Standard::Process(Item->Method,static_cast<const uint8_t*>(Input),
                    static_cast<size_t>(Size),Item->Canceled,Bytes);
                if (Item->Canceled) Status = LUI_NATIVE_CANCELED;
                if (!Status) Status = Host.CopyBytes(Host.Session,Item->Request,Bytes.data(),Bytes.size(),&Result);
            } catch (...) { Status = LUI_NATIVE_LIMIT_EXCEEDED; }
            Host.Complete(Host.Session,Item->Request,Status,Result,{});
            if (Result.Value) Host.ReleaseBytes(Host.Session,Result);
            std::lock_guard<std::mutex> Lock(Mutex);
            Active.erase(Item->Request.Value);
        }
        Done = true;
    }
};
void LUI_CALL Start(void* Context,LuiNativeIdV2 Request,uint32_t Method,LuiNativeIdV2 Input,LuiNativeIdV2) {
    auto& Owner = *static_cast<Worker*>(Context);
    uint32_t Status = LUI_NATIVE_LIMIT_EXCEEDED;
    try {
        auto Item = std::shared_ptr<Work>(new Work{Owner.Host,Request,Input,Method});
        std::lock_guard<std::mutex> Lock(Owner.Mutex);
        if (Owner.Closed) Status = LUI_NATIVE_CLOSED;
        else if (Owner.Active.size() < 8) {
            Status = Owner.Host.RetainSession(Owner.Host.Session);
            Item->SessionHeld = !Status;
            if (!Status) { Status = Owner.Host.RetainBytes(Owner.Host.Session,Input); Item->InputHeld = !Status; }
            if (!Status) {
                if (!Owner.Thread.joinable()) Owner.Thread = std::thread([&Owner]{Owner.Run();});
                Owner.Active.emplace(Request.Value,Item);
                try { Owner.Queue.push_back(Item); }
                catch (...) { Owner.Active.erase(Request.Value); throw; }
                Owner.Changed.notify_one();
                return;
            }
        }
    } catch (...) { Status = LUI_NATIVE_LIMIT_EXCEEDED; }
    Owner.Host.Complete(Owner.Host.Session,Request,Status,{},{});
}
void LUI_CALL Cancel(void* Context,LuiNativeIdV2 Request) {
    auto& Owner = *static_cast<Worker*>(Context);
    std::lock_guard<std::mutex> Lock(Owner.Mutex);
    const auto Found = Owner.Active.find(Request.Value);
    if (Found != Owner.Active.end()) Found->second->Canceled = true;
}
void LUI_CALL CloseHandle(void*,LuiNativeIdV2,uint32_t) {}
void LUI_CALL Shutdown(void* Context) {
    if (!Context) return;
    auto& Owner = *static_cast<Worker*>(Context);
    std::lock_guard<std::mutex> Lock(Owner.Mutex);
    Owner.Closed = true;
    for (auto& Item : Owner.Active) Item.second->Canceled = true;
    Owner.Changed.notify_one();
}
uint32_t LUI_CALL Poll(void* Context) {
    if (!Context) return 1;
    auto& Owner = *static_cast<Worker*>(Context);
    if (!Owner.Thread.joinable()) return 1;
    if (!Owner.Done) return 0;
    Owner.Thread.join();
    return 1;
}
void LUI_CALL Destroy(void* Context) { delete static_cast<Worker*>(Context); }
}
extern "C" LUI_EXTENSION_EXPORT uint32_t LUI_CALL LuiNativeQuery(LuiNativeServiceV2* Service,LuiNativeProviderV2* Provider) {
    if (!Service || !Provider || Service->StructSize != sizeof(*Service) || Service->AbiVersion != 2 ||
        Provider->StructSize != sizeof(*Provider) || Provider->AbiVersion != 2) return LUI_NATIVE_INVALID_ARGUMENT;
    *Service = Lui::Standard::Service;
    *Provider = {sizeof(*Provider),2,Start,Cancel,CloseHandle,Shutdown,Poll,Destroy};
    return 0;
}
extern "C" LUI_EXTENSION_EXPORT uint32_t LUI_CALL LuiNativeInit(const LuiNativeHostV2* Host,void** Context) {
    if (!Host || Host->StructSize != sizeof(*Host) || Host->AbiVersion != 2 || !Context) return LUI_NATIVE_INVALID_ARGUMENT;
    try { auto Owner = std::make_unique<Worker>(); Owner->Host = *Host; *Context = Owner.release(); return 0; }
    catch (...) { return LUI_NATIVE_LIMIT_EXCEEDED; }
}
#if defined(LUI_LIVE_SQLITE)
extern "C" LUI_EXTENSION_EXPORT uint32_t LUI_CALL LuiSQLiteGrantV1(void* Context,const char* Root) {
    if (!Context || !Root || !*Root) return LUI_NATIVE_INVALID_ARGUMENT;
    try {
        auto& Owner=*static_cast<Worker*>(Context);
        std::lock_guard<std::mutex> Lock(Owner.Mutex);
        if (Owner.Closed || Owner.Store || Owner.Thread.joinable()) return LUI_NATIVE_CAPABILITY_DENIED;
        Owner.Store=std::make_unique<Lui::Standard::LiveStore>(Root);
        return LUI_NATIVE_OK;
    } catch (...) { return LUI_NATIVE_CAPABILITY_DENIED; }
}
#endif
