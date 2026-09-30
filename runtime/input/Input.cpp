#include "../internal/Dispatch.h"
#include "../internal/Input.h"
#include "../reflection/Schema.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

bool CanReceiveInput(const LuiRuntime* Runtime, const Node* Value) {
    if (Value->Destroyed || !Value->Visible || !Value->Enabled) return false;
    for (const Node* Parent = Value; Parent->ParentId; ) {
        Parent = Runtime->Nodes.at(Parent->ParentId).get();
        if (!Parent->Visible) return false;
    }
    return true;
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

static void ClearFocus(LuiRuntime* Runtime, Node* Value) {
    if (!Value->IsFocused) return;
    Value->IsFocused = false;
    FireSignal(Runtime, Value, "FocusLost");
    EndActiveKeys(Runtime, Value);
}

void ClearInvalidInput(LuiRuntime* Runtime) {
    std::vector<int> InvalidIds;
    for (const auto& Pair : Runtime->Nodes) {
        if ((Pair.second->IsFocused || Pair.second->IsHovered || !Pair.second->ActivePointers.empty() ||
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
        Value->IsFocused = false;
        Value->IsHovered = false;
        if (WasFocused) FireSignal(Runtime, Value, "FocusLost");
        if (WasHovered) FireSignal(Runtime, Value, "MouseLeave");
        std::vector<unsigned int> PointerIds;
        for (const auto& Pair : ActivePointers) PointerIds.push_back(Pair.first);
        std::sort(PointerIds.begin(), PointerIds.end());
        for (unsigned int PointerId : PointerIds)
        {
            ActivePointers.at(PointerId).IsCanceled = true;
            FireSignal(Runtime, Value, "InputEnded", &ActivePointers.at(PointerId));
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

extern "C" LUI_API int LUI_CALL Lui_PointerInput(LuiRuntime* Runtime, int Id, int Phase, int Device,
    unsigned int PointerId, double X, double Y) {
    if (!CheckOwner(Runtime) || Runtime->BackendFailed || Phase < 0 || Phase > 3 || Device < 0 || Device > 3 ||
        (Phase != 3 && (!std::isfinite(X) || !std::isfinite(Y)))) return 0;
    BackendEvent Event{BackendEvent::Kind::PointerInput, Id};
    Event.Phase = Phase;
    Event.Device = Device;
    Event.PointerId = PointerId;
    Event.X = X;
    Event.Y = Y;
    const int Deferred = QueueBackendEventIfBusy(Runtime, std::move(Event));
    if (Deferred >= 0) return Deferred;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->Destroyed ||
        !LuiSchema::IsA(Found->second->ClassName, "GuiObject")) return 0;
    Node* Value = Found->second.get();
    auto Active = Value->ActivePointers.find(PointerId);
    static const char* Devices[] = {"Mouse", "Pen", "Touch", "Touchpad"};
    if (Phase == 0) {
        if (!CanReceiveInput(Runtime, Value)) return 0;
        if (Active == Value->ActivePointers.end()) {
            PointerInputValue Input{PointerId, Devices[Device], {X, Y}};
            Value->ActivePointers.emplace(PointerId, Input);
            FireSignal(Runtime, Value, "InputBegan", &Input);
        }
    } else if (Phase == 1) {
        if (!CanReceiveInput(Runtime, Value)) return 0;
        PointerInputValue Input{PointerId, Devices[Device], {X, Y}};
        if (Active != Value->ActivePointers.end()) Active->second = Input;
        FireSignal(Runtime, Value, "InputChanged", &Input);
    } else {
        if (Active == Value->ActivePointers.end()) return 0;
        PointerInputValue Input = Active->second;
        if (Phase == 2) Input.Position = {X, Y};
        else Input.IsCanceled = true;
        Value->ActivePointers.erase(Active);
        FireSignal(Runtime, Value, "InputEnded", &Input);
    }
    FlushLayout(Runtime);
    return 1;
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

