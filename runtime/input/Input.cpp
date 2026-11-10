#include "../internal/Dispatch.h"
#include "../internal/Input.h"
#include "../internal/Text.h"
#include "../reflection/Schema.h"
#include "../../ui/layout/Layout.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

bool CanReceiveInput(const LuiRuntime* Runtime, const Node* Value) {
    if (Value->Destroyed || Value->WindowClosed || Value->NativeWindowClosedPending || !Value->Visible || !Value->Enabled) return false;
    for (const Node* Parent = Value; Parent->ParentId; ) {
        Parent = Runtime->Nodes.at(Parent->ParentId).get();
        if (!Parent->Visible || Parent->WindowClosed || Parent->NativeWindowClosedPending) return false;
    }
    return LuiLayout::HasViewportIntersection(Runtime, Value);
}

extern "C" LUI_API int LUI_CALL Lui_HitTest(LuiRuntime* Runtime, int WindowId, double X, double Y, int* HitId) {
    if (HitId) *HitId = 0;
    if (!CheckOwner(Runtime) || !HitId || Runtime->BackendFailed || Runtime->VmDepth ||
        Runtime->BackendDepth || Runtime->DrainingBackendEvents || Runtime->ModuleDepth || Runtime->ProjectBootstrap ||
        Runtime->UiCompletionRunning || Runtime->ShuttingDown || !std::isfinite(X) || !std::isfinite(Y)) return 0;
    if ((Runtime->Backend.Create || Runtime->Backend.Property || Runtime->Backend.Parent ||
        Runtime->Backend.Arrange || Runtime->Backend.Destroy) && !Runtime->Presentation.Present) return 0;
    try {
        FlushLayout(Runtime);
        const auto Found = Runtime->Nodes.find(WindowId);
        if (Runtime->BackendFailed || Found == Runtime->Nodes.end() || Found->second->Destroyed ||
            Found->second->ClassName != "Window") return 0;
        *HitId = LuiLayout::HitTest(Runtime, Found->second.get(), X, Y);
        return 1;
    } catch (...) {
        Runtime->LastError = "[LUI:Viewport] Hit query failed";
        return 0;
    }
}

extern "C" LUI_API int LUI_CALL Lui_ScrollInput(LuiRuntime* Runtime, int OriginId, double DeltaX, double DeltaY) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed || !std::isfinite(DeltaX) || !std::isfinite(DeltaY) ||
        std::abs(DeltaX) > 1e6 || std::abs(DeltaY) > 1e6) return 0;
    BackendEvent Event{BackendEvent::Kind::ScrollInput, OriginId}; Event.X = DeltaX; Event.Y = DeltaY;
    const int Deferred = QueueBackendEventIfBusy(Runtime, std::move(Event));
    if (Deferred >= 0) return Deferred;
    FlushLayout(Runtime);
    auto Found = Runtime->Nodes.find(OriginId);
    if (Found == Runtime->Nodes.end() || !CanReceiveInput(Runtime, Found->second.get())) return 0;
    std::vector<int> Chain;
    for (const Node* Value = Found->second.get(); Value;) {
        if (Value->ClassName == "ScrollingFrame") Chain.push_back(Value->Id);
        Value = Value->ParentId ? Runtime->Nodes.at(Value->ParentId).get() : nullptr;
    }
    if (Chain.empty()) return 0;
    for (int Id : Chain) {
        auto Current = Runtime->Nodes.find(Id);
        if (Current == Runtime->Nodes.end() || !CanReceiveInput(Runtime, Current->second.get())) continue;
        Node* Value = Current->second.get();
        const VectorValue Previous = Value->AbsoluteCanvasPosition;
        const VectorValue Next{
            std::clamp(Previous.X + DeltaX, 0.0, Value->AbsoluteCanvasSize.X - Value->Bounds.Width),
            std::clamp(Previous.Y + DeltaY, 0.0, Value->AbsoluteCanvasSize.Y - Value->Bounds.Height)};
        DeltaX -= Next.X - Previous.X; DeltaY -= Next.Y - Previous.Y;
        if (Value->CanvasPosition.X != Next.X || Value->CanvasPosition.Y != Next.Y) {
            Value->CanvasPosition = Next; Runtime->LayoutDirty = true;
            FireSignal(Runtime, Value, "Changed");
        }
        if (DeltaX == 0 && DeltaY == 0) break;
    }
    FlushLayout(Runtime);
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_GetTextInputRevision(LuiRuntime* Runtime, int Id, uint64_t* Revision) {
    if(Revision) *Revision=0;
    if(!CheckOwner(Runtime) || !Revision || Runtime->ShuttingDown || Runtime->BackendFailed) return 0;
    const auto Found=Runtime->Nodes.find(Id);
    if(Found==Runtime->Nodes.end() || Found->second->Destroyed || !LuiSchema::IsA(Found->second->ClassName,"TextBox")) return 0;
    *Revision=Found->second->InputRevision; return 1;
}
extern "C" LUI_API int LUI_CALL Lui_GetCodeEditorTextRevision(LuiRuntime* Runtime, int Id, uint64_t* Revision) {
    if(Revision) *Revision=0;
    if(!CheckOwner(Runtime) || !Revision || Runtime->ShuttingDown || Runtime->BackendFailed) return 0;
    const auto Found=Runtime->Nodes.find(Id);
    if(Found==Runtime->Nodes.end() || Found->second->Destroyed || Found->second->ClassName!="CodeEditor") return 0;
    *Revision=Found->second->TextRevision;return 1;
}

static void EndActiveKeys(LuiRuntime* Runtime, Node* Value) {
    auto ActiveKeys = std::move(Value->ActiveKeys);
    Value->ActiveKeys.clear();
    std::vector<std::string> Keys;
    for (const auto& Pair : ActiveKeys) Keys.push_back(Pair.first);
    std::sort(Keys.begin(), Keys.end());
    for (const std::string& Key : Keys)
        FireSignal(Runtime, Value, "InputEnded", nullptr, &ActiveKeys.at(Key));
}

extern "C" LUI_API int LUI_CALL Lui_TextInput(LuiRuntime* Runtime, int Id, const char* Text, int Start, int Length) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed || Start < 0 || Length < 0) return 0;
    const auto Target = Runtime->Nodes.find(Id);
    if (Target == Runtime->Nodes.end() || Target->second->Destroyed) return 0;
    const bool CodeEditor=Target->second->ClassName=="CodeEditor";
    BackendEvent Event{BackendEvent::Kind::TextInput, Id};
    if (!(CodeEditor?LuiText::CaptureCodeEditor(Text, Event.Text):LuiText::Capture(Text, Event.Text))) return 0;
    int Characters;
    if (!(CodeEditor?LuiText::CountCodeEditor(Event.Text,Characters):LuiText::Count(Event.Text,Characters))) return 0;
    if (Start > Characters || Length > Characters - Start) return 0;
    Event.Value = Start; Event.Phase = Length;
    Event.InputRevision = Target->second->InputRevision;
    const int Deferred = QueueBackendEventIfBusy(Runtime, Event);
    if (Deferred >= 0) return Deferred;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || !LuiSchema::IsA(Found->second->ClassName,"TextBox") ||
        !CanReceiveInput(Runtime, Found->second.get()) || Characters > Found->second->MaxLength ||
        (Found->second->Secret && (Start || Length)) ||
        (Found->second->ClassName=="CodeEditor" && Event.Text.find('\r')!=std::string::npos)) return 0;
    Node* Value = Found->second.get();
    const bool TextChanged = Value->Text != Event.Text;
    const bool SelectionChanged = Value->SelectionStart != Start || Value->SelectionLength != Length;
    if (TextChanged && Value->ClassName=="CodeEditor") {
        int OldCharacters=0;LuiText::CountCodeEditor(Value->Text,OldCharacters);
        Value->CodeEditorUndoMaxLength=std::max(Value->CodeEditorUndoMaxLength,
            std::max(OldCharacters,Characters));
    }
    Value->Text = std::move(Event.Text); Value->SelectionStart = Start; Value->SelectionLength = Length;
    if (TextChanged && Value->ClassName=="CodeEditor") {
        ++Value->TextRevision;
        QueueProperty(Runtime,Value,"__CodeStyles","");
    }
    if (TextChanged || SelectionChanged) FireSignal(Runtime, Value, "Changed");
    if (TextChanged) FireSignal(Runtime, Value, "TextChanged");
    if (SelectionChanged) FireSignal(Runtime, Value, "SelectionChanged");
    FlushLayout(Runtime); return 1;
}

extern "C" LUI_API int LUI_CALL Lui_TextComposition(LuiRuntime* Runtime, int Id, int Phase, const char* Preedit) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed || Phase < 0 || Phase > 2) return 0;
    BackendEvent Event{BackendEvent::Kind::Composition, Id}; Event.Phase = Phase;
    if (!LuiText::Capture(Preedit, Event.Text, 4096)) return 0;
    const auto Target = Runtime->Nodes.find(Id);
    if (Target == Runtime->Nodes.end() || Target->second->Destroyed) return 0;
    Event.InputRevision = Target->second->InputRevision;
    const int Deferred = QueueBackendEventIfBusy(Runtime, Event);
    if (Deferred >= 0) return Deferred;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || !LuiSchema::IsA(Found->second->ClassName,"TextBox") || Found->second->Secret || !CanReceiveInput(Runtime, Found->second.get())) return 0;
    Node* Value = Found->second.get();
    if (Phase == 0) {
        if (Value->IsComposing) return 0;
        Value->IsComposing = true; Value->CompositionText = Event.Text;
        FireSignal(Runtime, Value, "CompositionStarted");
    } else {
        if (!Value->IsComposing) return 0;
        Value->CompositionText = Phase == 2 ? std::string{} : Event.Text;
        if (Phase == 2) Value->IsComposing = false;
        FireSignal(Runtime, Value, Phase == 2 ? "CompositionEnded" : "CompositionChanged");
    }
    FlushLayout(Runtime); return 1;
}

static void ClearFocus(LuiRuntime* Runtime, Node* Value) {
    if (!Value->IsFocused) return;
    Value->IsFocused = false;
    if (Value->IsComposing) {
        Value->IsComposing = false; Value->CompositionText.clear();
        FireSignal(Runtime, Value, "CompositionEnded");
    }
    FireSignal(Runtime, Value, "FocusLost");
    EndActiveKeys(Runtime, Value);
    std::vector<PointerInputValue> Canceled;
    for (auto Iterator = Value->ActivePointers.begin(); Iterator != Value->ActivePointers.end();) {
        if (Iterator->second.IsV2) {
            PointerInputValue Input = Iterator->second;
            Input.IsCanceled = true;
            Canceled.push_back(std::move(Input));
            Iterator = Value->ActivePointers.erase(Iterator);
        } else ++Iterator;
    }
    for (const auto& Input : Canceled) FireSignal(Runtime, Value, "InputEnded", &Input);
}

void ClearInvalidInput(LuiRuntime* Runtime) {
    std::vector<int> InvalidIds;
    for (const auto& Pair : Runtime->Nodes) {
        if ((Pair.second->IsFocused || Pair.second->IsHovered || Pair.second->IsComposing || !Pair.second->ActivePointers.empty() ||
            !Pair.second->ActiveKeys.empty()) &&
            !CanReceiveInput(Runtime, Pair.second.get())) InvalidIds.push_back(Pair.first);
    }
    std::sort(InvalidIds.begin(), InvalidIds.end());
    for (int Id : InvalidIds) {
        Node* Value = Runtime->Nodes.at(Id).get();
        if (CanReceiveInput(Runtime, Value)) continue;
        auto ActivePointers = std::move(Value->ActivePointers);
        Value->ActivePointers.clear();
        auto ActiveKeys = std::move(Value->ActiveKeys);
        Value->ActiveKeys.clear();
        const bool WasFocused = Value->IsFocused;
        const bool WasHovered = Value->IsHovered;
        const bool WasComposing = Value->IsComposing;
        Value->IsFocused = false;
        Value->IsHovered = false;
        Value->IsComposing = false; Value->CompositionText.clear();
        if (WasComposing) FireSignal(Runtime, Value, "CompositionEnded");
        if (WasFocused) FireSignal(Runtime, Value, "FocusLost");
        if (WasHovered) FireSignal(Runtime, Value, "MouseLeave");
        for (auto& Pair : ActivePointers) {
            Pair.second.IsCanceled = true;
            FireSignal(Runtime, Value, "InputEnded", &Pair.second);
        }
        std::vector<std::string> Keys;
        for (const auto& Pair : ActiveKeys) Keys.push_back(Pair.first);
        std::sort(Keys.begin(), Keys.end());
        for (const std::string& Key : Keys)
            FireSignal(Runtime, Value, "InputEnded", nullptr, &ActiveKeys.at(Key));
    }
}

extern "C" LUI_API int LUI_CALL Lui_FocusChanged(LuiRuntime* Runtime, int Id, int Focused) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed) return 0;
    BackendEvent Event{BackendEvent::Kind::FocusChanged, Id};
    Event.Value = Focused;
    const int Deferred = QueueBackendEventIfBusy(Runtime, std::move(Event));
    if (Deferred >= 0) return Deferred;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->Destroyed || !LuiSchema::IsA(Found->second->ClassName, "GuiObject")) return 0;
    Node* Value = Found->second.get();
    bool Parsed = Focused != 0;
    if (Parsed) {
        if (!CanReceiveInput(Runtime, Value)) return 0;
        std::vector<int> PreviousIds;
        for (const auto& Pair : Runtime->Nodes) {
            if (Pair.first != Id && Pair.second->IsFocused) PreviousIds.push_back(Pair.first);
        }
        std::sort(PreviousIds.begin(), PreviousIds.end());
        for (int PreviousId : PreviousIds) ClearFocus(Runtime, Runtime->Nodes.at(PreviousId).get());
        if (!CanReceiveInput(Runtime, Value)) {
            FlushLayout(Runtime);
            return 0;
        }
        if (!Value->IsFocused) {
            Value->IsFocused = true;
            FireSignal(Runtime, Value, "Focused");
        }
    } else {
        ClearFocus(Runtime, Value);
    }
    FlushLayout(Runtime);
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_HoverChanged(LuiRuntime* Runtime, int Id, int Hovered) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed) return 0;
    BackendEvent Event{BackendEvent::Kind::HoverChanged, Id};
    Event.Value = Hovered;
    const int Deferred = QueueBackendEventIfBusy(Runtime, std::move(Event));
    if (Deferred >= 0) return Deferred;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->Destroyed ||
        !LuiSchema::IsA(Found->second->ClassName, "GuiObject")) return 0;
    Node* Value = Found->second.get();
    const bool Parsed = Hovered != 0;
    if (Parsed && !CanReceiveInput(Runtime, Value)) return 0;
    if (Value->IsHovered != Parsed) {
        Value->IsHovered = Parsed;
        FireSignal(Runtime, Value, Parsed ? "MouseEnter" : "MouseLeave");
    }
    FlushLayout(Runtime);
    return 1;
}

static int ProcessPointerInput(LuiRuntime* Runtime, int Id, int Phase, int Device,
    unsigned int PointerId, double X, double Y, int Button, bool IsV2) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed || Phase < 0 || Phase > 3 || Device < 0 || Device > 3 ||
        (Phase != 3 && (!std::isfinite(X) || !std::isfinite(Y)))) return 0;
    if (IsV2 && ((Device == 0 && ((Phase == 0 || Phase == 2) ? Button < 1 || Button > 5 : Button != 0)) ||
        (Device != 0 && Button != 0))) return 0;
    BackendEvent Event{BackendEvent::Kind::PointerInput, Id};
    Event.Phase = Phase;
    Event.Device = Device;
    Event.Button = Button;
    Event.PointerV2 = IsV2;
    Event.PointerId = PointerId;
    Event.X = X;
    Event.Y = Y;
    const int Deferred = QueueBackendEventIfBusy(Runtime, std::move(Event));
    if (Deferred >= 0) return Deferred;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->Destroyed ||
        !LuiSchema::IsA(Found->second->ClassName, "GuiObject")) return 0;
    Node* Value = Found->second.get();
    const auto PointerKey = std::pair{PointerId, Button};
    auto Active = Value->ActivePointers.find(PointerKey);
    const bool AnyActive = std::any_of(Value->ActivePointers.begin(), Value->ActivePointers.end(),
        [PointerId](const auto& Pair) { return Pair.first.first == PointerId; });
    static const char* Devices[] = {"Mouse", "Pen", "Touch", "Touchpad"};
    static const char* Buttons[] = {"", "Left", "Right", "Middle", "Auxiliary1", "Auxiliary2"};
    if ((Phase == 0 || (Phase == 1 && !AnyActive)) &&
        !LuiLayout::ContainsViewportPoint(Runtime, Value, Value->Bounds.X + X, Value->Bounds.Y + Y)) return 0;
    if (Phase == 0) {
        if (!CanReceiveInput(Runtime, Value)) return 0;
        if (Active == Value->ActivePointers.end()) {
            if (IsV2 && Value->ActivePointers.size() >= 64) return 0;
            PointerInputValue Input{PointerId, Devices[Device], {X, Y}, false,
                IsV2 && Device == 0 ? Buttons[Button] : "", IsV2};
            Value->ActivePointers.emplace(PointerKey, Input);
            FireSignal(Runtime, Value, "InputBegan", &Input);
        }
    } else if (Phase == 1) {
        if (!CanReceiveInput(Runtime, Value)) return 0;
        PointerInputValue Input{PointerId, Devices[Device], {X, Y}};
        for (auto& Pair : Value->ActivePointers)
            if (Pair.first.first == PointerId) Pair.second.Position = {X, Y};
        FireSignal(Runtime, Value, "InputChanged", &Input);
    } else if (Phase == 2) {
        if (Active == Value->ActivePointers.end()) return 0;
        PointerInputValue Input = Active->second;
        Input.Position = {X, Y};
        Value->ActivePointers.erase(Active);
        FireSignal(Runtime, Value, "InputEnded", &Input);
    } else {
        std::vector<PointerInputValue> Canceled;
        for (auto Iterator = Value->ActivePointers.begin(); Iterator != Value->ActivePointers.end();) {
            if (Iterator->first.first == PointerId) {
                PointerInputValue Input = Iterator->second;
                Input.IsCanceled = true;
                Canceled.push_back(std::move(Input));
                Iterator = Value->ActivePointers.erase(Iterator);
            } else ++Iterator;
        }
        if (Canceled.empty()) return 0;
        for (const auto& Input : Canceled) FireSignal(Runtime, Value, "InputEnded", &Input);
    }
    FlushLayout(Runtime);
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_PointerInput(LuiRuntime* Runtime, int Id, int Phase, int Device,
    unsigned int PointerId, double X, double Y) {
    return ProcessPointerInput(Runtime, Id, Phase, Device, PointerId, X, Y, 0, false);
}

extern "C" LUI_API int LUI_CALL Lui_PointerInputV2(LuiRuntime* Runtime, int Id, int Phase, int Device,
    unsigned int PointerId, double X, double Y, int Button) {
    return ProcessPointerInput(Runtime, Id, Phase, Device, PointerId, X, Y, Button, true);
}

static bool IsPortableKey(const std::string& Key) {
    if (Key.size() == 1 && ((Key[0] >= 'A' && Key[0] <= 'Z') ||
        (Key[0] >= '0' && Key[0] <= '9'))) return true;
    if (Key.size() >= 2 && Key[0] == 'F') {
        const std::string Number = Key.substr(1);
        if (Number.size() == 1 && Number[0] >= '1' && Number[0] <= '9') return true;
        if (Number == "10" || Number == "11" || Number == "12") return true;
    }
    static const char* NamedKeys[] = {
        "Enter", "Escape", "Tab", "Space", "Backspace", "Delete", "Insert",
        "Home", "End", "PageUp", "PageDown", "ArrowLeft", "ArrowRight",
        "ArrowUp", "ArrowDown", "Shift", "Control", "Alt", "CapsLock"
    };
    for (const char* Name : NamedKeys) if (Key == Name) return true;
    return false;
}

extern "C" LUI_API int LUI_CALL Lui_WindowShortcut(LuiRuntime* Runtime, int WindowId, int OriginId,
    const char* Key, unsigned int Modifiers, int Repeat) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed || !Key || Modifiers > 7 || (Repeat != 0 && Repeat != 1)) return 0;
    const std::string Name = Key;
    if (!IsPortableKey(Name) || Name == "Control" || Name == "Shift" || Name == "Alt" || Name == "CapsLock") return 0;
    BackendEvent Event{BackendEvent::Kind::WindowShortcut, WindowId};
    Event.Value = OriginId; Event.Text = Name; Event.Device = static_cast<int>(Modifiers); Event.Phase = Repeat;
    const int Deferred = QueueBackendEventIfBusy(Runtime, std::move(Event));
    if (Deferred >= 0) return Deferred;
    const auto Window = Runtime->Nodes.find(WindowId), Origin = Runtime->Nodes.find(OriginId);
    if (Window == Runtime->Nodes.end() || Origin == Runtime->Nodes.end() || Window->second->ClassName != "Window" ||
        Window->second->Destroyed || !Window->second->Visible || !Origin->second->IsFocused ||
        !CanReceiveInput(Runtime, Origin->second.get()) || Origin->second->IsComposing) return 0;
    const Node* Ancestor = Origin->second.get();
    while (Ancestor && Ancestor->ClassName != "Window")
        Ancestor = Ancestor->ParentId ? Runtime->Nodes.at(Ancestor->ParentId).get() : nullptr;
    if (Ancestor != Window->second.get()) return 0;
    KeyboardInputValue Input{Name, Repeat != 0, true, Modifiers, LuiSchema::IsA(Origin->second->ClassName,"TextBox")};
    FireSignal(Runtime, Window->second.get(), "Shortcut", nullptr, &Input);
    FlushLayout(Runtime);
    return 1;
}

extern "C" LUI_API int LUI_CALL Lui_KeyInput(LuiRuntime* Runtime, int Id, int Phase, const char* Key) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed || Phase < 0 || Phase > 2 || !Key) return 0;
    const std::string Name = Key;
    if (!IsPortableKey(Name)) return 0;
    BackendEvent Event{BackendEvent::Kind::KeyInput, Id};
    Event.Phase = Phase;
    Event.Text = Name;
    const int Deferred = QueueBackendEventIfBusy(Runtime, std::move(Event));
    if (Deferred >= 0) return Deferred;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->Destroyed ||
        !LuiSchema::IsA(Found->second->ClassName, "GuiObject")) return 0;
    Node* Value = Found->second.get();
    auto Active = Value->ActiveKeys.find(Name);
    if (Phase == 2) {
        if (Active == Value->ActiveKeys.end()) return 0;
        KeyboardInputValue Input{Name, false};
        Value->ActiveKeys.erase(Active);
        FireSignal(Runtime, Value, "InputEnded", nullptr, &Input);
    } else {
        if (!CanReceiveInput(Runtime, Value) || !Value->IsFocused) return 0;
        if (Active == Value->ActiveKeys.end()) {
            if (Phase == 1) return 0;
            KeyboardInputValue Input{Name, false};
            Value->ActiveKeys.emplace(Name, Input);
            FireSignal(Runtime, Value, "InputBegan", nullptr, &Input);
        } else {
            KeyboardInputValue Input{Name, true};
            FireSignal(Runtime, Value, "InputChanged", nullptr, &Input);
        }
    }
    FlushLayout(Runtime);
    return 1;
}
