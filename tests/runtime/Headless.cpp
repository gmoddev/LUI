#include "LuiRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

struct TestBackend {
    LuiRuntime* Runtime = nullptr;
    std::unordered_map<int, std::string> Classes;
    std::unordered_map<int, int> Parents;
    std::unordered_map<int, double> Widths;
    std::unordered_map<int, std::string> Text;
    std::vector<std::string> Events;
    std::vector<std::string> Logs;
    int Destroyed = 0;
    bool TriggerPropertyEvent = false;
    bool TriggerArrangeEvent = false;
    bool InBackendCallback = false;
    bool ReenteredVm = false;
    int DeferredEventStatus = 0;
    std::string FailOperation;
    int FailCreateId = 0;
};

static int LUI_CALL OnCreate(void* Context, int Id, const char* ClassName) {
    auto* Backend = static_cast<TestBackend*>(Context);
    if (Backend->FailOperation == "Create" || Backend->FailCreateId == Id) {
        Lui_ReportBackendError(Backend->Runtime, "synthetic create failure");
        return 0;
    }
    Backend->Classes[Id] = ClassName;
    return 1;
}

static int LUI_CALL OnProperty(void* Context, int Id, const char* Name, const char* Value) {
    auto* Backend = static_cast<TestBackend*>(Context);
    if (Backend->FailOperation == "Property") {
        Lui_ReportBackendError(Backend->Runtime, "synthetic property failure");
        return 0;
    }
    Backend->Events.push_back(std::to_string(Id) + ":" + Name);
    if (std::string(Name) == "Text") Backend->Text[Id] = Value;
    if (Backend->TriggerPropertyEvent && Id == 3 && std::string(Name) == "Text" && std::string(Value) == "Reentrant") {
        Backend->TriggerPropertyEvent = false;
        Backend->InBackendCallback = true;
        Backend->DeferredEventStatus = Lui_Activate(Backend->Runtime, Id);
        Backend->InBackendCallback = false;
    }
    return 1;
}

static int LUI_CALL OnParent(void* Context, int Id, int ParentId) {
    auto* Backend = static_cast<TestBackend*>(Context);
    if (Backend->FailOperation == "Parent") {
        Lui_ReportBackendError(Backend->Runtime, "synthetic parent failure");
        return 0;
    }
    Backend->Parents[Id] = ParentId;
    Backend->Events.push_back(std::to_string(Id) + ":Parent");
    return 1;
}

static int LUI_CALL OnArrange(void* Context, int Id, double, double, double Width, double, int) {
    auto* Backend = static_cast<TestBackend*>(Context);
    if (Backend->FailOperation == "Arrange") {
        Lui_ReportBackendError(Backend->Runtime, "synthetic arrange failure");
        return 0;
    }
    Backend->Widths[Id] = Width;
    if (Backend->TriggerArrangeEvent && Id == 3) {
        Backend->TriggerArrangeEvent = false;
        Backend->InBackendCallback = true;
        Backend->DeferredEventStatus = Lui_Activate(Backend->Runtime, Id);
        Backend->InBackendCallback = false;
    }
    return 1;
}

static int LUI_CALL OnDestroy(void* Context, int) {
    auto* Backend = static_cast<TestBackend*>(Context);
    ++Backend->Destroyed;
    if (Backend->FailOperation == "Destroy") {
        Lui_ReportBackendError(Backend->Runtime, "synthetic destroy failure");
        return 0;
    }
    return 1;
}

static void LUI_CALL OnLog(void* Context, const char* Level, const char* Message) {
    auto* Backend = static_cast<TestBackend*>(Context);
    Backend->Logs.push_back(std::string(Level) + ":" + Message);
    if (Backend->InBackendCallback && std::string(Message) == "[LUI:Reentrancy] activation")
        Backend->ReenteredVm = true;
}

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:Test] %s\n", Message);
    return Condition ? 0 : 1;
}

static int CheckBackendFailure(const char* Operation, const char* Source) {
    LuiRuntime* Runtime = Lui_Create();
    TestBackend Backend;
    Backend.Runtime = Runtime;
    Backend.FailOperation = Operation;
    Lui_SetBackend(Runtime, {&Backend, OnCreate, OnProperty, OnParent, OnArrange, OnDestroy});
    Lui_SetLogCallback(Runtime, &Backend, OnLog);
    const int Status = Lui_RunScript(Runtime, Source, Operation);
    const std::string Error = Lui_GetLastError(Runtime);
    int Failures = Check(Status == 0 && Error.find(std::string(Operation) + " failed") != std::string::npos &&
        Error.find("synthetic") != std::string::npos, "backend failure was not reported to the runtime");
    Failures += Check(Lui_RunScript(Runtime, "print('must not run')", "AfterBackendFailure") == 0,
        "runtime continued after a backend failure");
    if (std::string(Operation) == "Create")
        Failures += Check(Backend.Destroyed == 1, "failed backend creation was not rolled back");
    Backend.FailOperation.clear();
    Lui_Destroy(Runtime);
    return Failures;
}

static int CheckCloneFailure() {
    LuiRuntime* Runtime = Lui_Create();
    TestBackend Backend;
    Backend.Runtime = Runtime;
    Backend.FailCreateId = 3;
    Lui_SetBackend(Runtime, {&Backend, OnCreate, OnProperty, OnParent, OnArrange, OnDestroy});
    Lui_SetLogCallback(Runtime, &Backend, OnLog);
    const int Status = Lui_RunScript(Runtime,
        "local W = Instance.new('Window'); local F = Instance.new('Frame', {Parent = W}); F:Clone()",
        "CloneCreateFailure");
    int Failures = Check(Status == 0 && std::string(Lui_GetLastError(Runtime)).find("Create failed") != std::string::npos,
        "clone creation failure was not propagated");
    Failures += Check(Backend.Destroyed == 1, "failed clone was not rolled back");
    Backend.FailCreateId = 0;
    Lui_Destroy(Runtime);
    return Failures;
}

int main() {
    LuiRuntime* Runtime = Lui_Create();
    if (!Runtime) return Check(false, "could not create runtime");
    TestBackend Backend;
    Backend.Runtime = Runtime;
    Lui_SetBackend(Runtime, {&Backend, OnCreate, OnProperty, OnParent, OnArrange, OnDestroy});
    Lui_SetLogCallback(Runtime, &Backend, OnLog);
    const char* Script = R"(
        Window = Instance.new("Window", {Title = "Hello", Size = UDim2.fromOffset(800, 600)})
        Frame = Instance.new("Frame", {Name = "Root", Size = UDim2.new(0.5, 0, 1, 0), Parent = Window})
        Button = Instance.new("TextButton", {Text = "Click", Size = UDim2.fromOffset(120, 36), Parent = Frame})
        Label = Instance.new("TextLabel", {Name = "Status", Text = "Ready", Size = UDim2.fromOffset(80, 20), Parent = Frame})
        Label.Text = "A"
        Label.Text = "B"
        assert(Button.Parent == Frame)
        assert(Frame:FindFirstChild("TextButton") == Button)
        assert(Frame:FindFirstChild("Status") == Label)
        assert(#Frame:GetChildren() == 2)
        assert(Button:IsA("GuiButton"))
        assert(Window.ClassName == "Window")
        assert(UDim.new(0, 2).Offset == 2)
        assert(not pcall(function() Instance.new = nil end))
        assert(not pcall(function() UDim2.fromOffset = nil end))
        assert(not pcall(function() Button.Size.X.Scale = 1 end))
        local BadParent = pcall(function() Button.Parent = Frame.Changed end)
        assert(not BadParent)
        Count = 0
        local Connection = Button.Activated:Connect(function() Count += 1 end)
        Button.Activated:Connect(function() Count += 10 end)
        Connection:Disconnect()
        task.defer(function() Count += 100 end)
        Frame.Destroying:Connect(function() Frame:Destroy() end)
        Window.Visible = true
    )";
    int Failures = Check(Lui_RunScript(Runtime, Script, "Headless") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Backend.Classes.size() == 4, "expected four native objects");
    Failures += Check(Backend.Parents[2] == 1 && Backend.Parents[3] == 2 && Backend.Parents[4] == 2, "parenting mismatch");
    auto TextEvent = std::find(Backend.Events.begin(), Backend.Events.end(), "3:Text");
    auto ParentEvent = std::find(Backend.Events.begin(), Backend.Events.end(), "3:Parent");
    Failures += Check(TextEvent != Backend.Events.end() && ParentEvent != Backend.Events.end() && TextEvent < ParentEvent, "Parent was not applied last");
    Failures += Check(std::count(Backend.Events.begin(), Backend.Events.end(), "4:Text") == 1, "text changes were not batched");
    Failures += Check(Backend.Text[4] == "B", "batched text did not use final value");
    Failures += Check(std::abs(Backend.Widths[2] - 400.0) < 0.01, "UDim2 layout mismatch");
    int RollbackStatus = Lui_RunScript(Runtime,
        "local Before = #Window:GetChildren(); "
        "assert(not pcall(function() Instance.new('TextButton', {Parent = Button}) end)); "
        "assert(#Window:GetChildren() == Before)", "Rollback");
    Failures += Check(RollbackStatus == 1, Lui_GetLastError(Runtime));
    Failures += Check(Backend.Destroyed == 1, "failed initialization did not destroy native object");
    Backend.TriggerPropertyEvent = true;
    Failures += Check(Lui_RunScript(Runtime,
        "ReentrantCount = 0; "
        "Button.Activated:Connect(function() ReentrantCount += 1; print('[LUI:Reentrancy] activation') end); "
        "Button.Text = 'Reentrant'; local Width = Button.AbsoluteSize.X; assert(Width >= 0 and ReentrantCount == 0)",
        "ReentrantProperty") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Backend.DeferredEventStatus == 1 && !Backend.ReenteredVm,
        "property callback reentered the active VM");
    Failures += Check(Lui_RunScript(Runtime, "assert(ReentrantCount == 1)", "DeferredPropertyEvent") == 1,
        Lui_GetLastError(Runtime));
    Backend.TriggerArrangeEvent = true;
    Failures += Check(Lui_RunScript(Runtime,
        "Button.Size = UDim2.fromOffset(130, 36); assert(ReentrantCount == 1)",
        "ReentrantArrange") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Backend.DeferredEventStatus == 1 && !Backend.ReenteredVm,
        "arrange callback reentered the active VM");
    Failures += Check(Lui_RunScript(Runtime, "assert(ReentrantCount == 2)", "DeferredArrangeEvent") == 1,
        Lui_GetLastError(Runtime));
    Failures += Check(Lui_Activate(Runtime, 3) == 1, "button activation failed");
    Failures += Check(Lui_Pump(Runtime) == 1, "deferred callback was not pumped");
    Failures += Check(Lui_RunScript(Runtime,
        "assert(Count == 130); local Copy = Frame:Clone(); assert(Copy.Parent == nil); "
        "assert(#Copy:GetDescendants() == 2); assert(Copy:GetChildren()[1] ~= Button); "
        "Copy:Destroy(); Frame:Destroy(); Frame:Destroy(); assert(#Window:GetChildren() == 0)",
        "Assertions") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_Activate(Runtime, 3) == 0, "destroyed button remained active");
    Failures += Check(Backend.Destroyed == 7, "destroyed descendants mismatch");
    Failures += Check(Lui_RunScript(Runtime, "print('hello', 42)", "Print") == 1, Lui_GetLastError(Runtime));
    Failures += Check(std::find(Backend.Logs.begin(), Backend.Logs.end(), "Print:hello\t42") != Backend.Logs.end(), "Luau print was not logged");
    Failures += Check(Lui_RunScript(Runtime, "error('expected diagnostic')", "Error") == 0, "expected script error");
    Failures += Check(std::any_of(Backend.Logs.begin(), Backend.Logs.end(),
        [](const std::string& Entry) { return Entry.find("Error:Runtime: ") == 0; }), "Luau error was not logged");
    Lui_Destroy(Runtime);
    Failures += CheckBackendFailure("Create", "Instance.new('Window')");
    Failures += CheckBackendFailure("Property", "Instance.new('Window', {Title = 'Failure'})");
    Failures += CheckBackendFailure("Parent", "local W = Instance.new('Window'); Instance.new('Frame', {Parent = W})");
    Failures += CheckBackendFailure("Arrange", "Instance.new('Window')");
    Failures += CheckBackendFailure("Destroy", "local W = Instance.new('Window'); W:Destroy()");
    Failures += CheckCloneFailure();
    std::ifstream ExampleFile(LUI_EXAMPLE_PATH);
    Failures += Check(ExampleFile.good(), "example file was not found");
    if (ExampleFile) {
        const std::string Source{std::istreambuf_iterator<char>{ExampleFile}, std::istreambuf_iterator<char>{}};
        LuiRuntime* ExampleRuntime = Lui_Create();
        Failures += Check(Lui_RunScript(ExampleRuntime, Source.c_str(), "hello.luau") == 1, Lui_GetLastError(ExampleRuntime));
        Lui_Destroy(ExampleRuntime);
    }
    std::ifstream ControlsFile(LUI_CONTROLS_EXAMPLE_PATH);
    Failures += Check(ControlsFile.good(), "controls example was not found");
    if (ControlsFile) {
        const std::string Source{std::istreambuf_iterator<char>{ControlsFile}, std::istreambuf_iterator<char>{}};
        LuiRuntime* ControlsRuntime = Lui_Create();
        int Status = Lui_RunScript(ControlsRuntime, Source.c_str(), "controls.luau");
        Failures += Check(Status == 1, Lui_GetLastError(ControlsRuntime));
        Lui_Destroy(ControlsRuntime);
    }
    std::ifstream GridFile(LUI_GRID_EXAMPLE_PATH);
    Failures += Check(GridFile.good(), "grid example was not found");
    if (GridFile) {
        const std::string Source{std::istreambuf_iterator<char>{GridFile}, std::istreambuf_iterator<char>{}};
        LuiRuntime* GridRuntime = Lui_Create();
        int Status = Lui_RunScript(GridRuntime, Source.c_str(), "grid.luau");
        Failures += Check(Status == 1, Lui_GetLastError(GridRuntime));
        Lui_Destroy(GridRuntime);
    }
    for (const char* Path : {LUI_WINUI_FOCUS_CASE_PATH, LUI_WINUI_INPUT_CASE_PATH}) {
        std::ifstream CaseFile(Path);
        Failures += Check(CaseFile.good(), "WinUI qualification case was not found");
        if (!CaseFile) continue;
        const std::string Source{std::istreambuf_iterator<char>{CaseFile}, std::istreambuf_iterator<char>{}};
        LuiRuntime* CaseRuntime = Lui_Create();
        int Status = Lui_RunScript(CaseRuntime, Source.c_str(), Path);
        Failures += Check(Status == 1, Lui_GetLastError(CaseRuntime));
        Lui_Destroy(CaseRuntime);
    }
    if (!Failures) std::puts("[LUI:Test] Foundation 0 headless semantics passed");
    return Failures ? 1 : 0;
}
