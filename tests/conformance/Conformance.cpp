#include "LuiRuntime.h"

#include <cstdio>
#include <cstring>
#include <limits>

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:Conformance] %s\n", Message);
    return Condition ? 0 : 1;
}

int main() {
    LuiRuntime* Runtime = Lui_Create();
    if (!Runtime) return Check(false, "could not create runtime");
    const char* Script = R"(
        Window = Instance.new("Window")
        Input = Instance.new("TextBox", {
            Text = "hello", AccessibilityLabel = "Name",
            AccessibilityDescription = "Enter a name", Parent = Window,
        })
        Check = Instance.new("CheckBox", {Text = "Agree", Parent = Window})
        Slider = Instance.new("Slider", {Minimum = 10, Maximum = 20, Value = 15, Parent = Window})
        Progress = Instance.new("ProgressBar", {Minimum = 0, Maximum = 10, Value = 5, Parent = Window})
        assert(Input.Text == "hello" and Check.Checked == false)
        assert(Input.AccessibilityLabel == "Name" and Input.AccessibilityDescription == "Enter a name")
        assert(Check.AccessibilityLabel == "" and not pcall(function() Input.AccessibilityLabel = 42 end))
        assert(Window.Destroy ~= nil and Input.Focused ~= nil)
        assert(Window.Focused == nil and Progress.Activated == nil)
        assert(not pcall(function() Window.Parent = Input end))
        assert(not pcall(function() Input.Parent = Check end))
        assert(Input.Parent == Window)
        assert(Slider.Value == 15 and Progress.Value == 5)
        assert(not pcall(function() Input.Text = 42 end))
        local HighRange = Instance.new("Slider", {Minimum = 200, Maximum = 300, Value = 250})
        local LowRange = Instance.new("ProgressBar", {Minimum = -100, Maximum = -50, Value = -75})
        SecondInput = Instance.new("TextBox", {Parent = Window})
        local InputClone = Input:Clone()
        assert(InputClone.AccessibilityLabel == "Name" and InputClone.AccessibilityDescription == "Enter a name")
        InputClone:Destroy()
        assert(HighRange.Value == 250 and LowRange.Value == -75)
        assert(Input:IsA("GuiObject") and not Progress:IsA("GuiButton"))
        assert(not pcall(function() Slider.Minimum = 30 end))
        local Windows = app:GetService("WindowService")
        assert(Windows == app:GetService("WindowService"))
        assert(#Windows:GetWindows() == 1)
        local Platform = app:GetService("PlatformService")
        assert(Platform.BackendName == "headless" and not Platform:Supports("WindowBackdrop.Mica"))
        TextEvents = 0
        CheckEvents = 0
        ValueEvents = 0
        FocusEvents = 0
        FocusLostEvents = 0
        SecondFocusEvents = 0
        SecondFocusLostEvents = 0
        FocusSequence = ""
        InputExitSequence = ""
        ActivationEvents = 0
        EnterEvents = 0
        LeaveEvents = 0
        PointerBegins = 0
        PointerChanges = 0
        PointerEnds = 0
        CanceledEnds = 0
        ReleasedEnds = 0
        KeyBegins = 0
        KeyChanges = 0
        KeyEnds = 0
        KeySequence = ""
        Input.TextChanged:Connect(function() TextEvents += 1 end)
        Check.CheckedChanged:Connect(function() CheckEvents += 1 end)
        Slider.ValueChanged:Connect(function() ValueEvents += 1 end)
        Input.Focused:Connect(function() FocusEvents += 1; FocusSequence ..= "A+ " end)
        Input.FocusLost:Connect(function()
            FocusLostEvents += 1
            FocusSequence ..= "A- "
            InputExitSequence ..= "F"
        end)
        SecondInput.Focused:Connect(function() SecondFocusEvents += 1; FocusSequence ..= "B+ " end)
        SecondInput.FocusLost:Connect(function() SecondFocusLostEvents += 1; FocusSequence ..= "B- " end)
        Check.Activated:Connect(function() ActivationEvents += 1 end)
        Input.MouseEnter:Connect(function() EnterEvents += 1 end)
        Input.MouseLeave:Connect(function() LeaveEvents += 1; InputExitSequence ..= "H" end)
        Input.InputBegan:Connect(function(Event)
            if Event.Device == "Keyboard" then
                assert(Event.Key ~= "" and not Event.IsRepeat)
                assert(not pcall(function() Event.Key = "other" end))
                KeyBegins += 1
                KeySequence ..= Event.Key .. "+ "
                return
            end
            assert(Event.PointerId > 0 and Event.Position.X >= 0)
            assert(not pcall(function() Event.Device = "Touch" end))
            PointerBegins += 1
        end)
        Input.InputChanged:Connect(function(Event)
            if Event.Device == "Keyboard" then
                assert(Event.IsRepeat)
                KeyChanges += 1
                KeySequence ..= Event.Key .. "* "
                return
            end
            PointerChanges += 1
            LastMoved = Event
        end)
        Input.InputEnded:Connect(function(Event)
            if Event.Device == "Keyboard" then
                assert(not Event.IsRepeat)
                KeyEnds += 1
                KeySequence ..= Event.Key .. "- "
                InputExitSequence ..= "K"
                return
            end
            PointerEnds += 1
            if Event.IsCanceled then CanceledEnds += 1 else ReleasedEnds += 1 end
            LastEnded = Event
            InputExitSequence ..= "P"
        end)
        Window.Visible = true
    )";
    int ScriptStatus = Lui_RunScript(Runtime, Script, "Conformance");
    int Failures = Check(ScriptStatus == 1, Lui_GetLastError(Runtime));
    Failures += Check(std::strstr(Lui_GetSchemaJson(), "\"name\":\"TextBox\"") != nullptr, "reflection omitted TextBox");
    Failures += Check(std::strstr(Lui_GetSchemaJson(), "\"schemaVersion\":2") != nullptr &&
        std::strstr(Lui_GetSchemaJson(), "\"parentRule\":\"visual\"") != nullptr &&
        std::strstr(Lui_GetSchemaJson(), "\"acceptsChildren\":true") != nullptr,
        "structured reflection metadata is incomplete");
    Failures += Check(Lui_TextChanged(Runtime, 2, "typed") == 1, "TextBox input was rejected");
    Failures += Check(Lui_CheckedChanged(Runtime, 3, 1) == 1, "CheckBox input was rejected");
    Failures += Check(Lui_ValueChanged(Runtime, 4, 17.5) == 1, "Slider input was rejected");
    Failures += Check(Lui_FocusChanged(Runtime, 2, 1) == 1, "focus input was rejected");
    Failures += Check(Lui_FocusChanged(Runtime, 8, 1) == 1, "focus transfer was rejected");
    Failures += Check(Lui_FocusChanged(Runtime, 2, 1) == 1, "focus return was rejected");
    Failures += Check(Lui_FocusChanged(Runtime, 2, 1) == 1, "duplicate focus was rejected");
    Failures += Check(Lui_KeyInput(Runtime, 2, 0, "A") == 1, "key press was rejected");
    Failures += Check(Lui_KeyInput(Runtime, 2, 1, "A") == 1, "key repeat was rejected");
    Failures += Check(Lui_KeyInput(Runtime, 2, 0, "A") == 1, "duplicate key press was rejected");
    Failures += Check(Lui_KeyInput(Runtime, 2, 2, "A") == 1, "key release was rejected");
    Failures += Check(Lui_KeyInput(Runtime, 2, 2, "A") == 0, "unpaired key release was accepted");
    Failures += Check(Lui_KeyInput(Runtime, 2, 1, "A") == 0, "unpaired repeat was accepted");
    Failures += Check(Lui_KeyInput(Runtime, 2, 0, "Unknown") == 0, "unknown key was accepted");
    Failures += Check(Lui_KeyInput(Runtime, 2, 0, nullptr) == 0, "null key was accepted");
    Failures += Check(Lui_Activate(Runtime, 3) == 1, "CheckBox activation was rejected");
    Failures += Check(Lui_HoverChanged(Runtime, 2, 1) == 1, "TextBox hover was rejected");
    Failures += Check(Lui_HoverChanged(Runtime, 2, 1) == 1, "duplicate hover was rejected");
    Failures += Check(Lui_HoverChanged(Runtime, 2, 0) == 1, "TextBox hover exit was rejected");
    Failures += Check(Lui_HoverChanged(Runtime, 2, 0) == 1, "duplicate hover exit was rejected");
    Failures += Check(Lui_HoverChanged(Runtime, 2, 1) == 1, "second TextBox hover was rejected");
    Failures += Check(Lui_PointerInput(Runtime, 2, 0, 0, 17, 4, 8) == 1, "pointer press was rejected");
    Failures += Check(Lui_PointerInput(Runtime, 2, 0, 0, 17, 4, 8) == 1, "duplicate press was rejected");
    Failures += Check(Lui_PointerInput(Runtime, 2, 1, 0, 17, 6, 9) == 1, "pointer move was rejected");
    Failures += Check(Lui_PointerInput(Runtime, 2, 2, 0, 17, 7, 10) == 1, "pointer release was rejected");
    Failures += Check(Lui_PointerInput(Runtime, 2, 2, 0, 17, 7, 10) == 0, "unpaired release was accepted");
    Failures += Check(Lui_PointerInput(Runtime, 2, 1, 0, 99, 2, 3) == 1, "unpressed pointer movement was rejected");
    ScriptStatus = Lui_RunScript(Runtime,
        "assert(PointerBegins == 1 and PointerChanges == 2 and PointerEnds == 1); "
        "assert(LastMoved.Device == 'Mouse' and LastMoved.Position.X == 2 and LastMoved.Position.Y == 3); "
        "assert(LastEnded.PointerId == 17 and LastEnded.Position.X == 7 and LastEnded.Position.Y == 10); "
        "assert(not LastEnded.IsCanceled and ReleasedEnds == 1 and CanceledEnds == 0)",
        "PointerPayload");
    Failures += Check(ScriptStatus == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_PointerInput(Runtime, 2, 0, 2, 18, 1, 2) == 1, "touch press was rejected");
    Failures += Check(Lui_PointerInput(Runtime, 2, 3, 2, 18, 0, 0) == 1, "touch cancel was rejected");
    Failures += Check(Lui_PointerInput(Runtime, 2, 3, 2, 18, 0, 0) == 0, "duplicate cancel was accepted");
    Failures += Check(Lui_PointerInput(Runtime, 2, 0, 1, 19, 3, 4) == 1, "pen press was rejected");
    Failures += Check(Lui_PointerInput(Runtime, 2, 0, 4, 20, 0, 0) == 0, "unknown pointer device was accepted");
    Failures += Check(Lui_PointerInput(Runtime, 2, 4, 0, 20, 0, 0) == 0, "unknown pointer phase was accepted");
    Failures += Check(Lui_PointerInput(Runtime, 2, 0, 0, 20,
        std::numeric_limits<double>::quiet_NaN(), 0) == 0, "nonfinite pointer position was accepted");
    ScriptStatus = Lui_RunScript(Runtime,
        "assert(Input.Text == 'typed' and TextEvents == 1); "
        "assert(Check.Checked and CheckEvents == 1 and ActivationEvents == 1); "
        "assert(Slider.Value == 17.5 and ValueEvents == 1); "
        "assert(Input.IsFocused and not SecondInput.IsFocused); "
        "assert(FocusEvents == 2 and FocusLostEvents == 1 and SecondFocusEvents == 1 and SecondFocusLostEvents == 1); "
        "assert(FocusSequence == 'A+ A- B+ B- A+ '); "
        "assert(EnterEvents == 2 and LeaveEvents == 1); "
        "assert(PointerBegins == 3 and PointerChanges == 2 and PointerEnds == 2); "
        "assert(ReleasedEnds == 1 and CanceledEnds == 1); "
        "assert(KeyBegins == 1 and KeyChanges == 2 and KeyEnds == 1); "
        "assert(KeySequence == 'A+ A* A* A- '); "
        "InputExitSequence = ''; "
        "Check.Enabled = false", "Assertions");
    Failures += Check(ScriptStatus == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_KeyInput(Runtime, 2, 0, "Tab") == 1, "active key before disable was rejected");
    ScriptStatus = Lui_RunScript(Runtime, "Input.Enabled = false", "Disable");
    Failures += Check(ScriptStatus == 1, Lui_GetLastError(Runtime));
    ScriptStatus = Lui_RunScript(Runtime,
        "assert(LeaveEvents == 2 and PointerEnds == 3 and LastEnded.Device == 'Pen' and LastEnded.PointerId == 19); "
        "assert(LastEnded.IsCanceled and CanceledEnds == 2); "
        "assert(not Input.IsFocused and FocusLostEvents == 2 and InputExitSequence == 'FHPK'); "
        "assert(KeyEnds == 2 and KeySequence == 'A+ A* A* A- Tab+ Tab- ')",
        "DisabledInputExit");
    Failures += Check(ScriptStatus == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_Activate(Runtime, 3) == 0, "disabled CheckBox activated");
    Failures += Check(Lui_TextChanged(Runtime, 2, "rejected") == 0, "disabled TextBox accepted input");
    Failures += Check(Lui_FocusChanged(Runtime, 2, 1) == 0, "disabled TextBox accepted focus");
    Failures += Check(Lui_FocusChanged(Runtime, 2, 0) == 1, "duplicate focus loss was rejected");
    Failures += Check(Lui_HoverChanged(Runtime, 2, 1) == 0, "disabled TextBox accepted hover");
    Failures += Check(Lui_PointerInput(Runtime, 2, 0, 0, 20, 0, 0) == 0, "disabled TextBox accepted press");
    Failures += Check(Lui_KeyInput(Runtime, 2, 0, "B") == 0, "disabled TextBox accepted key");
    Failures += Check(Lui_KeyInput(Runtime, 2, 2, "Tab") == 0, "late key release after disable was accepted");
    ScriptStatus = Lui_RunScript(Runtime, "Check.Enabled = true; Input.Enabled = true", "Enable");
    Failures += Check(ScriptStatus == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_HoverChanged(Runtime, 2, 1) == 1, "re-enabled TextBox hover was rejected");
    Failures += Check(Lui_FocusChanged(Runtime, 2, 1) == 1, "re-enabled TextBox focus was rejected");
    Failures += Check(Lui_PointerInput(Runtime, 2, 0, 0, 20, 5, 6) == 1, "re-enabled TextBox press was rejected");
    Failures += Check(Lui_PointerInput(Runtime, 2, 0, 2, 23, 7, 8) == 1, "second active pointer was rejected");
    Failures += Check(Lui_KeyInput(Runtime, 2, 0, "Enter") == 1, "active key before hide was rejected");
    ScriptStatus = Lui_RunScript(Runtime,
        "InputExitSequence = ''; Window.Visible = false; "
        "assert(LeaveEvents == 3 and PointerEnds == 5 and LastEnded.PointerId == 23); "
        "assert(LastEnded.IsCanceled and CanceledEnds == 4); "
        "assert(InputExitSequence == 'FHPPK'); "
        "assert(not Input.IsFocused and FocusLostEvents == 3 and KeyEnds == 3)",
        "Hide");
    Failures += Check(ScriptStatus == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_Activate(Runtime, 3) == 0, "hidden CheckBox activated");
    Failures += Check(Lui_ValueChanged(Runtime, 4, 19) == 0, "hidden Slider accepted input");
    Failures += Check(Lui_HoverChanged(Runtime, 2, 1) == 0, "hidden TextBox accepted hover");
    Failures += Check(Lui_FocusChanged(Runtime, 2, 1) == 0, "hidden TextBox accepted focus");
    Failures += Check(Lui_PointerInput(Runtime, 2, 0, 0, 21, 0, 0) == 0, "hidden TextBox accepted press");
    Failures += Check(Lui_KeyInput(Runtime, 2, 0, "B") == 0, "hidden TextBox accepted key");
    ScriptStatus = Lui_RunScript(Runtime, "Window.Visible = true", "Show");
    Failures += Check(ScriptStatus == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_HoverChanged(Runtime, 2, 1) == 1, "visible TextBox hover was rejected");
    Failures += Check(Lui_FocusChanged(Runtime, 2, 1) == 1, "visible TextBox focus was rejected");
    Failures += Check(Lui_PointerInput(Runtime, 2, 0, 0, 21, 1, 1) == 1, "visible TextBox press was rejected");
    Failures += Check(Lui_KeyInput(Runtime, 2, 0, "F12") == 1, "function key was rejected");
    ScriptStatus = Lui_RunScript(Runtime,
        "local HiddenFrame = Instance.new('Frame', {Visible = false, Parent = Window}); "
        "InputExitSequence = ''; Input.Parent = HiddenFrame; "
        "assert(InputExitSequence == 'FHPK'); "
        "assert(Input.Text == 'typed' and Slider.Value == 17.5 and EnterEvents == 4 and LeaveEvents == 4); "
        "assert(not Input.IsFocused and FocusEvents == 4 and FocusLostEvents == 4); "
        "assert(PointerBegins == 6 and PointerChanges == 2 and PointerEnds == 6 and CanceledEnds == 5); "
        "Input:Destroy()", "RejectedInput");
    Failures += Check(ScriptStatus == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_HoverChanged(Runtime, 2, 1) == 0, "destroyed TextBox accepted hover");
    Failures += Check(Lui_PointerInput(Runtime, 2, 0, 0, 22, 0, 0) == 0, "destroyed TextBox accepted press");
    Failures += Check(Lui_KeyInput(Runtime, 2, 0, "A") == 0, "destroyed TextBox accepted key");
    Lui_Destroy(Runtime);

    LuiRuntime* FocusRuntime = Lui_Create();
    Failures += Check(FocusRuntime != nullptr, "could not create focus ordering runtime");
    if (FocusRuntime) {
        ScriptStatus = Lui_RunScript(FocusRuntime, R"(
            Window = Instance.new("Window")
            First = Instance.new("TextBox", {Parent = Window})
            Second = Instance.new("TextBox", {Parent = Window})
            Sequence = ""
            First.FocusLost:Connect(function() Sequence ..= "lost " end)
            First.InputEnded:Connect(function(Event)
                if Event.Device == "Keyboard" then Sequence ..= Event.Key .. "- " end
            end)
            Second.Focused:Connect(function() Sequence ..= "focused " end)
            Window.Visible = true
        )", "KeyFocusOrder");
        Failures += Check(ScriptStatus == 1, Lui_GetLastError(FocusRuntime));
        Failures += Check(Lui_FocusChanged(FocusRuntime, 2, 1) == 1, "initial key focus was rejected");
        Failures += Check(Lui_KeyInput(FocusRuntime, 2, 0, "Enter") == 1, "key before focus transfer was rejected");
        Failures += Check(Lui_KeyInput(FocusRuntime, 2, 0, "Space") == 1, "second key before focus transfer was rejected");
        Failures += Check(Lui_FocusChanged(FocusRuntime, 3, 1) == 1, "key focus transfer was rejected");
        ScriptStatus = Lui_RunScript(FocusRuntime, "assert(Sequence == 'lost Enter- Space- focused ')", "KeyFocusOrderCheck");
        Failures += Check(ScriptStatus == 1, Lui_GetLastError(FocusRuntime));
        Failures += Check(Lui_KeyInput(FocusRuntime, 2, 2, "Enter") == 0, "late key release after focus transfer was accepted");
        Failures += Check(Lui_KeyInput(FocusRuntime, 2, 2, "Space") == 0, "late second key release was accepted");
        Lui_Destroy(FocusRuntime);
    }
    if (!Failures) std::puts("[LUI:Conformance] Foundation 1 control semantics passed");
    return Failures ? 1 : 0;
}
