#include "Domain.h"
#include "../internal/NativeServices.h"
#include "../internal/ApplicationServices.h"
#include "../internal/Dispatch.h"
#include "../project/Files.h"
#include <nlohmann/json.hpp>
#include <map>

struct DesktopContext;
struct DesktopServiceState {
    LuiDesktopServicesV1 Bridge{};
    bool Retiring = false;
    DesktopContext* Notifications = nullptr;
    DesktopContext* Tray = nullptr;
};
struct DesktopOperation { LuiNativeIdV2 Request, Input; };
struct DesktopContext {
    std::shared_ptr<DesktopServiceState> State;
    LuiNativeHostV2 Host{sizeof(LuiNativeHostV2),2};
    bool IsTray = false;
    std::map<uint64_t,DesktopOperation> Pending;
    std::map<uint64_t,LuiNativeIdV2> Watches;
    std::map<uint64_t,LuiNativeIdV2> Icons;
};

namespace {
constexpr LuiNativeIdV2 None{};
void LUI_CALL Start(void* Pointer,LuiNativeIdV2 Request,uint32_t Method,LuiNativeIdV2 Input,LuiNativeIdV2 Handle) {
    auto& Value = *static_cast<DesktopContext*>(Pointer); auto& Host = Value.Host;
    if (Value.State->Retiring) { Host.Complete(Host.Session,Request,LUI_NATIVE_CLOSED,None,None); return; }
    if (Value.IsTray && Method == 1) {
        if (!Value.Icons.count(Handle.Value)) { Host.Complete(Host.Session,Request,LUI_NATIVE_CLOSED,None,None); return; }
        for (const auto& Watch : Value.Watches) if (Watch.second.Value == Handle.Value) {
            Host.Complete(Host.Session,Request,LUI_NATIVE_INVALID_ARGUMENT,None,None); return;
        }
        const auto Status = Host.RetainHandle(Host.Session,Handle);
        if (Status) { Host.Complete(Host.Session,Request,Status,None,None); return; }
        try { Value.Watches.emplace(Request.Value,Handle); }
        catch (...) { Host.ReleaseHandle(Host.Session,Handle); Host.Complete(Host.Session,Request,LUI_NATIVE_LIMIT_EXCEEDED,None,None); }
        return;
    }
    const auto Status = Host.RetainBytes(Host.Session,Input);
    if (Status) { Host.Complete(Host.Session,Request,Status,None,None); return; }
    try { Value.Pending.emplace(Request.Value,DesktopOperation{Request,Input}); }
    catch (...) { Host.ReleaseBytes(Host.Session,Input); Host.Complete(Host.Session,Request,LUI_NATIVE_LIMIT_EXCEEDED,None,None); }
}
void LUI_CALL Cancel(void* Pointer,LuiNativeIdV2 Request) {
    auto& Value = *static_cast<DesktopContext*>(Pointer);
    auto Pending = Value.Pending.find(Request.Value);
    if (Pending != Value.Pending.end()) { Value.Host.ReleaseBytes(Value.Host.Session,Pending->second.Input); Value.Pending.erase(Pending); }
    auto Watch = Value.Watches.find(Request.Value);
    if (Watch != Value.Watches.end()) { Value.Host.ReleaseHandle(Value.Host.Session,Watch->second); Value.Watches.erase(Watch); }
}
void LUI_CALL Close(void* Pointer,LuiNativeIdV2 Handle,uint32_t) {
    auto& Value = *static_cast<DesktopContext*>(Pointer);
    const auto Icon = Value.Icons.find(Handle.Value);
    if (Icon != Value.Icons.end()) {
        Value.Icons.erase(Icon); Value.State->Bridge.RemoveTray(Value.State->Bridge.Context,Handle);
    }
    for (auto Watch = Value.Watches.begin(); Watch != Value.Watches.end();) {
        if (Watch->second.Value != Handle.Value) { ++Watch; continue; }
        const LuiNativeIdV2 Request{Handle.Session,Watch->first};
        Value.Host.Complete(Value.Host.Session,Request,LUI_NATIVE_CLOSED,None,None);
        Value.Host.ReleaseHandle(Value.Host.Session,Watch->second); Watch = Value.Watches.erase(Watch);
    }
}
void LUI_CALL Shutdown(void* Pointer) {
    auto& Value = *static_cast<DesktopContext*>(Pointer);
    while (!Value.Pending.empty()) Cancel(Pointer,Value.Pending.begin()->second.Request);
    while (!Value.Watches.empty()) {
        Value.Host.ReleaseHandle(Value.Host.Session,Value.Watches.begin()->second); Value.Watches.erase(Value.Watches.begin());
    }
}
uint32_t LUI_CALL Poll(void*) { return 1; }
void LUI_CALL Destroy(void* Pointer) {
    auto* Value = static_cast<DesktopContext*>(Pointer);
    if (Value->IsTray) Value->State->Tray = nullptr; else Value->State->Notifications = nullptr;
    delete Value;
}
const LuiNativeProviderV2 Provider{sizeof(LuiNativeProviderV2),2,Start,Cancel,Close,Shutdown,Poll,Destroy};
const LuiNativeMethodV2 NotificationMethods[] = {{sizeof(LuiNativeMethodV2),2,"Show",1024,0,0,0,30000,0}};
const LuiNativeMethodV2 TrayMethods[] = {
    {sizeof(LuiNativeMethodV2),2,"Create",127,0,0,1,30000,0},
    {sizeof(LuiNativeMethodV2),2,"Watch",0,0,1,0,300000,0}
};
const LuiNativeServiceV2 NotificationService{sizeof(LuiNativeServiceV2),2,"NotificationService",NotificationMethods,1,0,LUI_CAPABILITY_NOTIFICATIONS,1,0};
const LuiNativeServiceV2 TrayService{sizeof(LuiNativeServiceV2),2,"TrayService",TrayMethods,2,1,LUI_CAPABILITY_TRAY,1,0};
bool Text(const std::string& Bytes,size_t Maximum,bool Empty = true) {
    return Bytes.size() <= Maximum && (Empty || !Bytes.empty()) && Lui::Project::Internal::Utf8(Bytes);
}
void Pump(DesktopContext* Value) {
    if (!Value || Value->State->Retiring) return;
    // At most 32 admissions exist for one provider. Work admitted by a later
    // Luau callback waits for the next owner turn.
    auto Pending = std::move(Value->Pending); Value->Pending.clear();
    for (const auto& Pair : Pending) {
        const auto& Operation = Pair.second; auto& Host = Value->Host;
        if (!Lui::Native::RequestRunning(Host.Session,Operation.Request)) { Host.ReleaseBytes(Host.Session,Operation.Input); continue; }
        LuiNativeIdV2 Handle{}; uint32_t Status = 0;
        try {
            const void* Bytes = nullptr; uint64_t Length = 0;
            Status = Host.ViewBytes(Host.Session,Operation.Input,&Bytes,&Length);
            if (!Status) {
                const std::string Input(Bytes ? static_cast<const char*>(Bytes) : "",static_cast<size_t>(Length));
                if (Value->IsTray) {
                    if (!Text(Input,127,false)) Status = LUI_NATIVE_INVALID_ARGUMENT;
                    else {
                        Status = Host.CreateHandle(Host.Session,1,&Handle);
                        if (!Status) {
                            Value->Icons.emplace(Handle.Value,Handle);
                            Status = Value->State->Bridge.CreateTray(Value->State->Bridge.Context,Handle,Input.c_str());
                            if (Status) Value->Icons.erase(Handle.Value);
                        }
                    }
                } else {
                    const auto Message = nlohmann::json::parse(Input);
                    if (!Message.is_object() || Message.size() != 2 || !Message.contains("Title") || !Message.contains("Body") ||
                        !Message["Title"].is_string() || !Message["Body"].is_string()) Status = LUI_NATIVE_INVALID_ARGUMENT;
                    else {
                        const auto Title = Message["Title"].get<std::string>(), Body = Message["Body"].get<std::string>();
                        if (!Text(Title,63,false) || !Text(Body,255)) Status = LUI_NATIVE_INVALID_ARGUMENT;
                        else Status = Value->State->Bridge.Notify(Value->State->Bridge.Context,Title.c_str(),Body.c_str());
                    }
                }
            }
        } catch (const std::bad_alloc&) { Status = LUI_NATIVE_LIMIT_EXCEEDED; }
        catch (...) { Status = LUI_NATIVE_INVALID_ARGUMENT; }
        if (Status > LUI_NATIVE_PROVIDER_FAILURE) Status = LUI_NATIVE_PROVIDER_FAILURE;
        Host.Complete(Host.Session,Operation.Request,Status,None,Status ? None : Handle);
        if (Handle.Value) Host.ReleaseHandle(Host.Session,Handle);
        Host.ReleaseBytes(Host.Session,Operation.Input);
    }
}
}

void PumpDesktopServices(LuiRuntime* Runtime) {
    const auto State = Runtime->DesktopServices; if (!State || State->Retiring) return;
    ++Runtime->BackendDepth;
    struct Guard { LuiRuntime* Runtime; ~Guard() { --Runtime->BackendDepth; } } Reset{Runtime};
    Pump(State->Notifications); Pump(State->Tray);
}
void RetireDesktopServices(LuiRuntime* Runtime) {
    if (Runtime->DesktopServices) Runtime->DesktopServices->Retiring = true;
}
extern "C" LUI_API uint32_t LUI_CALL Lui_InstallDesktopServicesV1(LuiRuntime* Runtime,const LuiDesktopServicesV1* Services) {
    if (!CheckOwner(Runtime) || !Services || Services->StructSize != sizeof(*Services) || Services->AbiVersion != 1 ||
        !Runtime->CapabilitiesDeclared || Runtime->ApplicationStarted || Runtime->ProjectNativeVerified || Runtime->VmDepth || Runtime->BackendDepth || Runtime->ShuttingDown || Runtime->DesktopServices ||
        (Services->SupportedCapabilities & ~(LUI_CAPABILITY_NOTIFICATIONS | LUI_CAPABILITY_TRAY)) ||
        ((Services->SupportedCapabilities & LUI_CAPABILITY_NOTIFICATIONS) && !Services->Notify) ||
        ((Services->SupportedCapabilities & LUI_CAPABILITY_TRAY) && (!Services->CreateTray || !Services->RemoveTray))) return LUI_NATIVE_INVALID_ARGUMENT;
    const size_t Previous = Runtime->NativeServices.size();
    auto Rollback = [&] {
        while (Runtime->NativeServices.size() > Previous) {
            Lui_RetireNativeSessionV2(Runtime->NativeServices.back()->Host.Session); Runtime->NativeServices.pop_back();
        }
        Lui::Native::Pump(Runtime->NativeDomain);
    };
    try {
        auto State = std::make_shared<DesktopServiceState>(); State->Bridge = *Services;
        for (const auto* Service : {&NotificationService,&TrayService}) {
            if (!(Runtime->GrantedCapabilities & Service->RequiredCapabilities) || !(Services->SupportedCapabilities & Service->RequiredCapabilities)) continue;
            auto Value = std::make_unique<DesktopContext>(); Value->State = State; Value->IsTray = Service == &TrayService;
            const auto Status = RegisterPlatformNativeService(Runtime,Service,&Provider,Value.get(),&Value->Host);
            if (Status) { Rollback(); return Status; }
            if (Value->IsTray) State->Tray = Value.get(); else State->Notifications = Value.get();
            Value.release();
        }
        Runtime->DesktopServices = State; return 0;
    } catch (...) { Rollback(); return LUI_NATIVE_LIMIT_EXCEEDED; }
}
extern "C" LUI_API uint32_t LUI_CALL Lui_TrayActivatedV1(LuiRuntime* Runtime,LuiNativeIdV2 Icon) {
    if (!CheckOwner(Runtime)) return LUI_NATIVE_INVALID_ARGUMENT;
    auto State = Runtime->DesktopServices;
    if (!State || State->Retiring || Runtime->ShuttingDown || !State->Tray) return LUI_NATIVE_CLOSED;
    auto& Value = *State->Tray; const auto Found = Value.Icons.find(Icon.Value);
    if (Found == Value.Icons.end() || Found->second.Session != Icon.Session) return LUI_NATIVE_INVALID_ARGUMENT;
    if (!Lui::Native::HandleOpen(Value.Host.Session,Icon)) return LUI_NATIVE_CLOSED;
    uint32_t Status = LUI_NATIVE_WOULD_BLOCK;
    for (const auto& Watch : Value.Watches) if (Watch.second.Value == Icon.Value) {
        LuiNativeIdV2 Buffer{}; Status = Value.Host.CopyBytes(Value.Host.Session,None,nullptr,0,&Buffer);
        if (!Status) {
            Status = Value.Host.Emit(Value.Host.Session,{Icon.Session,Watch.first},Buffer);
            Value.Host.ReleaseBytes(Value.Host.Session,Buffer);
        }
        break;
    }
    return Status;
}
