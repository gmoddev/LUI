#include "LuiRuntime.h"
#include "../internal/Dispatch.h"
#include "../internal/Platform.h"

#include "lua.h"
#include "lualib.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

namespace {

int StartRequest(lua_State* State, int (LUI_CALL* Callback)(void*, uint64_t)) {
    auto* Runtime = static_cast<LuiRuntime*>(lua_touserdata(State, lua_upvalueindex(1)));
    luaL_checktype(State, 2, LUA_TFUNCTION);
    if (!Callback || Runtime->PlatformRequests.size() >= 1024) {
        luaL_error(State, "platform request is unavailable or queue is full");
        return 0;
    }
    const uint64_t Id = Runtime->NextPlatformRequestId++;
    const int Reference = lua_ref(State, 2);
    Runtime->PlatformRequests.emplace(Id, Reference);
    int Status = 0;
    try { Status = Callback(Runtime->Platform.Context, Id); }
    catch (...) { Status = 0; }
    if (!Status) {
        Runtime->PlatformRequests.erase(Id);
        lua_unref(State, Reference);
        luaL_error(State, "platform request could not start");
        return 0;
    }
    return 0;
}

int ReadText(lua_State* State) {
    auto* Runtime = static_cast<LuiRuntime*>(lua_touserdata(State, lua_upvalueindex(1)));
    return StartRequest(State, Runtime->Platform.ReadClipboardText);
}

int OpenFile(lua_State* State) {
    auto* Runtime = static_cast<LuiRuntime*>(lua_touserdata(State, lua_upvalueindex(1)));
    return StartRequest(State, Runtime->Platform.OpenFile);
}

int WriteText(lua_State* State) {
    auto* Runtime = static_cast<LuiRuntime*>(lua_touserdata(State, lua_upvalueindex(1)));
    size_t Length = 0;
    const char* Text = luaL_checklstring(State, 2, &Length);
    if (Length > 1024 * 1024) { luaL_error(State, "clipboard text is too large"); return 0; }
    if (!Runtime->Platform.WriteClipboardText) { luaL_error(State, "clipboard is unavailable"); return 0; }
    int Status = 0;
    try { Status = Runtime->Platform.WriteClipboardText(Runtime->Platform.Context, Text); }
    catch (...) { Status = 0; }
    if (!Status) { luaL_error(State, "clipboard write failed"); return 0; }
    lua_pushboolean(State, true);
    return 1;
}

void SetMethod(lua_State* State, LuiRuntime* Runtime, const char* Name, lua_CFunction Function) {
    lua_pushlightuserdata(State, Runtime);
    lua_pushcclosure(State, Function, Name, 1);
    lua_setfield(State, -2, Name);
}
} // namespace

bool PlatformServiceAvailable(const LuiRuntime* Runtime, const std::string& Name) {
    if (Name == "ClipboardService")
        return (Runtime->GrantedCapabilities & LUI_CAPABILITY_CLIPBOARD) &&
            Runtime->Platform.WriteClipboardText && Runtime->Platform.ReadClipboardText;
    if (Name == "DialogService")
        return (Runtime->GrantedCapabilities & LUI_CAPABILITY_DIALOGS) && Runtime->Platform.OpenFile;
    return false;
}

int PushPlatformService(lua_State* State, LuiRuntime* Runtime, const std::string& Name) {
    lua_newtable(State);
    if (Name == "ClipboardService") {
        SetMethod(State, Runtime, "WriteText", WriteText);
        SetMethod(State, Runtime, "ReadText", ReadText);
    } else if (Name == "DialogService") SetMethod(State, Runtime, "OpenFile", OpenFile);
    lua_setreadonly(State, -1, true);
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_SetPlatformCallbacks(LuiRuntime* Runtime,
    const LuiPlatformCallbacksV1* Callbacks) {
    if (!CheckOwner(Runtime)) return 0;
    if (!Callbacks || Callbacks->StructSize < sizeof(LuiPlatformCallbacksV1) ||
        Callbacks->AbiVersion != LUI_EXTENSION_ABI_VERSION || Runtime->ApplicationStarted ||
        !Runtime->ServiceRefs.empty()) {
        Runtime->LastError = "[LUI:Platform] invalid or late platform callback registration";
        return 0;
    }
    Runtime->Platform = *Callbacks;
    Runtime->LastError.clear();
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_CompletePlatformRequest(LuiRuntime* Runtime,
    uint64_t RequestId, const char* Result, const char* Error) {
    if (!CheckOwner(Runtime) || Runtime->ShuttingDown ||
        Runtime->PlatformRequests.find(RequestId) == Runtime->PlatformRequests.end()) return 0;
    for (const auto& Pending : Runtime->PlatformCompletions) if (Pending.Id == RequestId) return 0;
    if (Runtime->PlatformCompletions.size() >= 1024) return 0;
    try {
        PlatformRequestCompletion Completion{};
        Completion.Id = RequestId;
        Completion.HasResult = Result != nullptr;
        if (Result) Completion.Result = Result;
        if (Error) Completion.Error = Error;
        if (Completion.Result.size() > 1024 * 1024 || Completion.Error.size() > 4096) return 0;
        Runtime->PlatformCompletions.push_back(std::move(Completion));
        return 1;
    } catch (...) { return 0; }
}

int DrainPlatformRequests(LuiRuntime* Runtime) {
    int Count = 0;
    // A callback may synchronously queue another completion. Keep one pump bounded.
    while (Count < 64 && !Runtime->PlatformCompletions.empty()) {
        PlatformRequestCompletion Completion = std::move(Runtime->PlatformCompletions.front());
        Runtime->PlatformCompletions.pop_front();
        auto Found = Runtime->PlatformRequests.find(Completion.Id);
        if (Found == Runtime->PlatformRequests.end()) continue;
        const int Reference = Found->second;
        Runtime->PlatformRequests.erase(Found);
        lua_getref(Runtime->State, Reference);
        if (Completion.HasResult && Completion.Error.empty())
            lua_pushlstring(Runtime->State, Completion.Result.data(), Completion.Result.size());
        else lua_pushnil(Runtime->State);
        if (Completion.Error.empty()) lua_pushnil(Runtime->State);
        else lua_pushlstring(Runtime->State, Completion.Error.data(), Completion.Error.size());
        if (!Runtime->VmDepth) Runtime->InterruptCount = 0;
        ++Runtime->VmDepth;
        if (lua_pcall(Runtime->State, 2, 0, 0) != LUA_OK) {
            const char* Message = lua_tostring(Runtime->State, -1);
            Runtime->LastError = Message ? Message : "platform callback failed";
            if (Runtime->LogCallback) Runtime->LogCallback(Runtime->LogContext, "Error", Runtime->LastError.c_str());
            else std::fprintf(stderr, "[LUI:Platform] %s\n", Runtime->LastError.c_str());
            lua_pop(Runtime->State, 1);
        }
        --Runtime->VmDepth;
        lua_unref(Runtime->State, Reference);
        ++Count;
    }
    return Count;
}

void ClosePlatformRequests(LuiRuntime* Runtime) {
    for (const auto& Pair : Runtime->PlatformRequests) lua_unref(Runtime->State, Pair.second);
    Runtime->PlatformRequests.clear();
    Runtime->PlatformCompletions.clear();
}
