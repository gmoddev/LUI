#include "Domain.h"
#include "LuiOwnerWake.h"
#include <array>
#include "../internal/NativeServices.h"
#include "../internal/Dispatch.h"
#include "../internal/Extensions.h"
#include "../internal/Modules.h"
#include "../reflection/Schema.h"
#include "lua.h"
#include "lualib.h"
#include <cstring>
#include <filesystem>
#include <nlohmann/json.hpp>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
constexpr int HandleTag = 2, RequestTag = 3;
constexpr LuiNativeIdV2 None{};
const char* ErrorName(uint32_t Code) {
    static const char* Names[] = {"Ok", "InvalidArgument", "CapabilityDenied", "Unsupported", "LimitExceeded",
        "WouldBlock", "Canceled", "DeadlineExceeded", "Closed", "ProviderFailure", "AlreadySettled"};
    return Code <= LUI_NATIVE_ALREADY_SETTLED ? Names[Code] : "ProviderFailure";
}
int Raise(lua_State* State, uint32_t Code) {
    luaL_error(State, "[LUI:NativeService] %s", ErrorName(Code)); return 0;
}
struct NativeValue { LuiNativeHostV2 Host; LuiNativeIdV2 Id; };
NativeValue* CheckValue(lua_State* State, int Index, int Tag) {
    auto* Value = static_cast<NativeValue*>(lua_touserdatatagged(State, Index, Tag));
    if (!Value) luaL_error(State, "[LUI:NativeService] InvalidArgument");
    return Value;
}
int Close(lua_State* State) {
    CheckModuleEffect(State); auto* Value = CheckValue(State, 1, HandleTag);
    const auto Status = Lui_CloseNativeHandleV2(Value->Host.Session, Value->Id);
    lua_pushboolean(State, Status == 0); return 1;
}
int Cancel(lua_State* State) {
    CheckModuleEffect(State); auto* Value = CheckValue(State, 1, RequestTag);
    const auto Status = Lui_CancelNativeRequestV2(Value->Host.Session, Value->Id);
    lua_pushboolean(State, Status == 0); lua_pushstring(State, ErrorName(Status)); return 2;
}
int HandleIndex(lua_State* State) {
    CheckValue(State, 1, HandleTag); const char* Key = luaL_checkstring(State, 2);
    if (!std::strcmp(Key, "Close")) lua_pushcfunction(State, Close, "NativeHandle.Close");
    else lua_pushnil(State);
    return 1;
}
int RequestIndex(lua_State* State) {
    CheckValue(State, 1, RequestTag); const char* Key = luaL_checkstring(State, 2);
    if (!std::strcmp(Key, "Cancel")) lua_pushcfunction(State, Cancel, "NativeRequest.Cancel");
    else lua_pushnil(State);
    return 1;
}
int ValueText(lua_State* State) {
    lua_pushstring(State,lua_touserdatatagged(State,1,HandleTag) ? "NativeHandle" : "NativeRequest"); return 1;
}
int ValueEqual(lua_State* State) {
    auto* First = static_cast<NativeValue*>(lua_touserdatatagged(State,1,HandleTag));
    auto* Second = static_cast<NativeValue*>(lua_touserdatatagged(State,2,HandleTag));
    if (!First && !Second) {
        First = static_cast<NativeValue*>(lua_touserdatatagged(State,1,RequestTag));
        Second = static_cast<NativeValue*>(lua_touserdatatagged(State,2,RequestTag));
    }
    lua_pushboolean(State,First && Second && First->Host.Session == Second->Host.Session &&
        First->Id.Session == Second->Id.Session && First->Id.Value == Second->Id.Value); return 1;
}
void DestroyHandle(lua_State*, void* Pointer) {
    auto* Value = static_cast<NativeValue*>(Pointer);
    if (Value->Id.Value) Value->Host.ReleaseHandle(Value->Host.Session, Value->Id);
}
NativeValue* PushValue(lua_State* State, int Tag, const char* Meta, const LuiNativeHostV2& Host, LuiNativeIdV2 Id) {
    auto* Value = static_cast<NativeValue*>(lua_newuserdatatagged(State, sizeof(NativeValue), Tag));
    *Value = {Host, Id}; lua_getfield(State, LUA_REGISTRYINDEX, Meta); lua_setmetatable(State, -2); return Value;
}
int Call(lua_State* State) {
    CheckModuleEffect(State);
    auto* Runtime = static_cast<LuiRuntime*>(lua_touserdata(State, lua_upvalueindex(1)));
    auto* Binding = static_cast<NativeServiceBinding*>(lua_touserdata(State, lua_upvalueindex(2)));
    const auto Method = static_cast<uint32_t>(lua_tointeger(State, lua_upvalueindex(3)));
    auto* Session = Binding->Host.Session;
    if (Runtime->ShuttingDown || Runtime->PendingAsyncReferences.size() + Runtime->PendingNativeRequests >= 128)
        return Raise(State, LUI_NATIVE_LIMIT_EXCEEDED);
    if (lua_gettop(State) < 4 || lua_gettop(State) > 5 || !lua_isbuffer(State, 2))
        return Raise(State, LUI_NATIVE_INVALID_ARGUMENT);
    size_t Length = 0; const void* Bytes = lua_tobuffer(State, 2, &Length);
    if (Length > Lui::Native::Methods(Session)[Method].MaxInputBytes)
        return Raise(State, LUI_NATIVE_LIMIT_EXCEEDED);
    LuiNativeIdV2 Handle{};
    if (!lua_isnil(State, 3)) {
        auto* Value = CheckValue(State, 3, HandleTag);
        if (Value->Host.Session != Session) return Raise(State, LUI_NATIVE_INVALID_ARGUMENT);
        Handle = Value->Id;
    }
    luaL_checktype(State, 4, LUA_TFUNCTION);
    if (!lua_isnoneornil(State, 5)) luaL_checktype(State, 5, LUA_TFUNCTION);
    const bool HasProgress = !lua_isnoneornil(State, 5);
    auto* Value = PushValue(State, RequestTag, "LuiNativeRequestMeta", Binding->Host, None);
    const int Completion = lua_ref(State, 4);
    const int Progress = HasProgress ? lua_ref(State, 5) : 0;
    LuiNativeIdV2 Input{}, Request{};
    uint32_t Status = Binding->Host.CopyBytes(Session, None, Bytes, Length, &Input);
    if (!Status) {
        Status = Lui_StartNativeRequestV2(Session, Method, Input, Handle, &Request);
        Binding->Host.ReleaseBytes(Session, Input);
    }
    if (!Status) try {
        Binding->Waiters.emplace(Request.Value, NativeServiceWaiter{Completion, Progress});
        ++Runtime->PendingNativeRequests; Value->Id = Request; return 1;
    } catch (...) { Lui_CancelNativeRequestV2(Session, Request); Status = LUI_NATIVE_LIMIT_EXCEEDED; }
    lua_unref(State, Completion); if (Progress) lua_unref(State, Progress);
    return Raise(State, Status);
}
struct DeliveryContext {
    NativeServiceBinding* Binding;
    LuiNativeDeliveryV2* Value;
    int Reference;
};
int Deliver(lua_State* State) {
    auto& Context = *static_cast<DeliveryContext*>(lua_touserdata(State, 1));
    auto& Value = *Context.Value; auto& Host = Context.Binding->Host;
    lua_getref(State, Context.Reference);
    if (Value.Buffer.Value) {
        const void* Bytes = nullptr; uint64_t Length = 0;
        if (Host.ViewBytes(Host.Session, Value.Buffer, &Bytes, &Length)) return Raise(State, LUI_NATIVE_CLOSED);
        auto* Result = lua_newbuffer(State, static_cast<size_t>(Length));
        if (Length) std::memcpy(Result, Bytes, static_cast<size_t>(Length));
    } else lua_pushnil(State);
    int Arguments = 1;
    if (Value.Kind == LUI_NATIVE_TERMINAL) {
        if (Value.Handle.Value) {
            PushValue(State, HandleTag, "LuiNativeHandleMeta", Host, Value.Handle);
            Value.Handle = None; // Transfer delivery's reference into the userdata.
        } else lua_pushnil(State);
        if (Value.Status) lua_pushstring(State, ErrorName(Value.Status)); else lua_pushnil(State);
        Arguments = 3;
    }
    lua_call(State, Arguments, 0); return 0;
}
bool CanRegister(LuiRuntime* Runtime) {
    return CheckOwner(Runtime) && !Runtime->ApplicationStarted && !Runtime->VmDepth && !Runtime->BackendDepth &&
        !Runtime->ShuttingDown && !Runtime->UiCompletionRunning && !Runtime->ProjectNativeVerified;
}
}

void RegisterNativeTypes(lua_State* State) {
    lua_setuserdatadtor(State, HandleTag, DestroyHandle);
    for (const auto& Pair : {std::make_pair("LuiNativeHandleMeta", HandleIndex), std::make_pair("LuiNativeRequestMeta", RequestIndex)}) {
        luaL_newmetatable(State, Pair.first);
        lua_pushcfunction(State, Pair.second, "NativeValue.__index"); lua_setfield(State, -2, "__index");
        lua_pushcfunction(State, ValueText, "NativeValue.__tostring"); lua_setfield(State,-2,"__tostring");
        lua_pushcfunction(State, ValueEqual, "NativeValue.__eq"); lua_setfield(State,-2,"__eq");
        lua_pushstring(State, "locked"); lua_setfield(State, -2, "__metatable"); lua_pop(State, 1);
    }
}
bool HasNativeService(const LuiRuntime* Runtime, const std::string& Name) {
    for (const auto& Binding : Runtime->NativeServices) if (Lui::Native::Name(Binding->Host.Session) == Name) return true;
    return false;
}
bool NativeServiceNeedsHostGrant(const LuiRuntime* Runtime, const std::string& Name) {
    for (const auto& Binding : Runtime->NativeServices)
        if (Lui::Native::Name(Binding->Host.Session) == Name) return !Binding->IsPlatform;
    return false;
}
int PushNativeService(lua_State* State, LuiRuntime* Runtime, const std::string& Name) {
    for (const auto& Binding : Runtime->NativeServices) if (Lui::Native::Name(Binding->Host.Session) == Name) {
        lua_newtable(State); const auto& Methods = Lui::Native::Methods(Binding->Host.Session);
        for (size_t Index = 0; Index < Methods.size(); ++Index) {
            lua_pushlightuserdata(State, Runtime); lua_pushlightuserdata(State, Binding.get()); lua_pushinteger(State, static_cast<int>(Index));
            lua_pushcclosure(State, Call, "NativeService.Call", 3); lua_setfield(State, -2, Methods[Index].Name);
        }
        lua_setreadonly(State, -1, true); return 1;
    }
    return 0;
}
int DrainNativeServices(LuiRuntime* Runtime, size_t Maximum, std::chrono::steady_clock::time_point Deadline) {
    if (!Runtime->NativeDomain) return 0;
    Runtime->UiCompletionRunning = true;
    struct Reset { LuiRuntime* Runtime; ~Reset() { Runtime->UiCompletionRunning = false; } } Guard{Runtime};
    Lui::Native::Pump(Runtime->NativeDomain);
    std::array<LuiNativeSessionV2*,Lui::Native::SessionLimit> Sessions{};
    if (Runtime->NativeServices.size() > Sessions.size()) return 0;
    for (size_t Index = 0; Index < Runtime->NativeServices.size(); ++Index) Sessions[Index] = Runtime->NativeServices[Index]->Host.Session;
    int Count = 0;
    while (Count < 64 && static_cast<size_t>(Count) < Maximum && std::chrono::steady_clock::now() < Deadline) {
        LuiNativeDeliveryV2 Value{sizeof(Value), 2};
        uint32_t Index = 0;
        if (Lui::Native::Take(Runtime->NativeDomain,Sessions.data(),static_cast<uint32_t>(Runtime->NativeServices.size()),&Index,&Value)) break;
        auto* Selected = Runtime->NativeServices[Index].get();
        auto Found = Selected->Waiters.find(Value.Request.Value);
        if (Found != Selected->Waiters.end()) {
            const auto Waiter = Found->second;
            if (Value.Kind == LUI_NATIVE_TERMINAL) { Selected->Waiters.erase(Found); --Runtime->PendingNativeRequests; }
            const int Reference = Value.Kind == LUI_NATIVE_TERMINAL ? Waiter.Complete : Waiter.Progress;
            if (Reference) {
                DeliveryContext Context{Selected, &Value, Reference}; ++Runtime->VmDepth; Runtime->InterruptCount = 0;
                if (lua_cpcall(Runtime->State, Deliver, &Context) != LUA_OK) {
                    lua_pop(Runtime->State, 1);
                    DeliverDiagnostic(Runtime, "Error", "[LUI:NativeService] CallbackFailure");
                }
                --Runtime->VmDepth;
            }
            if (Value.Kind == LUI_NATIVE_TERMINAL) {
                lua_unref(Runtime->State, Waiter.Complete); if (Waiter.Progress) lua_unref(Runtime->State, Waiter.Progress);
            }
        }
        if (Value.Buffer.Value) Selected->Host.ReleaseBytes(Selected->Host.Session, Value.Buffer);
        if (Value.Handle.Value) Selected->Host.ReleaseHandle(Selected->Host.Session, Value.Handle);
        ++Count;
    }
    if(Count==64)Lui::Native::WakePending(Runtime->NativeDomain,Sessions.data(),static_cast<uint32_t>(Runtime->NativeServices.size()));
    return Count;
}
void CloseNativeServices(LuiRuntime* Runtime) {
    for (const auto& Binding : Runtime->NativeServices) {
        Lui_RetireNativeSessionV2(Binding->Host.Session);
        for (const auto& Pair : Binding->Waiters) {
            lua_unref(Runtime->State, Pair.second.Complete);
            if (Pair.second.Progress) lua_unref(Runtime->State, Pair.second.Progress);
        }
        Binding->Waiters.clear();
    }
    Runtime->PendingNativeRequests = 0;
}
extern "C" LUI_API LuiNativeDomainV2* LUI_CALL Lui_GetOwnerDomainV1(LuiRuntime* Runtime) {
    if(!Runtime||Runtime->Owner!=std::this_thread::get_id()||Runtime->ShuttingDown)return nullptr;
    try{return Lui::Native::WrapDomain(Runtime->NativeDomain);}catch(...){return nullptr;}
}
extern "C" LUI_API int LUI_CALL Lui_IsOwnerPumpReadyV1(LuiRuntime* Runtime) {
    if(!Runtime||Runtime->Owner!=std::this_thread::get_id())return 0;
    if(!Runtime->State||Runtime->ShuttingDown||Runtime->BackendFailed)return -1;
    return !Runtime->VmDepth&&!Runtime->BackendDepth&&!Runtime->DrainingBackendEvents&&!Runtime->UiCompletionRunning;
}
std::string NativeServiceSchema(const LuiRuntime* Runtime, const std::string& Base) {
    auto Output = nlohmann::json::parse(Base);
    for (const auto& Binding : Runtime->NativeServices) {
        if (Binding->IsPlatform) continue;
        nlohmann::json Methods = nlohmann::json::array();
        const auto& Name = Lui::Native::Name(Binding->Host.Session);
        for (const auto& Method : Lui::Native::Methods(Binding->Host.Session)) Methods.push_back({{"name", Method.Name},
            {"type", Lui::Project::OwnedSignature(Name)},
            {"inputKind", Method.InputKind}, {"resultKind", Method.ResultKind}, {"maxInputBytes", Method.MaxInputBytes},
            {"maxResultBytes", Method.MaxResultBytes}, {"deadlineMs", Method.DeadlineMilliseconds}});
        Output["services"].push_back({{"name", Name}, {"nativeAbiVersion", 2}, {"methods", Methods}, {"signals", nlohmann::json::array()}});
    }
    return Output.dump();
}
Lui::Project::ServiceDeclaration DescribeNativeService(LuiNativeSessionV2* Session) {
    Lui::Project::ServiceDeclaration Value; Value.ResourceKindCount = Lui::Native::KindCount(Session);
    for (const auto& Method : Lui::Native::Methods(Session)) Value.OwnedMethods.emplace(Method.Name,
        Lui::Project::OwnedMethodDeclaration{Method.MaxInputBytes, Method.MaxResultBytes, Method.InputKind, Method.ResultKind, Method.DeadlineMilliseconds});
    return Value;
}
void ObserveNativeServices(const LuiRuntime* Runtime, Lui::Project::DeclarationSet& Observed, bool HostOnly) {
    for (const auto& Binding : Runtime->NativeServices) {
        if (Binding->IsPlatform) continue;
        if (HostOnly && Binding->IsExtension) continue;
        Observed.Services.emplace(Lui::Native::Name(Binding->Host.Session), DescribeNativeService(Binding->Host.Session));
    }
}
uint32_t LoadNativeImageV2(LuiRuntime* Runtime, void* Image, std::shared_ptr<void> Pin,
    const Lui::Project::DeclarationSet* Declared, const Lui::Project::NativeObservation* Expected) {
#if defined(_WIN32)
    const auto Symbol = [&](const char* Name) { return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(Image), Name)); };
#else
    const auto Symbol = [&](const char* Name) { return dlsym(Image, Name); };
#endif
    auto Query = reinterpret_cast<LuiNativeQueryV2>(Symbol("LuiNativeQuery"));
    auto Init = reinterpret_cast<LuiNativeInitV2>(Symbol("LuiNativeInit"));
    if (!Query || !Init) return LUI_NATIVE_UNSUPPORTED;
    LuiNativeServiceV2 Service{sizeof(Service), 2}; LuiNativeProviderV2 Provider{sizeof(Provider), 2};
    uint32_t Status = Query(&Service, &Provider);
    if (Status) return LUI_NATIVE_UNSUPPORTED;
    if (Expected) {
        // Validate the captured sidecar against Query before invoking Init.
        if (Service.StructSize != sizeof(Service) || Service.AbiVersion != 2 || Service.Reserved ||
            !Service.Name || !Service.Methods || Service.MethodCount == 0 || Service.MethodCount > 32 ||
            Service.ResourceKindCount > 64 || Service.Version != Expected->Version ||
            Service.RequiredCapabilities != Expected->RequiredCapabilities) return LUI_NATIVE_INVALID_ARGUMENT;
        size_t Length = 0; while (Length < 65 && Service.Name[Length]) ++Length;
        if (Length == 65 || std::string(Service.Name, Length) != Expected->Name) return LUI_NATIVE_INVALID_ARGUMENT;
        auto Actual = *Expected; Actual.Services.clear(); auto& Description = Actual.Services[Expected->Name];
        Description.ResourceKindCount = Service.ResourceKindCount;
        for (uint32_t Index = 0; Index < Service.MethodCount; ++Index) {
            const auto& Method = Service.Methods[Index];
            if (Method.StructSize != sizeof(Method) || Method.AbiVersion != 2 || Method.Reserved || !Method.Name)
                return LUI_NATIVE_INVALID_ARGUMENT;
            Length = 0; while (Length < 65 && Method.Name[Length]) ++Length;
            if (Length == 65 || !Description.OwnedMethods.emplace(std::string(Method.Name, Length),
                Lui::Project::OwnedMethodDeclaration{Method.MaxInputBytes,Method.MaxResultBytes,Method.InputKind,Method.ResultKind,Method.DeadlineMilliseconds}).second)
                return LUI_NATIVE_INVALID_ARGUMENT;
        }
        std::string Error; if (!Lui::Project::MatchNative(*Declared, Actual, Error)) return LUI_NATIVE_INVALID_ARGUMENT;
    }
    LuiNativeHostV2 Host{sizeof(Host), 2};
    Status = Lui_RegisterNativeServiceV2(Runtime, &Service, &Provider, nullptr, &Host);
    if (Status) return Status;
    Runtime->NativeServices.back()->IsExtension = true;
    Lui::Native::SetImage(Host.Session, std::move(Pin));
    void* Context = nullptr;
    try { Status = Init(&Host, &Context); } catch (...) { Status = LUI_NATIVE_PROVIDER_FAILURE; }
    Lui::Native::SetContext(Host.Session, Context);
    if (Status) {
        Lui_RetireNativeSessionV2(Host.Session); Runtime->NativeServices.pop_back();
        Lui::Native::Pump(Runtime->NativeDomain); return LUI_NATIVE_PROVIDER_FAILURE;
    }
    return 0;
}

extern "C" LUI_API uint32_t LUI_CALL Lui_SetNativeDomainV2(LuiRuntime* Runtime, LuiNativeDomainV2* Domain) {
    if (!CanRegister(Runtime) || !Domain || !Runtime->NativeServices.empty()) return LUI_NATIVE_INVALID_ARGUMENT;
    auto Value = Lui::Native::GetDomain(Domain);
    // Pump validates the owner without exposing the domain implementation.
    if (Lui_PumpNativeDomainV2(Domain)) return LUI_NATIVE_INVALID_ARGUMENT;
    Runtime->NativeDomain = std::move(Value); return 0;
}
static uint32_t RegisterService(LuiRuntime* Runtime, const LuiNativeServiceV2* Service,
    const LuiNativeProviderV2* Provider, void* Context, LuiNativeHostV2* Host, bool Platform) {
    if (!CanRegister(Runtime) || !Service || Service->StructSize != sizeof(*Service) || Service->AbiVersion != 2 || !Service->Name || !Host ||
        Host->StructSize != sizeof(*Host) || Host->AbiVersion != 2) return LUI_NATIVE_INVALID_ARGUMENT;
    size_t Length = 0; while (Length < 65 && Service->Name[Length]) ++Length;
    if (!Length || Length == 65) return LUI_NATIVE_INVALID_ARGUMENT;
    if ((!Platform && !(Runtime->GrantedCapabilities & LUI_CAPABILITY_HOST_SERVICES)) ||
        (Service->RequiredCapabilities & ~Runtime->GrantedCapabilities)) return LUI_NATIVE_CAPABILITY_DENIED;
    if ((!Platform && (LuiSchema::FindService(Service->Name) || LuiSchema::FindClass(Service->Name))) ||
        HasExtensionService(Runtime, Service->Name) || HasNativeService(Runtime, Service->Name))
        return LUI_NATIVE_INVALID_ARGUMENT;
    try {
        if (!Runtime->NativeDomain) Runtime->NativeDomain = Lui::Native::GetDomain();
        std::unique_ptr<LuiNativeDomainV2, decltype(&Lui_ReleaseNativeDomainV2)> Domain(Lui::Native::WrapDomain(Runtime->NativeDomain), Lui_ReleaseNativeDomainV2);
        auto Binding = std::make_shared<NativeServiceBinding>(); Runtime->NativeServices.reserve(Runtime->NativeServices.size() + 1);
        const auto Status = Lui_OpenNativeSessionV2(Domain.get(), Service, Provider, Context, &Binding->Host);
        if (Status) return Status;
        const auto Lease = Binding->Host.RetainSession(Binding->Host.Session);
        if (Lease) { Lui_RetireNativeSessionV2(Binding->Host.Session); return Lease; }
        Binding->OwnsSession = true;
        Binding->IsPlatform = Platform;
        Runtime->NativeServices.push_back(Binding); *Host = Binding->Host; return 0;
    } catch (...) { return LUI_NATIVE_LIMIT_EXCEEDED; }
}
uint32_t RegisterPlatformNativeService(LuiRuntime* Runtime, const LuiNativeServiceV2* Service,
    const LuiNativeProviderV2* Provider, void* Context, LuiNativeHostV2* Host) {
    return RegisterService(Runtime, Service, Provider, Context, Host, true);
}
extern "C" LUI_API uint32_t LUI_CALL Lui_RegisterNativeServiceV2(LuiRuntime* Runtime, const LuiNativeServiceV2* Service,
    const LuiNativeProviderV2* Provider, void* Context, LuiNativeHostV2* Host) {
    return RegisterService(Runtime, Service, Provider, Context, Host, false);
}
extern "C" LUI_API uint32_t LUI_CALL Lui_LoadNativeServiceV2(LuiRuntime* Runtime, const char* AbsolutePath) {
    if (!CanRegister(Runtime) || !AbsolutePath) return LUI_NATIVE_INVALID_ARGUMENT;
    if (!(Runtime->GrantedCapabilities & LUI_CAPABILITY_NATIVE_EXTENSIONS) || !(Runtime->GrantedCapabilities & LUI_CAPABILITY_HOST_SERVICES))
        return LUI_NATIVE_CAPABILITY_DENIED;
    try {
        const auto Path = std::filesystem::u8path(AbsolutePath);
        if (!Path.is_absolute()) return LUI_NATIVE_INVALID_ARGUMENT;
#if defined(_WIN32)
        DWORD Previous = 0;
        if (!SetThreadErrorMode(GetThreadErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, &Previous))
            return LUI_NATIVE_PROVIDER_FAILURE;
        void* Image = LoadLibraryExW(Path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        SetThreadErrorMode(Previous, nullptr);
        const auto Drop = [](void* Value) { FreeLibrary(static_cast<HMODULE>(Value)); };
#else
        void* Image = dlopen(Path.c_str(), RTLD_NOW | RTLD_LOCAL);
        const auto Drop = [](void* Value) { dlclose(Value); };
#endif
        if (!Image) return LUI_NATIVE_PROVIDER_FAILURE;
        std::shared_ptr<void> Pin(Image, Drop);
        return LoadNativeImageV2(Runtime, Image, std::move(Pin));
    } catch (...) { return LUI_NATIVE_PROVIDER_FAILURE; }
}
