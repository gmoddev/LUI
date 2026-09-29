#include "LuiRuntime.h"
#include "reflection/Schema.h"

#include "Luau/Compiler.h"
#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <chrono>
#include <climits>
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

struct VectorValue {
    double X = 0;
    double Y = 0;
};

struct BoundsValue {
    double X = 0;
    double Y = 0;
    double Width = 0;
    double Height = 0;
};

struct Node {
    int Id = 0;
    int Reference = 0;
    int ParentId = 0;
    bool Destroyed = false;
    bool DestroyingInProgress = false;
    bool Visible = true;
    bool Enabled = true;
    bool Checked = false;
    bool IsFocused = false;
    std::string ClassName;
    std::string Name;
    std::string Title;
    std::string Text;
    double Minimum = 0;
    double Maximum = 100;
    double Value = 0;
    SizeValue Size;
    SizeValue Position;
    VectorValue AnchorPoint;
    BoundsValue Bounds;
    int LayoutOrder = 0;
    Dimension Padding;
    Dimension PaddingTop;
    Dimension PaddingBottom;
    Dimension PaddingLeft;
    Dimension PaddingRight;
    std::string FillDirection = "Vertical";
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

struct BackendChange {
    enum class Kind { Property, Parent } Type;
    int Id = 0;
    std::string Name;
    std::string Value;
    int ParentId = 0;
};

struct LuiRuntime {
    lua_State* State = nullptr;
    std::thread::id Owner;
    LuiBackendCallbacks Backend{};
    void* LogContext = nullptr;
    LuiLogCallback LogCallback = nullptr;
    std::string BackendName = "headless";
    std::unordered_map<std::string, int> ServiceRefs;
    std::unordered_map<int, std::unique_ptr<Node>> Nodes;
    std::unordered_map<int, Listener> Listeners;
    std::vector<ScheduledCall> Tasks;
    std::vector<BackendChange> PendingChanges;
    std::string LastError;
    int NextNodeId = 1;
    int NextListenerId = 1;
    bool LayoutDirty = false;
};

struct SignalValue {
    int NodeId;
    const char* Name;
};

static void FlushLayout(LuiRuntime* Runtime);
static LuiRuntime* GetRuntime(lua_State* State);

static void EmitLog(LuiRuntime* Runtime, const char* Level, const std::string& Message) {
    if (Runtime->LogCallback) Runtime->LogCallback(Runtime->LogContext, Level, Message.c_str());
    else std::fprintf(Level[0] == 'E' ? stderr : stdout, "[LUI:%s] %s\n", Level, Message.c_str());
}

static int LuiPrint(lua_State* State) {
    std::string Message;
    const int Count = lua_gettop(State);
    for (int Index = 1; Index <= Count; ++Index) {
        if (Index > 1) Message += '\t';
        luaL_tolstring(State, Index, nullptr);
        const char* Text = lua_tostring(State, -1);
        if (Text) Message += Text;
        lua_pop(State, 1);
    }
    EmitLog(GetRuntime(State), "Print", Message);
    return 0;
}

static bool CanReceiveInput(const LuiRuntime* Runtime, const Node* Value) {
    if (Value->Destroyed || !Value->Visible || !Value->Enabled) return false;
    for (const Node* Parent = Value; Parent->ParentId; ) {
        Parent = Runtime->Nodes.at(Parent->ParentId).get();
        if (!Parent->Visible) return false;
    }
    return true;
}

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

static void QueueProperty(LuiRuntime* Runtime, const Node* Value, const char* Name, const std::string& Text) {
    if (!LuiSchema::IsNative(Value->ClassName)) return;
    for (BackendChange& Change : Runtime->PendingChanges) {
        if (Change.Type == BackendChange::Kind::Property && Change.Id == Value->Id && Change.Name == Name) {
            Change.Value = Text;
            return;
        }
    }
    Runtime->PendingChanges.push_back({BackendChange::Kind::Property, Value->Id, Name, Text, 0});
}

static void QueueParent(LuiRuntime* Runtime, const Node* Value) {
    if (!LuiSchema::IsNative(Value->ClassName)) return;
    for (BackendChange& Change : Runtime->PendingChanges) {
        if (Change.Type == BackendChange::Kind::Parent && Change.Id == Value->Id) {
            Change.ParentId = Value->ParentId;
            return;
        }
    }
    Runtime->PendingChanges.push_back({BackendChange::Kind::Parent, Value->Id, {}, {}, Value->ParentId});
}

static void FlushChanges(LuiRuntime* Runtime) {
    std::vector<BackendChange> Changes;
    Changes.swap(Runtime->PendingChanges);
    for (const BackendChange& Change : Changes) {
        auto Found = Runtime->Nodes.find(Change.Id);
        if (Found == Runtime->Nodes.end() || Found->second->Destroyed) continue;
        if (Change.Type == BackendChange::Kind::Property && Runtime->Backend.Property)
            Runtime->Backend.Property(Runtime->Backend.Context, Change.Id, Change.Name.c_str(), Change.Value.c_str());
        else if (Change.Type == BackendChange::Kind::Parent && Runtime->Backend.Parent)
            Runtime->Backend.Parent(Runtime->Backend.Context, Change.Id, Change.ParentId);
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

static bool ReadVector(lua_State* State, int Index, VectorValue& Value) {
    if (lua_type(State, Index) != LUA_TTABLE) return false;
    lua_getfield(State, Index, "X");
    if (lua_type(State, -1) != LUA_TNUMBER) { lua_pop(State, 1); return false; }
    Value.X = lua_tonumber(State, -1);
    lua_pop(State, 1);
    lua_getfield(State, Index, "Y");
    if (lua_type(State, -1) != LUA_TNUMBER) { lua_pop(State, 1); return false; }
    Value.Y = lua_tonumber(State, -1);
    lua_pop(State, 1);
    return std::isfinite(Value.X) && std::isfinite(Value.Y);
}

static void PushVector(lua_State* State, VectorValue Value) {
    lua_createtable(State, 0, 2);
    lua_pushnumber(State, Value.X);
    lua_setfield(State, -2, "X");
    lua_pushnumber(State, Value.Y);
    lua_setfield(State, -2, "Y");
    lua_setreadonly(State, -1, true);
}

static void PushDimension(lua_State* State, const Dimension& Value) {
    lua_createtable(State, 0, 2);
    lua_pushnumber(State, Value.Scale);
    lua_setfield(State, -2, "Scale");
    lua_pushnumber(State, Value.Offset);
    lua_setfield(State, -2, "Offset");
    lua_setreadonly(State, -1, true);
}

static void PushSize(lua_State* State, const SizeValue& Value) {
    lua_createtable(State, 0, 2);
    PushDimension(State, Value.X);
    lua_setfield(State, -2, "X");
    PushDimension(State, Value.Y);
    lua_setfield(State, -2, "Y");
    lua_setreadonly(State, -1, true);
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
            EmitLog(Runtime, "Error", "Signal: " + Runtime->LastError);
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
        luaL_error(Runtime->State, "Parent must be Window or Frame");
    if (Parent && (Value->ClassName == "UIListLayout" || Value->ClassName == "UIPadding")) {
        for (int Id : Parent->Children) {
            const Node* Sibling = Runtime->Nodes.at(Id).get();
            if (Sibling != Value && Sibling->ClassName == Value->ClassName)
                luaL_error(Runtime->State, "only one %s is allowed per container", Value->ClassName.c_str());
        }
    }
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
    QueueParent(Runtime, Value);
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
    auto& Pending = Runtime->PendingChanges;
    Pending.erase(std::remove_if(Pending.begin(), Pending.end(),
        [Value](const BackendChange& Change) { return Change.Id == Value->Id; }), Pending.end());
    for (int Id : Value->Listeners) Disconnect(Runtime, Id);
    if (LuiSchema::IsNative(Value->ClassName) && Runtime->Backend.Destroy)
        Runtime->Backend.Destroy(Runtime->Backend.Context, Value->Id);
    lua_unref(Runtime->State, Value->Reference);
    Value->Reference = 0;
    Runtime->LayoutDirty = true;
}

static void SetProperty(lua_State* State, Node* Value, const char* Name, int ValueIndex) {
    auto* Runtime = GetRuntime(State);
    const std::string Key = Name;
    const double PreviousValue = Value->Value;
    const LuiSchema::PropertyDefinition* Definition = LuiSchema::FindProperty(Value->ClassName, Key);
    if (!Definition || Definition->ReadOnly) luaL_error(State, "unknown or read-only property '%s'", Name);
    const bool IsComponent = Value->ClassName == "UIListLayout" || Value->ClassName == "UIPadding";
    if (Key == "Parent") {
        SetParent(Runtime, Value, lua_isnil(State, ValueIndex) ? nullptr : GetNode(State, ValueIndex));
        return;
    }
    if (Key == "Name" || Key == "Title" || Key == "Text") {
        if (lua_type(State, ValueIndex) != LUA_TSTRING) luaL_error(State, "%s must be string", Name);
        const char* Text = lua_tostring(State, ValueIndex);
        std::string* Target = Key == "Name" ? &Value->Name : Key == "Title" ? &Value->Title : &Value->Text;
        if (Key == "Title" && Value->ClassName != "Window") luaL_error(State, "Title belongs to Window");
        if (Key == "Text" && Value->ClassName != "TextLabel" && Value->ClassName != "TextButton" &&
            Value->ClassName != "TextBox" && Value->ClassName != "CheckBox") luaL_error(State, "Text belongs to text controls");
        *Target = Text;
        QueueProperty(Runtime, Value, Name, *Target);
    } else if (Key == "Visible") {
        if (IsComponent) luaL_error(State, "Visible does not belong to UI components");
        if (lua_type(State, ValueIndex) != LUA_TBOOLEAN) luaL_error(State, "Visible must be boolean");
        Value->Visible = lua_toboolean(State, ValueIndex) != 0;
        QueueProperty(Runtime, Value, Name, Value->Visible ? "true" : "false");
    } else if (Key == "Enabled" || Key == "Checked") {
        if (lua_type(State, ValueIndex) != LUA_TBOOLEAN) luaL_error(State, "%s must be boolean", Name);
        bool Parsed = lua_toboolean(State, ValueIndex) != 0;
        if (Key == "Enabled") Value->Enabled = Parsed;
        else Value->Checked = Parsed;
        QueueProperty(Runtime, Value, Name, Parsed ? "true" : "false");
    } else if (Key == "Minimum" || Key == "Maximum" || Key == "Value") {
        if (lua_type(State, ValueIndex) != LUA_TNUMBER) luaL_error(State, "%s must be number", Name);
        double Parsed = lua_tonumber(State, ValueIndex);
        if (!std::isfinite(Parsed)) luaL_error(State, "%s must be finite", Name);
        if (Key == "Minimum") {
            if (Parsed > Value->Maximum) luaL_error(State, "Minimum cannot exceed Maximum");
            Value->Minimum = Parsed;
            Value->Value = std::max(Value->Value, Parsed);
        } else if (Key == "Maximum") {
            if (Parsed < Value->Minimum) luaL_error(State, "Maximum cannot be below Minimum");
            Value->Maximum = Parsed;
            Value->Value = std::min(Value->Value, Parsed);
        } else {
            if (Parsed < Value->Minimum || Parsed > Value->Maximum) luaL_error(State, "Value is outside Minimum and Maximum");
            Value->Value = Parsed;
        }
        QueueProperty(Runtime, Value, Name, std::to_string(Parsed));
        if (Key != "Value") QueueProperty(Runtime, Value, "Value", std::to_string(Value->Value));
    } else if (Key == "Size" || Key == "Position") {
        if (IsComponent || (Key == "Position" && Value->ClassName == "Window")) luaL_error(State, "%s is invalid for %s", Name, Value->ClassName.c_str());
        SizeValue Parsed;
        if (!ReadSize(State, ValueIndex, Parsed)) luaL_error(State, "%s must be UDim2 with finite values", Name);
        if (Key == "Size") Value->Size = Parsed;
        else Value->Position = Parsed;
        const std::string Formatted = FormatSize(Parsed);
        QueueProperty(Runtime, Value, Name, Formatted);
        Runtime->LayoutDirty = true;
    } else if (Key == "AnchorPoint") {
        if (IsComponent || Value->ClassName == "Window") luaL_error(State, "AnchorPoint belongs to GuiObject");
        VectorValue Parsed;
        if (!ReadVector(State, ValueIndex, Parsed)) luaL_error(State, "AnchorPoint must be Vector2 with finite values");
        Value->AnchorPoint = Parsed;
        Runtime->LayoutDirty = true;
    } else if (Key == "LayoutOrder") {
        if (IsComponent || Value->ClassName == "Window") luaL_error(State, "LayoutOrder belongs to GuiObject");
        if (lua_type(State, ValueIndex) != LUA_TNUMBER) luaL_error(State, "LayoutOrder must be integer");
        double Parsed = lua_tonumber(State, ValueIndex);
        if (!std::isfinite(Parsed) || std::floor(Parsed) != Parsed || Parsed < INT_MIN || Parsed > INT_MAX) luaL_error(State, "LayoutOrder must be integer");
        Value->LayoutOrder = static_cast<int>(Parsed);
        Runtime->LayoutDirty = true;
    } else if (Key == "FillDirection") {
        if (Value->ClassName != "UIListLayout") luaL_error(State, "FillDirection belongs to UIListLayout");
        if (lua_type(State, ValueIndex) != LUA_TSTRING) luaL_error(State, "FillDirection must be string");
        const std::string Parsed = lua_tostring(State, ValueIndex);
        if (Parsed != "Vertical" && Parsed != "Horizontal") luaL_error(State, "FillDirection must be Vertical or Horizontal");
        Value->FillDirection = Parsed;
        Runtime->LayoutDirty = true;
    } else if (Key == "Padding" || Key == "PaddingTop" || Key == "PaddingBottom" || Key == "PaddingLeft" || Key == "PaddingRight") {
        if ((Key == "Padding" && Value->ClassName != "UIListLayout") ||
            (Key != "Padding" && Value->ClassName != "UIPadding")) luaL_error(State, "%s is invalid for %s", Name, Value->ClassName.c_str());
        Dimension Parsed;
        if (!ReadDimension(State, ValueIndex, Parsed)) luaL_error(State, "%s must be UDim with finite values", Name);
        if (Key == "Padding") Value->Padding = Parsed;
        else if (Key == "PaddingTop") Value->PaddingTop = Parsed;
        else if (Key == "PaddingBottom") Value->PaddingBottom = Parsed;
        else if (Key == "PaddingLeft") Value->PaddingLeft = Parsed;
        else Value->PaddingRight = Parsed;
        Runtime->LayoutDirty = true;
    } else {
        luaL_error(State, "unknown or read-only property '%s'", Name);
    }
    FireSignal(Runtime, Value, "Changed");
    if (Key == "Text" && Value->ClassName == "TextBox") FireSignal(Runtime, Value, "TextChanged");
    if (Key == "Checked") FireSignal(Runtime, Value, "CheckedChanged");
    if ((Key == "Value" || Key == "Minimum" || Key == "Maximum") && Value->Value != PreviousValue)
        FireSignal(Runtime, Value, "ValueChanged");
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

static void AppendDescendants(lua_State* State, LuiRuntime* Runtime, Node* Value, int& Position) {
    for (int Id : Value->Children) {
        Node* Child = Runtime->Nodes.at(Id).get();
        PushNode(State, Child);
        lua_rawseti(State, -2, Position++);
        AppendDescendants(State, Runtime, Child, Position);
    }
}

static int NodeGetDescendants(lua_State* State) {
    Node* Value = GetNode(State, 1);
    lua_newtable(State);
    int Position = 1;
    AppendDescendants(State, GetRuntime(State), Value, Position);
    return 1;
}

static void CloneTree(lua_State* State, LuiRuntime* Runtime, const Node* Source, Node* Parent) {
    auto Value = std::make_unique<Node>();
    Value->Id = Runtime->NextNodeId++;
    Value->ClassName = Source->ClassName;
    Value->Name = Source->Name;
    Value->Title = Source->Title;
    Value->Text = Source->Text;
    Value->Visible = Source->Visible;
    Value->Enabled = Source->Enabled;
    Value->Checked = Source->Checked;
    Value->Minimum = Source->Minimum;
    Value->Maximum = Source->Maximum;
    Value->Value = Source->Value;
    Value->Size = Source->Size;
    Value->Position = Source->Position;
    Value->AnchorPoint = Source->AnchorPoint;
    Value->LayoutOrder = Source->LayoutOrder;
    Value->Padding = Source->Padding;
    Value->PaddingTop = Source->PaddingTop;
    Value->PaddingBottom = Source->PaddingBottom;
    Value->PaddingLeft = Source->PaddingLeft;
    Value->PaddingRight = Source->PaddingRight;
    Value->FillDirection = Source->FillDirection;
    int Id = Value->Id;
    *static_cast<int*>(lua_newuserdata(State, sizeof(int))) = Id;
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiNodeMeta");
    lua_setmetatable(State, -2);
    Value->Reference = lua_ref(State, -1);
    Runtime->Nodes.emplace(Id, std::move(Value));
    Node* Copy = Runtime->Nodes.at(Id).get();
    if (LuiSchema::IsNative(Copy->ClassName) && Runtime->Backend.Create)
        Runtime->Backend.Create(Runtime->Backend.Context, Id, Copy->ClassName.c_str());
    if (Parent) SetParent(Runtime, Copy, Parent);
    if (LuiSchema::IsNative(Copy->ClassName)) {
        if (Copy->ClassName == "Window") QueueProperty(Runtime, Copy, "Title", Copy->Title);
        if (Copy->ClassName == "TextLabel" || Copy->ClassName == "TextButton" ||
            Copy->ClassName == "TextBox" || Copy->ClassName == "CheckBox")
            QueueProperty(Runtime, Copy, "Text", Copy->Text);
        if (LuiSchema::FindProperty(Copy->ClassName, "Enabled"))
            QueueProperty(Runtime, Copy, "Enabled", Copy->Enabled ? "true" : "false");
        if (Copy->ClassName == "CheckBox")
            QueueProperty(Runtime, Copy, "Checked", Copy->Checked ? "true" : "false");
        if (Copy->ClassName == "Slider" || Copy->ClassName == "ProgressBar") {
            const std::string Minimum = std::to_string(Copy->Minimum);
            const std::string Maximum = std::to_string(Copy->Maximum);
            const std::string Current = std::to_string(Copy->Value);
            QueueProperty(Runtime, Copy, "Minimum", Minimum);
            QueueProperty(Runtime, Copy, "Maximum", Maximum);
            QueueProperty(Runtime, Copy, "Value", Current);
        }
        QueueProperty(Runtime, Copy, "Visible", Copy->Visible ? "true" : "false");
    }
    for (int ChildId : Source->Children) {
        CloneTree(State, Runtime, Runtime->Nodes.at(ChildId).get(), Copy);
        lua_pop(State, 1);
    }
    Runtime->LayoutDirty = true;
}

static int NodeClone(lua_State* State) {
    Node* Value = GetNode(State, 1);
    CloneTree(State, GetRuntime(State), Value, nullptr);
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
    lua_pushboolean(State, LuiSchema::IsA(Value->ClassName, ClassName));
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
    const bool IsMethod = Key == "Destroy" || Key == "Clone" || Key == "GetChildren" ||
        Key == "GetDescendants" || Key == "FindFirstChild" || Key == "IsA";
    const bool IsSignal = Key == "Changed" || Key == "Destroying" ||
        ((Key == "Focused" || Key == "FocusLost") && LuiSchema::IsA(Value->ClassName, "GuiObject")) ||
        (Key == "Activated" && (Value->ClassName == "TextButton" || Value->ClassName == "CheckBox")) ||
        (Key == "TextChanged" && Value->ClassName == "TextBox") ||
        (Key == "CheckedChanged" && Value->ClassName == "CheckBox") ||
        (Key == "ValueChanged" && Value->ClassName == "Slider");
    if (!IsMethod && !IsSignal && !LuiSchema::FindProperty(Value->ClassName, Key)) {
        lua_pushnil(State);
        return 1;
    }
    if (Key == "Destroy") lua_pushcfunction(State, NodeDestroy, "Destroy");
    else if (Key == "Clone") lua_pushcfunction(State, NodeClone, "Clone");
    else if (Key == "GetChildren") lua_pushcfunction(State, NodeGetChildren, "GetChildren");
    else if (Key == "GetDescendants") lua_pushcfunction(State, NodeGetDescendants, "GetDescendants");
    else if (Key == "FindFirstChild") lua_pushcfunction(State, NodeFindFirstChild, "FindFirstChild");
    else if (Key == "IsA") lua_pushcfunction(State, NodeIsA, "IsA");
    else if (Key == "ClassName") lua_pushstring(State, Value->ClassName.c_str());
    else if (Key == "Name") lua_pushstring(State, Value->Name.c_str());
    else if (Key == "Title") lua_pushstring(State, Value->Title.c_str());
    else if (Key == "Text") lua_pushstring(State, Value->Text.c_str());
    else if (Key == "Visible") lua_pushboolean(State, Value->Visible);
    else if (Key == "Enabled") lua_pushboolean(State, Value->Enabled);
    else if (Key == "Checked") lua_pushboolean(State, Value->Checked);
    else if (Key == "IsFocused") lua_pushboolean(State, Value->IsFocused);
    else if (Key == "Minimum") lua_pushnumber(State, Value->Minimum);
    else if (Key == "Maximum") lua_pushnumber(State, Value->Maximum);
    else if (Key == "Value") lua_pushnumber(State, Value->Value);
    else if (Key == "Size") PushSize(State, Value->Size);
    else if (Key == "Position") PushSize(State, Value->Position);
    else if (Key == "AnchorPoint") PushVector(State, Value->AnchorPoint);
    else if (Key == "AbsolutePosition" || Key == "AbsoluteSize") {
        FlushLayout(GetRuntime(State));
        if (Key == "AbsolutePosition") PushVector(State, {Value->Bounds.X, Value->Bounds.Y});
        else PushVector(State, {Value->Bounds.Width, Value->Bounds.Height});
    }
    else if (Key == "LayoutOrder") lua_pushinteger(State, Value->LayoutOrder);
    else if (Key == "FillDirection") lua_pushstring(State, Value->FillDirection.c_str());
    else if (Key == "Padding") PushDimension(State, Value->Padding);
    else if (Key == "PaddingTop") PushDimension(State, Value->PaddingTop);
    else if (Key == "PaddingBottom") PushDimension(State, Value->PaddingBottom);
    else if (Key == "PaddingLeft") PushDimension(State, Value->PaddingLeft);
    else if (Key == "PaddingRight") PushDimension(State, Value->PaddingRight);
    else if (Key == "Parent") {
        auto* Runtime = GetRuntime(State);
        auto Found = Runtime->Nodes.find(Value->ParentId);
        PushNode(State, Found == Runtime->Nodes.end() ? nullptr : Found->second.get());
    } else if (Key == "Activated" && (Value->ClassName == "TextButton" || Value->ClassName == "CheckBox")) PushSignal(State, Value, "Activated");
    else if (Key == "TextChanged" && Value->ClassName == "TextBox") PushSignal(State, Value, "TextChanged");
    else if (Key == "CheckedChanged" && Value->ClassName == "CheckBox") PushSignal(State, Value, "CheckedChanged");
    else if (Key == "ValueChanged" && Value->ClassName == "Slider") PushSignal(State, Value, "ValueChanged");
    else if (Key == "Focused" && LuiSchema::IsA(Value->ClassName, "GuiObject")) PushSignal(State, Value, "Focused");
    else if (Key == "FocusLost" && LuiSchema::IsA(Value->ClassName, "GuiObject")) PushSignal(State, Value, "FocusLost");
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

static int InitializeNode(lua_State* State) {
    Node* Value = GetNode(State, 1);
    luaL_checktype(State, 2, LUA_TTABLE);
    const bool IsRange = Value->ClassName == "Slider" || Value->ClassName == "ProgressBar";
    if (IsRange) {
        double Minimum = Value->Minimum;
        double Maximum = Value->Maximum;
        lua_getfield(State, 2, "Minimum");
        bool HasMinimum = !lua_isnil(State, -1);
        if (HasMinimum) {
            if (lua_type(State, -1) != LUA_TNUMBER) luaL_error(State, "Minimum must be number");
            Minimum = lua_tonumber(State, -1);
        }
        lua_pop(State, 1);
        lua_getfield(State, 2, "Maximum");
        bool HasMaximum = !lua_isnil(State, -1);
        if (HasMaximum) {
            if (lua_type(State, -1) != LUA_TNUMBER) luaL_error(State, "Maximum must be number");
            Maximum = lua_tonumber(State, -1);
        }
        lua_pop(State, 1);
        if (!std::isfinite(Minimum) || !std::isfinite(Maximum) || Minimum > Maximum)
            luaL_error(State, "Minimum must not exceed Maximum");
        Value->Minimum = Minimum;
        Value->Maximum = Maximum;
        Value->Value = std::clamp(Value->Value, Minimum, Maximum);
        if (HasMinimum) QueueProperty(GetRuntime(State), Value, "Minimum", std::to_string(Minimum));
        if (HasMaximum) QueueProperty(GetRuntime(State), Value, "Maximum", std::to_string(Maximum));
        if (HasMinimum || HasMaximum) QueueProperty(GetRuntime(State), Value, "Value", std::to_string(Value->Value));
    }
    lua_pushnil(State);
    while (lua_next(State, 2) != 0) {
        const char* Key = luaL_checkstring(State, -2);
        const std::string Property = Key;
        if (Property != "Parent" && (!IsRange || (Property != "Minimum" && Property != "Maximum" && Property != "Value")))
            SetProperty(State, Value, Key, -1);
        lua_pop(State, 1);
    }
    if (IsRange) {
        lua_getfield(State, 2, "Value");
        if (!lua_isnil(State, -1)) SetProperty(State, Value, "Value", -1);
        lua_pop(State, 1);
    }
    lua_getfield(State, 2, "Parent");
    if (!lua_isnil(State, -1)) SetProperty(State, Value, "Parent", -1);
    lua_pop(State, 1);
    return 0;
}

static int InstanceNew(lua_State* State) {
    const int ArgumentCount = lua_gettop(State);
    const std::string ClassName = luaL_checkstring(State, 1);
    const LuiSchema::ClassDefinition* Definition = LuiSchema::FindClass(ClassName);
    if (!Definition || !Definition->Creatable) {
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
    if (Definition->Native && Runtime->Backend.Create)
        Runtime->Backend.Create(Runtime->Backend.Context, Id, ClassName.c_str());
    Runtime->LayoutDirty = true;

    if (ArgumentCount >= 2 && !lua_isnil(State, 2)) {
        const int ResultIndex = lua_gettop(State);
        lua_pushcfunction(State, InitializeNode, "InitializeNode");
        lua_pushvalue(State, ResultIndex);
        lua_pushvalue(State, 2);
        if (lua_pcall(State, 2, 0, 0) != LUA_OK) {
            DestroyNode(Runtime, Runtime->Nodes.at(Id).get());
            Runtime->Nodes.erase(Id);
            lua_error(State);
        }
    }
    return 1;
}

static int UDimNew(lua_State* State) {
    Dimension Value{luaL_checknumber(State, 1), luaL_checknumber(State, 2)};
    if (!std::isfinite(Value.Scale) || !std::isfinite(Value.Offset)) luaL_error(State, "UDim values must be finite");
    PushDimension(State, Value);
    return 1;
}

static int Vector2New(lua_State* State) {
    VectorValue Value{luaL_checknumber(State, 1), luaL_checknumber(State, 2)};
    if (!std::isfinite(Value.X) || !std::isfinite(Value.Y)) luaL_error(State, "Vector2 values must be finite");
    PushVector(State, Value);
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

static double Resolve(Dimension Value, double ParentExtent) {
    return ParentExtent * Value.Scale + Value.Offset;
}

static void ArrangeNode(LuiRuntime* Runtime, Node* Value, BoundsValue Bounds) {
    Value->Bounds = Bounds;
    if (Runtime->Backend.Arrange) Runtime->Backend.Arrange(Runtime->Backend.Context, Value->Id,
        Bounds.X, Bounds.Y, Bounds.Width, Bounds.Height);

    Node* Padding = nullptr;
    Node* List = nullptr;
    std::vector<Node*> VisualChildren;
    for (int Id : Value->Children) {
        Node* Child = Runtime->Nodes.at(Id).get();
        if (Child->ClassName == "UIPadding") Padding = Child;
        else if (Child->ClassName == "UIListLayout") List = Child;
        else VisualChildren.push_back(Child);
    }

    double Left = Padding ? Resolve(Padding->PaddingLeft, Bounds.Width) : 0;
    double Right = Padding ? Resolve(Padding->PaddingRight, Bounds.Width) : 0;
    double Top = Padding ? Resolve(Padding->PaddingTop, Bounds.Height) : 0;
    double Bottom = Padding ? Resolve(Padding->PaddingBottom, Bounds.Height) : 0;
    BoundsValue Inner{Bounds.X + Left, Bounds.Y + Top,
        std::max(0.0, Bounds.Width - Left - Right), std::max(0.0, Bounds.Height - Top - Bottom)};

    if (List) std::stable_sort(VisualChildren.begin(), VisualChildren.end(),
        [](const Node* LeftNode, const Node* RightNode) { return LeftNode->LayoutOrder < RightNode->LayoutOrder; });
    double Cursor = 0;
    for (Node* Child : VisualChildren) {
        double Width = std::max(0.0, Resolve(Child->Size.X, Inner.Width));
        double Height = std::max(0.0, Resolve(Child->Size.Y, Inner.Height));
        double X = Inner.X + Resolve(Child->Position.X, Inner.Width) - Child->AnchorPoint.X * Width;
        double Y = Inner.Y + Resolve(Child->Position.Y, Inner.Height) - Child->AnchorPoint.Y * Height;
        if (List) {
            X = Inner.X + (List->FillDirection == "Horizontal" ? Cursor : 0);
            Y = Inner.Y + (List->FillDirection == "Vertical" ? Cursor : 0);
            Cursor += (List->FillDirection == "Vertical" ? Height : Width) +
                Resolve(List->Padding, List->FillDirection == "Vertical" ? Inner.Height : Inner.Width);
        }
        ArrangeNode(Runtime, Child, {X, Y, Width, Height});
    }
}

static void FlushLayout(LuiRuntime* Runtime) {
    FlushChanges(Runtime);
    if (!Runtime->LayoutDirty) return;
    Runtime->LayoutDirty = false;
    for (auto& Pair : Runtime->Nodes) {
        Node* Value = Pair.second.get();
        if (!Value->Destroyed && Value->ClassName == "Window") {
            ArrangeNode(Runtime, Value, {0, 0,
                std::max(0.0, Resolve(Value->Size.X, 0)), std::max(0.0, Resolve(Value->Size.Y, 0))});
        }
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

static int WindowServiceGetWindows(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    std::vector<int> WindowIds;
    for (const auto& Pair : Runtime->Nodes) {
        if (!Pair.second->Destroyed && Pair.second->ClassName == "Window") WindowIds.push_back(Pair.first);
    }
    std::sort(WindowIds.begin(), WindowIds.end());
    lua_createtable(State, static_cast<int>(WindowIds.size()), 0);
    int Position = 1;
    for (int Id : WindowIds) {
        PushNode(State, Runtime->Nodes.at(Id).get());
        lua_rawseti(State, -2, Position++);
    }
    return 1;
}

static int PlatformServiceSupports(lua_State* State) {
    luaL_checkstring(State, 2);
    lua_pushboolean(State, false);
    return 1;
}

static int AppGetService(lua_State* State) {
    const std::string Name = luaL_checkstring(State, 2);
    if (!LuiSchema::FindService(Name)) luaL_error(State, "unknown service '%s'", Name.c_str());
    auto* Runtime = GetRuntime(State);
    auto Found = Runtime->ServiceRefs.find(Name);
    if (Found != Runtime->ServiceRefs.end()) {
        lua_getref(State, Found->second);
        return 1;
    }
    lua_newtable(State);
    if (Name == "WindowService") {
        lua_pushcfunction(State, WindowServiceGetWindows, "WindowService.GetWindows");
        lua_setfield(State, -2, "GetWindows");
    } else if (Name == "PlatformService") {
        lua_pushcfunction(State, PlatformServiceSupports, "PlatformService.Supports");
        lua_setfield(State, -2, "Supports");
        lua_pushstring(State, Runtime->BackendName.c_str());
        lua_setfield(State, -2, "BackendName");
    }
    lua_setreadonly(State, -1, true);
    Runtime->ServiceRefs.emplace(Name, lua_ref(State, -1));
    return 1;
}

static void RegisterGlobals(lua_State* State) {
    lua_pushcfunction(State, LuiPrint, "print");
    lua_setglobal(State, "print");
    RegisterMeta(State, "LuiNodeMeta", NodeIndex, NodeNewIndex);
    RegisterMeta(State, "LuiSignalMeta", SignalIndex);
    RegisterMeta(State, "LuiConnectionMeta", ConnectionIndex);
    lua_newtable(State);
    lua_pushcfunction(State, InstanceNew, "Instance.new");
    lua_setfield(State, -2, "new");
    lua_setglobal(State, "Instance");
    lua_newtable(State);
    lua_pushcfunction(State, AppGetService, "app.GetService");
    lua_setfield(State, -2, "GetService");
    lua_setreadonly(State, -1, true);
    lua_setglobal(State, "app");
    lua_newtable(State);
    lua_pushcfunction(State, Vector2New, "Vector2.new");
    lua_setfield(State, -2, "new");
    lua_setglobal(State, "Vector2");
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

extern "C" LUI_API void LUI_CALL Lui_SetBackendName(LuiRuntime* Runtime, const char* Name) {
    if (!CheckOwner(Runtime) || !Name) return;
    if (!Runtime->ServiceRefs.empty()) {
        Runtime->LastError = "[LUI:Runtime] Backend name must be set before services are requested";
        return;
    }
    Runtime->BackendName = Name;
}

extern "C" LUI_API void LUI_CALL Lui_SetLogCallback(LuiRuntime* Runtime, void* Context, LuiLogCallback Callback) {
    if (!CheckOwner(Runtime)) return;
    Runtime->LogContext = Context;
    Runtime->LogCallback = Callback;
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
            EmitLog(Runtime, "Error", "Runtime: " + Runtime->LastError);
            lua_pop(Runtime->State, 1);
            FlushLayout(Runtime);
            return 0;
        }
        Runtime->LastError.clear();
        FlushLayout(Runtime);
        return 1;
    } catch (const std::exception& Error) {
        Runtime->LastError = Error.what();
        EmitLog(Runtime, "Error", "Runtime: " + Runtime->LastError);
        FlushLayout(Runtime);
        return 0;
    }
}

extern "C" LUI_API int LUI_CALL Lui_Activate(LuiRuntime* Runtime, int Id) {
    if (!CheckOwner(Runtime)) return 0;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || !CanReceiveInput(Runtime, Found->second.get()) ||
        (Found->second->ClassName != "TextButton" && Found->second->ClassName != "CheckBox")) return 0;
    FireSignal(Runtime, Found->second.get(), "Activated");
    FlushLayout(Runtime);
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_TextChanged(LuiRuntime* Runtime, int Id, const char* Text) {
    if (!CheckOwner(Runtime) || !Text) return 0;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->ClassName != "TextBox" ||
        !CanReceiveInput(Runtime, Found->second.get())) return 0;
    Node* Value = Found->second.get();
    if (Value->Text != Text) {
        Value->Text = Text;
        FireSignal(Runtime, Value, "Changed");
        FireSignal(Runtime, Value, "TextChanged");
    }
    FlushLayout(Runtime);
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_CheckedChanged(LuiRuntime* Runtime, int Id, int Checked) {
    if (!CheckOwner(Runtime)) return 0;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->ClassName != "CheckBox" ||
        !CanReceiveInput(Runtime, Found->second.get())) return 0;
    Node* Value = Found->second.get();
    bool Parsed = Checked != 0;
    if (Value->Checked != Parsed) {
        Value->Checked = Parsed;
        FireSignal(Runtime, Value, "Changed");
        FireSignal(Runtime, Value, "CheckedChanged");
    }
    FlushLayout(Runtime);
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_ValueChanged(LuiRuntime* Runtime, int Id, double NewValue) {
    if (!CheckOwner(Runtime) || !std::isfinite(NewValue)) return 0;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->ClassName != "Slider" ||
        !CanReceiveInput(Runtime, Found->second.get())) return 0;
    Node* Value = Found->second.get();
    if (NewValue < Value->Minimum || NewValue > Value->Maximum) return 0;
    if (Value->Value != NewValue) {
        Value->Value = NewValue;
        FireSignal(Runtime, Value, "Changed");
        FireSignal(Runtime, Value, "ValueChanged");
    }
    FlushLayout(Runtime);
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_FocusChanged(LuiRuntime* Runtime, int Id, int Focused) {
    if (!CheckOwner(Runtime)) return 0;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->Destroyed || !LuiSchema::IsA(Found->second->ClassName, "GuiObject")) return 0;
    Node* Value = Found->second.get();
    bool Parsed = Focused != 0;
    if (Parsed && !CanReceiveInput(Runtime, Value)) return 0;
    if (Value->IsFocused != Parsed) {
        Value->IsFocused = Parsed;
        FireSignal(Runtime, Value, Parsed ? "Focused" : "FocusLost");
    }
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
            EmitLog(Runtime, "Error", "Scheduler: " + Runtime->LastError);
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

extern "C" LUI_API const char* LUI_CALL Lui_GetSchemaJson(void) {
    return LuiSchema::GetJson().c_str();
}

extern "C" LUI_API void LUI_CALL Lui_Destroy(LuiRuntime* Runtime) {
    if (!CheckOwner(Runtime)) return;
    for (auto& Pair : Runtime->Nodes) {
        if (!Pair.second->Destroyed && Pair.second->ClassName == "Window") DestroyNode(Runtime, Pair.second.get());
    }
    for (auto& Pair : Runtime->Nodes) if (!Pair.second->Destroyed) DestroyNode(Runtime, Pair.second.get());
    for (const auto& Call : Runtime->Tasks) lua_unref(Runtime->State, Call.Reference);
    for (const auto& Pair : Runtime->ServiceRefs) lua_unref(Runtime->State, Pair.second);
    lua_close(Runtime->State);
    delete Runtime;
}
