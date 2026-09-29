#include "LuiRuntime.h"

#include <cstdio>
#include <cstring>

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:Conformance] %s\n", Message);
    return Condition ? 0 : 1;
}

int main() {
    LuiRuntime* Runtime = Lui_Create();
    if (!Runtime) return Check(false, "could not create runtime");
    const char* Script = R"(
        Window = Instance.new("Window")
        Input = Instance.new("TextBox", {Text = "hello", Parent = Window})
        Check = Instance.new("CheckBox", {Text = "Agree", Parent = Window})
        Slider = Instance.new("Slider", {Minimum = 10, Maximum = 20, Value = 15, Parent = Window})
        Progress = Instance.new("ProgressBar", {Minimum = 0, Maximum = 10, Value = 5, Parent = Window})
        assert(Input.Text == "hello" and Check.Checked == false)
        assert(Slider.Value == 15 and Progress.Value == 5)
        assert(not pcall(function() Input.Text = 42 end))
        local HighRange = Instance.new("Slider", {Minimum = 200, Maximum = 300, Value = 250})
        local LowRange = Instance.new("ProgressBar", {Minimum = -100, Maximum = -50, Value = -75})
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
        ActivationEvents = 0
        Input.TextChanged:Connect(function() TextEvents += 1 end)
        Check.CheckedChanged:Connect(function() CheckEvents += 1 end)
        Slider.ValueChanged:Connect(function() ValueEvents += 1 end)
        Input.Focused:Connect(function() FocusEvents += 1 end)
        Check.Activated:Connect(function() ActivationEvents += 1 end)
        Window.Visible = true
    )";
    int ScriptStatus = Lui_RunScript(Runtime, Script, "Conformance");
    int Failures = Check(ScriptStatus == 1, Lui_GetLastError(Runtime));
    Failures += Check(std::strstr(Lui_GetSchemaJson(), "\"name\":\"TextBox\"") != nullptr, "reflection omitted TextBox");
    Failures += Check(Lui_TextChanged(Runtime, 2, "typed") == 1, "TextBox input was rejected");
    Failures += Check(Lui_CheckedChanged(Runtime, 3, 1) == 1, "CheckBox input was rejected");
    Failures += Check(Lui_ValueChanged(Runtime, 4, 17.5) == 1, "Slider input was rejected");
    Failures += Check(Lui_FocusChanged(Runtime, 2, 1) == 1, "focus input was rejected");
    Failures += Check(Lui_Activate(Runtime, 3) == 1, "CheckBox activation was rejected");
    ScriptStatus = Lui_RunScript(Runtime,
        "assert(Input.Text == 'typed' and TextEvents == 1); "
        "assert(Check.Checked and CheckEvents == 1 and ActivationEvents == 1); "
        "assert(Slider.Value == 17.5 and ValueEvents == 1); "
        "assert(Input.IsFocused and FocusEvents == 1); "
        "Check.Enabled = false; Input.Enabled = false", "Assertions");
    Failures += Check(ScriptStatus == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_Activate(Runtime, 3) == 0, "disabled CheckBox activated");
    Failures += Check(Lui_TextChanged(Runtime, 2, "rejected") == 0, "disabled TextBox accepted input");
    Failures += Check(Lui_FocusChanged(Runtime, 2, 1) == 0, "disabled TextBox accepted focus");
    ScriptStatus = Lui_RunScript(Runtime, "Check.Enabled = true; Input.Enabled = true; Window.Visible = false", "Hide");
    Failures += Check(ScriptStatus == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_Activate(Runtime, 3) == 0, "hidden CheckBox activated");
    Failures += Check(Lui_ValueChanged(Runtime, 4, 19) == 0, "hidden Slider accepted input");
    ScriptStatus = Lui_RunScript(Runtime, "assert(Input.Text == 'typed' and Slider.Value == 17.5)", "RejectedInput");
    Failures += Check(ScriptStatus == 1, Lui_GetLastError(Runtime));
    Lui_Destroy(Runtime);
    if (!Failures) std::puts("[LUI:Conformance] Foundation 1 control semantics passed");
    return Failures ? 1 : 0;
}
