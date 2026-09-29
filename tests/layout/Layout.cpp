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
    const char* Constraints = R"(
        Limit = Instance.new("UISizeConstraint", {
            MinSize = Vector2.new(120, 25), MaxSize = Vector2.new(180, 35), Parent = Button,
        })
        assert(Limit.MinSize.X == 120 and Limit.MaxSize.Y == 35)
        assert(Button.AbsoluteSize.X == 120 and Button.AbsoluteSize.Y == 25)
        assert(Label.AbsolutePosition.Y == 150)
        assert(not pcall(function() Instance.new("UISizeConstraint", {Parent = Button}) end))
        assert(not pcall(function()
            Instance.new("UISizeConstraint", {
                MinSize = Vector2.new(40, 40), MaxSize = Vector2.new(30, 50), Parent = Frame,
            })
        end))
        assert(not pcall(function() Limit.MaxSize = Vector2.new(100, 20) end))
        assert(not pcall(function() Limit.MinSize = Vector2.new(-1, 0) end))
        assert(Limit.MinSize.X == 120 and Limit.MaxSize.Y == 35)
        Button.Size = UDim2.fromOffset(300, 50)
        assert(Button.AbsoluteSize.X == 180 and Button.AbsoluteSize.Y == 35)
        assert(Label.AbsolutePosition.Y == 160)
        Limit.MaxSize = nil
        assert(Button.AbsoluteSize.X == 300 and Button.AbsoluteSize.Y == 50)
        assert(Limit.MaxSize == nil)
        LimitClone = Limit:Clone()
        assert(LimitClone.MinSize.X == 120 and LimitClone.MaxSize == nil)
        LimitClone:Destroy()
        WindowLimit = Instance.new("UISizeConstraint", {MinSize = Vector2.new(900, 700), Parent = Window})
        assert(Window.AbsoluteSize.X == 900 and Window.AbsoluteSize.Y == 700)
        FrameLimit = Instance.new("UISizeConstraint", {MinSize = Vector2.new(500, 300), Parent = Frame})
        assert(Frame.AbsoluteSize.X == 500 and Frame.AbsolutePosition.X == 50)
    )";
    Failures += Check(Lui_RunScript(Runtime, Constraints, "Constraints") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Backend.Created == 4, "size constraints must not create native controls");
    Failures += Check(Near(Backend.Arranged[6].Width, 300) && Near(Backend.Arranged[6].Height, 50),
        "constrained bounds did not reach the backend");
    Failures += Check(Near(Backend.Arranged[1].Width, 900), "window constraint did not reach the backend");
    const char* GridScript = R"(
        GridHost = Instance.new("Frame", {Size = UDim2.fromOffset(250, 200), Parent = Window})
        Instance.new("UIPadding", {
            PaddingLeft = UDim.new(0, 10), PaddingRight = UDim.new(0, 10),
            PaddingTop = UDim.new(0, 5), Parent = GridHost,
        })
        Grid = Instance.new("UIGridLayout", {
            CellSize = UDim2.fromOffset(100, 30),
            CellPadding = UDim2.fromOffset(10, 5), Parent = GridHost,
        })
        GridA = Instance.new("TextLabel", {
            LayoutOrder = 2, Size = UDim2.fromOffset(5, 5),
            Position = UDim2.fromOffset(500, 500), AnchorPoint = Vector2.new(1, 1), Parent = GridHost,
        })
        GridB = Instance.new("TextLabel", {LayoutOrder = 1, Parent = GridHost})
        GridC = Instance.new("TextLabel", {LayoutOrder = 2, Parent = GridHost})
        assert(GridB.AbsolutePosition.X == 10 and GridB.AbsolutePosition.Y == 5)
        assert(GridA.AbsolutePosition.X == 120 and GridA.AbsolutePosition.Y == 5)
        assert(GridC.AbsolutePosition.X == 10 and GridC.AbsolutePosition.Y == 40)
        assert(GridA.AbsoluteSize.X == 100 and GridA.AbsoluteSize.Y == 30)
        assert(not pcall(function() Instance.new("UIGridLayout", {Parent = GridHost}) end))
        assert(not pcall(function() Instance.new("UIListLayout", {Parent = GridHost}) end))
        assert(not pcall(function() Instance.new("UIGridLayout", {Parent = Frame}) end))
        GridLimit = Instance.new("UISizeConstraint", {MinSize = Vector2.new(120, 40), Parent = GridA})
        assert(GridB.AbsolutePosition.Y == 5 and GridA.AbsolutePosition.Y == 50)
        assert(GridC.AbsolutePosition.Y == 95 and GridA.AbsoluteSize.X == 120)
        GridHost.Size = UDim2.fromOffset(400, 200)
        assert(GridB.AbsolutePosition.X == 10 and GridA.AbsolutePosition.X == 140)
        assert(GridC.AbsolutePosition.X == 270 and GridC.AbsolutePosition.Y == 5)
        Grid.CellPadding = UDim2.fromOffset(20, 10)
        assert(GridA.AbsolutePosition.X == 150 and GridC.AbsolutePosition.Y == 55)
        assert(not pcall(function() Grid.CellPadding = UDim2.fromOffset(-1, 0) end))
        assert(Grid.CellPadding.X.Offset == 20)
        Grid.CellSize = UDim2.fromOffset(150, 30)
        assert(GridA.AbsolutePosition.X == 180 and GridC.AbsolutePosition.Y == 55)
        GridC.LayoutOrder = 0
        assert(GridC.AbsolutePosition.X == 10 and GridB.AbsolutePosition.X == 180)
        assert(GridA.AbsolutePosition.Y == 55)
        GridClone = Grid:Clone()
        assert(GridClone.CellSize.X.Offset == 150 and GridClone.CellPadding.Y.Offset == 10)
        GridClone:Destroy()
    )";
    Failures += Check(Lui_RunScript(Runtime, GridScript, "Grid") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Backend.Created == 8 && Backend.Arranged.size() == 8,
        "grid components must not create or arrange native controls");
    Lui_Destroy(Runtime);
    if (!Failures) std::puts("[LUI:LayoutTest] Foundation 1 layout semantics passed");
    return Failures ? 1 : 0;
}
