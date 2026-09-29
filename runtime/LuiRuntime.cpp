#include "LuiRuntime.h"

#include "Luau/Compiler.h"
#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

struct Dimension {
    double Scale = 0;
    double Offset = 0;
};

struct SizeValue {
    Dimension X;
    Dimension Y;
};

struct Node {
    int Id = 0;
    int Reference = 0;
    int ParentId = 0;
    bool Destroyed = false;
    bool DestroyingInProgress = false;
    bool Visible = true;
    std::string ClassName;
    std::string Name;
    std::string Title;
    std::string Text;
    SizeValue Size;
    std::vector<int> Children;
    std::vector<int> Listeners;
};

struct Listener {
    int Id = 0;
    int NodeId = 0;
    int Reference = 0;
    std::string Signal;
    bool Active = true;
};

struct ScheduledCall {
    int Reference = 0;
    std::chrono::steady_clock::time_point Due;
};

struct LuiRuntime {
    lua_State* State = nullptr;
    std::thread::id Owner;
    LuiBackendCallbacks Backend{};
    std::unordered_map<int, std::unique_ptr<Node>> Nodes;
    std::unordered_map<int, Listener> Listeners;
    std::vector<ScheduledCall> Tasks;
    std::string LastError;
    int NextNodeId = 1;
    int NextListenerId = 1;
    bool LayoutDirty = false;
};

struct SignalValue {
    int NodeId;
    const char* Name;
};

static LuiRuntime* GetRuntime(lua_State* State) {
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiRuntime");
    auto* Runtime = static_cast<LuiRuntime*>(lua_touserdata(State, -1));
    lua_pop(State, 1);
    return Runtime;
}

static bool HasMeta(lua_State* State, int Index, const char* Name) {
    if (lua_type(State, Index) != LUA_TUSERDATA || !lua_getmetatable(State, Index)) return false;
    lua_getfield(State, LUA_REGISTRYINDEX, Name);
    bool Result = lua_rawequal(State, -1, -2) != 0;
    lua_pop(State, 2);
    return Result;
}

static Node* GetNode(lua_State* State, int Index, bool AllowDestroyed = false) {
    if (!HasMeta(State, Index, "LuiNodeMeta")) {
        luaL_error(State, "expected LUI Instance");
    }
    auto* Id = static_cast<int*>(lua_touserdata(State, Index));
    auto* Runtime = GetRuntime(State);
    auto Found = Runtime->Nodes.find(*Id);
    if (Found == Runtime->Nodes.end() || (!AllowDestroyed && Found->second->Destroyed)) {
        luaL_error(State, "Instance is destroyed");
    }
    return Found->second.get();
}

static void PushNode(lua_State* State, const Node* Value) {
    if (!Value || Value->Destroyed || !Value->Reference) {
        lua_pushnil(State);
    } else {
        lua_getref(State, Value->Reference);
    }
}

static bool ReadDimension(lua_State* State, int Index, Dimension& Value) {
    if (lua_type(State, Index) != LUA_TTABLE) return false;
    lua_getfield(State, Index, "Scale");
    if (lua_type(State, -1) != LUA_TNUMBER) { lua_pop(State, 1); return false; }
    Value.Scale = lua_tonumber(State, -1);
    lua_pop(State, 1);
    lua_getfield(State, Index, "Offset");
    if (lua_type(State, -1) != LUA_TNUMBER) { lua_pop(State, 1); return false; }
    Value.Offset = lua_tonumber(State, -1);
    lua_pop(State, 1);
    return std::isfinite(Value.Scale) && std::isfinite(Value.Offset);
}

static bool ReadSize(lua_State* State, int Index, SizeValue& Value) {
    if (lua_type(State, Index) != LUA_TTABLE) return false;
    lua_getfield(State, Index, "X");
    bool ValidX = ReadDimension(State, -1, Value.X);
    lua_pop(State, 1);
    lua_getfield(State, Index, "Y");
    bool ValidY = ReadDimension(State, -1, Value.Y);
    lua_pop(State, 1);
    return ValidX && ValidY;
}

static void PushDimension(lua_State* State, const Dimension& Value) {
    lua_createtable(State, 0, 2);
    lua_pushnumber(State, Value.Scale);
    lua_setfield(State, -2, "Scale");
    lua_pushnumber(State, Value.Offset);
    lua_setfield(State, -2, "Offset");
}

static void PushSize(lua_State* State, const SizeValue& Value) {
    lua_createtable(State, 0, 2);
    PushDimension(State, Value.X);
    lua_setfield(State, -2, "X");
    PushDimension(State, Value.Y);
    lua_setfield(State, -2, "Y");
}

static std::string FormatSize(const SizeValue& Value) {
    char Buffer[128];
    std::snprintf(Buffer, sizeof(Buffer), "%.17g,%.17g,%.17g,%.17g", Value.X.Scale, Value.X.Offset, Value.Y.Scale, Value.Y.Offset);
    return Buffer;
}

static void FireSignal(LuiRuntime* Runtime, Node* Value, const char* Signal) {
    const std::vector<int> Snapshot = Value->Listeners;
    for (int Id : Snapshot) {
        auto Found = Runtime->Listeners.find(Id);
        if (Found == Runtime->Listeners.end() || !Found->second.Active || Found->second.Signal != Signal || Value->Destroyed) continue;
        lua_getref(Runtime->State, Found->second.Reference);
        if (lua_pcall(Runtime->State, 0, 0, 0) != LUA_OK) {
            const char* Message = lua_tostring(Runtime->State, -1);
            Runtime->LastError = Message ? Message : "Luau callback failed";
            std::fprintf(stderr, "[LUI:Signal] %s\n", Runtime->LastError.c_str());
            lua_pop(Runtime->State, 1);
        }
    }
}

static void Disconnect(LuiRuntime* Runtime, int ListenerId) {
    auto Found = Runtime->Listeners.find(ListenerId);
    if (Found == Runtime->Listeners.end() || !Found->second.Active) return;
    Found->second.Active = false;
    lua_unref(Runtime->State, Found->second.Reference);
    Found->second.Reference = 0;
}

static void SetParent(LuiRuntime* Runtime, Node* Value, Node* Parent) {
    if (Value->ClassName == "Window" && Parent) luaL_error(Runtime->State, "Window cannot have a Parent");
    if (Parent && Parent->ClassName != "Window" && Parent->ClassName != "Frame")
        luaL_error(Runtime->State, "Parent must be Window or Frame in Foundation 0");
    for (Node* Cursor = Parent; Cursor; ) {
        if (Cursor->Id == Value->Id) luaL_error(Runtime->State, "Parent cycle is not allowed");
        auto Found = Runtime->Nodes.find(Cursor->ParentId);
        Cursor = Found == Runtime->Nodes.end() ? nullptr : Found->second.get();
    }
    if (Value->ParentId == (Parent ? Parent->Id : 0)) return;
    if (Value->ParentId) {
        auto& Siblings = Runtime->Nodes.at(Value->ParentId)->Children;
        Siblings.erase(std::remove(Siblings.begin(), Siblings.end(), Value->Id), Siblings.end());
    }
    Value->ParentId = Parent ? Parent->Id : 0;
    if (Parent) Parent->Children.push_back(Value->Id);
    if (Runtime->Backend.Parent) Runtime->Backend.Parent(Runtime->Backend.Context, Value->Id, Value->ParentId);
    Runtime->LayoutDirty = true;
    FireSignal(Runtime, Value, "Changed");
}

static void DestroyNode(LuiRuntime* Runtime, Node* Value) {
    if (Value->Destroyed || Value->DestroyingInProgress) return;
    Value->DestroyingInProgress = true;
    FireSignal(Runtime, Value, "Destroying");
    if (Value->ParentId) {
        auto& Siblings = Runtime->Nodes.at(Value->ParentId)->Children;
        Siblings.erase(std::remove(Siblings.begin(), Siblings.end(), Value->Id), Siblings.end());
        Value->ParentId = 0;
    }
    const std::vector<int> Children = Value->Children;
    for (int Id : Children) DestroyNode(Runtime, Runtime->Nodes.at(Id).get());
    Value->Children.clear();
    Value->Destroyed = true;
    for (int Id : Value->Listeners) Disconnect(Runtime, Id);
    if (Runtime->Backend.Destroy) Runtime->Backend.Destroy(Runtime->Backend.Context, Value->Id);
    lua_unref(Runtime->State, Value->Reference);
    Value->Reference = 0;
    Runtime->LayoutDirty = true;
}

static void SetProperty(lua_State* State, Node* Value, const char* Name, int ValueIndex) {
    auto* Runtime = GetRuntime(State);
    if (std::string(Name) == "Parent") {
        SetParent(Runtime, Value, lua_isnil(State, ValueIndex) ? nullptr : GetNode(State, ValueIndex));
        return;
    }
    if (std::string(Name) == "Name" || std::string(Name) == "Title" || std::string(Name) == "Text") {
        const char* Text = luaL_checkstring(State, ValueIndex);
        std::string* Target = std::string(Name) == "Name" ? &Value->Name : std::string(Name) == "Title" ? &Value->Title : &Value->Text;
        if (std::string(Name) == "Title" && Value->ClassName != "Window") luaL_error(State, "Title belongs to Window");
        if (std::string(Name) == "Text" && Value->ClassName != "TextLabel" && Value->ClassName != "TextButton") luaL_error(State, "Text belongs to text controls");
        *Target = Text;
        if (Runtime->Backend.Property) Runtime->Backend.Property(Runtime->Backend.Context, Value->Id, Name, Target->c_str());
    } else if (std::string(Name) == "Visible") {
        if (lua_type(State, ValueIndex) != LUA_TBOOLEAN) luaL_error(State, "Visible must be boolean");
        Value->Visible = lua_toboolean(State, ValueIndex) != 0;
        if (Runtime->Backend.Property) Runtime->Backend.Property(Runtime->Backend.Context, Value->Id, Name, Value->Visible ? "true" : "false");
    } else if (std::string(Name) == "Size") {
        SizeValue Parsed;
        if (!ReadSize(State, ValueIndex, Parsed)) luaL_error(State, "Size must be UDim2 with finite values");
        Value->Size = Parsed;
        const std::string Formatted = FormatSize(Value->Size);
        if (Runtime->Backend.Property) Runtime->Backend.Property(Runtime->Backend.Context, Value->Id, Name, Formatted.c_str());
        Runtime->LayoutDirty = true;
    } else {
        luaL_error(State, "unknown or read-only property '%s'", Name);
    }
    FireSignal(Runtime, Value, "Changed");
}

static int NodeDestroy(lua_State* State) {
    Node* Value = GetNode(State, 1, true);
    DestroyNode(GetRuntime(State), Value);
    return 0;
}

static int NodeGetChildren(lua_State* State) {
    Node* Value = GetNode(State, 1);
    auto* Runtime = GetRuntime(State);
    lua_createtable(State, static_cast<int>(Value->Children.size()), 0);
    int Position = 1;
    for (int Id : Value->Children) {
        PushNode(State, Runtime->Nodes.at(Id).get());
        lua_rawseti(State, -2, Position++);
    }
    return 1;
}

static int NodeFindFirstChild(lua_State* State) {
    Node* Value = GetNode(State, 1);
    const char* Name = luaL_checkstring(State, 2);
    auto* Runtime = GetRuntime(State);
    for (int Id : Value->Children) {
        Node* Child = Runtime->Nodes.at(Id).get();
        if (Child->Name == Name) { PushNode(State, Child); return 1; }
    }
    lua_pushnil(State);
    return 1;
}

static int NodeIsA(lua_State* State) {
    Node* Value = GetNode(State, 1);
    const std::string ClassName = luaL_checkstring(State, 2);
    bool Result = ClassName == "Instance" || ClassName == Value->ClassName ||
        (ClassName == "GuiObject" && Value->ClassName != "Window") ||
        (ClassName == "GuiButton" && Value->ClassName == "TextButton");
    lua_pushboolean(State, Result);
    return 1;
}

static int SignalConnect(lua_State* State) {
    if (!HasMeta(State, 1, "LuiSignalMeta")) luaL_error(State, "expected Signal");
    auto* Signal = static_cast<SignalValue*>(lua_touserdata(State, 1));
    luaL_checktype(State, 2, LUA_TFUNCTION);
    auto* Runtime = GetRuntime(State);
    auto Found = Runtime->Nodes.find(Signal->NodeId);
    if (Found == Runtime->Nodes.end() || Found->second->Destroyed) luaL_error(State, "Signal owner is destroyed");
    int Reference = lua_ref(State, 2);
    int Id = Runtime->NextListenerId++;
    Runtime->Listeners.emplace(Id, Listener{Id, Signal->NodeId, Reference, Signal->Name, true});
    Found->second->Listeners.push_back(Id);
    *static_cast<int*>(lua_newuserdata(State, sizeof(int))) = Id;
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiConnectionMeta");
    lua_setmetatable(State, -2);
    return 1;
}

static int ConnectionDisconnect(lua_State* State) {
    if (!HasMeta(State, 1, "LuiConnectionMeta")) luaL_error(State, "expected Connection");
    Disconnect(GetRuntime(State), *static_cast<int*>(lua_touserdata(State, 1)));
    return 0;
}

static void PushSignal(lua_State* State, Node* Value, const char* Name) {
    *static_cast<SignalValue*>(lua_newuserdata(State, sizeof(SignalValue))) = {Value->Id, Name};
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiSignalMeta");
    lua_setmetatable(State, -2);
}

static int NodeIndex(lua_State* State) {
    Node* Value = GetNode(State, 1, true);
    const std::string Key = luaL_checkstring(State, 2);
    if (Key == "Destroy") lua_pushcfunction(State, NodeDestroy, "Destroy");
    else if (Key == "GetChildren") lua_pushcfunction(State, NodeGetChildren, "GetChildren");
    else if (Key == "FindFirstChild") lua_pushcfunction(State, NodeFindFirstChild, "FindFirstChild");
    else if (Key == "IsA") lua_pushcfunction(State, NodeIsA, "IsA");
    else if (Key == "ClassName") lua_pushstring(State, Value->ClassName.c_str());
    else if (Key == "Name") lua_pushstring(State, Value->Name.c_str());
    else if (Key == "Title") lua_pushstring(State, Value->Title.c_str());
    else if (Key == "Text") lua_pushstring(State, Value->Text.c_str());
    else if (Key == "Visible") lua_pushboolean(State, Value->Visible);
    else if (Key == "Size") PushSize(State, Value->Size);
    else if (Key == "Parent") {
        auto* Runtime = GetRuntime(State);
        auto Found = Runtime->Nodes.find(Value->ParentId);
        PushNode(State, Found == Runtime->Nodes.end() ? nullptr : Found->second.get());
    } else if (Key == "Activated" && Value->ClassName == "TextButton") PushSignal(State, Value, "Activated");
    else if (Key == "Changed") PushSignal(State, Value, "Changed");
    else if (Key == "Destroying") PushSignal(State, Value, "Destroying");
    else lua_pushnil(State);
    return 1;
}

static int NodeNewIndex(lua_State* State) {
    Node* Value = GetNode(State, 1);
    SetProperty(State, Value, luaL_checkstring(State, 2), 3);
    return 0;
}

static int InstanceNew(lua_State* State) {
    const std::string ClassName = luaL_checkstring(State, 1);
    if (ClassName != "Window" && ClassName != "Frame" && ClassName != "TextLabel" && ClassName != "TextButton") {
        luaL_error(State, "unknown class '%s'", ClassName.c_str());
    }
    auto* Runtime = GetRuntime(State);
    auto Value = std::make_unique<Node>();
    Value->Id = Runtime->NextNodeId++;
    Value->ClassName = ClassName;
    Value->Name = ClassName;
    Value->Visible = ClassName != "Window";
    Value->Size = ClassName == "Window" ? SizeValue{{0, 800}, {0, 600}} : SizeValue{{1, 0}, {1, 0}};
    int Id = Value->Id;
    *static_cast<int*>(lua_newuserdata(State, sizeof(int))) = Id;
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiNodeMeta");
    lua_setmetatable(State, -2);
    Value->Reference = lua_ref(State, -1);
    Runtime->Nodes.emplace(Id, std::move(Value));
    if (Runtime->Backend.Create) Runtime->Backend.Create(Runtime->Backend.Context, Id, ClassName.c_str());
    Runtime->LayoutDirty = true;

    if (!lua_isnoneornil(State, 2)) {
        luaL_checktype(State, 2, LUA_TTABLE);
        lua_pushnil(State);
        while (lua_next(State, 2) != 0) {
            const char* Key = luaL_checkstring(State, -2);
            if (std::string(Key) != "Parent") SetProperty(State, Runtime->Nodes.at(Id).get(), Key, -1);
            lua_pop(State, 1);
        }
        lua_getfield(State, 2, "Parent");
        if (!lua_isnil(State, -1)) SetProperty(State, Runtime->Nodes.at(Id).get(), "Parent", -1);
        lua_pop(State, 1);
    }
    return 1;
}

static int UDimNew(lua_State* State) {
    Dimension Value{luaL_checknumber(State, 1), luaL_checknumber(State, 2)};
    if (!std::isfinite(Value.Scale) || !std::isfinite(Value.Offset)) luaL_error(State, "UDim values must be finite");
    PushDimension(State, Value);
    return 1;
}

static int UDim2New(lua_State* State) {
    SizeValue Value{{luaL_checknumber(State, 1), luaL_checknumber(State, 2)},
                    {luaL_checknumber(State, 3), luaL_checknumber(State, 4)}};
    if (!std::isfinite(Value.X.Scale) || !std::isfinite(Value.X.Offset) ||
        !std::isfinite(Value.Y.Scale) || !std::isfinite(Value.Y.Offset)) luaL_error(State, "UDim2 values must be finite");
    PushSize(State, Value);
    return 1;
}

static int UDim2FromOffset(lua_State* State) {
    SizeValue Value{{0, luaL_checknumber(State, 1)}, {0, luaL_checknumber(State, 2)}};
    PushSize(State, Value);
    return 1;
}

static int UDim2FromScale(lua_State* State) {
    SizeValue Value{{luaL_checknumber(State, 1), 0}, {luaL_checknumber(State, 2), 0}};
    PushSize(State, Value);
    return 1;
}

static int TaskSchedule(lua_State* State) {
    luaL_checktype(State, 1, LUA_TFUNCTION);
    auto* Runtime = GetRuntime(State);
    int Reference = lua_ref(State, 1);
    Runtime->Tasks.push_back({Reference, std::chrono::steady_clock::now()});
    return 0;
}

static int TaskDelay(lua_State* State) {
    double Seconds = luaL_checknumber(State, 1);
    luaL_checktype(State, 2, LUA_TFUNCTION);
    if (!std::isfinite(Seconds) || Seconds < 0) luaL_error(State, "delay must be finite and nonnegative");
    auto* Runtime = GetRuntime(State);
    int Reference = lua_ref(State, 2);
    Runtime->Tasks.push_back({Reference, std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int64_t>(Seconds * 1000))});
    return 0;
}

static void ArrangeNode(LuiRuntime* Runtime, Node* Value, double ParentWidth, double ParentHeight) {
    double Width = std::max(0.0, ParentWidth * Value->Size.X.Scale + Value->Size.X.Offset);
    double Height = std::max(0.0, ParentHeight * Value->Size.Y.Scale + Value->Size.Y.Offset);
    if (Runtime->Backend.Arrange) Runtime->Backend.Arrange(Runtime->Backend.Context, Value->Id, 0, 0, Width, Height);
    for (int Id : Value->Children) ArrangeNode(Runtime, Runtime->Nodes.at(Id).get(), Width, Height);
}

static void FlushLayout(LuiRuntime* Runtime) {
    if (!Runtime->LayoutDirty) return;
    Runtime->LayoutDirty = false;
    for (auto& Pair : Runtime->Nodes) {
        Node* Value = Pair.second.get();
        if (!Value->Destroyed && Value->ClassName == "Window") ArrangeNode(Runtime, Value, 0, 0);
    }
}

static void RegisterMeta(lua_State* State, const char* Name, lua_CFunction Index, lua_CFunction NewIndex = nullptr) {
    lua_newtable(State);
    lua_pushcfunction(State, Index, "__index");
    lua_setfield(State, -2, "__index");
    if (NewIndex) {
        lua_pushcfunction(State, NewIndex, "__newindex");
        lua_setfield(State, -2, "__newindex");
    }
    lua_setfield(State, LUA_REGISTRYINDEX, Name);
}

static int SignalIndex(lua_State* State) {
    const std::string Key = luaL_checkstring(State, 2);
    if (Key == "Connect") lua_pushcfunction(State, SignalConnect, "Connect");
    else lua_pushnil(State);
    return 1;
}

static int ConnectionIndex(lua_State* State) {
    const std::string Key = luaL_checkstring(State, 2);
    if (Key == "Disconnect") lua_pushcfunction(State, ConnectionDisconnect, "Disconnect");
    else lua_pushnil(State);
    return 1;
}

static void RegisterGlobals(lua_State* State) {
    RegisterMeta(State, "LuiNodeMeta", NodeIndex, NodeNewIndex);
    RegisterMeta(State, "LuiSignalMeta", SignalIndex);
    RegisterMeta(State, "LuiConnectionMeta", ConnectionIndex);
    lua_newtable(State);
    lua_pushcfunction(State, InstanceNew, "Instance.new");
    lua_setfield(State, -2, "new");
    lua_setglobal(State, "Instance");
    lua_newtable(State);
    lua_pushcfunction(State, UDimNew, "UDim.new");
    lua_setfield(State, -2, "new");
    lua_setglobal(State, "UDim");
    lua_newtable(State);
    lua_pushcfunction(State, UDim2New, "UDim2.new");
    lua_setfield(State, -2, "new");
    lua_pushcfunction(State, UDim2FromOffset, "UDim2.fromOffset");
    lua_setfield(State, -2, "fromOffset");
    lua_pushcfunction(State, UDim2FromScale, "UDim2.fromScale");
    lua_setfield(State, -2, "fromScale");
    lua_setglobal(State, "UDim2");
    lua_newtable(State);
    lua_pushcfunction(State, TaskSchedule, "task.defer");
    lua_setfield(State, -2, "defer");
    lua_pushcfunction(State, TaskSchedule, "task.spawn");
    lua_setfield(State, -2, "spawn");
    lua_pushcfunction(State, TaskDelay, "task.delay");
    lua_setfield(State, -2, "delay");
    lua_setglobal(State, "task");
}

static bool CheckOwner(LuiRuntime* Runtime) {
    if (!Runtime) return false;
    if (Runtime->Owner == std::this_thread::get_id()) return true;
    Runtime->LastError = "[LUI:Scheduler] Runtime called from the wrong thread";
    return false;
}

extern "C" LUI_API LuiRuntime* LUI_CALL Lui_Create(void) {
    auto* Runtime = new LuiRuntime();
    Runtime->Owner = std::this_thread::get_id();
    Runtime->State = luaL_newstate();
    if (!Runtime->State) { delete Runtime; return nullptr; }
    luaL_openlibs(Runtime->State);
    lua_pushlightuserdata(Runtime->State, Runtime);
    lua_setfield(Runtime->State, LUA_REGISTRYINDEX, "LuiRuntime");
    RegisterGlobals(Runtime->State);
    return Runtime;
}

extern "C" LUI_API void LUI_CALL Lui_SetBackend(LuiRuntime* Runtime, LuiBackendCallbacks Callbacks) {
    if (CheckOwner(Runtime)) Runtime->Backend = Callbacks;
}

extern "C" LUI_API int LUI_CALL Lui_RunScript(LuiRuntime* Runtime, const char* Source, const char* ChunkName) {
    if (!CheckOwner(Runtime) || !Source) return 0;
    try {
        const std::string Bytecode = Luau::compile(Source);
        int Status = luau_load(Runtime->State, ChunkName ? ChunkName : "LUI", Bytecode.data(), Bytecode.size(), 0);
        if (Status == LUA_OK) Status = lua_pcall(Runtime->State, 0, 0, 0);
        if (Status != LUA_OK) {
            const char* Message = lua_tostring(Runtime->State, -1);
            Runtime->LastError = Message ? Message : "Luau script failed";
            std::fprintf(stderr, "[LUI:Runtime] %s\n", Runtime->LastError.c_str());
            lua_pop(Runtime->State, 1);
            return 0;
        }
        Runtime->LastError.clear();
        FlushLayout(Runtime);
        return 1;
    } catch (const std::exception& Error) {
        Runtime->LastError = Error.what();
        std::fprintf(stderr, "[LUI:Runtime] %s\n", Runtime->LastError.c_str());
        return 0;
    }
}

extern "C" LUI_API int LUI_CALL Lui_Activate(LuiRuntime* Runtime, int Id) {
    if (!CheckOwner(Runtime)) return 0;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->Destroyed || !Found->second->Visible || Found->second->ClassName != "TextButton") return 0;
    FireSignal(Runtime, Found->second.get(), "Activated");
    FlushLayout(Runtime);
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_Pump(LuiRuntime* Runtime) {
    if (!CheckOwner(Runtime)) return 0;
    int Count = 0;
    const auto Now = std::chrono::steady_clock::now();
    std::vector<ScheduledCall> Pending;
    Pending.swap(Runtime->Tasks);
    for (const auto& Call : Pending) {
        if (Call.Due > Now) { Runtime->Tasks.push_back(Call); continue; }
        lua_getref(Runtime->State, Call.Reference);
        if (lua_pcall(Runtime->State, 0, 0, 0) != LUA_OK) {
            const char* Message = lua_tostring(Runtime->State, -1);
            Runtime->LastError = Message ? Message : "scheduled callback failed";
            std::fprintf(stderr, "[LUI:Scheduler] %s\n", Runtime->LastError.c_str());
            lua_pop(Runtime->State, 1);
        }
        lua_unref(Runtime->State, Call.Reference);
        ++Count;
    }
    FlushLayout(Runtime);
    return Count;
}

extern "C" LUI_API const char* LUI_CALL Lui_GetLastError(LuiRuntime* Runtime) {
    return Runtime ? Runtime->LastError.c_str() : "runtime is null";
}

extern "C" LUI_API void LUI_CALL Lui_Destroy(LuiRuntime* Runtime) {
    if (!CheckOwner(Runtime)) return;
    for (auto& Pair : Runtime->Nodes) {
        if (!Pair.second->Destroyed && Pair.second->ClassName == "Window") DestroyNode(Runtime, Pair.second.get());
    }
    for (auto& Pair : Runtime->Nodes) if (!Pair.second->Destroyed) DestroyNode(Runtime, Pair.second.get());
    for (const auto& Call : Runtime->Tasks) lua_unref(Runtime->State, Call.Reference);
    lua_close(Runtime->State);
    delete Runtime;
}
