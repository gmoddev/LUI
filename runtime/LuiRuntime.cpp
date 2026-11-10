#include "LuiRuntime.h"
#include "reflection/Schema.h"
#include "internal/State.h"
#include "internal/Dispatch.h"
#include "internal/Input.h"
#include "internal/Text.h"
#include "internal/Images.h"
#include "internal/Frames.h"
#include "../ui/style/Style.h"
#include "internal/Extensions.h"
#include "internal/NativeServices.h"
#include "internal/ApplicationServices.h"
#include "services/Domain.h"
#include "internal/Platform.h"
#include "internal/Diagnostics.h"
#include "internal/Scheduler.h"
#include "internal/Network.h"
#include "internal/Modules.h"
#include "project/Host.h"
#include "../ui/layout/Layout.h"

#include "Luau/Compiler.h"
#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

void FlushLayout(LuiRuntime* Runtime);
static void DrainBackendEvents(LuiRuntime* Runtime);
static LuiRuntime* GetRuntime(lua_State* State);

static SourceLocation CaptureSourceLocation(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    if (!Runtime || !Runtime->SourceProvenanceEnabled) return {};
    for (int Level = 0; Level < 16; ++Level) {
        lua_Debug Debug{};
        if (!lua_getinfo(State, Level, "sl", &Debug)) break;
        if (Debug.currentline <= 0 || !Debug.source || !*Debug.source ||
            !(std::strcmp(Debug.what, "Lua") == 0 || std::strcmp(Debug.what, "main") == 0)) continue;
        std::string Source = Debug.source;
        if (!Source.empty() && (Source[0] == '@' || Source[0] == '=')) Source.erase(0, 1);
        if (Source.size() > 1024) Source.resize(1024);
        return {Source, Debug.currentline};
    }
    return {};
}

static void RecordSourceMutation(lua_State* State, Node* Value, const char* Property) {
    SourceLocation Location = CaptureSourceLocation(State);
    if (Location.Line <= 0) return;
    Value->LastChangedAt = std::move(Location);
    Value->LastChangedProperty = Property;
}

struct DepthGuard {
    int& Depth;
    explicit DepthGuard(int& Value, LuiRuntime* Runtime = nullptr) : Depth(Value) {
        if (Runtime && !Depth) Runtime->InterruptCount = 0;
        ++Depth;
    }
    ~DepthGuard() { --Depth; }
};

static void* VmAllocate(void* Context, void* Pointer, size_t OldSize, size_t NewSize) {
    auto* Runtime = static_cast<LuiRuntime*>(Context);
    if (!NewSize) {
        std::free(Pointer);
        Runtime->VmBytes -= Pointer ? OldSize : 0;
        return nullptr;
    }
    const uint64_t Previous = Pointer ? OldSize : 0;
    if (NewSize > Previous && Runtime->VmLimitBytes &&
        NewSize - Previous > Runtime->VmLimitBytes - Runtime->VmBytes) return nullptr;
    void* Result = std::realloc(Pointer, NewSize);
    if (Result) Runtime->VmBytes = Runtime->VmBytes - Previous + NewSize;
    return Result;
}

static void VmInterrupt(lua_State* State, int) {
    auto* Runtime = static_cast<LuiRuntime*>(lua_callbacks(State)->userdata);
    if (Runtime && Runtime->MaxInterrupts && ++Runtime->InterruptCount > Runtime->MaxInterrupts) {
        MarkModuleLimit(Runtime);
        luaL_error(State, "[LUI:Sandbox] execution limit exceeded");
    }
}

static void EmitLog(LuiRuntime* Runtime, const char* Level, const std::string& Message) {
    DeliverDiagnostic(Runtime,Level,Message,std::string(Level)=="Print");
}

void FailBackend(LuiRuntime* Runtime, const char* Operation, int Id) {
    Runtime->BackendFailed = true;
    Runtime->LastError = "[LUI:Backend] " + std::string(Operation) + " failed for Instance " + std::to_string(Id);
    if (!Runtime->BackendError.empty()) Runtime->LastError += ": " + Runtime->BackendError;
    Runtime->BackendError.clear();
    Runtime->PendingChanges.clear();
    Runtime->PendingBackendEvents.clear();
    Runtime->PendingBackendBytes = 0;
    EmitLog(Runtime, "Error", Runtime->LastError);
}

int QueueBackendEventIfBusy(LuiRuntime* Runtime, BackendEvent Event) {
    if (!Runtime->VmDepth && !Runtime->BackendDepth) return -1;
    if (Runtime->PendingBackendEvents.size() >= 4096 || Event.Text.size() > 8*1024*1024 - Runtime->PendingBackendBytes) {
        Runtime->LastError = "[LUI:Scheduler] Backend event queue limit exceeded";
        EmitLog(Runtime, "Error", Runtime->LastError);
        return 0;
    }
    Runtime->PendingBackendBytes += Event.Text.size();
    Runtime->PendingBackendEvents.push_back(std::move(Event));
    return 1;
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

static LuiRuntime* GetRuntime(lua_State* State) {
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiRuntime");
    auto* Runtime = static_cast<LuiRuntime*>(lua_touserdata(State, -1));
    lua_pop(State, 1);
    return Runtime;
}

// Readable dead metadata follows the application's userdata lifetime.
struct NodeHandle {int Id; LuiRuntime* Owner; Node* Retired;};
static constexpr int NodeUserdataTag = 1;
static void DestroyNodeHandle(lua_State*, void* Memory) {
    auto* Handle = static_cast<NodeHandle*>(Memory);
    if (Handle->Retired) {
        delete Handle->Retired; --Handle->Owner->RetiredUserdata;
    } else {
        const auto Found = Handle->Owner->Nodes.find(Handle->Id);
        if (Found != Handle->Owner->Nodes.end()) Found->second->Handle = nullptr;
    }
}
static void NewNodeHandle(lua_State* State, LuiRuntime* Runtime, Node* Value) {
    auto* Handle = static_cast<NodeHandle*>(lua_newuserdatatagged(State, sizeof(NodeHandle), NodeUserdataTag));
    *Handle = {Value->Id, Runtime, nullptr}; Value->Handle = Handle;
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
    Node* Value = Found == Runtime->Nodes.end() ? static_cast<NodeHandle*>(lua_touserdata(State, Index))->Retired : Found->second.get();
    if (!Value || (!AllowDestroyed && Value->Destroyed)) {
        luaL_error(State, "Instance is destroyed");
    }
    return Value;
}

static void PushNode(lua_State* State, const Node* Value) {
    if (!Value || Value->Destroyed || !Value->Reference) {
        lua_pushnil(State);
    } else {
        lua_getref(State, Value->Reference);
    }
}

void QueueProperty(LuiRuntime* Runtime, const Node* Value, const char* Name, const std::string& Text) {
    if (!LuiSchema::IsNative(Value->ClassName)) return;
    for (auto Change = Runtime->PendingChanges.begin(); Change != Runtime->PendingChanges.end(); ++Change) {
        if (Change->Type == BackendChange::Kind::Property && Change->Id == Value->Id && Change->Name == Name) {
            if (std::string(Name) == "__CodeStyles") {
                Runtime->PendingChanges.erase(Change);
                break;
            }
            Change->Value = Text;
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
        if (Change.Type == BackendChange::Kind::Property && Change.Name == "Source") StartImage(Runtime, Found->second.get());
        Runtime->BackendError.clear();
        if (Change.Type == BackendChange::Kind::Property && Runtime->Backend.Property) {
            if (!Runtime->Backend.Property(Runtime->Backend.Context, Change.Id, Change.Name.c_str(), Change.Value.c_str())) {
                FailBackend(Runtime, "Property", Change.Id);
                return;
            }
        } else if (Change.Type == BackendChange::Kind::Parent && Runtime->Backend.Parent) {
            if (!Runtime->Backend.Parent(Runtime->Backend.Context, Change.Id, Change.ParentId)) {
                FailBackend(Runtime, "Parent", Change.Id);
                return;
            }
        }
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

void FireSignal(LuiRuntime* Runtime, Node* Value, const char* Signal,
    const PointerInputValue* Pointer, const KeyboardInputValue* Keyboard, bool* Failed) {
    if ((Runtime->Modules && Runtime->ShuttingDown) || Runtime->ModuleDepth) return;
    const std::vector<int> Snapshot = Value->Listeners;
    for (int Id : Snapshot) {
        if (Runtime->BackendFailed) break;
        auto Found = Runtime->Listeners.find(Id);
        if (Found == Runtime->Listeners.end() || !Found->second.Active || Found->second.Signal != Signal || Value->Destroyed) continue;
        lua_getref(Runtime->State, Found->second.Reference);
        if (Pointer) {
            lua_createtable(Runtime->State, 0, Pointer->Button.empty() ? 4 : 5);
            lua_pushstring(Runtime->State, Pointer->Device.c_str());
            lua_setfield(Runtime->State, -2, "Device");
            lua_pushnumber(Runtime->State, Pointer->PointerId);
            lua_setfield(Runtime->State, -2, "PointerId");
            PushVector(Runtime->State, Pointer->Position);
            lua_setfield(Runtime->State, -2, "Position");
            lua_pushboolean(Runtime->State, Pointer->IsCanceled);
            lua_setfield(Runtime->State, -2, "IsCanceled");
            if (!Pointer->Button.empty()) {
                lua_pushstring(Runtime->State, Pointer->Button.c_str());
                lua_setfield(Runtime->State, -2, "Button");
            }
            lua_setreadonly(Runtime->State, -1, true);
        } else if (Keyboard) {
            lua_createtable(Runtime->State, 0, 3);
            lua_pushstring(Runtime->State, "Keyboard");
            lua_setfield(Runtime->State, -2, "Device");
            lua_pushstring(Runtime->State, Keyboard->Key.c_str());
            lua_setfield(Runtime->State, -2, "Key");
            lua_pushboolean(Runtime->State, Keyboard->IsRepeat);
            lua_setfield(Runtime->State, -2, "IsRepeat");
            if (Keyboard->IsShortcut) {
                lua_pushboolean(Runtime->State, (Keyboard->Modifiers & 1) != 0);
                lua_setfield(Runtime->State, -2, "Control");
                lua_pushboolean(Runtime->State, (Keyboard->Modifiers & 2) != 0);
                lua_setfield(Runtime->State, -2, "Shift");
                lua_pushboolean(Runtime->State, (Keyboard->Modifiers & 4) != 0);
                lua_setfield(Runtime->State, -2, "Alt");
                lua_pushboolean(Runtime->State, Keyboard->TextEditing);
                lua_setfield(Runtime->State, -2, "TextEditing");
            }
            lua_setreadonly(Runtime->State, -1, true);
        }
        DepthGuard Guard(Runtime->VmDepth, Runtime);
        if (lua_pcall(Runtime->State, Pointer || Keyboard ? 1 : 0, 0, 0) != LUA_OK) {
            if (Failed) *Failed = true;
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
    if (Parent && (Parent->WindowClosed || Parent->WindowClosePending || Parent->NativeWindowClosedPending))
        luaL_error(Runtime->State, "[LUI:Window] Closed or closing parent");
    const std::string ParentRule = LuiSchema::GetParentRule(Value->ClassName);
    if (Parent && ParentRule == "none") luaL_error(Runtime->State, "%s cannot have a Parent", Value->ClassName.c_str());
    if (Parent) {
        const LuiSchema::ClassDefinition* ParentClass = LuiSchema::FindClass(Parent->ClassName);
        const bool Container = ParentClass && ParentClass->AcceptsChildren;
        if ((ParentRule == "container" && !Container) ||
            (ParentRule == "visual" && !Container && !LuiSchema::IsA(Parent->ClassName, "GuiObject")))
            luaL_error(Runtime->State, "invalid Parent for %s", Value->ClassName.c_str());
    }
    if (Parent && LuiSchema::IsA(Value->ClassName, "UIComponent")) {
        for (int Id : Parent->Children) {
            const Node* Sibling = Runtime->Nodes.at(Id).get();
            if (Sibling != Value && Sibling->ClassName == Value->ClassName)
                luaL_error(Runtime->State, "only one %s is allowed per container", Value->ClassName.c_str());
            const bool AddingLayout = Value->ClassName == "UIListLayout" || Value->ClassName == "UIGridLayout";
            const bool ExistingLayout = Sibling->ClassName == "UIListLayout" || Sibling->ClassName == "UIGridLayout";
            if (Sibling != Value && AddingLayout && ExistingLayout)
                luaL_error(Runtime->State, "only one list or grid layout is allowed per container");
        }
    }
    for (Node* Cursor = Parent; Cursor; ) {
        if (Cursor->Id == Value->Id) luaL_error(Runtime->State, "Parent cycle is not allowed");
        auto Found = Runtime->Nodes.find(Cursor->ParentId);
        Cursor = Found == Runtime->Nodes.end() ? nullptr : Found->second.get();
    }
    if (Value->ParentId == (Parent ? Parent->Id : 0)) return;
    if (Value->ClassName == "UISizeConstraint") {
        auto PreviousParent = Runtime->Nodes.find(Value->ParentId);
        if (PreviousParent != Runtime->Nodes.end() && PreviousParent->second->ClassName == "Window")
            PreviousParent->second->HasViewportSize = false;
        if (Parent && Parent->ClassName == "Window") Parent->HasViewportSize = false;
    }
    if (Value->ParentId) {
        auto& Siblings = Runtime->Nodes.at(Value->ParentId)->Children;
        Siblings.erase(std::remove(Siblings.begin(), Siblings.end(), Value->Id), Siblings.end());
    }
    Value->ParentId = Parent ? Parent->Id : 0;
    if (Parent) Parent->Children.push_back(Value->Id);
    if(Parent && Parent->EffectiveSensitive) ClassifyNode(Runtime,Value);
    QueueParent(Runtime, Value);
    Runtime->LayoutDirty = true;
    ClearInvalidInput(Runtime);
    RecordSourceMutation(Runtime->State, Value, "Parent");
    FireSignal(Runtime, Value, "Changed");
}

static void DestroyNode(LuiRuntime* Runtime, Node* Value) {
    if (Value->Destroyed || Value->DestroyingInProgress) return;
    Value->DestroyingInProgress = true;
    ClearFrameSurface(Runtime, Value, true);
    CancelImage(Runtime, Value);
    if (!Runtime->BackendFailed && !(Runtime->ShuttingDown && Value->ClassName == "FrameSurface"))
        FireSignal(Runtime, Value, "Destroying");
    if (Value->ClassName == "UISizeConstraint") {
        auto Parent = Runtime->Nodes.find(Value->ParentId);
        if (Parent != Runtime->Nodes.end() && Parent->second->ClassName == "Window")
            Parent->second->HasViewportSize = false;
    }
    if (Value->ParentId) {
        auto& Siblings = Runtime->Nodes.at(Value->ParentId)->Children;
        Siblings.erase(std::remove(Siblings.begin(), Siblings.end(), Value->Id), Siblings.end());
        Value->ParentId = 0;
    }
    const std::vector<int> Children = Value->Children;
    for (int Id : Children) DestroyNode(Runtime, Runtime->Nodes.at(Id).get());
    Value->Children.clear();
    Value->IsFocused = false;
    Value->IsHovered = false;
    Value->ActivePointers.clear();
    Value->ActiveKeys.clear();
    Value->Destroyed = true;
    auto& Pending = Runtime->PendingChanges;
    Pending.erase(std::remove_if(Pending.begin(), Pending.end(),
        [Value](const BackendChange& Change) { return Change.Id == Value->Id; }), Pending.end());
    for (int Id : Value->Listeners) Disconnect(Runtime, Id);
    if (Value->NativeCreated && LuiSchema::IsNative(Value->ClassName) && Runtime->Backend.Destroy) {
        DepthGuard Guard(Runtime->BackendDepth);
        Runtime->BackendError.clear();
        if (!Runtime->Backend.Destroy(Runtime->Backend.Context, Value->Id))
            FailBackend(Runtime, "Destroy", Value->Id);
    }
    lua_unref(Runtime->State, Value->Reference);
    Value->Reference = 0;
    Value->DestroyingInProgress = false;
    Runtime->LayoutDirty = true;
}

static bool HasNativeBackend(const LuiBackendCallbacks& Backend) {
    return Backend.Create || Backend.Property || Backend.Parent || Backend.Arrange || Backend.Destroy;
}

static bool UsesViewportFeatures(const Node* Value) {
    return Value->ClassName == "ScrollingFrame" || Value->ClipsDescendants || Value->ZIndex != 0;
}
static void CancelComposition(LuiRuntime* Runtime, Node* Value) {
    if (!Value->IsComposing) return;
    Value->IsComposing = false; Value->CompositionText.clear();
    FireSignal(Runtime, Value, "CompositionEnded");
}

static void SetProperty(lua_State* State, Node* Value, const char* Name, int ValueIndex) {
    CheckModuleEffect(State);
    auto* Runtime = GetRuntime(State);
    const std::string Key = Name;
    if ((Value->WindowClosed || Value->WindowClosePending) && Key != "Name" && Key != "Sensitive")
        luaL_error(State, "[LUI:Window] Closed or closing");
    const double PreviousValue = Value->Value;
    const LuiSchema::PropertyDefinition* Definition = LuiSchema::FindProperty(Value->ClassName, Key);
    if (!Definition || Definition->ReadOnly) luaL_error(State, "unknown or read-only property '%s'", Name);
    const bool IsComponent = LuiSchema::IsA(Value->ClassName, "UIComponent");
    if (LuiStyle::IsProperty(Key) && HasNativeBackend(Runtime->Backend) && !Runtime->Presentation.Present)
        luaL_error(State, "[LUI:Style] Native mapping is not implemented");
    if (Key == "Parent") {
        SetParent(Runtime, Value, lua_isnil(State, ValueIndex) ? nullptr : GetNode(State, ValueIndex));
        return;
    }
    if (Key == "ScaleMode") {
        if (lua_type(State, ValueIndex) != LUA_TSTRING) luaL_error(State, "[LUI:FrameSurface] InvalidArgument");
        size_t Length = 0; const char* Mode = lua_tolstring(State, ValueIndex, &Length);
        if (!((Length == 3 && std::memcmp(Mode, "Fit", 3) == 0) ||
            (Length == 4 && std::memcmp(Mode, "Fill", 4) == 0) ||
            (Length == 7 && std::memcmp(Mode, "Stretch", 7) == 0))) luaL_error(State, "[LUI:FrameSurface] InvalidArgument");
        Value->ScaleMode.assign(Mode, Length);
        QueueProperty(Runtime, Value, "ScaleMode", Value->ScaleMode);
    } else if (LuiStyle::Set(State, Value, Key, ValueIndex)) {
        Runtime->LayoutDirty = true;
    } else if (Key == "Secret" || Key == "Multiline" || Key == "MaxLength" || Key == "PlaceholderText") {
        if (Value->ClassName == "CodeEditor" && (Key == "Secret" || Key == "Multiline"))
            luaL_error(State, "[LUI:CodeEditor] Editing mode is immutable");
        if (HasNativeBackend(Runtime->Backend) && !Runtime->Presentation.Present)
            luaL_error(State, "[LUI:Text] Native mapping is not implemented");
        if (Key == "Secret" || Key == "Multiline") {
            if (lua_type(State, ValueIndex) != LUA_TBOOLEAN) luaL_error(State, "[LUI:Text] Expected boolean");
            const bool Parsed = lua_toboolean(State, ValueIndex) != 0;
            if (Parsed && (Key == "Secret" ? Value->Multiline : Value->Secret))
                luaL_error(State, "[LUI:Text] Multiline secret input is unsupported");
            if (Key == "Secret") {
                Value->Secret = Parsed;
                if (Parsed) {ClassifyNode(Runtime, Value); Value->SelectionStart = Value->SelectionLength = 0;}
            } else Value->Multiline = Parsed;
        } else if (Key == "MaxLength") {
            if (lua_type(State, ValueIndex) != LUA_TNUMBER) luaL_error(State, "[LUI:Text] Expected integer");
            const double Parsed = lua_tonumber(State, ValueIndex); int Characters;
            const bool CodeEditor=Value->ClassName=="CodeEditor";
            const int Maximum=CodeEditor?LuiText::CodeEditorMaxCharacters:LuiText::MaxCharacters;
            if (!std::isfinite(Parsed) || Parsed != std::floor(Parsed) || Parsed < 0 || Parsed > Maximum ||
                !(CodeEditor?LuiText::CountCodeEditor(Value->Text,Characters):LuiText::Count(Value->Text,Characters)) ||
                Characters > Parsed)
                luaL_error(State, "[LUI:Text] Invalid character limit");
            if (Value->ClassName == "CodeEditor" && Parsed < Value->CodeEditorUndoMaxLength)
                luaL_error(State, "[LUI:CodeEditor] MaxLength would reject native Undo");
            Value->MaxLength = static_cast<int>(Parsed);
        } else {
            if (lua_type(State, ValueIndex) != LUA_TSTRING) luaL_error(State, "[LUI:Text] Expected string");
            size_t Bytes; const char* Text = lua_tolstring(State, ValueIndex, &Bytes); int Characters;
            const std::string Placeholder(Text, Bytes);
            if (Bytes > 4096 || !LuiText::Count(Placeholder, Characters)) luaL_error(State, "[LUI:Text] Invalid placeholder");
            Value->PlaceholderText = Placeholder;
        }
        ++Value->InputRevision;
        CancelComposition(Runtime, Value);
        QueueProperty(Runtime, Value, "__TextConfig", std::string(Value->Multiline ? "1" : "0") + "," +
            (Value->Secret ? "1" : "0") + "," + std::to_string(Value->MaxLength) + "," + Value->PlaceholderText);
        Runtime->LayoutDirty = true;
    } else if (Key == "SelectionStart" || Key == "SelectionLength") {
        if (HasNativeBackend(Runtime->Backend) && !Runtime->Presentation.Present)
            luaL_error(State, "[LUI:Text] Native mapping is not implemented");
        if (lua_type(State, ValueIndex) != LUA_TNUMBER) luaL_error(State, "[LUI:Text] Expected integer");
        const double Parsed = lua_tonumber(State, ValueIndex); int Characters;
        const bool CodeEditor=Value->ClassName=="CodeEditor";
        const int Maximum=CodeEditor?LuiText::CodeEditorMaxCharacters:LuiText::MaxCharacters;
        if (!std::isfinite(Parsed) || Parsed != std::floor(Parsed) || Parsed < 0 || Parsed > Maximum ||
            !(CodeEditor?LuiText::CountCodeEditor(Value->Text,Characters):LuiText::Count(Value->Text,Characters)) ||
            (Value->Secret && Parsed != 0))
            luaL_error(State, "[LUI:Text] Invalid selection");
        if (Key == "SelectionStart") Value->SelectionStart = std::min(static_cast<int>(Parsed), Characters);
        else Value->SelectionLength = static_cast<int>(Parsed);
        Value->SelectionLength = std::min(Value->SelectionLength, Characters - Value->SelectionStart);
        ++Value->InputRevision;
        QueueProperty(Runtime, Value, "__Selection", std::to_string(Value->SelectionStart) + "," + std::to_string(Value->SelectionLength));
        FireSignal(Runtime, Value, "SelectionChanged");
    } else if(Key=="Sensitive") {
        if(lua_type(State,ValueIndex)!=LUA_TBOOLEAN) luaL_error(State,"Sensitive must be boolean");
        Value->Sensitive=lua_toboolean(State,ValueIndex)!=0;
        if(Value->Sensitive) ClassifyNode(Runtime,Value);
    } else if (Key == "ClipsDescendants") {
        if (lua_type(State, ValueIndex) != LUA_TBOOLEAN) luaL_error(State, "ClipsDescendants must be boolean");
        const bool Parsed = lua_toboolean(State, ValueIndex) != 0;
        if (Value->ClassName == "ScrollingFrame" && !Parsed) luaL_error(State, "ScrollingFrame always clips descendants");
        if (Parsed && HasNativeBackend(Runtime->Backend) && !Runtime->Presentation.Present) luaL_error(State, "[LUI:Viewport] Native mapping is not implemented");
        Value->ClipsDescendants = Parsed;
        Runtime->LayoutDirty = true;
    } else if (Key == "ZIndex") {
        if (lua_type(State, ValueIndex) != LUA_TNUMBER) luaL_error(State, "ZIndex must be integer");
        const double Parsed = lua_tonumber(State, ValueIndex);
        if (!std::isfinite(Parsed) || Parsed != std::floor(Parsed) || Parsed < INT_MIN || Parsed > INT_MAX)
            luaL_error(State, "ZIndex must be a finite 32-bit integer");
        if (Parsed != 0 && HasNativeBackend(Runtime->Backend) && !Runtime->Presentation.Present) luaL_error(State, "[LUI:Viewport] Native mapping is not implemented");
        Value->ZIndex = static_cast<int>(Parsed);
        Runtime->LayoutDirty = true;
    } else if (Key == "CanvasSize") {
        SizeValue Parsed;
        if (!ReadSize(State, ValueIndex, Parsed) || Parsed.X.Scale < 0 || Parsed.Y.Scale < 0 ||
            Parsed.X.Offset < 0 || Parsed.Y.Offset < 0 || Parsed.X.Scale > 1e6 || Parsed.Y.Scale > 1e6 ||
            Parsed.X.Offset > 1e9 || Parsed.Y.Offset > 1e9)
            luaL_error(State, "CanvasSize requires nonnegative finite components within canvas limits");
        Value->CanvasSize = Parsed;
        Runtime->LayoutDirty = true;
    } else if (Key == "CanvasPosition") {
        VectorValue Parsed;
        if (!ReadVector(State, ValueIndex, Parsed) || Parsed.X < 0 || Parsed.Y < 0 || Parsed.X > 1e9 || Parsed.Y > 1e9)
            luaL_error(State, "CanvasPosition requires finite components between zero and 1e9");
        Value->CanvasPosition = Parsed;
        Runtime->LayoutDirty = true;
    } else if (Key == "Source") {
        if (Value->ClassName != "ImageLabel" || lua_type(State, ValueIndex) != LUA_TSTRING)
            luaL_error(State, "Source belongs to ImageLabel and must be string");
        const std::string Source = lua_tostring(State, ValueIndex);
        if (!Source.empty() && Runtime->Assets.find(Source) == Runtime->Assets.end())
            luaL_error(State, "asset '%s' was not declared", Source.c_str());
        CancelImage(Runtime, Value);
        Value->Source = Source;
        QueueProperty(Runtime, Value, "Source", Source);
    } else if (Key == "Name" || Key == "Title" || Key == "Text" ||
        Key == "AccessibilityLabel" || Key == "AccessibilityDescription") {
        if (lua_type(State, ValueIndex) != LUA_TSTRING) luaL_error(State, "%s must be string", Name);
        const char* Text = lua_tostring(State, ValueIndex);
        std::string* Target = Key == "Name" ? &Value->Name : Key == "Title" ? &Value->Title :
            Key == "AccessibilityLabel" ? &Value->AccessibilityLabel :
            Key == "AccessibilityDescription" ? &Value->AccessibilityDescription : &Value->Text;
        if (Key == "Title" && Value->ClassName != "Window") luaL_error(State, "Title belongs to Window");
        if (Key == "Text" && Value->ClassName != "TextLabel" && Value->ClassName != "TextButton" &&
            !LuiSchema::IsA(Value->ClassName, "TextBox") && Value->ClassName != "CheckBox") luaL_error(State, "Text belongs to text controls");
        if (Key == "Text" && LuiSchema::IsA(Value->ClassName, "TextBox")) {
            size_t Bytes; const char* Input = lua_tolstring(State, ValueIndex, &Bytes); int Characters;
            const bool CodeEditor=Value->ClassName=="CodeEditor";
            if (Bytes > (CodeEditor?LuiText::CodeEditorMaxBytes:LuiText::MaxBytes))
                luaL_error(State, "[LUI:Text] Invalid or oversized UTF-8 text");
            const std::string Candidate(Input, Bytes);
            if (!(CodeEditor?LuiText::CountCodeEditor(Candidate,Characters):LuiText::Count(Candidate,Characters)) ||
                Characters > Value->MaxLength)
                luaL_error(State, "[LUI:Text] Invalid or oversized UTF-8 text");
            if (Value->ClassName == "CodeEditor" && Candidate.find('\r') != std::string::npos)
                luaL_error(State, "[LUI:CodeEditor] Text must use LF line endings");
            ++Value->InputRevision;
            if (Value->IsComposing) {
                CancelComposition(Runtime, Value);
                QueueProperty(Runtime, Value, "__TextConfig", std::string(Value->Multiline ? "1" : "0") + "," +
                    (Value->Secret ? "1" : "0") + "," + std::to_string(Value->MaxLength) + "," + Value->PlaceholderText);
            }
            Value->SelectionStart = std::min(Value->SelectionStart, Characters);
            Value->SelectionLength = std::min(Value->SelectionLength, Characters - Value->SelectionStart);
        }
        const bool CodeTextChanged = Key == "Text" && Value->ClassName == "CodeEditor" && *Target != Text;
        *Target = Text;
        if (CodeTextChanged) ++Value->TextRevision;
        QueueProperty(Runtime, Value, Name, *Target);
        if (CodeTextChanged) QueueProperty(Runtime, Value, "__CodeStyles", "");
        if (Key == "Text" && LuiSchema::IsA(Value->ClassName, "TextBox"))
            QueueProperty(Runtime, Value, "__Selection", std::to_string(Value->SelectionStart) + "," + std::to_string(Value->SelectionLength));
    } else if (Key == "Visible") {
        if (IsComponent) luaL_error(State, "Visible does not belong to UI components");
        if (lua_type(State, ValueIndex) != LUA_TBOOLEAN) luaL_error(State, "Visible must be boolean");
        Value->Visible = lua_toboolean(State, ValueIndex) != 0;
        Runtime->LayoutDirty = true;
        QueueProperty(Runtime, Value, Name, Value->Visible ? "true" : "false");
        if (!Value->Visible) ClearInvalidInput(Runtime);
    } else if (Key == "Enabled" || Key == "Checked") {
        if (lua_type(State, ValueIndex) != LUA_TBOOLEAN) luaL_error(State, "%s must be boolean", Name);
        bool Parsed = lua_toboolean(State, ValueIndex) != 0;
        if (Key == "Enabled") {
            Value->Enabled = Parsed;
            Runtime->LayoutDirty = true;
            if (!Parsed) ClearInvalidInput(Runtime);
        }
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
        if (Key == "Size") {
            Value->Size = Parsed;
            if (Value->ClassName == "Window") Value->HasViewportSize = false;
        }
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
    } else if (Key == "MinSize" || Key == "MaxSize") {
        VectorValue Parsed;
        const bool Unbounded = Key == "MaxSize" && lua_isnil(State, ValueIndex);
        if (!Unbounded && (!ReadVector(State, ValueIndex, Parsed) || Parsed.X < 0 || Parsed.Y < 0))
            luaL_error(State, "%s must be a nonnegative finite Vector2", Name);
        if (Key == "MinSize") {
            if (Value->HasMaxSize && (Parsed.X > Value->MaxSize.X || Parsed.Y > Value->MaxSize.Y))
                luaL_error(State, "MinSize cannot exceed MaxSize");
            Value->MinSize = Parsed;
        } else {
            if (!Unbounded && (Parsed.X < Value->MinSize.X || Parsed.Y < Value->MinSize.Y))
                luaL_error(State, "MaxSize cannot be below MinSize");
            Value->HasMaxSize = !Unbounded;
            if (!Unbounded) Value->MaxSize = Parsed;
        }
        auto Parent = Runtime->Nodes.find(Value->ParentId);
        if (Value->ClassName == "UISizeConstraint" && Parent != Runtime->Nodes.end() &&
            Parent->second->ClassName == "Window") Parent->second->HasViewportSize = false;
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
    } else if (Key == "CellSize" || Key == "CellPadding") {
        SizeValue Parsed;
        if (!ReadSize(State, ValueIndex, Parsed)) luaL_error(State, "%s must be UDim2 with finite values", Name);
        if (Key == "CellPadding" && (Parsed.X.Scale < 0 || Parsed.X.Offset < 0 ||
            Parsed.Y.Scale < 0 || Parsed.Y.Offset < 0))
            luaL_error(State, "CellPadding must have nonnegative values");
        if (Key == "CellSize") Value->CellSize = Parsed;
        else Value->CellPadding = Parsed;
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
    RecordSourceMutation(State, Value, Name);
    FireSignal(Runtime, Value, "Changed");
    if (Key == "Text" && LuiSchema::IsA(Value->ClassName, "TextBox")) FireSignal(Runtime, Value, "TextChanged");
    if (Key == "Checked") FireSignal(Runtime, Value, "CheckedChanged");
    if ((Key == "Value" || Key == "Minimum" || Key == "Maximum") && Value->Value != PreviousValue)
        FireSignal(Runtime, Value, "ValueChanged");
}

static int NodeDestroy(lua_State* State) {
    CheckModuleEffect(State);
    if (!HasMeta(State, 1, "LuiNodeMeta")) luaL_error(State, "expected LUI Instance");
    auto* Runtime = GetRuntime(State);
    const int Id = *static_cast<int*>(lua_touserdata(State, 1));
    const auto Found = Runtime->Nodes.find(Id);
    if (Found != Runtime->Nodes.end()) DestroyNode(Runtime, Found->second.get());
    return 0;
}

static void CommitWindowClosed(LuiRuntime* Runtime, Node* Value) {
    if (Value->Destroyed || Value->WindowClosed) return;
    // Closure retires the native top level, not the application-owned tree.
    // A document owner may still move retained children into another Window.
    Value->WindowClosed = true;
    Value->NativeWindowClosedPending = false;
    Value->WindowClosePending = false;
    Value->WindowCloseRequestQueued = false;
    Value->WindowClosePrevented = false;
    Value->Visible = false;
    Runtime->LayoutDirty = true;
    ClearInvalidInput(Runtime);
    if (Value->Destroyed) return;
    FireSignal(Runtime, Value, "Changed");
    if (!Value->Destroyed) FireSignal(Runtime, Value, "Closed");
}

static const char* RequestWindowClose(LuiRuntime* Runtime, Node* Value) {
    if (Value->Destroyed || Value->DestroyingInProgress || Value->WindowClosed) return "Closed";
    if (Value->WindowCloseDispatch || Value->WindowClosePending || Value->WindowCloseRequestQueued) return "InProgress";
    if (Runtime->BackendFailed || Runtime->ShuttingDown) return "Unavailable";
    if (HasNativeBackend(Runtime->Backend) && !Runtime->Windows.Close) return "Unsupported";
    Value->WindowCloseDispatch = true;
    Value->WindowClosePrevented = false;
    bool Failed = false;
    FireSignal(Runtime, Value, "CloseRequested", nullptr, nullptr, &Failed);
    Value->WindowCloseDispatch = false;
    if (Value->Destroyed || Value->DestroyingInProgress || Value->WindowClosed) return "Closed";
    if (Failed || Runtime->BackendFailed || Runtime->ShuttingDown) return "CallbackFailed";
    if (Value->WindowClosePrevented) return "Prevented";
    Value->WindowClosePending = true;
    if (!HasNativeBackend(Runtime->Backend)) {
        CommitWindowClosed(Runtime, Value);
    } else {
        int Accepted = 0;
        {
            DepthGuard Guard(Runtime->BackendDepth);
            // A close handler may have redocked retained controls. Publish those
            // native parent changes before retiring the old top-level surface.
            FlushChanges(Runtime);
            if (!Runtime->BackendFailed) Accepted = Runtime->Windows.Close(Runtime->Windows.Context, Value->Id);
        }
        if (!Accepted && !Value->NativeWindowClosedPending && !Value->WindowClosed) {
            Value->WindowClosePending = false; return "BackendRejected";
        }
    }
    return nullptr;
}

static int NodeRequestClose(lua_State* State) {
    CheckModuleEffect(State);
    Node* Value = GetNode(State, 1);
    if (Value->ClassName != "Window") luaL_error(State, "[LUI:Window] InvalidReceiver");
    const char* Error = RequestWindowClose(GetRuntime(State), Value);
    lua_pushboolean(State, Error == nullptr);
    if (Error) lua_pushstring(State, Error); else lua_pushnil(State);
    return 2;
}

static int NodePreventClose(lua_State* State) {
    CheckModuleEffect(State);
    Node* Value = GetNode(State, 1);
    if (Value->ClassName != "Window") luaL_error(State, "[LUI:Window] InvalidReceiver");
    const bool Valid = Value->WindowCloseDispatch && !Value->Destroyed && !Value->WindowClosed;
    if (Valid) Value->WindowClosePrevented = true;
    lua_pushboolean(State, Valid);
    return 1;
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

static bool CloneTree(lua_State* State, LuiRuntime* Runtime, const Node* Source, Node* Parent) {
    auto Value = std::make_unique<Node>();
    Value->Id = Runtime->NextNodeId++;
    Value->CreatedAt = CaptureSourceLocation(State);
    Value->LastChangedAt = Value->CreatedAt;
    if (Value->LastChangedAt.Line > 0) Value->LastChangedProperty = "Clone";
    Value->ClassName = Source->ClassName;
    if (Value->ClassName == "FrameSurface") {
        if (Runtime->ShuttingDown) {
            Runtime->LastError = "[LUI:FrameSurface] Closed"; return false;
        }
        if (!FrameBackendAvailable(Runtime)) {
            Runtime->LastError = "[LUI:FrameSurface] Unsupported"; return false;
        }
        if (!RegisterFrameSurface(Runtime, Value.get())) {
            Runtime->LastError = "[LUI:FrameSurface] LimitExceeded"; return false;
        }
        Value->ScaleMode = Source->ScaleMode;
    }
    Value->Sensitive=Source->Sensitive; Value->EffectiveSensitive=Source->EffectiveSensitive;
    Value->Name = Source->Name;
    Value->Title = Source->Title;
    Value->Text = Source->Text;
    Value->Multiline = Source->Multiline; Value->Secret = Source->Secret; Value->MaxLength = Source->MaxLength;
    Value->PlaceholderText = Source->PlaceholderText;
    Value->SelectionStart = Source->SelectionStart; Value->SelectionLength = Source->SelectionLength;
    Value->Source = Source->Source;
    Value->AccessibilityLabel = Source->AccessibilityLabel;
    Value->AccessibilityDescription = Source->AccessibilityDescription;
    Value->Visible = Source->Visible;
    Value->Enabled = Source->Enabled;
    Value->Checked = Source->Checked;
    Value->Minimum = Source->Minimum;
    Value->Maximum = Source->Maximum;
    Value->Value = Source->Value;
    Value->Size = Source->Size;
    Value->Position = Source->Position;
    Value->CanvasSize = Source->CanvasSize;
    Value->CanvasPosition = Source->CanvasPosition;
    Value->ClipsDescendants = Source->ClipsDescendants;
    Value->ZIndex = Source->ZIndex;
    Value->StyleProperties = Source->StyleProperties;
    Value->StyleGeometry = Source->StyleGeometry;
    Value->CellSize = Source->CellSize;
    Value->CellPadding = Source->CellPadding;
    Value->AnchorPoint = Source->AnchorPoint;
    Value->MinSize = Source->MinSize;
    Value->MaxSize = Source->MaxSize;
    Value->HasMaxSize = Source->HasMaxSize;
    Value->LayoutOrder = Source->LayoutOrder;
    Value->Padding = Source->Padding;
    Value->PaddingTop = Source->PaddingTop;
    Value->PaddingBottom = Source->PaddingBottom;
    Value->PaddingLeft = Source->PaddingLeft;
    Value->PaddingRight = Source->PaddingRight;
    Value->FillDirection = Source->FillDirection;
    int Id = Value->Id;
    NewNodeHandle(State, Runtime, Value.get());
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiNodeMeta");
    lua_setmetatable(State, -2);
    Value->Reference = lua_ref(State, -1);
    Runtime->Nodes.emplace(Id, std::move(Value));
    Node* Copy = Runtime->Nodes.at(Id).get();
    Copy->NativeCreated = !Runtime->ProjectBootstrap;
    if (!Runtime->ProjectBootstrap && LuiSchema::IsNative(Copy->ClassName) && Runtime->Backend.Create) {
        DepthGuard Guard(Runtime->BackendDepth);
        Runtime->BackendError.clear();
        if (!Runtime->Backend.Create(Runtime->Backend.Context, Id, Copy->ClassName.c_str())) {
            FailBackend(Runtime, "Create", Id);
            DestroyNode(Runtime, Copy);
            Runtime->Nodes.erase(Id);
            lua_pop(State, 1);
            return false;
        }
    }
    if (Parent) SetParent(Runtime, Copy, Parent);
    if (LuiSchema::IsNative(Copy->ClassName)) {
        if (LuiSchema::IsA(Copy->ClassName, "TextBox")) {
            QueueProperty(Runtime, Copy, "__TextConfig", std::string(Copy->Multiline ? "1" : "0") + "," +
                (Copy->Secret ? "1" : "0") + "," + std::to_string(Copy->MaxLength) + "," + Copy->PlaceholderText);
            QueueProperty(Runtime, Copy, "__Selection", std::to_string(Copy->SelectionStart) + "," + std::to_string(Copy->SelectionLength));
        }
        if (Copy->ClassName == "Window") QueueProperty(Runtime, Copy, "Title", Copy->Title);
        if (Copy->ClassName == "FrameSurface") QueueProperty(Runtime, Copy, "ScaleMode", Copy->ScaleMode);
        if (Copy->ClassName == "ImageLabel" && !Copy->Source.empty())
            QueueProperty(Runtime, Copy, "Source", Copy->Source);
        if (Copy->ClassName == "TextLabel" || Copy->ClassName == "TextButton" ||
            LuiSchema::IsA(Copy->ClassName, "TextBox") || Copy->ClassName == "CheckBox")
            QueueProperty(Runtime, Copy, "Text", Copy->Text);
        if (!Copy->AccessibilityLabel.empty())
            QueueProperty(Runtime, Copy, "AccessibilityLabel", Copy->AccessibilityLabel);
        if (!Copy->AccessibilityDescription.empty())
            QueueProperty(Runtime, Copy, "AccessibilityDescription", Copy->AccessibilityDescription);
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
        if (!CloneTree(State, Runtime, Runtime->Nodes.at(ChildId).get(), Copy)) {
            DestroyNode(Runtime, Copy);
            lua_pop(State, 1);
            return false;
        }
        lua_pop(State, 1);
    }
    Runtime->LayoutDirty = true;
    return true;
}

static int NodeClone(lua_State* State) {
    CheckModuleEffect(State);
    Node* Value = GetNode(State, 1);
    if (!CloneTree(State, GetRuntime(State), Value, nullptr))
        luaL_error(State, "%s", GetRuntime(State)->LastError.c_str());
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
    CheckModuleEffect(State);
    if (!HasMeta(State, 1, "LuiSignalMeta")) luaL_error(State, "expected Signal");
    auto* Signal = static_cast<SignalValue*>(lua_touserdata(State, 1));
    luaL_checktype(State, 2, LUA_TFUNCTION);
    auto* Runtime = GetRuntime(State);
    auto Found = Runtime->Nodes.find(Signal->NodeId);
    if (Signal->NodeId != 0 && (Found == Runtime->Nodes.end() || Found->second->Destroyed))
        luaL_error(State, "Signal owner is destroyed");
    if (Signal->NodeId == 0 && std::string(Signal->Name) != "ThemeChanged")
        luaL_error(State, "unknown service signal");
    int Reference = lua_ref(State, 2);
    int Id = Runtime->NextListenerId++;
    Runtime->Listeners.emplace(Id, Listener{Id, Signal->NodeId, Reference, Signal->Name, true});
    if (Signal->NodeId == 0) Runtime->ThemeListeners.push_back(Id);
    else Found->second->Listeners.push_back(Id);
    *static_cast<int*>(lua_newuserdata(State, sizeof(int))) = Id;
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiConnectionMeta");
    lua_setmetatable(State, -2);
    return 1;
}

static int ConnectionDisconnect(lua_State* State) {
    CheckModuleEffect(State);
    if (!HasMeta(State, 1, "LuiConnectionMeta")) luaL_error(State, "expected Connection");
    Disconnect(GetRuntime(State), *static_cast<int*>(lua_touserdata(State, 1)));
    return 0;
}

static void PushSignal(lua_State* State, Node* Value, const char* Name) {
    *static_cast<SignalValue*>(lua_newuserdata(State, sizeof(SignalValue))) = {Value->Id, Name};
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiSignalMeta");
    lua_setmetatable(State, -2);
}

static int NodeSubmitFrame(lua_State* State) {
    CheckModuleEffect(State);
    Node* Value = GetNode(State, 1, true);
    if (Value->ClassName != "FrameSurface") luaL_error(State, "[LUI:FrameSurface] InvalidArgument");
    auto* Runtime = GetRuntime(State);
    const char* Failure = "InvalidArgument";
    if (Value->Destroyed || Value->DestroyingInProgress) Failure = "Closed";
    else if (lua_gettop(State) == 6 && lua_type(State, 2) == LUA_TBUFFER &&
        lua_type(State, 3) == LUA_TNUMBER && lua_type(State, 4) == LUA_TNUMBER &&
        lua_type(State, 5) == LUA_TNUMBER && lua_type(State, 6) == LUA_TSTRING) {
        size_t Length = 0, FormatLength = 0;
        const void* Pixels = lua_tobuffer(State, 2, &Length);
        const char* Format = lua_tolstring(State, 6, &FormatLength);
        Failure = SubmitFrameSurface(Runtime, Value, Pixels, Length, lua_tonumber(State, 3),
            lua_tonumber(State, 4), lua_tonumber(State, 5), Format, FormatLength);
    }
    if (Failure) ++Runtime->FramesRejected;
    lua_pushboolean(State, Failure == nullptr);
    if (Failure) lua_pushstring(State, Failure); else lua_pushnil(State);
    return 2;
}
static int NodeClearFrame(lua_State* State) {
    CheckModuleEffect(State);
    Node* Value = GetNode(State, 1, true);
    if (Value->ClassName != "FrameSurface" || lua_gettop(State) != 1) luaL_error(State, "[LUI:FrameSurface] InvalidArgument");
    if (!Value->Destroyed && !Value->DestroyingInProgress) ClearFrameSurface(GetRuntime(State), Value);
    return 0;
}

static int NodeFocus(lua_State* State) {
    CheckModuleEffect(State);
    Node* Value = GetNode(State, 1, true);
    if (lua_gettop(State) != 1 || !LuiSchema::IsA(Value->ClassName, "GuiObject"))
        luaL_error(State, "[LUI:Input] InvalidArgument");
    auto* Runtime = GetRuntime(State);
    // Focus eligibility and native focus must observe the same submitted tree.
    // A preceding enable, hide, reparent or layout change is still queued here.
    FlushLayout(Runtime);
    bool Accepted = false;
    if (!Runtime->BackendFailed && !Value->Destroyed && !Value->DestroyingInProgress && CanReceiveInput(Runtime, Value)) {
        if (Runtime->Backend.Create) {
            if (Runtime->Focus.RequestFocus) {
                DepthGuard Guard(Runtime->BackendDepth);
                Accepted = Runtime->Focus.RequestFocus(Runtime->Focus.Context, Value->Id) == 1;
            }
        } else Accepted = Lui_FocusChanged(Runtime, Value->Id, 1) == 1;
    }
    lua_pushboolean(State, Accepted);
    return 1;
}

static int NodeSetStyles(lua_State* State) {
    CheckModuleEffect(State);
    Node* Value = GetNode(State, 1);
    if (Value->ClassName != "CodeEditor") luaL_error(State, "[LUI:CodeEditor] Invalid instance");
    if (lua_type(State, 2) != LUA_TNUMBER) luaL_error(State, "[LUI:CodeEditor] Expected revision");
    const double Requested = lua_tonumber(State, 2);
    luaL_checktype(State, 3, LUA_TTABLE);
    if (!std::isfinite(Requested) || Requested < 1 || Requested != std::floor(Requested) || Requested > 9007199254740991.0)
        luaL_error(State, "[LUI:CodeEditor] Invalid revision");
    if (static_cast<uint64_t>(Requested) != Value->TextRevision || Value->IsComposing) {
        lua_pushboolean(State, false);
        return 1;
    }
    const size_t Count = lua_objlen(State, 3);
    if (Count > 128) luaL_error(State, "[LUI:CodeEditor] Too many style ranges");
    lua_pushnil(State);
    size_t Entries = 0;
    while (lua_next(State, 3) != 0) {
        ++Entries;
        if (lua_type(State, -2) != LUA_TNUMBER ||
            lua_tonumber(State, -2) < 1 || lua_tonumber(State, -2) > static_cast<double>(Count) ||
            lua_tonumber(State, -2) != std::floor(lua_tonumber(State, -2)))
            luaL_error(State, "[LUI:CodeEditor] Ranges must be a dense array");
        lua_pop(State, 1);
    }
    if (Entries != Count) luaL_error(State, "[LUI:CodeEditor] Ranges must be a dense array");
    int Characters = 0;
    if (!LuiText::CountCodeEditor(Value->Text, Characters)) luaL_error(State, "[LUI:CodeEditor] Invalid source");
    std::string Encoded = std::to_string(Value->TextRevision);
    int PreviousEnd = 0;
    for (size_t Index = 1; Index <= Count; ++Index) {
        lua_rawgeti(State, 3, static_cast<int>(Index));
        if (lua_type(State, -1) != LUA_TTABLE) luaL_error(State, "[LUI:CodeEditor] Expected style range");
        auto Number = [&](const char* Key, int Maximum) -> int {
            lua_pushstring(State, Key);
            lua_rawget(State, -2);
            if (lua_type(State, -1) != LUA_TNUMBER) luaL_error(State, "[LUI:CodeEditor] Invalid style %s", Key);
            const double Parsed = lua_tonumber(State, -1);
            if (!std::isfinite(Parsed) || Parsed != std::floor(Parsed) || Parsed < 0 || Parsed > Maximum)
                luaL_error(State, "[LUI:CodeEditor] Invalid style %s", Key);
            lua_pop(State, 1);
            return static_cast<int>(Parsed);
        };
        const int Start = Number("Start", Characters);
        const int End = Number("End", Characters);
        const int Color = Number("Color", 0xFFFFFF);
        if (Start < PreviousEnd || End <= Start) luaL_error(State, "[LUI:CodeEditor] Unordered style ranges");
        PreviousEnd = End;
        Encoded += ";" + std::to_string(Start) + "," + std::to_string(End) + "," + std::to_string(Color);
        lua_pop(State, 1);
    }
    // Keep the native prototype private until editable styling admission passes.
    // Valid requests remain observable as unsupported and cannot reach a backend.
    lua_pushboolean(State, false);
    return 1;
}

static int NodeIndex(lua_State* State) {
    Node* Value = GetNode(State, 1, true);
    const std::string Key = luaL_checkstring(State, 2);
    if (const LuiSchema::MethodDefinition* Method = LuiSchema::FindMethod(Value->ClassName, Key)) {
        if (Key == "Destroy") lua_pushcfunction(State, NodeDestroy, Method->Name);
        else if (Key == "Clone") lua_pushcfunction(State, NodeClone, Method->Name);
        else if (Key == "GetChildren") lua_pushcfunction(State, NodeGetChildren, Method->Name);
        else if (Key == "GetDescendants") lua_pushcfunction(State, NodeGetDescendants, Method->Name);
        else if (Key == "FindFirstChild") lua_pushcfunction(State, NodeFindFirstChild, Method->Name);
        else if (Key == "IsA") lua_pushcfunction(State, NodeIsA, Method->Name);
        else if (Key == "SubmitFrame") lua_pushcfunction(State, NodeSubmitFrame, Method->Name);
        else if (Key == "ClearFrame") lua_pushcfunction(State, NodeClearFrame, Method->Name);
        else if (Key == "Focus") lua_pushcfunction(State, NodeFocus, Method->Name);
        else if (Key == "SetStyles") lua_pushcfunction(State, NodeSetStyles, Method->Name);
        else if (Key == "RequestClose") lua_pushcfunction(State, NodeRequestClose, Method->Name);
        else if (Key == "PreventClose") lua_pushcfunction(State, NodePreventClose, Method->Name);
        else lua_pushnil(State);
        return 1;
    }
    if (const LuiSchema::SignalDefinition* Signal = LuiSchema::FindSignal(Value->ClassName, Key)) {
        PushSignal(State, Value, Signal->Name);
        return 1;
    }
    if (!LuiSchema::FindProperty(Value->ClassName, Key)) {
        lua_pushnil(State);
        return 1;
    }
    if (LuiStyle::Push(State, Value, Key)) return 1;
    if (Key == "ClassName") lua_pushstring(State, Value->ClassName.c_str());
    else if(Key=="Sensitive") lua_pushboolean(State,Value->Sensitive);
    else if(Key=="EffectiveSensitive") lua_pushboolean(State,Value->EffectiveSensitive);
    else if (Key == "Name") lua_pushstring(State, Value->Name.c_str());
    else if (Key == "Title") lua_pushstring(State, Value->Title.c_str());
    else if (Key == "Text") lua_pushstring(State, Value->Text.c_str());
    else if (Key == "Secret") lua_pushboolean(State, Value->Secret);
    else if (Key == "Multiline") lua_pushboolean(State, Value->Multiline);
    else if (Key == "MaxLength") lua_pushinteger(State, Value->MaxLength);
    else if (Key == "TextRevision") lua_pushnumber(State, static_cast<double>(Value->TextRevision));
    else if (Key == "SelectionStart") lua_pushinteger(State, Value->SelectionStart);
    else if (Key == "SelectionLength") lua_pushinteger(State, Value->SelectionLength);
    else if (Key == "IsComposing") lua_pushboolean(State, Value->IsComposing);
    else if (Key == "CompositionText") lua_pushstring(State, Value->CompositionText.c_str());
    else if (Key == "PlaceholderText") lua_pushstring(State, Value->PlaceholderText.c_str());
    else if (Key == "AccessibilityLabel") lua_pushstring(State, Value->AccessibilityLabel.c_str());
    else if (Key == "AccessibilityDescription") lua_pushstring(State, Value->AccessibilityDescription.c_str());
    else if (Key == "Source") lua_pushstring(State, Value->Source.c_str());
    else if (Key == "LoadState") lua_pushstring(State, Value->LoadState.c_str());
    else if (Key == "ScaleMode") lua_pushstring(State, Value->ScaleMode.c_str());
    else if (Key == "FrameState") lua_pushstring(State, Value->FrameState.c_str());
    else if (Key == "Visible") lua_pushboolean(State, Value->Visible);
    else if (Key == "IsClosed") lua_pushboolean(State, Value->WindowClosed);
    else if (Key == "Enabled") lua_pushboolean(State, Value->Enabled);
    else if (Key == "Checked") lua_pushboolean(State, Value->Checked);
    else if (Key == "IsFocused") lua_pushboolean(State, Value->IsFocused);
    else if (Key == "Minimum") lua_pushnumber(State, Value->Minimum);
    else if (Key == "Maximum") lua_pushnumber(State, Value->Maximum);
    else if (Key == "Value") lua_pushnumber(State, Value->Value);
    else if (Key == "Size") PushSize(State, Value->Size);
    else if (Key == "Position") PushSize(State, Value->Position);
    else if (Key == "ClipsDescendants") lua_pushboolean(State, Value->ClipsDescendants);
    else if (Key == "ZIndex") lua_pushinteger(State, Value->ZIndex);
    else if (Key == "CanvasSize") PushSize(State, Value->CanvasSize);
    else if (Key == "CanvasPosition") PushVector(State, Value->CanvasPosition);
    else if (Key == "AbsoluteCanvasSize" || Key == "AbsoluteCanvasPosition") {
        FlushLayout(GetRuntime(State));
        PushVector(State, Key == "AbsoluteCanvasSize" ? Value->AbsoluteCanvasSize : Value->AbsoluteCanvasPosition);
    }
    else if (Key == "CellSize" && Value->ClassName == "UIGridLayout") PushSize(State, Value->CellSize);
    else if (Key == "CellPadding" && Value->ClassName == "UIGridLayout") PushSize(State, Value->CellPadding);
    else if (Key == "AnchorPoint") PushVector(State, Value->AnchorPoint);
    else if (Key == "MinSize" && Value->ClassName == "UISizeConstraint") PushVector(State, Value->MinSize);
    else if (Key == "MaxSize" && Value->ClassName == "UISizeConstraint") {
        if (Value->HasMaxSize) PushVector(State, Value->MaxSize);
        else lua_pushnil(State);
    }
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
    } else lua_pushnil(State);
    return 1;
}

static int NodeNewIndex(lua_State* State) {
    CheckModuleEffect(State);
    Node* Value = GetNode(State, 1);
    SetProperty(State, Value, luaL_checkstring(State, 2), 3);
    return 0;
}

static int InitializeNode(lua_State* State) {
    Node* Value = GetNode(State, 1);
    luaL_checktype(State, 2, LUA_TTABLE);
    lua_getfield(State,2,"Sensitive"); if(!lua_isnil(State,-1)) SetProperty(State,Value,"Sensitive",-1); lua_pop(State,1);
    lua_getfield(State,2,"Parent");
    if(!lua_isnil(State,-1) && GetNode(State,-1)->EffectiveSensitive) ClassifyNode(GetRuntime(State),Value);
    lua_pop(State,1);
    const bool IsTextBox = LuiSchema::IsA(Value->ClassName, "TextBox");
    if (IsTextBox) for (const char* Property : {"Secret", "Multiline", "MaxLength", "PlaceholderText", "Text", "SelectionStart", "SelectionLength"}) {
        lua_getfield(State, 2, Property);
        if (!lua_isnil(State, -1)) SetProperty(State, Value, Property, -1);
        lua_pop(State, 1);
    }
    const bool IsRange = Value->ClassName == "Slider" || Value->ClassName == "ProgressBar";
    const bool IsSizeConstraint = Value->ClassName == "UISizeConstraint";
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
    if (IsSizeConstraint) {
        VectorValue MinSize = Value->MinSize;
        VectorValue MaxSize = Value->MaxSize;
        bool HasMaxSize = Value->HasMaxSize;
        lua_getfield(State, 2, "MinSize");
        if (!lua_isnil(State, -1) &&
            (!ReadVector(State, -1, MinSize) || MinSize.X < 0 || MinSize.Y < 0))
            luaL_error(State, "MinSize must be a nonnegative finite Vector2");
        lua_pop(State, 1);
        lua_getfield(State, 2, "MaxSize");
        if (!lua_isnil(State, -1)) {
            if (!ReadVector(State, -1, MaxSize) || MaxSize.X < 0 || MaxSize.Y < 0)
                luaL_error(State, "MaxSize must be a nonnegative finite Vector2");
            HasMaxSize = true;
        }
        lua_pop(State, 1);
        if (HasMaxSize && (MinSize.X > MaxSize.X || MinSize.Y > MaxSize.Y))
            luaL_error(State, "MinSize cannot exceed MaxSize");
        Value->MinSize = MinSize;
        Value->MaxSize = MaxSize;
        Value->HasMaxSize = HasMaxSize;
        GetRuntime(State)->LayoutDirty = true;
    }
    lua_pushnil(State);
    while (lua_next(State, 2) != 0) {
        const char* Key = luaL_checkstring(State, -2);
        const std::string Property = Key;
        if (Property != "Parent" && Property != "Sensitive" &&
            (!IsTextBox || (Property != "Secret" && Property != "Multiline" && Property != "MaxLength" && Property != "PlaceholderText" && Property != "Text" && Property != "SelectionStart" && Property != "SelectionLength")) &&
            (!IsRange || (Property != "Minimum" && Property != "Maximum" && Property != "Value")) &&
            (!IsSizeConstraint || (Property != "MinSize" && Property != "MaxSize")))
            SetProperty(State, Value, Key, -1);
        lua_pop(State, 1);
    }
    if (IsRange) {
        lua_getfield(State, 2, "Value");
        if (!lua_isnil(State, -1)) SetProperty(State, Value, "Value", -1);
        lua_pop(State, 1);
    }
    lua_getfield(State,2,"Parent"); if(!lua_isnil(State,-1)) SetProperty(State,Value,"Parent",-1); lua_pop(State,1);
    return 0;
}

static int InstanceNew(lua_State* State) {
    CheckModuleEffect(State);
    const int ArgumentCount = lua_gettop(State);
    const std::string ClassName = luaL_checkstring(State, 1);
    const LuiSchema::ClassDefinition* Definition = LuiSchema::FindClass(ClassName);
    if (!Definition || !Definition->Creatable) {
        luaL_error(State, "unknown class '%s'", ClassName.c_str());
    }
    auto* Runtime = GetRuntime(State);
    if (ClassName == "CodeEditor" && HasNativeBackend(Runtime->Backend) &&
        Runtime->BackendName != "winui3" && Runtime->BackendName != "gtk4")
        luaL_error(State, "[LUI:CodeEditor] Unsupported backend");
    if (ClassName == "ScrollingFrame" && HasNativeBackend(Runtime->Backend) && !Runtime->Presentation.Present)
        luaL_error(State, "[LUI:Viewport] Native mapping is not implemented");
    if (ClassName == "FrameSurface" && Runtime->ShuttingDown)
        luaL_error(State, "[LUI:FrameSurface] Closed");
    if (ClassName == "FrameSurface" && !FrameBackendAvailable(Runtime))
        luaL_error(State, "[LUI:FrameSurface] Unsupported");
    auto Value = std::make_unique<Node>();
    if (ClassName == "FrameSurface" && !RegisterFrameSurface(Runtime, Value.get()))
        luaL_error(State, "[LUI:FrameSurface] LimitExceeded");
    Value->Id = Runtime->NextNodeId++;
    Value->CreatedAt = CaptureSourceLocation(State);
    Value->ClassName = ClassName;
    if (ClassName == "CodeEditor") Value->MaxLength=LuiText::CodeEditorMaxCharacters;
    Value->Name = ClassName;
    Value->Visible = ClassName != "Window";
    Value->Multiline = ClassName == "CodeEditor";
    Value->ClipsDescendants = ClassName == "ScrollingFrame";
    Value->Size = ClassName == "Window" ? SizeValue{{0, 800}, {0, 600}} : SizeValue{{1, 0}, {1, 0}};
    int Id = Value->Id;
    NewNodeHandle(State, Runtime, Value.get());
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiNodeMeta");
    lua_setmetatable(State, -2);
    Value->Reference = lua_ref(State, -1);
    Runtime->Nodes.emplace(Id, std::move(Value));
    int Created = 1;
    Runtime->Nodes.at(Id)->NativeCreated = !Runtime->ProjectBootstrap;
    if (!Runtime->ProjectBootstrap && Definition->Native && Runtime->Backend.Create) {
        DepthGuard Guard(Runtime->BackendDepth);
        Runtime->BackendError.clear();
        Created = Runtime->Backend.Create(Runtime->Backend.Context, Id, ClassName.c_str());
    }
    if (!Created) {
        FailBackend(Runtime, "Create", Id);
        DestroyNode(Runtime, Runtime->Nodes.at(Id).get());
        Runtime->Nodes.erase(Id);
        luaL_error(State, "%s", Runtime->LastError.c_str());
    }
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
    CheckModuleEffect(State);
    luaL_checktype(State, 1, LUA_TFUNCTION);
    auto* Runtime = GetRuntime(State);
    if (Runtime->TaskLimit && Runtime->Tasks.size() + Runtime->TaskRound.size() - Runtime->TaskRoundIndex >= Runtime->TaskLimit)
        luaL_error(State, "[LUI:Scheduler] PendingLimit");
    lua_State* Thread = lua_newthread(State);
    if (Runtime->Sandboxed) luaL_sandboxthread(Thread);
    int Reference = lua_ref(State, -1);
    lua_pop(State, 1);
    lua_pushvalue(State, 1);
    lua_xmove(State, Thread, 1);
    Runtime->Tasks.push_back({Reference, std::chrono::steady_clock::now()});
    return 0;
}

static int TaskDelay(lua_State* State) {
    CheckModuleEffect(State);
    double Seconds = luaL_checknumber(State, 1);
    luaL_checktype(State, 2, LUA_TFUNCTION);
    if (!std::isfinite(Seconds) || Seconds < 0) luaL_error(State, "delay must be finite and nonnegative");
    auto* Runtime = GetRuntime(State);
    if (Runtime->TaskLimit && Runtime->Tasks.size() + Runtime->TaskRound.size() - Runtime->TaskRoundIndex >= Runtime->TaskLimit)
        luaL_error(State, "[LUI:Scheduler] PendingLimit");
    if (Runtime->TaskLimit && Seconds > 31536000)
        luaL_error(State, "[LUI:Scheduler] DelayLimit");
    lua_State* Thread = lua_newthread(State);
    if (Runtime->Sandboxed) luaL_sandboxthread(Thread);
    int Reference = lua_ref(State, -1);
    lua_pop(State, 1);
    lua_pushvalue(State, 2);
    lua_xmove(State, Thread, 1);
    Runtime->Tasks.push_back({Reference, std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int64_t>(Seconds * 1000))});
    return 0;
}

static void CollectRetired(LuiRuntime* Runtime) {
    if (Runtime->VmDepth || Runtime->BackendDepth || Runtime->DrainingBackendEvents || Runtime->UiCompletionRunning || Runtime->ShuttingDown) return;
    for (auto Iterator = Runtime->Nodes.begin(); Iterator != Runtime->Nodes.end();) {
        if (Iterator->second->Destroyed && !Iterator->second->DestroyingInProgress) {
            if (Iterator->second->Handle) {
                auto* Handle = Iterator->second->Handle;
                Handle->Retired = Iterator->second.release(); ++Runtime->RetiredUserdata;
            }
            Iterator = Runtime->Nodes.erase(Iterator);
        }
        else ++Iterator;
    }
    for (auto Iterator = Runtime->Listeners.begin(); Iterator != Runtime->Listeners.end();) {
        if (!Iterator->second.Active) Iterator = Runtime->Listeners.erase(Iterator); else ++Iterator;
    }
    for (auto& Pair : Runtime->Nodes) {
        auto& Listeners = Pair.second->Listeners;
        Listeners.erase(std::remove_if(Listeners.begin(), Listeners.end(), [&](int Id) {return Runtime->Listeners.find(Id) == Runtime->Listeners.end();}), Listeners.end());
    }
    auto& ThemeListeners = Runtime->ThemeListeners;
    ThemeListeners.erase(std::remove_if(ThemeListeners.begin(), ThemeListeners.end(), [&](int Id) {return Runtime->Listeners.find(Id) == Runtime->Listeners.end();}), ThemeListeners.end());
}

void FlushLayout(LuiRuntime* Runtime) {
    if (Runtime->BackendFailed || Runtime->ProjectBootstrap || Runtime->ModuleDepth) return;
    bool Arranged = false;
    {
        DepthGuard Guard(Runtime->BackendDepth);
        DepthGuard LayoutGuard(Runtime->LayoutDepth);
        FlushChanges(Runtime);
        if (!Runtime->BackendFailed && Runtime->LayoutDirty) {
            Runtime->LayoutDirty = false;
            Arranged = true;
            const auto Started = std::chrono::steady_clock::now();
            for (auto& Pair : Runtime->Nodes) {
                Node* Value = Pair.second.get();
                if (!Value->Destroyed && !Value->WindowClosed && Value->ClassName == "Window") {
                    const VectorValue Resolved = Value->HasViewportSize
                        ? Value->ViewportSize : LuiLayout::ResolveConstrainedSize(Runtime, Value, 0, 0);
                    LuiLayout::ArrangeNode(Runtime, Value, {0, 0, Resolved.X, Resolved.Y});
                    Value->PresentationRevision = Runtime->LayoutPasses + 1;
                    if (Runtime->Presentation.Present) {
                        struct PresentationScope {
                            int& WindowId;
                            explicit PresentationScope(int& Target, int Id) : WindowId(Target) {WindowId = Id;}
                            ~PresentationScope() {WindowId = 0;}
                        } Scope(Runtime->PresentationWindowId, Value->Id);
                        LuiLayout::PresentTree(Runtime, Value);
                    }
                }
            }
            ++Runtime->LayoutPasses;
            Runtime->LayoutSamples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count());
            if (Runtime->LayoutSamples.size() > 256) Runtime->LayoutSamples.pop_front();
        }
    }
    if (Arranged && !Runtime->BackendFailed) ClearInvalidInput(Runtime);
    auto CanvasSignals = std::move(Runtime->CanvasSignals);
    Runtime->CanvasSignals.clear();
    for (int Id : CanvasSignals) {
        auto Found = Runtime->Nodes.find(Id);
        if (Found != Runtime->Nodes.end() && !Found->second->Destroyed && !Runtime->BackendFailed)
            FireSignal(Runtime, Found->second.get(), "CanvasChanged");
    }
    std::vector<int> ImageSignals;
    for (auto& Pair : Runtime->Nodes) if (!Pair.second->Destroyed && Pair.second->ImageStatePending) {
        Pair.second->ImageStatePending = false; ImageSignals.push_back(Pair.first);
    }
    std::sort(ImageSignals.begin(), ImageSignals.end());
    for (int Id : ImageSignals) {
        const auto Found = Runtime->Nodes.find(Id);
        if (Found != Runtime->Nodes.end() && !Found->second->Destroyed && !Runtime->BackendFailed) FireSignal(Runtime, Found->second.get(), "LoadChanged");
    }
    if (!Runtime->BackendFailed) DrainBackendEvents(Runtime);
    CollectRetired(Runtime);
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
        if (!Pair.second->Destroyed && !Pair.second->WindowClosed && Pair.second->ClassName == "Window") WindowIds.push_back(Pair.first);
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
    const char* Capability = luaL_checkstring(State, 2);
    auto* Runtime = GetRuntime(State);
    const std::string Name = Capability;
    lua_pushboolean(State, (Name == "NativeExtensions" &&
        (Runtime->GrantedCapabilities & LUI_CAPABILITY_NATIVE_EXTENSIONS) != 0) ||
        (Name == "HostServices" &&
        (!Runtime->Sandboxed || (Runtime->GrantedCapabilities & LUI_CAPABILITY_HOST_SERVICES) != 0)) ||
        (Name == "Clipboard" && PlatformServiceAvailable(Runtime, "ClipboardService")) ||
        (Name == "Dialogs" && PlatformServiceAvailable(Runtime, "DialogService")) ||
        (Name == "Storage" && HasNativeService(Runtime, "StorageService")) ||
        (Name == "Notifications" && HasNativeService(Runtime, "NotificationService")) ||
        (Name == "Tray" && HasNativeService(Runtime, "TrayService")) ||
        (Name == "FrameSurface" && Runtime->Frames.Submit != nullptr) ||
        (Name == "WindowLifecycle" && (!HasNativeBackend(Runtime->Backend) || Runtime->Windows.Close != nullptr)) ||
        (Name == "network.client" && (Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_CLIENT) != 0) ||
        (Name == "network.server" && (Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_SERVER) != 0) ||
        (Name == "network.raw" && (Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_RAW) != 0));
    return 1;
}

static int ThemeServiceIndex(lua_State* State) {
    const std::string Name = luaL_checkstring(State, 2);
    auto* Runtime = GetRuntime(State);
    if (Name == "CurrentTheme") lua_pushstring(State, Runtime->CurrentTheme.c_str());
    else if (Name == "ThemeChanged") {
        *static_cast<SignalValue*>(lua_newuserdata(State, sizeof(SignalValue))) = {0, "ThemeChanged"};
        lua_getfield(State, LUA_REGISTRYINDEX, "LuiSignalMeta");
        lua_setmetatable(State, -2);
    } else lua_pushnil(State);
    return 1;
}

static int AssetServiceHas(lua_State* State) {
    const char* Name = luaL_checkstring(State, 2);
    auto* Runtime = GetRuntime(State);
    lua_pushboolean(State, Runtime->Assets.find(Name) != Runtime->Assets.end());
    return 1;
}

static int PerformanceGetStats(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    size_t Live = 0, Connections = 0;
    for (const auto& Pair : Runtime->Nodes) if (!Pair.second->Destroyed) ++Live;
    for (const auto& Pair : Runtime->Listeners) if (Pair.second.Active) ++Connections;
    std::vector<double> Samples(Runtime->LayoutSamples.begin(), Runtime->LayoutSamples.end());
    std::sort(Samples.begin(), Samples.end());
    lua_createtable(State, 0, 12);
    auto Number = [&](const char* Name, double Value) {lua_pushnumber(State, Value); lua_setfield(State, -2, Name);};
    Number("LiveInstances", static_cast<double>(Live)); Number("RetainedInstances", static_cast<double>(Runtime->Nodes.size() + Runtime->RetiredUserdata));
    Number("Connections", static_cast<double>(Connections)); Number("LuaMemoryBytes", static_cast<double>(Runtime->VmBytes));
    Number("QueuedEvents", static_cast<double>(Runtime->PendingBackendEvents.size())); Number("QueuedBytes", static_cast<double>(Runtime->PendingBackendBytes));
    Number("LayoutPasses", static_cast<double>(Runtime->LayoutPasses)); Number("PresentationCalls", static_cast<double>(Runtime->PresentationCalls));
    Number("LayoutSamples", static_cast<double>(Samples.size()));
    Number("LayoutP95Ms", Samples.empty() ? 0 : Samples[static_cast<size_t>(std::ceil(Samples.size()*0.95))-1]);
    Number("LastLayoutMs", Runtime->LayoutSamples.empty() ? 0 : Runtime->LayoutSamples.back());
    auto Distribution = [&](const char* CountName, const char* P95Name, const char* P99Name, const std::deque<double>& Input) {
        std::vector<double> Sorted(Input.begin(), Input.end()); std::sort(Sorted.begin(), Sorted.end());
        Number(CountName, static_cast<double>(Sorted.size()));
        Number(P95Name, Sorted.empty()?0:Sorted[static_cast<size_t>(std::ceil(Sorted.size()*0.95))-1]);
        Number(P99Name, Sorted.empty()?0:Sorted[static_cast<size_t>(std::ceil(Sorted.size()*0.99))-1]);
    };
    Distribution("InputSamples", "InputP95Ms", "InputP99Ms", Runtime->InputSamples);
    Distribution("RenderSamples", "RenderP95Ms", "RenderP99Ms", Runtime->RenderSamples);
    lua_pushboolean(State, Runtime->BackendName == "headless" || Runtime->BackendName == "preview"); lua_setfield(State, -2, "IsHeadless");
    lua_setreadonly(State, -1, true); return 1;
}
static int PerformanceTime(lua_State* State) {
    lua_pushnumber(State, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count()); return 1;
}

static int ThemeServiceNewIndex(lua_State* State) {
    luaL_error(State, "ThemeService is read-only");
    return 0;
}

static void DeliverTheme(LuiRuntime* Runtime) {
    if (Runtime->PendingTheme.empty() || Runtime->CurrentTheme == Runtime->PendingTheme) {
        Runtime->PendingTheme.clear();
        return;
    }
    Runtime->CurrentTheme.swap(Runtime->PendingTheme);
    Runtime->LayoutDirty = true;
    Runtime->PendingTheme.clear();
    const std::vector<int> Snapshot = Runtime->ThemeListeners;
    for (int Id : Snapshot) {
        auto Found = Runtime->Listeners.find(Id);
        if (Found == Runtime->Listeners.end() || !Found->second.Active) continue;
        lua_getref(Runtime->State, Found->second.Reference);
        lua_pushstring(Runtime->State, Runtime->CurrentTheme.c_str());
        DepthGuard Guard(Runtime->VmDepth, Runtime);
        if (lua_pcall(Runtime->State, 1, 0, 0) != LUA_OK) {
            const char* Message = lua_tostring(Runtime->State, -1);
            Runtime->LastError = Message ? Message : "theme callback failed";
            EmitLog(Runtime, "Error", "Theme: " + Runtime->LastError);
            lua_pop(Runtime->State, 1);
        }
    }
}

static int AppGetService(lua_State* State) {
    CheckModuleEffect(State);
    const std::string Name = luaL_checkstring(State, 2);
    auto* Runtime = GetRuntime(State);
    if (!LuiSchema::FindService(Name) && !HasExtensionService(Runtime, Name) && !HasNativeService(Runtime, Name))
        luaL_error(State, "unknown service '%s'", Name.c_str());
    if (((Runtime->Sandboxed && HasExtensionService(Runtime, Name)) || NativeServiceNeedsHostGrant(Runtime, Name)) &&
        !(Runtime->GrantedCapabilities & LUI_CAPABILITY_HOST_SERVICES))
        luaL_error(State, "host service capability was not granted");
    if ((Name == "StorageService" || Name == "NotificationService" || Name == "TrayService") && !HasNativeService(Runtime, Name))
        luaL_error(State, "[LUI:NativeService] Unsupported");
    if ((Name == "ClipboardService" || Name == "DialogService") &&
        !PlatformServiceAvailable(Runtime, Name))
        luaL_error(State, "%s capability was not granted or backend is unavailable", Name.c_str());
    if (Name == "NetworkService" &&
        !(Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_RAW))
        luaL_error(State, "network.raw capability was not granted");
    if (Name == "HttpService" && !(Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_CLIENT))
        luaL_error(State, "network.client capability was not granted");
    if (Name == "HttpServerService" && !(Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_SERVER))
        luaL_error(State, "network.server capability was not granted");
    auto Found = Runtime->ServiceRefs.find(Name);
    if (Found != Runtime->ServiceRefs.end()) {
        lua_getref(State, Found->second);
        return 1;
    }
    if (HasNativeService(Runtime, Name)) PushNativeService(State, Runtime, Name);
    else if (HasExtensionService(Runtime, Name)) PushExtensionService(State, Runtime, Name);
    else if (Name == "ClipboardService" || Name == "DialogService")
        PushPlatformService(State, Runtime, Name);
    else if (Name == "NetworkService") PushNetworkService(State);
    else if (Name == "HttpService") PushHttpService(State);
    else if (Name == "HttpServerService") PushHttpServerService(State);
    else if (Name == "ThemeService") {
        lua_newuserdata(State, 1);
        lua_getfield(State, LUA_REGISTRYINDEX, "LuiThemeServiceMeta");
        lua_setmetatable(State, -2);
    } else lua_newtable(State);
    if (Name == "WindowService") {
        lua_pushcfunction(State, WindowServiceGetWindows, "WindowService.GetWindows");
        lua_setfield(State, -2, "GetWindows");
    } else if (Name == "PerformanceService") {
        lua_pushcfunction(State, PerformanceGetStats, "PerformanceService.GetStats"); lua_setfield(State, -2, "GetStats");
        lua_pushcfunction(State, PerformanceTime, "PerformanceService.GetTime"); lua_setfield(State, -2, "GetTime");
    } else if (Name == "AssetService") {
        lua_pushcfunction(State, AssetServiceHas, "AssetService.Has");
        lua_setfield(State, -2, "Has");
    } else if (Name == "PlatformService") {
        lua_pushcfunction(State, PlatformServiceSupports, "PlatformService.Supports");
        lua_setfield(State, -2, "Supports");
        lua_pushstring(State, Runtime->BackendName.c_str());
        lua_setfield(State, -2, "BackendName");
    }
    if (Name != "ThemeService") lua_setreadonly(State, -1, true);
    Runtime->ServiceRefs.emplace(Name, lua_ref(State, -1));
    return 1;
}

static void RegisterGlobals(lua_State* State) {
    lua_setuserdatadtor(State, NodeUserdataTag, DestroyNodeHandle);
    RegisterModules(State);
    lua_pushcfunction(State, LuiPrint, "print");
    lua_setglobal(State, "print");
    RegisterMeta(State, "LuiNodeMeta", NodeIndex, NodeNewIndex);
    RegisterMeta(State, "LuiSignalMeta", SignalIndex);
    RegisterMeta(State, "LuiConnectionMeta", ConnectionIndex);
    RegisterMeta(State, "LuiThemeServiceMeta", ThemeServiceIndex, ThemeServiceNewIndex);
    RegisterNetworkTypes(State);
    RegisterNativeTypes(State);
    LuiStyle::Register(State);
    lua_newtable(State);
    lua_pushcfunction(State, InstanceNew, "Instance.new");
    lua_setfield(State, -2, "new");
    lua_setreadonly(State, -1, true);
    lua_setglobal(State, "Instance");
    lua_newtable(State);
    lua_pushcfunction(State, AppGetService, "app.GetService");
    lua_setfield(State, -2, "GetService");
    lua_setreadonly(State, -1, true);
    lua_setglobal(State, "app");
    lua_newtable(State);
    lua_pushcfunction(State, Vector2New, "Vector2.new");
    lua_setfield(State, -2, "new");
    lua_setreadonly(State, -1, true);
    lua_setglobal(State, "Vector2");
    lua_newtable(State);
    lua_pushcfunction(State, UDimNew, "UDim.new");
    lua_setfield(State, -2, "new");
    lua_setreadonly(State, -1, true);
    lua_setglobal(State, "UDim");
    lua_newtable(State);
    lua_pushcfunction(State, UDim2New, "UDim2.new");
    lua_setfield(State, -2, "new");
    lua_pushcfunction(State, UDim2FromOffset, "UDim2.fromOffset");
    lua_setfield(State, -2, "fromOffset");
    lua_pushcfunction(State, UDim2FromScale, "UDim2.fromScale");
    lua_setfield(State, -2, "fromScale");
    lua_setreadonly(State, -1, true);
    lua_setglobal(State, "UDim2");
    lua_newtable(State);
    lua_pushcfunction(State, TaskSchedule, "task.defer");
    lua_setfield(State, -2, "defer");
    lua_pushcfunction(State, TaskSchedule, "task.spawn");
    lua_setfield(State, -2, "spawn");
    lua_pushcfunction(State, TaskDelay, "task.delay");
    lua_setfield(State, -2, "delay");
    lua_setreadonly(State, -1, true);
    lua_setglobal(State, "task");
}

bool CheckOwner(LuiRuntime* Runtime) {
    if (!Runtime) return false;
    if (Runtime->Owner == std::this_thread::get_id()) return true;
    Runtime->LastError = "[LUI:Scheduler] Runtime called from the wrong thread";
    return false;
}

static LuiRuntime* CreateRuntime(uint32_t Mode) {
    auto* Runtime = new LuiRuntime();
    Runtime->DiagnosticMode=Mode; Runtime->LastError.Owner=Runtime; Runtime->ExtensionError.Owner=Runtime;
    Runtime->Owner = std::this_thread::get_id();
    Runtime->NativeDomain = Lui::Native::GetDomain();
    Runtime->State = lua_newstate(VmAllocate, Runtime);
    if (!Runtime->State) { delete Runtime; return nullptr; }
    try {
    luaL_openlibs(Runtime->State);
    lua_callbacks(Runtime->State)->userdata = Runtime;
    lua_callbacks(Runtime->State)->interrupt = VmInterrupt;
    lua_pushlightuserdata(Runtime->State, Runtime);
    lua_setfield(Runtime->State, LUA_REGISTRYINDEX, "LuiRuntime");
    RegisterGlobals(Runtime->State);
    return Runtime;
    } catch(...) {lua_close(Runtime->State); delete Runtime; throw;}
}
extern "C" LUI_API LuiRuntime* LUI_CALL Lui_Create(void) {return CreateRuntime(0);}
extern "C" LUI_API LuiRuntime* LUI_CALL Lui_CreateWithPolicy(const LuiDiagnosticPolicyV1* Policy) {
    if(!Policy || Policy->StructSize!=sizeof(*Policy) || Policy->AbiVersion!=LUI_DIAGNOSTICS_ABI_VERSION || Policy->Reserved ||
        (Policy->Mode!=LUI_DIAGNOSTICS_DEVELOPMENT && Policy->Mode!=LUI_DIAGNOSTICS_RESTRICTED)) return nullptr;
    try {return CreateRuntime(Policy->Mode);} catch(...) {return nullptr;}
}

extern "C" LUI_API void LUI_CALL Lui_SetBackend(LuiRuntime* Runtime, LuiBackendCallbacks Callbacks) {
    if (!CheckOwner(Runtime)) return;
    if (HasNativeBackend(Callbacks) && !Runtime->Frames.Submit) {
        for (const auto& Pair : Runtime->Nodes) if (!Pair.second->Destroyed && Pair.second->FrameSlot) {
            Runtime->LastError = "[LUI:FrameSurface] Unsupported"; return;
        }
    }
    if (HasNativeBackend(Callbacks) && !Runtime->Presentation.Present) {
        for (const auto& Pair : Runtime->Nodes) {
            if (!Pair.second->Destroyed && UsesViewportFeatures(Pair.second.get())) {
                Runtime->LastError = "[LUI:Viewport] Native mapping is not implemented";
                return;
            }
        }
    }
    Runtime->Backend = Callbacks;
}

extern "C" LUI_API int LUI_CALL Lui_SetPresentationCallbacks(LuiRuntime* Runtime, const LuiPresentationCallbacksV1* Callbacks) {
    if (!CheckOwner(Runtime) || !Callbacks || Callbacks->StructSize != sizeof(*Callbacks) ||
        Callbacks->AbiVersion != 1 || !Callbacks->Present || Runtime->Presentation.Present ||
        Runtime->NextNodeId != 1 || Runtime->VmDepth || Runtime->BackendDepth || Runtime->ApplicationStarted ||
        Runtime->ShuttingDown) return 0;
    Runtime->Presentation = *Callbacks;
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_SetFocusCallbacks(LuiRuntime* Runtime, const LuiFocusCallbacksV1* Callbacks) {
    if (!CheckOwner(Runtime) || !Callbacks || Callbacks->StructSize != sizeof(*Callbacks) ||
        Callbacks->AbiVersion != 1 || !Callbacks->RequestFocus || Callbacks->Reserved ||
        Runtime->Focus.RequestFocus || Runtime->NextNodeId != 1 || Runtime->VmDepth ||
        Runtime->BackendDepth || Runtime->ApplicationStarted || Runtime->ShuttingDown) return 0;
    Runtime->Focus = *Callbacks;
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_SetWindowCallbacks(LuiRuntime* Runtime, const LuiWindowCallbacksV1* Callbacks) {
    if (!CheckOwner(Runtime) || !Callbacks || Callbacks->StructSize != sizeof(*Callbacks) ||
        Callbacks->AbiVersion != 1 || !Callbacks->Close || Callbacks->Reserved || Runtime->Windows.Close ||
        Runtime->NextNodeId != 1 || Runtime->VmDepth || Runtime->BackendDepth ||
        Runtime->ApplicationStarted || Runtime->ShuttingDown) return 0;
    Runtime->Windows = *Callbacks;
    return 1;
}

extern "C" LUI_API void LUI_CALL Lui_ReportBackendError(LuiRuntime* Runtime, const char* Message) {
    if (CheckOwner(Runtime) && Message) Runtime->BackendError = Message;
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
extern "C" LUI_API int LUI_CALL Lui_RecordPerformance(LuiRuntime* Runtime, int Kind, double Milliseconds) {
    if (!CheckOwner(Runtime) || Runtime->ShuttingDown || (Kind != 1 && Kind != 2) ||
        !std::isfinite(Milliseconds) || Milliseconds < 0 || Milliseconds > 60000) return 0;
    auto& Samples = Kind == 1 ? Runtime->InputSamples : Runtime->RenderSamples;
    Samples.push_back(Milliseconds); if (Samples.size() > 256) Samples.pop_front(); return 1;
}
extern "C" LUI_API const char* LUI_CALL Lui_GetPerformanceJson(LuiRuntime* Runtime) {
    if (!CheckOwner(Runtime)) return nullptr;
    try {
        nlohmann::json Output;
        size_t Live = 0, Connections = 0;
        for (const auto& Pair : Runtime->Nodes) if (!Pair.second->Destroyed) ++Live;
        for (const auto& Pair : Runtime->Listeners) if (Pair.second.Active) ++Connections;
        Output["LiveInstances"] = Live; Output["RetainedInstances"] = Runtime->Nodes.size() + Runtime->RetiredUserdata;
        Output["Connections"] = Connections; Output["LuaMemoryBytes"] = Runtime->VmBytes;
        Output["QueuedEvents"] = Runtime->PendingBackendEvents.size(); Output["QueuedBytes"] = Runtime->PendingBackendBytes;
        Output["PendingImages"] = Runtime->PendingImages; Output["LayoutPasses"] = Runtime->LayoutPasses;
        auto Distribution = [&](const char* Name,const std::deque<double>& Input) {
            std::vector<double> Values(Input.begin(),Input.end()); std::sort(Values.begin(),Values.end());
            auto Percentile = [&](double Fraction) {return Values.empty()?0:Values[static_cast<size_t>(std::ceil(Values.size()*Fraction))-1];};
            Output[Name] = {{"Samples",Values.size()},{"MedianMs",Percentile(0.5)},{"P95Ms",Percentile(0.95)},{"P99Ms",Percentile(0.99)},{"MaxMs",Values.empty()?0:Values.back()}};
        };
        Distribution("Layout",Runtime->LayoutSamples); Distribution("Input",Runtime->InputSamples); Distribution("Render",Runtime->RenderSamples);
        Runtime->PerformanceJson = Output.dump(); return Runtime->PerformanceJson.c_str();
    } catch (...) {return nullptr;}
}

static const Node* PresentationOwner(LuiRuntime* Runtime, int WindowId) {
    if (!CheckOwner(Runtime) || Runtime->ShuttingDown || Runtime->BackendFailed || Runtime->LayoutDirty ||
        !Runtime->PendingChanges.empty() ||
        ((Runtime->DrainingBackendEvents || Runtime->UiCompletionRunning) && Runtime->PresentationWindowId != WindowId) ||
        Runtime->ProjectBootstrap || Runtime->ModuleDepth ||
        (Runtime->LayoutDepth && Runtime->PresentationWindowId != WindowId) ||
        (Runtime->VmDepth && Runtime->PresentationWindowId != WindowId)) return nullptr;
    const auto Found = Runtime->Nodes.find(WindowId);
    if (Found == Runtime->Nodes.end() || Found->second->Destroyed || Found->second->WindowClosed ||
        Found->second->WindowClosePending || Found->second->NativeWindowClosedPending ||
        Found->second->ClassName != "Window" || !Found->second->PresentationRevision) return nullptr;
    return Found->second.get();
}

extern "C" LUI_API int LUI_CALL Lui_GetWindowPresentation(LuiRuntime* Runtime, int WindowId,
    LuiWindowPresentationV1* Snapshot) {
    if (!Snapshot || Snapshot->StructSize != sizeof(*Snapshot) ||
        Snapshot->AbiVersion != LUI_WINDOW_PRESENTATION_ABI_VERSION) return 0;
    Snapshot->Count = 0; Snapshot->Revision = 0;
    if (!Snapshot->Entries || !Snapshot->Capacity || Snapshot->Capacity > LUI_WINDOW_PRESENTATION_MAX_ENTRIES) return 0;
    const Node* Window = PresentationOwner(Runtime, WindowId);
    if (!Window) return 0;
    try {
        const int Result = LuiLayout::GetWindowPresentation(Runtime, Window, Snapshot);
        if (Result == 1) Snapshot->Revision = Window->PresentationRevision;
        return Result;
    }
    catch (...) {return 0;}
}

extern "C" LUI_API int LUI_CALL Lui_IsWindowPresentationCurrent(LuiRuntime* Runtime, int WindowId, uint64_t Revision) {
    const Node* Window = PresentationOwner(Runtime, WindowId);
    return Window && Revision && Window->PresentationRevision == Revision ? 1 : 0;
}

extern "C" LUI_API int LUI_CALL Lui_ConfigureSandbox(LuiRuntime* Runtime, const LuiSandboxLimitsV1* Limits) {
    if (!CheckOwner(Runtime)) return 0;
    if (!Limits || Limits->StructSize < sizeof(LuiSandboxLimitsV1) ||
        Limits->AbiVersion != LUI_EXTENSION_ABI_VERSION ||
        Limits->MaxMemoryBytes < 1024 * 1024 || !Limits->MaxInterrupts ||
        Limits->MaxMemoryBytes < Runtime->VmBytes || Runtime->Sandboxed ||
        Runtime->ApplicationStarted || !Runtime->Extensions.empty() ||
        (Runtime->GrantedCapabilities & LUI_CAPABILITY_NATIVE_EXTENSIONS)) {
        Runtime->LastError = "[LUI:Sandbox] invalid or late sandbox configuration";
        return 0;
    }
    luaL_sandbox(Runtime->State);
    luaL_sandboxthread(Runtime->State);
    if (Runtime->VmBytes > Limits->MaxMemoryBytes) {
        Runtime->LastError = "[LUI:Sandbox] memory limit is below VM startup size";
        return 0;
    }
    Runtime->VmLimitBytes = Limits->MaxMemoryBytes;
    Runtime->MaxInterrupts = Limits->MaxInterrupts;
    Runtime->Sandboxed = true;
    Runtime->LastError.clear();
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_SetNetworkPolicy(LuiRuntime* Runtime, const LuiNetworkPolicyV1* Policy) {
    if (!CheckOwner(Runtime)) return 0;
    auto ValidRange = [](uint32_t Minimum, uint32_t Maximum) {
        return (Minimum == 0 && Maximum == 0) ||
            (Minimum >= 1 && Minimum <= Maximum && Maximum <= 65535);
    };
    if (!Policy || Policy->StructSize < sizeof(LuiNetworkPolicyV1) ||
        Policy->AbiVersion != LUI_EXTENSION_ABI_VERSION ||
        (Policy->Flags & ~(LUI_NETWORK_POLICY_CLIENT_LOOPBACK_ONLY |
            LUI_NETWORK_POLICY_SERVER_LOOPBACK_ONLY)) ||
        !ValidRange(Policy->ClientPortMin, Policy->ClientPortMax) ||
        !ValidRange(Policy->ServerPortMin, Policy->ServerPortMax) ||
        Runtime->NetworkPolicyDeclared || Runtime->ApplicationStarted || Runtime->Network) {
        Runtime->LastError = "[LUI:Network] invalid or late network policy";
        return 0;
    }
    Runtime->NetworkPolicyFlags = Policy->Flags;
    Runtime->ClientPortMin = Policy->ClientPortMin;
    Runtime->ClientPortMax = Policy->ClientPortMax;
    Runtime->ServerPortMin = Policy->ServerPortMin;
    Runtime->ServerPortMax = Policy->ServerPortMax;
    Runtime->NetworkPolicyDeclared = true;
    Runtime->LastError.clear();
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_SetTlsOptions(LuiRuntime* Runtime, const LuiTlsOptionsV1* Options) {
    if (!CheckOwner(Runtime)) return 0;
    return ConfigureNetworkTls(Runtime, Options);
}

extern "C" LUI_API int LUI_CALL Lui_EnableSourceProvenance(LuiRuntime* Runtime) {
    if (!CheckOwner(Runtime)) return 0;
    if (Runtime->ApplicationStarted) {
        Runtime->LastError = "[LUI:Preview] Source provenance must be enabled before scripts";
        return 0;
    }
    Runtime->SourceProvenanceEnabled = true;
    Runtime->LastError.clear();
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_RegisterAsset(LuiRuntime* Runtime, const char* Name) {
    if (!CheckOwner(Runtime)) return 0;
    if (Runtime->ApplicationStarted || !Name || !*Name || std::strlen(Name) > 128 ||
        Runtime->Assets.size() >= 4096) {
        Runtime->LastError = "[LUI:Asset] invalid or late asset registration";
        return 0;
    }
    for (const char* Cursor = Name; *Cursor; ++Cursor) {
        if (!((*Cursor >= 'a' && *Cursor <= 'z') || (*Cursor >= 'A' && *Cursor <= 'Z') ||
            (*Cursor >= '0' && *Cursor <= '9') || *Cursor == '.' || *Cursor == '_' || *Cursor == '-')) {
            Runtime->LastError = "[LUI:Asset] asset name must be a filename";
            return 0;
        }
    }
    if (std::string(Name) == "." || std::string(Name) == ".." || !Runtime->Assets.insert(Name).second) {
        Runtime->LastError = "[LUI:Asset] asset name is duplicated or reserved";
        return 0;
    }
    Runtime->LastError.clear();
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_CheckScript(LuiRuntime* Runtime, const char* Source) {
    if (!CheckOwner(Runtime) || !Source) return 0;
    try {
        const std::string Bytecode = Luau::compile(Source);
        if (Bytecode.empty() || Bytecode[0] == '\0') {
            Runtime->LastError = Bytecode.size() > 1 ? Bytecode.substr(1) : "Luau compilation failed";
            return 0;
        }
        Runtime->LastError.clear();
        return 1;
    } catch (const std::exception& Error) {
        Runtime->LastError = Error.what();
        return 0;
    }
}

extern "C" LUI_API int LUI_CALL Lui_RunScript(LuiRuntime* Runtime, const char* Source, const char* ChunkName) {
    if (!CheckOwner(Runtime) || !Source) return 0;
    if (Runtime->BackendFailed) return 0;
    if (Runtime->Modules) {
        Runtime->LastError = "[LUI:Modules] ProjectAlreadyStarted";
        return 0;
    }
    if (Runtime->Sandboxed && std::strlen(Source) > Runtime->VmLimitBytes) {
        Runtime->LastError = "[LUI:Sandbox] script source exceeds memory limit";
        EmitLog(Runtime, "Error", Runtime->LastError);
        return 0;
    }
    if (Runtime->VmDepth || Runtime->BackendDepth || Runtime->DrainingBackendEvents || Runtime->UiCompletionRunning) {
        Runtime->LastError = "[LUI:Scheduler] Cannot run a script during an active dispatch";
        EmitLog(Runtime, "Error", Runtime->LastError);
        return 0;
    }
    Runtime->ApplicationStarted = true;
    try {
        const std::string Bytecode = Luau::compile(Source);
        int Status = luau_load(Runtime->State, ChunkName ? ChunkName : "LUI", Bytecode.data(), Bytecode.size(), 0);
        if (Status == LUA_OK) {
            DepthGuard Guard(Runtime->VmDepth, Runtime);
            Status = lua_pcall(Runtime->State, 0, 0, 0);
        }
        if (Runtime->BackendFailed) {
            if (Status != LUA_OK) lua_pop(Runtime->State, 1);
            return 0;
        }
        if (Status != LUA_OK) {
            const char* Message = lua_tostring(Runtime->State, -1);
            Runtime->LastError = Message ? Message : "Luau script failed";
            EmitLog(Runtime, "Error", "Runtime: " + Runtime->LastError);
            lua_pop(Runtime->State, 1);
            FlushLayout(Runtime);
            return 0;
        }
        FlushLayout(Runtime);
        if (Runtime->BackendFailed) return 0;
        Runtime->LastError.clear();
        return 1;
    } catch (const std::exception& Error) {
        Runtime->LastError = Error.what();
        EmitLog(Runtime, "Error", "Runtime: " + Runtime->LastError);
        FlushLayout(Runtime);
        return 0;
    }
}

extern "C" LUI_API int LUI_CALL Lui_Activate(LuiRuntime* Runtime, int Id) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed) return 0;
    const int Deferred = QueueBackendEventIfBusy(Runtime, {BackendEvent::Kind::Activate, Id});
    if (Deferred >= 0) return Deferred;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || !CanReceiveInput(Runtime, Found->second.get()) ||
        (Found->second->ClassName != "TextButton" && Found->second->ClassName != "CheckBox")) return 0;
    FireSignal(Runtime, Found->second.get(), "Activated");
    FlushLayout(Runtime);
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_TextChanged(LuiRuntime* Runtime, int Id, const char* Text) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed || !Text) return 0;
    const auto Target = Runtime->Nodes.find(Id);
    if (Target == Runtime->Nodes.end() || Target->second->Destroyed) return 0;
    const bool CodeEditor=Target->second->ClassName=="CodeEditor";
    BackendEvent Event{BackendEvent::Kind::TextChanged, Id};
    if (!(CodeEditor?LuiText::CaptureCodeEditor(Text,Event.Text):LuiText::Capture(Text,Event.Text))) return 0;
    Event.InputRevision = Target->second->InputRevision;
    const int Deferred = QueueBackendEventIfBusy(Runtime, Event);
    if (Deferred >= 0) return Deferred;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || !LuiSchema::IsA(Found->second->ClassName, "TextBox") ||
        !CanReceiveInput(Runtime, Found->second.get())) return 0;
    Node* Value = Found->second.get();
    int Characters;
    if (!(CodeEditor?LuiText::CountCodeEditor(Event.Text,Characters):LuiText::Count(Event.Text,Characters))) return 0;
    if (Characters > Value->MaxLength ||
        (Value->ClassName == "CodeEditor" && Event.Text.find('\r') != std::string::npos)) return 0;
    if (Value->Text != Text) {
        if (Value->ClassName == "CodeEditor") {
            int OldCharacters=0;LuiText::CountCodeEditor(Value->Text,OldCharacters);
            Value->CodeEditorUndoMaxLength=std::max(Value->CodeEditorUndoMaxLength,std::max(OldCharacters,Characters));
        }
        Value->Text = Text;
        if (Value->ClassName == "CodeEditor") {
            ++Value->TextRevision;
            QueueProperty(Runtime, Value, "__CodeStyles", "");
        }
        Value->SelectionStart = std::min(Value->SelectionStart, Characters);
        Value->SelectionLength = std::min(Value->SelectionLength, Characters - Value->SelectionStart);
        FireSignal(Runtime, Value, "Changed");
        FireSignal(Runtime, Value, "TextChanged");
    }
    FlushLayout(Runtime);
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_CheckedChanged(LuiRuntime* Runtime, int Id, int Checked) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed) return 0;
    BackendEvent Event{BackendEvent::Kind::CheckedChanged, Id};
    Event.Value = Checked;
    const int Deferred = QueueBackendEventIfBusy(Runtime, std::move(Event));
    if (Deferred >= 0) return Deferred;
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
    if (!CheckOwner(Runtime) || Runtime->BackendFailed || !std::isfinite(NewValue)) return 0;
    BackendEvent Event{BackendEvent::Kind::ValueChanged, Id};
    Event.Number = NewValue;
    const int Deferred = QueueBackendEventIfBusy(Runtime, std::move(Event));
    if (Deferred >= 0) return Deferred;
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

extern "C" LUI_API int LUI_CALL Lui_WindowResized(LuiRuntime* Runtime, int Id, double Width, double Height) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed || !std::isfinite(Width) || !std::isfinite(Height) ||
        Width <= 0 || Height <= 0) return 0;
    BackendEvent Event{BackendEvent::Kind::WindowResized, Id};
    Event.X = Width;
    Event.Y = Height;
    const int Deferred = QueueBackendEventIfBusy(Runtime, std::move(Event));
    if (Deferred >= 0) return Deferred;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->Destroyed || Found->second->WindowClosed || Found->second->ClassName != "Window") return 0;
    Node* Value = Found->second.get();
    if (!Value->HasViewportSize || Value->ViewportSize.X != Width || Value->ViewportSize.Y != Height) {
        Value->ViewportSize = {Width, Height};
        Value->HasViewportSize = true;
        Runtime->LayoutDirty = true;
        FlushLayout(Runtime);
    }
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_WindowCloseRequested(LuiRuntime* Runtime, int Id) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed || Runtime->ShuttingDown) return 0;
    const auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->Destroyed || Found->second->ClassName != "Window" ||
        Found->second->WindowClosed || Found->second->WindowClosePending || Found->second->WindowCloseDispatch) return 0;
    if (Found->second->WindowCloseRequestQueued) return 1;
    const int Deferred = QueueBackendEventIfBusy(Runtime, {BackendEvent::Kind::WindowCloseRequested, Id});
    if (Deferred >= 0) {
        if (Deferred) Found->second->WindowCloseRequestQueued = true;
        return Deferred;
    }
    const char* Error = RequestWindowClose(Runtime, Found->second.get());
    FlushLayout(Runtime);
    return !Error || std::strcmp(Error, "Prevented") == 0 || std::strcmp(Error, "CallbackFailed") == 0 ||
        std::strcmp(Error, "Closed") == 0;
}

extern "C" LUI_API int LUI_CALL Lui_WindowClosed(LuiRuntime* Runtime, int Id) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed || Runtime->ShuttingDown) return 0;
    const auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->Destroyed || Found->second->WindowClosed || Found->second->NativeWindowClosedPending ||
        Found->second->ClassName != "Window") return 0;
    if (Runtime->VmDepth || Runtime->BackendDepth) {
        // A native surface has already closed. Its acknowledgement must survive
        // ordinary input queue saturation without re-entering Luau callbacks.
        Found->second->NativeWindowClosedPending = true;
        Found->second->WindowClosePending = true;
        Runtime->PendingWindowClosures = true;
        return 1;
    }
    CommitWindowClosed(Runtime, Found->second.get());
    FlushLayout(Runtime);
    return 1;
}

static void DrainBackendEvents(LuiRuntime* Runtime) {
    if (Runtime->VmDepth || Runtime->BackendDepth || Runtime->DrainingBackendEvents) return;
    Runtime->DrainingBackendEvents = true;
    try {
        // At most one terminal acknowledgement is retained per live Window.
        // Snapshot IDs because a Closed callback may create/destroy objects.
        size_t ClosureTurns = 0;
        const auto DrainClosures = [&] {
        while (Runtime->PendingWindowClosures && ClosureTurns++ < 4096) {
            std::vector<int> Closed;
            for (const auto& Pair : Runtime->Nodes)
                if (Pair.second->NativeWindowClosedPending && !Pair.second->Destroyed) Closed.push_back(Pair.first);
            std::sort(Closed.begin(), Closed.end());
            Runtime->PendingWindowClosures = false;
            for (int Id : Closed) {
                auto Found = Runtime->Nodes.find(Id);
                if (Found != Runtime->Nodes.end() && !Found->second->Destroyed && Found->second->NativeWindowClosedPending)
                    CommitWindowClosed(Runtime, Found->second.get());
            }
        }
        };
        DrainClosures();
        size_t Count = 0;
        while (!Runtime->PendingBackendEvents.empty() && Count < 4096) {
            BackendEvent Event = std::move(Runtime->PendingBackendEvents.front());
            Runtime->PendingBackendBytes -= Event.Text.size();
            Runtime->PendingBackendEvents.pop_front();
            if (Event.InputRevision) {
                const auto Target = Runtime->Nodes.find(Event.Id);
                if (Target == Runtime->Nodes.end() || Target->second->Destroyed || Target->second->InputRevision != Event.InputRevision) {++Count; continue;}
            }
            switch (Event.Type) {
            case BackendEvent::Kind::Activate: Lui_Activate(Runtime, Event.Id); break;
            case BackendEvent::Kind::TextChanged: Lui_TextChanged(Runtime, Event.Id, Event.Text.c_str()); break;
            case BackendEvent::Kind::TextInput: Lui_TextInput(Runtime, Event.Id, Event.Text.c_str(), Event.Value, Event.Phase); break;
            case BackendEvent::Kind::Composition: Lui_TextComposition(Runtime, Event.Id, Event.Phase, Event.Text.c_str()); break;
            case BackendEvent::Kind::CheckedChanged: Lui_CheckedChanged(Runtime, Event.Id, Event.Value); break;
            case BackendEvent::Kind::ValueChanged: Lui_ValueChanged(Runtime, Event.Id, Event.Number); break;
            case BackendEvent::Kind::WindowResized: Lui_WindowResized(Runtime, Event.Id, Event.X, Event.Y); break;
            case BackendEvent::Kind::WindowCloseRequested: {
                auto Found = Runtime->Nodes.find(Event.Id);
                if (Found != Runtime->Nodes.end()) Found->second->WindowCloseRequestQueued = false;
                Lui_WindowCloseRequested(Runtime, Event.Id); break;
            }
            case BackendEvent::Kind::WindowClosed: Lui_WindowClosed(Runtime, Event.Id); break;
            case BackendEvent::Kind::FocusChanged: Lui_FocusChanged(Runtime, Event.Id, Event.Value); break;
            case BackendEvent::Kind::HoverChanged: Lui_HoverChanged(Runtime, Event.Id, Event.Value); break;
            case BackendEvent::Kind::PointerInput:
                if (Event.PointerV2)
                    Lui_PointerInputV2(Runtime, Event.Id, Event.Phase, Event.Device, Event.PointerId, Event.X, Event.Y, Event.Button);
                else Lui_PointerInput(Runtime, Event.Id, Event.Phase, Event.Device, Event.PointerId, Event.X, Event.Y);
                break;
            case BackendEvent::Kind::KeyInput:
                Lui_KeyInput(Runtime, Event.Id, Event.Phase, Event.Text.c_str());
                break;
            case BackendEvent::Kind::WindowShortcut:
                Lui_WindowShortcut(Runtime, Event.Id, Event.Value, Event.Text.c_str(), Event.Device, Event.Phase);
                break;
            case BackendEvent::Kind::ScrollInput:
                Lui_ScrollInput(Runtime, Event.Id, Event.X, Event.Y);
                break;
            }
            DrainClosures();
            ++Count;
        }
        if (!Runtime->PendingBackendEvents.empty())
            EmitLog(Runtime, "Warning", "Scheduler: backend event drain yielded after 4096 events");
    } catch (...) {
        Runtime->DrainingBackendEvents = false;
        throw;
    }
    Runtime->DrainingBackendEvents = false;
}

void ResumeScheduledTask(LuiRuntime* Runtime, int Reference, int ArgumentCount, bool AsError) {
    if (!NetworkTaskReady(Runtime, Reference)) {
        CompleteNetworkTask(Runtime, Reference, nullptr, LUA_ERRRUN);
        lua_unref(Runtime->State, Reference);
        return;
    }
    lua_getref(Runtime->State, Reference);
    lua_State* Thread = lua_tothread(Runtime->State, -1);
    lua_pop(Runtime->State, 1);
    if (!Thread) {
        Runtime->LastError = "[LUI:Scheduler] scheduled coroutine is missing";
        EmitLog(Runtime, "Error", Runtime->LastError);
        CompleteNetworkTask(Runtime, Reference, nullptr, LUA_ERRRUN);
        lua_unref(Runtime->State, Reference);
        return;
    }
    const int PreviousReference = Runtime->ActiveTaskReference;
    lua_State* PreviousThread = Runtime->ActiveTaskThread;
    const bool PreviousWaiter = Runtime->ActiveTaskHasWaiter;
    Runtime->ActiveTaskReference = Reference;
    Runtime->ActiveTaskThread = Thread;
    Runtime->ActiveTaskHasWaiter = false;
    int Status;
    {
        DepthGuard Guard(Runtime->VmDepth, Runtime);
        Status = AsError ? lua_resumeerror(Thread, Runtime->State)
            : lua_resume(Thread, Runtime->State, ArgumentCount);
    }
    const bool HasWaiter = Runtime->ActiveTaskHasWaiter;
    Runtime->ActiveTaskReference = PreviousReference;
    Runtime->ActiveTaskThread = PreviousThread;
    Runtime->ActiveTaskHasWaiter = PreviousWaiter;
    if (Status == LUA_YIELD && HasWaiter) return;
    const bool Handled = CompleteNetworkTask(Runtime, Reference, Thread, Status);
    if (Status != LUA_OK && !Handled) {
        const char* Message = Status == LUA_YIELD
            ? "[LUI:Scheduler] task yielded without a registered async operation"
            : lua_tostring(Thread, -1);
        Runtime->LastError = Message ? Message : "[LUI:Scheduler] scheduled task failed";
        EmitLog(Runtime, "Error", "Scheduler: " + Runtime->LastError);
    }
    lua_settop(Thread, 0);
    lua_unref(Runtime->State, Reference);
}

bool ConfigureBoundedTasks(LuiRuntime* Runtime, size_t Maximum) {
    if (!CheckOwner(Runtime) || !Maximum || Maximum > 4096 || Runtime->TaskLimit ||
        Runtime->ApplicationStarted || !Runtime->CapabilitiesDeclared || (Runtime->GrantedCapabilities & ~LUI_CAPABILITY_STORAGE) ||
        Runtime->VmDepth || Runtime->BackendDepth || Runtime->NextNodeId != 1) return false;
    Runtime->TaskLimit = Maximum;
    return true;
}

bool PumpBoundedTasks(LuiRuntime* Runtime, size_t MaximumEntries,
    std::chrono::steady_clock::time_point Deadline, size_t* Dispatched) {
    if (Dispatched) *Dispatched = 0;
    if (!CheckOwner(Runtime) || !Runtime->TaskLimit || !MaximumEntries || MaximumEntries > 4096 ||
        Runtime->BackendFailed || Runtime->ShuttingDown || Runtime->VmDepth || Runtime->BackendDepth ||
        Runtime->DrainingBackendEvents || Runtime->UiCompletionRunning) return false;
    // Restricted hosts drain their bounded built-in storage completions before
    // entering this scheduler. Network/platform/dynamic providers are excluded.
    // Fix one round's due-time cutoff across slices. Future tasks and tasks
    // scheduled by callbacks enter Tasks in the same order as legacy Lui_Pump.
    if (!Runtime->TaskRoundActive) {
        DeliverTheme(Runtime);
        Runtime->TaskRound.swap(Runtime->Tasks);
        Runtime->TaskRoundIndex = 0;
        Runtime->TaskRoundTime = std::chrono::steady_clock::now();
        Runtime->TaskRoundActive = true;
    }
    size_t Entries = 0, Count = 0;
    while (Runtime->TaskRoundIndex < Runtime->TaskRound.size() && Entries < MaximumEntries &&
        std::chrono::steady_clock::now() < Deadline && !Runtime->BackendFailed) {
        const auto Call = Runtime->TaskRound[Runtime->TaskRoundIndex++];
        ++Entries;
        if (Call.Due > Runtime->TaskRoundTime) Runtime->Tasks.push_back(Call);
        else { ResumeScheduledTask(Runtime, Call.Reference, Call.ArgumentCount); ++Count; }
    }
    if (Runtime->TaskRoundIndex == Runtime->TaskRound.size()) {
        Runtime->TaskRound.clear(); Runtime->TaskRoundIndex = 0; Runtime->TaskRoundActive = false;
    }
    FlushLayout(Runtime);
    if (Dispatched) *Dispatched = Count;
    return !Runtime->BackendFailed && Runtime->InterruptCount <= Runtime->MaxInterrupts;
}

extern "C" LUI_API int LUI_CALL Lui_Pump(LuiRuntime* Runtime) {
    if (!CheckOwner(Runtime)) return 0;
    // A private bounded host may not accidentally bypass its profile by entering
    // the legacy whole-round path. All ordinary desktop/browser callers keep it.
    if (Runtime->TaskLimit) return 0;
    if (Runtime->BackendFailed) return 0;
    if (Runtime->VmDepth || Runtime->BackendDepth || Runtime->DrainingBackendEvents || Runtime->UiCompletionRunning) {
        Runtime->LastError = "[LUI:Scheduler] Cannot pump during an active dispatch";
        EmitLog(Runtime, "Error", Runtime->LastError);
        return 0;
    }
    DeliverTheme(Runtime);
    PumpDesktopServices(Runtime);
    int Count = DrainUiCompletions(Runtime) + DrainPlatformRequests(Runtime) + DrainNetworkCompletions(Runtime) + DrainNativeServices(Runtime);
    const auto Now = std::chrono::steady_clock::now();
    std::vector<ScheduledCall> Pending;
    Pending.swap(Runtime->Tasks);
    for (const auto& Call : Pending) {
        if (Runtime->BackendFailed) break;
        if (Call.Due > Now) { Runtime->Tasks.push_back(Call); continue; }
        ResumeScheduledTask(Runtime, Call.Reference, Call.ArgumentCount);
        ++Count;
    }
    FlushLayout(Runtime);
    Count += PumpFrameSurfaces(Runtime);
    return Runtime->BackendFailed ? 0 : Count;
}

extern "C" LUI_API int LUI_CALL Lui_SystemThemeChanged(LuiRuntime* Runtime, const char* Theme) {
    if (!CheckOwner(Runtime) || !Theme || Runtime->ShuttingDown) return 0;
    const std::string Value = Theme;
    if (Value != "Light" && Value != "Dark" && Value != "HighContrast") {
        Runtime->LastError = "[LUI:Theme] invalid system theme";
        return 0;
    }
    if (!Runtime->ApplicationStarted) {
        Runtime->CurrentTheme = Value;
        return 1;
    }
    Runtime->PendingTheme = Value;
    return 1;
}

extern "C" LUI_API const char* LUI_CALL Lui_GetLastError(LuiRuntime* Runtime) {
    return Runtime ? Runtime->LastError.c_str() : "runtime is null";
}

extern "C" LUI_API const char* LUI_CALL Lui_GetSchemaJson(void) {
    return LuiSchema::GetJson().c_str();
}

static void StopGeneration(LuiRuntime* Runtime) {
    if (!Runtime->State) return;
    {
        std::lock_guard<std::mutex> Lock(Runtime->CompletionMutex);
        Runtime->ShuttingDown = true;
        Runtime->PendingUiCompletions.clear();
    }
    CloseNetwork(Runtime);
    RetireDesktopServices(Runtime);
    CloseNativeServices(Runtime);
    // Shutdown is terminal. Destroying must not execute an old generation's
    // callbacks or admit new work while native resources are being retired.
    for (auto& Pair : Runtime->Nodes)
        if (!Pair.second->Destroyed && Pair.second->ClassName == "Window") DestroyNode(Runtime, Pair.second.get());
    for (auto& Pair : Runtime->Nodes) if (!Pair.second->Destroyed) DestroyNode(Runtime, Pair.second.get());
    for (const auto& Call : Runtime->Tasks) lua_unref(Runtime->State, Call.Reference);
    Runtime->Tasks.clear();
    for (size_t Index = Runtime->TaskRoundIndex; Index < Runtime->TaskRound.size(); ++Index)
        lua_unref(Runtime->State, Runtime->TaskRound[Index].Reference);
    Runtime->TaskRound.clear(); Runtime->TaskRoundIndex = 0; Runtime->TaskRoundActive = false;
    ClosePlatformRequests(Runtime);
    for (int Id : Runtime->ThemeListeners) Disconnect(Runtime, Id);
    CloseExtensionListeners(Runtime);
    for (const auto& Pair : Runtime->ServiceRefs) lua_unref(Runtime->State, Pair.second);
    lua_close(Runtime->State); Runtime->State = nullptr;
    Runtime->NativeServices.clear();
    Runtime->DesktopServices.reset();
    Runtime->UiCompletionRunning = true;
    Lui::Native::Pump(Runtime->NativeDomain);
    Runtime->UiCompletionRunning = false;
    CloseExtensions(Runtime);
    Runtime->Modules.reset(); Runtime->ModuleDepth = 0;
    Runtime->Nodes.clear(); Runtime->Listeners.clear(); Runtime->ThemeListeners.clear(); Runtime->ServiceRefs.clear();
    Runtime->PendingChanges.clear(); Runtime->PendingBackendEvents.clear(); Runtime->PendingTheme.clear();
    Runtime->PendingBackendBytes = 0; Runtime->CanvasSignals.clear();
    Runtime->DiagnosticRecords.clear(); Runtime->DiagnosticSnapshot.clear(); Runtime->DiagnosticBytes=0;
    Runtime->InspectionGranted=false; Runtime->Inspecting=false; Runtime->InspectionSnapshot.clear();
}

extern "C" LUI_API int LUI_CALL Lui_RunProject(LuiRuntime* Runtime, const LuiProject* Project, const LuiProjectRunOptionsV1* Options) {
    if (!CheckOwner(Runtime)) return 0;
    if (Runtime->ApplicationStarted || Runtime->VmDepth || Runtime->BackendDepth || Runtime->DrainingBackendEvents ||
        Runtime->UiCompletionRunning || Runtime->BackendFailed || Runtime->ShuttingDown) {
        Runtime->LastError = "[LUI:Modules] ProjectAlreadyStarted"; return 0;
    }
    if (!ConfigureProject(Runtime, GetProjectSnapshot(Project), Options)) return 0;
    Runtime->ApplicationStarted = true; Runtime->ProjectBootstrap = true;
    int Status = LUA_ERRRUN;
    std::string Failure = "LoadFailed";
    try {
        {
            DepthGuard Guard(Runtime->VmDepth, Runtime);
            Status = lua_cpcall(Runtime->State, ProjectEntry, nullptr);
        }
        if (Status == LUA_ERRMEM || Runtime->InterruptCount > Runtime->MaxInterrupts) Failure = "LimitExceeded";
        else if (Status != LUA_OK) {
            const char* Message = lua_tostring(Runtime->State, -1);
            if (Message) for (const auto* Code : {"InvalidPath","MissingModule","ForbiddenImport","EntryImportDenied","Cycle",
                "InitializationEffect","YieldNotAllowed","InvalidResult","LoadFailed","LimitExceeded","ProjectNotConfigured"})
                if (std::string(Message) == std::string("[LUI:Modules] ") + Code) Failure = Code;
        }
        if (Status == LUA_OK && Runtime->InterruptCount <= Runtime->MaxInterrupts && !Runtime->BackendFailed) {
            // Admit native objects only after successful entry, in deterministic ID
            // order. A failed commit destroys every attempted native object.
            std::vector<int> Ids;
            for (const auto& Pair : Runtime->Nodes)
                if (!Pair.second->Destroyed && LuiSchema::IsNative(Pair.second->ClassName)) Ids.push_back(Pair.first);
            std::sort(Ids.begin(), Ids.end());
            for (int Id : Ids) {
                auto* Value = Runtime->Nodes.at(Id).get(); Value->NativeCreated = true;
                if (Runtime->Backend.Create) {
                    DepthGuard Guard(Runtime->BackendDepth);
                    if (!Runtime->Backend.Create(Runtime->Backend.Context, Id, Value->ClassName.c_str())) {
                        Runtime->BackendFailed = true; break;
                    }
                }
            }
            Runtime->ProjectBootstrap = false;
            FlushLayout(Runtime);
            if (!Runtime->BackendFailed) { Runtime->LastError.clear(); return 1; }
        }
    } catch (const std::bad_alloc&) { Failure = "LimitExceeded"; }
    catch (...) { Failure = "LoadFailed"; }
    // Retire callbacks/VM/cache/queued work before returning a fail-empty tree.
    Runtime->BackendFailed = true;
    StopGeneration(Runtime); Runtime->ProjectBootstrap = false;
    Runtime->LastError = "[LUI:Modules] " + Failure;
    EmitLog(Runtime, "Error", Runtime->LastError); return 0;
}

extern "C" LUI_API void LUI_CALL Lui_Destroy(LuiRuntime* Runtime) {
    if (!CheckOwner(Runtime)) return;
    if (Runtime->VmDepth || Runtime->BackendDepth || Runtime->DrainingBackendEvents || Runtime->UiCompletionRunning) {
        Runtime->LastError = "[LUI:Scheduler] Cannot destroy the runtime during an active dispatch";
        EmitLog(Runtime, "Error", Runtime->LastError);
        return;
    }
    StopGeneration(Runtime);
    delete Runtime;
}
