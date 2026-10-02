#include "LuiRuntime.h"
#include "../internal/Dispatch.h"
#include "../internal/Extensions.h"
#include "../reflection/Schema.h"

#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace {

void Report(LuiRuntime* Runtime, const char* Level, const std::string& Message) {
    if (Runtime->LogCallback) Runtime->LogCallback(Runtime->LogContext, Level, Message.c_str());
    else std::fprintf(Level[0] == 'E' ? stderr : stdout, "%s\n", Message.c_str());
}

int Fail(LuiRuntime* Runtime, const std::string& Message) {
    Runtime->LastError = "[LUI:Extension] " + Message;
    Report(Runtime, "Error", Runtime->LastError);
    return 0;
}

bool IsIdentifier(const char* Name) {
    if (!Name || !((Name[0] >= 'A' && Name[0] <= 'Z') ||
        (Name[0] >= 'a' && Name[0] <= 'z') || Name[0] == '_')) return false;
    size_t Length = 0;
    for (const char* Current = Name; *Current; ++Current) {
        if (++Length > 64 || !((*Current >= 'A' && *Current <= 'Z') ||
            (*Current >= 'a' && *Current <= 'z') ||
            (*Current >= '0' && *Current <= '9') || *Current == '_')) return false;
    }
    return true;
}

void* OpenLibrary(const std::filesystem::path& Path) {
#if defined(_WIN32)
    return LoadLibraryExW(Path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
    return dlopen(Path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* FindSymbol(void* Library, const char* Name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(Library), Name));
#else
    return dlsym(Library, Name);
#endif
}

void CloseLibrary(void* Library) {
    if (!Library) return;
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(Library));
#else
    dlclose(Library);
#endif
}

void RefreshSchema(LuiRuntime* Runtime);
int LUI_EXTENSION_CALL EmitSignal(void* HostContext, const char* ServiceName,
    const char* SignalName, const LuiValueV1* Arguments, uint32_t ArgumentCount);

struct LibraryGuard {
    void* Library;
    explicit LibraryGuard(void* Value) : Library(Value) { }
    ~LibraryGuard() { CloseLibrary(Library); }
    void Release() { Library = nullptr; }
};

struct InitGuard {
    LuiRuntime* Runtime;
    size_t PreviousCount;
    size_t PreviousSignalCount;
    LuiExtensionShutdownV1 Shutdown;
    void* Context = nullptr;
    bool InitCalled = false;
    bool Committed = false;
    ~InitGuard() {
        if (Committed) return;
        Runtime->InitializingExtension = false;
        Runtime->InitializingExtensionName.clear();
        Runtime->ExtensionMethods.resize(PreviousCount);
        Runtime->ExtensionSignals.resize(PreviousSignalCount);
        if (InitCalled) {
            try { Shutdown(Context); }
            catch (...) { try { Report(Runtime, "Error", "[LUI:Extension] shutdown failed after init rejection"); } catch (...) { } }
        }
        try { RefreshSchema(Runtime); } catch (...) { }
    }
};

int LUI_EXTENSION_CALL RegisterMethod(void* HostContext, const char* ServiceName,
    const char* MethodName, const char* Type, LuiExtensionMethodV1 Callback, void* MethodContext) {
    auto* Runtime = static_cast<LuiRuntime*>(HostContext);
    if (!CheckOwner(Runtime) || !Runtime->InitializingExtension) return 0;
    if (!IsIdentifier(ServiceName) || !IsIdentifier(MethodName) || !Type || !*Type || !Callback ||
        std::char_traits<char>::length(Type) > 256 || LuiSchema::FindService(ServiceName)) {
        Runtime->ExtensionError = "invalid or reserved service method registration";
        return 0;
    }
    for (const auto& Method : Runtime->ExtensionMethods) {
        if (Method->ServiceName != ServiceName) continue;
        if (Method->ExtensionName != Runtime->InitializingExtensionName || Method->Name == MethodName) {
            Runtime->ExtensionError = "duplicate service or method registration";
            return 0;
        }
    }
    for (const auto& Signal : Runtime->ExtensionSignals)
        if (Signal->ServiceName == ServiceName && Signal->Name == MethodName) {
            Runtime->ExtensionError = "method duplicates a signal";
            return 0;
        }
    try {
        auto Method = std::make_unique<ExtensionMethod>();
        Method->ExtensionName = Runtime->InitializingExtensionName;
        Method->ServiceName = ServiceName;
        Method->Name = MethodName;
        Method->Type = Type;
        Method->Callback = Callback;
        Method->Context = MethodContext;
        Runtime->ExtensionMethods.push_back(std::move(Method));
        return 1;
    } catch (...) { return 0; }
}

int LUI_EXTENSION_CALL RegisterSignal(void* HostContext, const char* ServiceName,
    const char* SignalName, const char* Type) {
    auto* Runtime = static_cast<LuiRuntime*>(HostContext);
    if (!CheckOwner(Runtime) || !Runtime->InitializingExtension) return 0;
    if (!IsIdentifier(ServiceName) || !IsIdentifier(SignalName) || !Type || !*Type ||
        std::char_traits<char>::length(Type) > 256 || LuiSchema::FindService(ServiceName)) {
        Runtime->ExtensionError = "invalid or reserved signal registration";
        return 0;
    }
    for (const auto& Method : Runtime->ExtensionMethods)
        if (Method->ServiceName == ServiceName &&
            (Method->ExtensionName != Runtime->InitializingExtensionName || Method->Name == SignalName)) {
            Runtime->ExtensionError = "duplicate service or signal registration";
            return 0;
        }
    for (const auto& Signal : Runtime->ExtensionSignals)
        if (Signal->ServiceName == ServiceName &&
            (Signal->ExtensionName != Runtime->InitializingExtensionName || Signal->Name == SignalName)) {
            Runtime->ExtensionError = "duplicate service or signal registration";
            return 0;
        }
    try {
        auto Signal = std::make_unique<ExtensionSignal>();
        Signal->ExtensionName = Runtime->InitializingExtensionName;
        Signal->ServiceName = ServiceName;
        Signal->Name = SignalName;
        Signal->Type = Type;
        Runtime->ExtensionSignals.push_back(std::move(Signal));
        return 1;
    } catch (...) { return 0; }
}

} // namespace

extern "C" LUI_API int LUI_CALL Lui_RegisterHostMethod(LuiRuntime* Runtime,
    const char* ServiceName, const char* MethodName, const char* Type,
    LuiExtensionMethodV1 Method, void* MethodContext) {
    if (!CheckOwner(Runtime)) return 0;
    if (Runtime->ApplicationStarted || Runtime->VmDepth || Runtime->BackendDepth ||
        Runtime->InitializingExtension) return Fail(Runtime, "host methods must register before scripts");
    Runtime->InitializingExtension = true;
    Runtime->InitializingExtensionName = "Host";
    Runtime->ExtensionError.clear();
    const int Status = RegisterMethod(Runtime, ServiceName, MethodName, Type, Method, MethodContext);
    Runtime->InitializingExtension = false;
    Runtime->InitializingExtensionName.clear();
    if (!Status) return Fail(Runtime, Runtime->ExtensionError.empty() ?
        "host method registration failed" : Runtime->ExtensionError);
    RefreshSchema(Runtime);
    Runtime->LastError.clear();
    return 1;
}

extern "C" LUI_API void LUI_CALL Lui_SetHostError(LuiRuntime* Runtime, const char* Message) {
    if (CheckOwner(Runtime)) Runtime->ExtensionError = Message ? Message : "host method failed";
}

extern "C" LUI_API int LUI_CALL Lui_RegisterHostSignal(LuiRuntime* Runtime,
    const char* ServiceName, const char* SignalName, const char* Type) {
    if (!CheckOwner(Runtime)) return 0;
    if (Runtime->ApplicationStarted || Runtime->VmDepth || Runtime->BackendDepth ||
        Runtime->InitializingExtension) return Fail(Runtime, "host signals must register before scripts");
    Runtime->InitializingExtension = true;
    Runtime->InitializingExtensionName = "Host";
    Runtime->ExtensionError.clear();
    const int Status = RegisterSignal(Runtime, ServiceName, SignalName, Type);
    Runtime->InitializingExtension = false;
    Runtime->InitializingExtensionName.clear();
    if (!Status) return Fail(Runtime, Runtime->ExtensionError.empty() ?
        "host signal registration failed" : Runtime->ExtensionError);
    RefreshSchema(Runtime);
    Runtime->LastError.clear();
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_EmitHostSignal(LuiRuntime* Runtime,
    const char* ServiceName, const char* SignalName,
    const LuiValueV1* Arguments, uint32_t ArgumentCount) {
    return EmitSignal(Runtime, ServiceName, SignalName, Arguments, ArgumentCount);
}

namespace {

void LUI_EXTENSION_CALL HostLog(void* HostContext, const char* Message) {
    auto* Runtime = static_cast<LuiRuntime*>(HostContext);
    try {
        if (CheckOwner(Runtime) && Message)
            Report(Runtime, "Info", "[LUI:Extension] " + std::string(Message));
    } catch (...) { }
}

void LUI_EXTENSION_CALL HostSetError(void* HostContext, const char* Message) {
    auto* Runtime = static_cast<LuiRuntime*>(HostContext);
    try {
        if (CheckOwner(Runtime)) Runtime->ExtensionError = Message ? Message : "extension reported an error";
    } catch (...) { }
}

int LUI_EXTENSION_CALL ScheduleUi(void* HostContext, LuiUiCompletionV1 Completion,
    void* CompletionContext) {
    auto* Runtime = static_cast<LuiRuntime*>(HostContext);
    if (!Runtime || !Completion) return 0;
    try {
        std::lock_guard<std::mutex> Lock(Runtime->CompletionMutex);
        if (Runtime->ShuttingDown || Runtime->PendingUiCompletions.size() >= 4096) return 0;
        Runtime->PendingUiCompletions.push_back({Completion, CompletionContext});
        return 1;
    } catch (...) { return 0; }
}

int LUI_EXTENSION_CALL EmitSignal(void* HostContext, const char* ServiceName,
    const char* SignalName, const LuiValueV1* Arguments, uint32_t ArgumentCount) {
    auto* Runtime = static_cast<LuiRuntime*>(HostContext);
    if (!CheckOwner(Runtime) || Runtime->VmDepth || Runtime->BackendDepth ||
        Runtime->ShuttingDown || ArgumentCount > 32 || (ArgumentCount && !Arguments)) return 0;
    try {
    ExtensionSignal* Signal = nullptr;
    for (const auto& Current : Runtime->ExtensionSignals)
        if (Current->ServiceName == (ServiceName ? ServiceName : "") &&
            Current->Name == (SignalName ? SignalName : "")) { Signal = Current.get(); break; }
    if (!Signal) return 0;
    for (uint32_t Index = 0; Index < ArgumentCount; ++Index) {
        const LuiValueV1& Value = Arguments[Index];
        if (Value.StructSize < sizeof(LuiValueV1) || Value.Type > LUI_VALUE_STRING ||
            (Value.Type == LUI_VALUE_NUMBER && !std::isfinite(Value.Number)) ||
            (Value.Type == LUI_VALUE_STRING &&
                (Value.TextLength > 1024 * 1024 || (!Value.Text && Value.TextLength)))) return 0;
    }
    const auto Snapshot = Signal->Listeners;
    for (const auto& Listener : Snapshot) {
        if (!Listener.Active) continue;
        auto Current = std::find_if(Signal->Listeners.begin(), Signal->Listeners.end(),
            [&](const ExtensionSignalListener& Entry) { return Entry.Id == Listener.Id && Entry.Active; });
        if (Current == Signal->Listeners.end()) continue;
        lua_getref(Runtime->State, Current->Reference);
        for (uint32_t Index = 0; Index < ArgumentCount; ++Index) {
            const LuiValueV1& Value = Arguments[Index];
            switch (Value.Type) {
            case LUI_VALUE_NIL: lua_pushnil(Runtime->State); break;
            case LUI_VALUE_BOOLEAN: lua_pushboolean(Runtime->State, Value.Boolean != 0); break;
            case LUI_VALUE_NUMBER: lua_pushnumber(Runtime->State, Value.Number); break;
            case LUI_VALUE_STRING:
                lua_pushlstring(Runtime->State, Value.Text ? Value.Text : "",
                    static_cast<size_t>(Value.TextLength));
                break;
            }
        }
        Runtime->InterruptCount = 0;
        ++Runtime->VmDepth;
        if (lua_pcall(Runtime->State, static_cast<int>(ArgumentCount), 0, 0) != LUA_OK) {
            const char* Message = lua_tostring(Runtime->State, -1);
            Report(Runtime, "Error", "[LUI:Extension] signal callback failed: " +
                std::string(Message ? Message : "unknown error"));
            lua_pop(Runtime->State, 1);
        }
        --Runtime->VmDepth;
    }
    return 1;
    } catch (...) { return 0; }
}

int DisconnectExtensionSignal(lua_State* State) {
    auto* Runtime = static_cast<LuiRuntime*>(lua_touserdata(State, lua_upvalueindex(1)));
    auto* Signal = static_cast<ExtensionSignal*>(lua_touserdata(State, lua_upvalueindex(2)));
    const int Id = static_cast<int>(lua_tointeger(State, lua_upvalueindex(3)));
    for (auto& Listener : Signal->Listeners) {
        if (Listener.Id != Id || !Listener.Active) continue;
        Listener.Active = false;
        lua_unref(Runtime->State, Listener.Reference);
        Listener.Reference = 0;
        break;
    }
    return 0;
}

int ConnectExtensionSignal(lua_State* State) {
    auto* Runtime = static_cast<LuiRuntime*>(lua_touserdata(State, lua_upvalueindex(1)));
    auto* Signal = static_cast<ExtensionSignal*>(lua_touserdata(State, lua_upvalueindex(2)));
    luaL_checktype(State, 2, LUA_TFUNCTION);
    const int Reference = lua_ref(State, 2);
    const int Id = Runtime->NextExtensionListenerId++;
    Signal->Listeners.push_back({Id, Reference, true});
    lua_newtable(State);
    lua_pushlightuserdata(State, Runtime);
    lua_pushlightuserdata(State, Signal);
    lua_pushinteger(State, Id);
    lua_pushcclosure(State, DisconnectExtensionSignal, "ExtensionSignal.Disconnect", 3);
    lua_setfield(State, -2, "Disconnect");
    lua_setreadonly(State, -1, true);
    return 1;
}

int CallMethod(lua_State* State) {
    auto* Method = static_cast<ExtensionMethod*>(lua_touserdata(State, lua_upvalueindex(1)));
    const int Count = lua_gettop(State) - 1;
    if (Count < 0 || Count > 32) { luaL_error(State, "extension method accepts at most 32 arguments"); return 0; }
    std::vector<LuiValueV1> Arguments;
    Arguments.reserve(static_cast<size_t>(Count));
    for (int Index = 2; Index <= Count + 1; ++Index) {
        LuiValueV1 Value{};
        Value.StructSize = sizeof(Value);
        switch (lua_type(State, Index)) {
        case LUA_TNIL: Value.Type = LUI_VALUE_NIL; break;
        case LUA_TBOOLEAN:
            Value.Type = LUI_VALUE_BOOLEAN;
            Value.Boolean = lua_toboolean(State, Index);
            break;
        case LUA_TNUMBER:
            Value.Type = LUI_VALUE_NUMBER;
            Value.Number = lua_tonumber(State, Index);
            if (!std::isfinite(Value.Number)) {
                luaL_error(State, "extension method arguments must be finite numbers");
                return 0;
            }
            break;
        case LUA_TSTRING:
            Value.Type = LUI_VALUE_STRING;
            {
                size_t Length = 0;
                Value.Text = lua_tolstring(State, Index, &Length);
                Value.TextLength = Length;
                if (Length > 1024 * 1024) {
                    luaL_error(State, "extension method string argument is too large");
                    return 0;
                }
            }
            break;
        default:
            luaL_error(State, "extension method arguments must be nil, boolean, number, or string");
            return 0;
        }
        Arguments.push_back(Value);
    }
    auto* Runtime = static_cast<LuiRuntime*>(lua_touserdata(State, lua_upvalueindex(2)));
    if (Runtime->Sandboxed && Method->ExtensionName == "Host" &&
        !(Runtime->GrantedCapabilities & LUI_CAPABILITY_HOST_SERVICES)) {
        luaL_error(State, "host service capability was not granted");
        return 0;
    }
    Runtime->ExtensionError.clear();
    LuiValueV1 Result{};
    Result.StructSize = sizeof(Result);
    int Status = 0;
    try {
        Status = Method->Callback(Method->Context, Arguments.data(), static_cast<uint32_t>(Arguments.size()), &Result);
    } catch (...) {
        Runtime->ExtensionError = "native method threw an exception";
    }
    if (!Status) {
        const std::string Error = Runtime->ExtensionError.empty() ? "native method failed" : Runtime->ExtensionError;
        luaL_error(State, "%s", Error.c_str());
        return 0;
    }
    if (Result.StructSize < sizeof(Result)) { luaL_error(State, "native method returned a short value structure"); return 0; }
    switch (Result.Type) {
    case LUI_VALUE_NIL: lua_pushnil(State); return 1;
    case LUI_VALUE_BOOLEAN: lua_pushboolean(State, Result.Boolean != 0); return 1;
    case LUI_VALUE_NUMBER:
        if (!std::isfinite(Result.Number)) { luaL_error(State, "native method returned a non-finite number"); return 0; }
        lua_pushnumber(State, Result.Number);
        return 1;
    case LUI_VALUE_STRING:
        if (Result.TextLength > 1024 * 1024 || (!Result.Text && Result.TextLength))
            { luaL_error(State, "native method returned an invalid string"); return 0; }
        lua_pushlstring(State, Result.Text ? Result.Text : "", static_cast<size_t>(Result.TextLength));
        return 1;
    default: luaL_error(State, "native method returned an unknown value type"); return 0;
    }
}

std::string EscapeJson(const std::string& Value) {
    std::string Result;
    for (unsigned char Character : Value) {
        if (Character == '"' || Character == '\\') { Result += '\\'; Result += static_cast<char>(Character); }
        else if (Character < 32) Result += '?';
        else Result += static_cast<char>(Character);
    }
    return Result;
}

void RefreshSchema(LuiRuntime* Runtime) {
    std::map<std::string, std::map<std::string, std::string>> Services;
    std::map<std::string, std::map<std::string, std::string>> Signals;
    std::set<std::string> Names;
    for (const auto& Method : Runtime->ExtensionMethods)
        { Services[Method->ServiceName][Method->Name] = Method->Type; Names.insert(Method->ServiceName); }
    for (const auto& Signal : Runtime->ExtensionSignals)
        { Signals[Signal->ServiceName][Signal->Name] = Signal->Type; Names.insert(Signal->ServiceName); }
    std::string Json = "{\"schemaVersion\":2,\"services\":[";
    bool FirstService = true;
    for (const auto& Name : Names) {
        if (!FirstService) Json += ',';
        FirstService = false;
        Json += "{\"name\":\"" + EscapeJson(Name) + "\",\"methods\":[";
        bool FirstMethod = true;
        for (const auto& Method : Services[Name]) {
            if (!FirstMethod) Json += ',';
            FirstMethod = false;
            Json += "{\"name\":\"" + EscapeJson(Method.first) + "\",\"type\":\"" +
                EscapeJson(Method.second) + "\"}";
        }
        Json += "],\"signals\":[";
        bool FirstSignal = true;
        for (const auto& Signal : Signals[Name]) {
            if (!FirstSignal) Json += ',';
            FirstSignal = false;
            Json += "{\"name\":\"" + EscapeJson(Signal.first) + "\",\"type\":\"" +
                EscapeJson(Signal.second) + "\"}";
        }
        Json += "]}";
    }
    Runtime->ExtensionSchemaJson = Json + "]}";
}

} // namespace

bool HasExtensionService(const LuiRuntime* Runtime, const std::string& Name) {
    for (const auto& Method : Runtime->ExtensionMethods)
        if (Method->ServiceName == Name) return true;
    for (const auto& Signal : Runtime->ExtensionSignals)
        if (Signal->ServiceName == Name) return true;
    return false;
}

int PushExtensionService(lua_State* State, LuiRuntime* Runtime, const std::string& Name) {
    lua_newtable(State);
    for (const auto& Method : Runtime->ExtensionMethods) {
        if (Method->ServiceName != Name) continue;
        lua_pushlightuserdata(State, Method.get());
        lua_pushlightuserdata(State, Runtime);
        lua_pushcclosure(State, CallMethod, Method->Name.c_str(), 2);
        lua_setfield(State, -2, Method->Name.c_str());
    }
    for (const auto& Signal : Runtime->ExtensionSignals) {
        if (Signal->ServiceName != Name) continue;
        lua_newtable(State);
        lua_pushlightuserdata(State, Runtime);
        lua_pushlightuserdata(State, Signal.get());
        lua_pushcclosure(State, ConnectExtensionSignal, "ExtensionSignal.Connect", 2);
        lua_setfield(State, -2, "Connect");
        lua_setreadonly(State, -1, true);
        lua_setfield(State, -2, Signal->Name.c_str());
    }
    lua_setreadonly(State, -1, true);
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_DeclareCapabilities(LuiRuntime* Runtime,
    const LuiCapabilityDeclarationV1* Declaration) {
    if (!CheckOwner(Runtime)) return 0;
    if (!Declaration || Declaration->StructSize < sizeof(LuiCapabilityDeclarationV1) ||
        Declaration->AbiVersion != LUI_EXTENSION_ABI_VERSION ||
        (Declaration->GrantedCapabilities & ~(LUI_CAPABILITY_NATIVE_EXTENSIONS | LUI_CAPABILITY_HOST_SERVICES |
            LUI_CAPABILITY_CLIPBOARD | LUI_CAPABILITY_DIALOGS | LUI_CAPABILITY_NETWORK_CLIENT |
            LUI_CAPABILITY_NETWORK_SERVER | LUI_CAPABILITY_NETWORK_RAW)))
        return Fail(Runtime, "invalid capability declaration");
    if (Runtime->CapabilitiesDeclared || Runtime->ApplicationStarted || !Runtime->Extensions.empty())
        return Fail(Runtime, "capabilities must be declared once before scripts or extensions");
    if (Runtime->Sandboxed && (Declaration->GrantedCapabilities & LUI_CAPABILITY_NATIVE_EXTENSIONS))
        return Fail(Runtime, "sandboxed applications cannot load native extensions");
    Runtime->GrantedCapabilities = Declaration->GrantedCapabilities;
    Runtime->CapabilitiesDeclared = true;
    Runtime->LastError.clear();
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_LoadExtension(LuiRuntime* Runtime, const char* Path) {
    if (!CheckOwner(Runtime)) return 0;
    if (Runtime->ApplicationStarted || Runtime->VmDepth || Runtime->BackendDepth || Runtime->InitializingExtension)
        return Fail(Runtime, "extensions must load before application scripts");
    if (!(Runtime->GrantedCapabilities & LUI_CAPABILITY_NATIVE_EXTENSIONS))
        return Fail(Runtime, "native extensions capability was not granted");
    if (!Path || !*Path) return Fail(Runtime, "extension path is empty");
    try {
        const std::filesystem::path LibraryPath = std::filesystem::u8path(Path);
        if (!LibraryPath.is_absolute()) return Fail(Runtime, "extension path must be absolute");
        LibraryGuard Library(OpenLibrary(LibraryPath));
        if (!Library.Library) return Fail(Runtime, "could not load extension: " + LibraryPath.u8string());
        auto Query = reinterpret_cast<LuiExtensionQueryV1>(FindSymbol(Library.Library, "LuiExtensionQuery"));
        auto Init = reinterpret_cast<LuiExtensionInitV1>(FindSymbol(Library.Library, "LuiExtensionInit"));
        auto Shutdown = reinterpret_cast<LuiExtensionShutdownV1>(FindSymbol(Library.Library, "LuiExtensionShutdown"));
        if (!Query || !Init || !Shutdown) {
            return Fail(Runtime, "extension is missing a required ABI entry point");
        }
        LuiExtensionInfoV1 Info{};
        Info.StructSize = sizeof(Info);
        Info.AbiVersion = LUI_EXTENSION_ABI_VERSION;
        int QueryStatus = 0;
        try { QueryStatus = Query(&Info); }
        catch (...) { QueryStatus = 0; }
        if (!QueryStatus || Info.StructSize < sizeof(Info) || Info.AbiVersion != LUI_EXTENSION_ABI_VERSION ||
            !IsIdentifier(Info.Name) || !Info.ExtensionVersion ||
            (Info.RequiredCapabilities & ~Runtime->GrantedCapabilities)) {
            return Fail(Runtime, "extension ABI, identity, or required capabilities are incompatible");
        }
        const std::string Name = Info.Name;
        for (const auto& Existing : Runtime->Extensions) {
            if (Existing.Name == Name) {
                return Fail(Runtime, "extension name is already loaded: " + Name);
            }
        }
        LuiHostApiV1 Host{sizeof(LuiHostApiV1), LUI_EXTENSION_ABI_VERSION, Runtime,
            RegisterMethod, HostLog, HostSetError, ScheduleUi, RegisterSignal, EmitSignal};
        InitGuard Pending{Runtime, Runtime->ExtensionMethods.size(), Runtime->ExtensionSignals.size(), Shutdown};
        Runtime->InitializingExtension = true;
        Runtime->InitializingExtensionName = Name;
        Runtime->ExtensionError.clear();
        int InitStatus = 0;
        Pending.InitCalled = true;
        try { InitStatus = Init(&Host, &Pending.Context); }
        catch (...) { Runtime->ExtensionError = "extension initializer threw an exception"; }
        Runtime->InitializingExtension = false;
        Runtime->InitializingExtensionName.clear();
        if (!InitStatus || !Runtime->ExtensionError.empty()) {
            const std::string Error = Runtime->ExtensionError.empty() ? "extension initializer failed" : Runtime->ExtensionError;
            return Fail(Runtime, Error);
        }
        RefreshSchema(Runtime);
        Runtime->Extensions.push_back({Name, Library.Library, Shutdown, Pending.Context});
        Pending.Committed = true;
        Library.Release();
        Runtime->LastError.clear();
        try { Report(Runtime, "Info", "[LUI:Extension] loaded " + Name); } catch (...) { }
        return 1;
    } catch (const std::exception& Error) {
        return Fail(Runtime, std::string("extension loader failed: ") + Error.what());
    }
}

extern "C" LUI_API const char* LUI_CALL Lui_GetExtensionSchemaJson(LuiRuntime* Runtime) {
    if (!CheckOwner(Runtime)) return "{}";
    if (Runtime->ExtensionSchemaJson.empty()) RefreshSchema(Runtime);
    return Runtime->ExtensionSchemaJson.c_str();
}

void CloseExtensions(LuiRuntime* Runtime) {
    for (auto Current = Runtime->Extensions.rbegin(); Current != Runtime->Extensions.rend(); ++Current) {
        try { Current->Shutdown(Current->Context); }
        catch (...) { Report(Runtime, "Error", "[LUI:Extension] shutdown failed for " + Current->Name); }
        CloseLibrary(Current->Library);
    }
    Runtime->Extensions.clear();
    Runtime->ExtensionMethods.clear();
    Runtime->ExtensionSignals.clear();
}

void CloseExtensionListeners(LuiRuntime* Runtime) {
    for (const auto& Signal : Runtime->ExtensionSignals)
        for (auto& Listener : Signal->Listeners) {
            if (Listener.Active) lua_unref(Runtime->State, Listener.Reference);
            Listener.Active = false;
            Listener.Reference = 0;
        }
}

int DrainUiCompletions(LuiRuntime* Runtime) {
    std::deque<PendingUiCompletion> Ready;
    {
        std::lock_guard<std::mutex> Lock(Runtime->CompletionMutex);
        Ready.swap(Runtime->PendingUiCompletions);
    }
    int Count = 0;
    for (const PendingUiCompletion& Completion : Ready) {
        Runtime->UiCompletionRunning = true;
        try { Completion.Callback(Completion.Context); }
        catch (...) { Report(Runtime, "Error", "[LUI:Extension] UI completion threw an exception"); }
        Runtime->UiCompletionRunning = false;
        ++Count;
    }
    return Count;
}
