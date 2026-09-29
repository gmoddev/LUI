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
    std::unordered_map<int, std::string> Classes;
    std::unordered_map<int, int> Parents;
    std::unordered_map<int, double> Widths;
    std::vector<std::string> Events;
    int Destroyed = 0;
};

static void LUI_CALL OnCreate(void* Context, int Id, const char* ClassName) {
    static_cast<TestBackend*>(Context)->Classes[Id] = ClassName;
}

static void LUI_CALL OnProperty(void* Context, int Id, const char* Name, const char*) {
    static_cast<TestBackend*>(Context)->Events.push_back(std::to_string(Id) + ":" + Name);
}

static void LUI_CALL OnParent(void* Context, int Id, int ParentId) {
    static_cast<TestBackend*>(Context)->Parents[Id] = ParentId;
    static_cast<TestBackend*>(Context)->Events.push_back(std::to_string(Id) + ":Parent");
}

static void LUI_CALL OnArrange(void* Context, int Id, double, double, double Width, double) {
    static_cast<TestBackend*>(Context)->Widths[Id] = Width;
}

static void LUI_CALL OnDestroy(void* Context, int) {
    ++static_cast<TestBackend*>(Context)->Destroyed;
}

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:Test] %s\n", Message);
    return Condition ? 0 : 1;
}

int main() {
    LuiRuntime* Runtime = Lui_Create();
    if (!Runtime) return Check(false, "could not create runtime");
    TestBackend Backend;
    Lui_SetBackend(Runtime, {&Backend, OnCreate, OnProperty, OnParent, OnArrange, OnDestroy});
    const char* Script = R"(
        Window = Instance.new("Window", {Title = "Hello", Size = UDim2.fromOffset(800, 600)})
        Frame = Instance.new("Frame", {Name = "Root", Size = UDim2.new(0.5, 0, 1, 0), Parent = Window})
        Button = Instance.new("TextButton", {Text = "Click", Size = UDim2.fromOffset(120, 36), Parent = Frame})
        Label = Instance.new("TextLabel", {Name = "Status", Text = "Ready", Size = UDim2.fromOffset(80, 20), Parent = Frame})
        assert(Button.Parent == Frame)
        assert(Frame:FindFirstChild("TextButton") == Button)
        assert(Frame:FindFirstChild("Status") == Label)
        assert(#Frame:GetChildren() == 2)
        assert(Button:IsA("GuiButton"))
        assert(Window.ClassName == "Window")
        assert(UDim.new(0, 2).Offset == 2)
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
    Failures += Check(std::abs(Backend.Widths[2] - 400.0) < 0.01, "UDim2 layout mismatch");
    Failures += Check(Lui_Activate(Runtime, 3) == 1, "button activation failed");
    Failures += Check(Lui_Pump(Runtime) == 1, "deferred callback was not pumped");
    Failures += Check(Lui_RunScript(Runtime, "assert(Count == 110); Frame:Destroy(); Frame:Destroy(); assert(#Window:GetChildren() == 0)", "Assertions") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_Activate(Runtime, 3) == 0, "destroyed button remained active");
    Failures += Check(Backend.Destroyed == 3, "destroyed descendants mismatch");
    Lui_Destroy(Runtime);
    std::ifstream ExampleFile(LUI_EXAMPLE_PATH);
    Failures += Check(ExampleFile.good(), "example file was not found");
    if (ExampleFile) {
        const std::string Source{std::istreambuf_iterator<char>{ExampleFile}, std::istreambuf_iterator<char>{}};
        LuiRuntime* ExampleRuntime = Lui_Create();
        Failures += Check(Lui_RunScript(ExampleRuntime, Source.c_str(), "hello.luau") == 1, Lui_GetLastError(ExampleRuntime));
        Lui_Destroy(ExampleRuntime);
    }
    if (!Failures) std::puts("[LUI:Test] Foundation 0 headless semantics passed");
    return Failures ? 1 : 0;
}
