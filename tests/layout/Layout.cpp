#include "LuiRuntime.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

struct Bounds {
    double X = 0;
    double Y = 0;
    double Width = 0;
    double Height = 0;
};

struct LayoutBackend {
    std::unordered_map<int, Bounds> Arranged;
    int Created = 0;
};

static void LUI_CALL OnCreate(void* Context, int, const char*) {
    ++static_cast<LayoutBackend*>(Context)->Created;
}

static void LUI_CALL OnProperty(void*, int, const char*, const char*) {}
static void LUI_CALL OnParent(void*, int, int) {}
static void LUI_CALL OnDestroy(void*, int) {}

static void LUI_CALL OnArrange(void* Context, int Id, double X, double Y, double Width, double Height) {
    static_cast<LayoutBackend*>(Context)->Arranged[Id] = {X, Y, Width, Height};
}

static bool Near(double Actual, double Expected) {
    return std::abs(Actual - Expected) < 0.001;
}

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:LayoutTest] %s\n", Message);
    return Condition ? 0 : 1;
}

int main() {
    LuiRuntime* Runtime = Lui_Create();
    if (!Runtime) return Check(false, "could not create runtime");
    LayoutBackend Backend;
    Lui_SetBackend(Runtime, {&Backend, OnCreate, OnProperty, OnParent, OnArrange, OnDestroy});
    const char* Script = R"(
        Window = Instance.new("Window", {Size = UDim2.fromOffset(800, 600)})
        Frame = Instance.new("Frame", {
            Position = UDim2.fromScale(0.5, 0.5),
            AnchorPoint = Vector2.new(0.5, 0.5),
            Size = UDim2.fromOffset(400, 300),
            Parent = Window,
        })
        Padding = Instance.new("UIPadding", {
            PaddingLeft = UDim.new(0, 10), PaddingRight = UDim.new(0, 10),
            PaddingTop = UDim.new(0, 20), Parent = Frame,
        })
        List = Instance.new("UIListLayout", {Padding = UDim.new(0, 5), Parent = Frame})
        Label = Instance.new("TextLabel", {LayoutOrder = 2, Size = UDim2.fromOffset(100, 30), Parent = Frame})
        Button = Instance.new("TextButton", {LayoutOrder = 1, Size = UDim2.fromOffset(100, 20), Parent = Frame})
        assert(Frame.AbsolutePosition.X == 200)
        assert(Frame.AbsoluteSize.Y == 300)
        assert(Button.AbsolutePosition.Y == 170)
        assert(Label.AbsolutePosition.Y == 195)
        assert(not pcall(function() Frame.AbsoluteSize = Vector2.new(1, 1) end))
        assert(not pcall(function() Instance.new("UIListLayout", {Parent = Frame}) end))
    )";
    int Failures = Check(Lui_RunScript(Runtime, Script, "Layout") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Backend.Created == 4, "layout components must not create native controls");
    Failures += Check(Near(Backend.Arranged[2].X, 200) && Near(Backend.Arranged[2].Y, 150), "anchor and position mismatch");
    Failures += Check(Near(Backend.Arranged[6].X, 210) && Near(Backend.Arranged[6].Y, 170), "padding or list order mismatch");
    Failures += Check(Near(Backend.Arranged[5].Y, 195), "list gap mismatch");
    Failures += Check(Lui_RunScript(Runtime, "Frame.Position = UDim2.fromOffset(300, 250); assert(Frame.AbsolutePosition.X == 100)", "Reposition") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Near(Backend.Arranged[6].X, 110), "reposition did not propagate to descendants");
    Lui_Destroy(Runtime);
    if (!Failures) std::puts("[LUI:LayoutTest] Foundation 1 layout semantics passed");
    return Failures ? 1 : 0;
}
