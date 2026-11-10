#include "LuiRuntime.h"
#include "../../runtime/internal/State.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <limits>
#include <thread>
#include <unordered_map>
#include <vector>

struct Bounds {
    double X = 0;
    double Y = 0;
    double Width = 0;
    double Height = 0;
};

struct LayoutBackend {
    std::unordered_map<int, Bounds> Arranged;
    int WindowResizeRequests = 0;
    int Created = 0;
    std::unordered_map<int, Bounds> Clips;
    std::unordered_map<int, int> Ranks;
    std::unordered_map<int, std::string> Styles;
    LuiRuntime* Runtime = nullptr;
    int SnapshotFailures = 0, PresentationQueries = 0, ArrangeQueries = 0;
    bool QueueTextFromPresent = false;
    int DeferredIngress = 0;
};

static int LUI_CALL OnCreate(void* Context, int, const char*) {
    ++static_cast<LayoutBackend*>(Context)->Created;
    return 1;
}

static int LUI_CALL OnProperty(void* Context, int Id, const char* Name, const char* Value) {
    if (std::string(Name) == "__Style") static_cast<LayoutBackend*>(Context)->Styles[Id] = Value;
    return 1;
}
static int LUI_CALL OnParent(void*, int, int) { return 1; }
static int LUI_CALL OnDestroy(void*, int) { return 1; }

static int LUI_CALL OnArrange(void* Context, int Id, double X, double Y, double Width, double Height, int ResizeWindow) {
    auto* Backend = static_cast<LayoutBackend*>(Context);
    Backend->Arranged[Id] = {X, Y, Width, Height};
    if (Id == 1 && ResizeWindow) ++Backend->WindowResizeRequests;
    if (Backend->Runtime) {
        LuiPresentationEntryV1 Entry{};
        LuiWindowPresentationV1 Snapshot{sizeof(Snapshot), LUI_WINDOW_PRESENTATION_ABI_VERSION, 1, 0, &Entry, 0};
        if (Lui_GetWindowPresentation(Backend->Runtime, 1, &Snapshot) != 0) ++Backend->SnapshotFailures;
        ++Backend->ArrangeQueries;
    }
    return 1;
}

static bool Near(double Actual, double Expected) {
    return std::abs(Actual - Expected) < 0.001;
}

static int LUI_CALL OnPresent(void* Context, int Id, int, double X, double Y, double Width, double Height, int Rank, int) {
    auto& Backend = *static_cast<LayoutBackend*>(Context);
    if (Backend.Runtime && Id == 1) {
        std::vector<LuiPresentationEntryV1> Entries(LUI_WINDOW_PRESENTATION_MAX_ENTRIES);
        LuiWindowPresentationV1 Snapshot{sizeof(Snapshot), LUI_WINDOW_PRESENTATION_ABI_VERSION,
            static_cast<uint32_t>(Entries.size()), 0, Entries.data(), 0};
        if (Lui_GetWindowPresentation(Backend.Runtime, Id, &Snapshot) != 1 || !Snapshot.Count ||
            Lui_IsWindowPresentationCurrent(Backend.Runtime, Id, Snapshot.Revision) != 1) ++Backend.SnapshotFailures;
        ++Backend.PresentationQueries;
        if (Backend.QueueTextFromPresent) {
            Backend.QueueTextFromPresent=false;
            if (Lui_TextInput(Backend.Runtime,2,"deferred native callback",0,0)!=1) ++Backend.SnapshotFailures;
            else ++Backend.DeferredIngress;
        }
    }
    Backend.Clips[Id] = {X, Y, Width, Height}; Backend.Ranks[Id] = Rank; return 1;
}

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:LayoutTest] %s\n", Message);
    return Condition ? 0 : 1;
}

static int PresentationTests() {
    LuiRuntime* Runtime = Lui_Create();
    LayoutBackend Backend; Backend.Runtime = Runtime;
    Lui_SetBackend(Runtime, {&Backend, OnCreate, OnProperty, OnParent, OnArrange, OnDestroy});
    LuiPresentationCallbacksV1 Presentation{sizeof(Presentation), 1, &Backend, OnPresent};
    Lui_SetPresentationCallbacks(Runtime, &Presentation);
    int Failures = Check(Lui_RunScript(Runtime, R"(
        W = Instance.new('Window', {Visible=true,Size=UDim2.fromOffset(300,240)})
        local function Frame(Parent, Z)
            return Instance.new('Frame', {Parent=Parent, ZIndex=Z,
                Size=UDim2.fromOffset(80,40), BackgroundColor3=Color3.fromRGB(20,30,40)})
        end
        Editor=Instance.new('TextBox',{Parent=W,Size=UDim2.fromOffset(280,220)})
        Lower=Frame(W,1); Grandchild=Frame(Lower,999); Higher=Frame(W,2)
        Disabled=Instance.new('TextButton',{Parent=W,ZIndex=3,Size=UDim2.fromOffset(80,40),Enabled=false})
        Transparent=Frame(W,4); Transparent.BackgroundTransparency=0.5
        Rounded=Frame(W,5); Rounded.CornerRadius=4
        Scroll=Instance.new('ScrollingFrame',{Parent=W,ZIndex=6,Position=UDim2.fromOffset(10,10),
            Size=UDim2.fromOffset(100,80),CanvasSize=UDim2.fromOffset(200,200),CanvasPosition=Vector2.new(20,30),
            BackgroundColor3=Color3.fromRGB(20,30,40)})
        Scrolled=Frame(Scroll,0); Scrolled.Position=UDim2.fromOffset(50,60)
        Faded=Frame(W,7); Faded.Opacity=0.5; FadedChild=Frame(Faded,0)
        Hidden=Frame(W,8); Hidden.Visible=false; HiddenChild=Frame(Hidden,0)
        Other=Instance.new('Window',{Visible=true,Size=UDim2.fromOffset(300,240)})
    )", "PresentationSnapshot") == 1, Lui_GetLastError(Runtime));
    std::vector<LuiPresentationEntryV1> Entries(LUI_WINDOW_PRESENTATION_MAX_ENTRIES);
    LuiWindowPresentationV1 Snapshot{sizeof(Snapshot), LUI_WINDOW_PRESENTATION_ABI_VERSION,
        static_cast<uint32_t>(Entries.size()), 0, Entries.data(), 0};
    Failures += Check(Lui_GetWindowPresentation(Runtime, 1, &Snapshot) == 1 && Snapshot.Count == 14,
        "window presentation query failed");
    Failures += Check(Backend.ArrangeQueries > 0 && Backend.PresentationQueries > 0 && !Backend.SnapshotFailures,
        "snapshot admitted partial arrange or rejected committed native Present callback");
    for (uint32_t Index=0; Index<Snapshot.Count; ++Index)
        Failures += Check(Entries[Index].Id == static_cast<int>(Index+1) && Entries[Index].PaintOrder == Index,
            "nested sibling paint order escaped ancestor stacking context");
    Failures += Check((Entries[5].Flags & LUI_PRESENTATION_VISIBLE) &&
        !(Entries[5].Flags & LUI_PRESENTATION_INPUT_ELIGIBLE) &&
        !(Entries[5].Flags & LUI_PRESENTATION_OPAQUE_RECTANGLE), "disabled native control stopped painting or claimed opaque");
    Failures += Check(!(Entries[6].Flags & LUI_PRESENTATION_OPAQUE_RECTANGLE) &&
        !(Entries[7].Flags & LUI_PRESENTATION_OPAQUE_RECTANGLE) &&
        !(Entries[10].Flags & LUI_PRESENTATION_OPAQUE_RECTANGLE) &&
        !(Entries[11].Flags & LUI_PRESENTATION_OPAQUE_RECTANGLE) && !Entries[12].Flags && !Entries[13].Flags,
        "transparent rounded ancestor-opacity or hidden coverage claimed opaque");
    const auto& Scrolled = Entries[9];
    Failures += Check(Near(Scrolled.X,40) && Near(Scrolled.Y,40) && Near(Scrolled.ClipX,10) &&
        Near(Scrolled.ClipY,10) && Near(Scrolled.ClipWidth,100) && Near(Scrolled.ClipHeight,80),
        "scrolled window-local bounds/ancestor clip differ");
    Backend.QueueTextFromPresent=true;
    Failures += Check(Lui_RunScript(Runtime,
        "Lower.BorderSizePixel=2; Editor.TextChanged:Connect(function() Lower.ZIndex=1 end)",
        "SnapshotNativeCallbackSetup")==1,Lui_GetLastError(Runtime));
    Failures += Check(Lui_TextInput(Runtime,2,"native callback",0,0)==1 &&
        Lui_GetWindowPresentation(Runtime,1,&Snapshot)==1 &&
        !(Entries[2].Flags & LUI_PRESENTATION_OPAQUE_RECTANGLE) && !Backend.SnapshotFailures && Backend.DeferredIngress==1,
        "native text callback presentation failed or border-only coverage overclaimed");
    const uint64_t OldRevision = Snapshot.Revision;
    Failures += Check(Lui_IsWindowPresentationCurrent(Runtime,1,OldRevision)==1 &&
        Lui_IsWindowPresentationCurrent(Runtime,15,OldRevision)==1,
        "committed window revision unavailable");
    // The identity is the (WindowId, Revision) pair, not a globally unique revision.
    Failures += Check(Lui_RunScript(Runtime,"Higher.Parent=Other", "SnapshotReparent")==1,Lui_GetLastError(Runtime));
    Failures += Check(Lui_IsWindowPresentationCurrent(Runtime,1,OldRevision)==0 &&
        Lui_GetWindowPresentation(Runtime,1,&Snapshot)==1 && Snapshot.Count==13,
        "reparent retained stale window mask revision");
    LuiPresentationEntryV1 Sentinel{}; Sentinel.Id=9876;
    LuiWindowPresentationV1 Small{sizeof(Small),LUI_WINDOW_PRESENTATION_ABI_VERSION,1,77,&Sentinel,99};
    Failures += Check(Lui_GetWindowPresentation(Runtime,1,&Small)==2 && Small.Count==0 &&
        Small.Revision==0 && Sentinel.Id==9876,"capacity overflow leaked partial snapshot");
    Small.Capacity=LUI_WINDOW_PRESENTATION_MAX_ENTRIES+1;
    Failures += Check(Lui_GetWindowPresentation(Runtime,1,&Small)==0,"oversized output accepted");
    Failures += Check(Lui_GetWindowPresentation(nullptr,1,&Snapshot)==0 && !Snapshot.Count &&
        Lui_IsWindowPresentationCurrent(Runtime,1,0)==0,"invalid snapshot runtime/revision accepted");
    Failures += Check(Lui_GetWindowPresentation(Runtime,1,&Snapshot)==1,"snapshot reset after rejection failed");
    int OtherThreadResult=1;
    std::thread OtherThread([&]{OtherThreadResult=Lui_IsWindowPresentationCurrent(Runtime,1,Snapshot.Revision);});
    OtherThread.join();
    Failures += Check(OtherThreadResult==0,"presentation freshness ignored owner thread");
    Failures += Check(Lui_WindowClosed(Runtime,1)==1,"native closure acknowledgement rejected");
    Failures += Check(Lui_GetWindowPresentation(Runtime,1,&Snapshot)==0 && !Snapshot.Count && !Snapshot.Revision &&
        Lui_GetWindowPresentation(Runtime,2,&Snapshot)==0,"closed/wrong-class presentation owner accepted");
    Lui_Destroy(Runtime);
    Runtime=Lui_Create();
    const std::string OverflowScript="W=Instance.new('Window',{}) for Index=1,"+
        std::to_string(LUI_WINDOW_PRESENTATION_MAX_ENTRIES)+" do Instance.new('TextLabel',{Parent=W}) end";
    Failures += Check(Lui_RunScript(Runtime,OverflowScript.c_str(),"SnapshotTraversalBound")==1,Lui_GetLastError(Runtime));
    Entries[0].Id=9876;
    Failures += Check(Lui_GetWindowPresentation(Runtime,1,&Snapshot)==2 && !Snapshot.Count &&
        !Snapshot.Revision && Entries[0].Id==9876,"tree overflow emitted a truncated mask source");
    Failures += Check(Lui_RunScript(Runtime,"W:Destroy()", "SnapshotDestroyed")==1,Lui_GetLastError(Runtime));
    Failures += Check(Lui_GetWindowPresentation(Runtime,1,&Snapshot)==0,"destroyed window snapshot accepted");
    Lui_Destroy(Runtime);
    Runtime=Lui_Create();
    Failures += Check(Lui_RunScript(Runtime,
        "W=Instance.new('Window',{Size=UDim2.fromOffset(1e308,100)}) "
        "Instance.new('Frame',{Parent=W,Size=UDim2.fromScale(1e308,1)})",
        "SnapshotFiniteGeometry")==1,Lui_GetLastError(Runtime));
    Failures += Check(Lui_GetWindowPresentation(Runtime,1,&Snapshot)==0 && !Snapshot.Count,
        "nonfinite derived geometry escaped query validation");
    Lui_Destroy(Runtime);
    return Failures;
}

static int StyleGeometryTests() {
    LuiRuntime* Runtime = Lui_Create();
    int Failures = 0;
    auto Number = [](const Node* Value, const char* Name, double Default) {
        const auto Found = Value->StyleProperties.find(Name);
        return Found == Value->StyleProperties.end() ? Default : std::strtod(Found->second.c_str(), nullptr);
    };
    auto HasBackground = [](const Node* Value) {
        const auto Found = Value->StyleProperties.find("BackgroundColor3");
        return Found != Value->StyleProperties.end() && Found->second != "null";
    };
    auto Verify = [&] {
        // Recompute from authoritative serialized values, never from the facts.
        for (const auto& Pair : Runtime->Nodes) {
            const Node* Value = Pair.second.get();
            if (Value->Destroyed) continue;
            const unsigned char Expected = static_cast<unsigned char>(
                (HasBackground(Value) ? StyleGeometryValue::Background : 0) |
                (Number(Value,"BackgroundTransparency",0)==0 ? StyleGeometryValue::NoTransparency : 0) |
                (Number(Value,"CornerRadius",0)==0 ? StyleGeometryValue::SquareCorners : 0) |
                (Number(Value,"BorderSizePixel",0)==0 ? StyleGeometryValue::NoBorder : 0) |
                (Number(Value,"Opacity",1)>0 ? StyleGeometryValue::VisibleOpacity : 0) |
                (Number(Value,"Opacity",1)==1 ? StyleGeometryValue::UnitOpacity : 0));
            Failures += Check(Value->StyleGeometry.Facts==Expected,"normalized style facts differ from authoritative values");
        }
        std::vector<LuiPresentationEntryV1> Entries(64);
        LuiWindowPresentationV1 Snapshot{sizeof(Snapshot),1,static_cast<uint32_t>(Entries.size()),0,Entries.data(),0};
        if (Lui_GetWindowPresentation(Runtime,1,&Snapshot)!=1) {Failures+=Check(false,"style snapshot unavailable");return;}
        for (uint32_t Index=0; Index<Snapshot.Count; ++Index) {
            const auto& Entry = Entries[Index];
            const Node* Value = Runtime->Nodes.at(Entry.Id).get();
            bool Visible=true, Eligible=true, UnitOpacity=true;
            for (const Node* Ancestor=Value; Ancestor;
                Ancestor=Ancestor->ParentId ? Runtime->Nodes.at(Ancestor->ParentId).get() : nullptr) {
                Visible=Visible && Ancestor->Visible && Number(Ancestor,"Opacity",1)>0;
                Eligible=Eligible && Ancestor->Visible && Ancestor->Enabled;
                UnitOpacity=UnitOpacity && Number(Ancestor,"Opacity",1)==1;
            }
            const bool Intersects=std::min(Entry.X+Entry.Width,Entry.ClipX+Entry.ClipWidth)>std::max(Entry.X,Entry.ClipX) &&
                std::min(Entry.Y+Entry.Height,Entry.ClipY+Entry.ClipHeight)>std::max(Entry.Y,Entry.ClipY);
            const bool Opaque=(Value->ClassName=="Frame" || Value->ClassName=="ScrollingFrame") &&
                HasBackground(Value) && Number(Value,"BackgroundTransparency",0)==0 &&
                Number(Value,"CornerRadius",0)==0 && Number(Value,"BorderSizePixel",0)==0 && UnitOpacity;
            const uint32_t Expected=(Visible && Intersects ? LUI_PRESENTATION_VISIBLE : 0) |
                (Eligible && Intersects ? LUI_PRESENTATION_INPUT_ELIGIBLE : 0) |
                (Visible && Intersects && Opaque ? LUI_PRESENTATION_OPAQUE_RECTANGLE : 0);
            Failures += Check(Entry.Flags==Expected,"cached query flags differ from recomputed style/ancestor proof");
        }
    };
    auto Run = [&](const char* Script) {
        const int Result=Lui_RunScript(Runtime,Script,"StyleGeometry");
        Failures += Check(Result==1,Lui_GetLastError(Runtime));
        if (Result==1) Verify();
    };
    Run(R"(
        W=Instance.new('Window',{Visible=true,Size=UDim2.fromOffset(300,240)})
        F=Instance.new('Frame',{Parent=W,Size=UDim2.fromOffset(80,40)})
        P=Instance.new('Frame',{Parent=W,Size=UDim2.fromOffset(160,120)})
        C=Instance.new('Frame',{Parent=P,Size=UDim2.fromOffset(40,30),BackgroundColor3=Color3.fromRGB(20,30,40)})
        Copy=C:Clone(); Copy.Parent=W
        assert(F.BackgroundColor3==nil and F.Opacity==1 and F.BackgroundTransparency==0 and F.CornerRadius==0 and F.BorderSizePixel==0)
    )");
    Run("F.BackgroundColor3=Color3.fromRGB(1,2,3)");
    Run("F.BackgroundColor3=nil; assert(F.BackgroundColor3==nil and Copy.BackgroundColor3~=nil)");
    Run("F.BackgroundColor3=Color3.fromRGB(1,2,3); F.BackgroundTransparency=0.5; assert(F.BackgroundTransparency==0.5)");
    Run("F.BackgroundTransparency=0; F.CornerRadius=7; assert(F.CornerRadius==7)");
    Run("F.CornerRadius=0; F.BorderSizePixel=2; assert(F.BorderSizePixel==2)");
    Run("F.BorderSizePixel=0; F.Opacity=0; assert(F.Opacity==0)");
    Run("F.Opacity=0.5; assert(F.Opacity==0.5)");
    Run("F.Opacity=1; P.Opacity=0; assert(C.Opacity==1)");
    Run("P.Opacity=0.5; C.Parent=W");
    Run("C.Parent=P; P.Opacity=1");
    Run(R"(
        assert(not pcall(function() F.Opacity=nil end))
        assert(not pcall(function() F.Opacity=0/0 end))
        assert(not pcall(function() F.BackgroundTransparency=-1 end))
        assert(not pcall(function() F.CornerRadius=-1 end))
        assert(not pcall(function() F.BorderSizePixel=math.huge end))
        assert(not pcall(function() F.BackgroundColor3={R=2,G=0,B=0} end))
        assert(F.Opacity==1 and F.BackgroundTransparency==0 and F.CornerRadius==0 and F.BorderSizePixel==0)
        F.TextSize=nil; F.TextColor3=nil; F.FontFamily=nil
        Copy2=F:Clone(); Copy2.Parent=W; Copy2.Opacity=0.5
        assert(F.Opacity==1 and Copy2.Opacity==0.5)
    )");
    Node Measured;
    std::printf("[LUI:StyleGeometry] NodeBytes=%zu ZIndexOffset=%zu StyleMapOffset=%zu FactsBytes=%zu\n",
        sizeof(Node),static_cast<size_t>(reinterpret_cast<uintptr_t>(&Measured.ZIndex)-reinterpret_cast<uintptr_t>(&Measured)),
        static_cast<size_t>(reinterpret_cast<uintptr_t>(&Measured.StyleProperties)-reinterpret_cast<uintptr_t>(&Measured)),sizeof(StyleGeometryValue));
    Lui_Destroy(Runtime);
    return Failures;
}

static int ViewportTests() {
    LuiRuntime* Runtime = Lui_Create();
    int Failures = Check(Lui_RunScript(Runtime, R"(
        Window = Instance.new("Window", {Visible = true, Size = UDim2.fromOffset(200, 150)})
        Scroll = Instance.new("ScrollingFrame", {Parent = Window, Position = UDim2.fromOffset(10, 10),
            Size = UDim2.fromOffset(100, 80), CanvasSize = UDim2.fromOffset(200, 200),
            CanvasPosition = Vector2.new(20, 30)})
        Button = Instance.new("TextButton", {Parent = Scroll, Position = UDim2.fromOffset(50, 60),
            Size = UDim2.fromOffset(80, 40)})
        Bottom = Instance.new("TextButton", {Parent = Scroll, Position = UDim2.fromOffset(0, 190),
            Size = UDim2.fromOffset(20, 10)})
        Nested = Instance.new("Frame", {Parent = Scroll, Position = UDim2.fromOffset(100, 100),
            Size = UDim2.fromOffset(40, 40), ClipsDescendants = true})
        NestedButton = Instance.new("TextButton", {Parent = Nested, Position = UDim2.fromOffset(20, 0),
            Size = UDim2.fromOffset(40, 40)})
        assert(Scroll:IsA("Frame") and Scroll.ClipsDescendants)
        assert(Scroll.AbsoluteCanvasSize.X == 200 and Scroll.AbsoluteCanvasPosition.Y == 30)
        assert(Button.AbsolutePosition.X == 40 and Button.AbsolutePosition.Y == 40)
        assert(not pcall(function() Scroll.ClipsDescendants = false end))
        assert(not pcall(function() Scroll.CanvasSize = UDim2.fromOffset(-1, 2) end))
        assert(not pcall(function() Scroll.CanvasSize = UDim2.fromOffset(1e10, 2) end))
        assert(not pcall(function() Scroll.CanvasSize = UDim2.fromOffset(0/0, 2) end))
        assert(not pcall(function() Scroll.CanvasPosition = Vector2.new(-1, 0) end))
        assert(not pcall(function() Scroll.CanvasPosition = Vector2.new(1e10, 0) end))
        assert(not pcall(function() Scroll.AbsoluteCanvasPosition = Vector2.new(0, 0) end))
        assert(not pcall(function() Button.ZIndex = 1.5 end))
        Ended, Canceled, Lost = 0, 0, 0
        Button.InputEnded:Connect(function(Input)
            Ended += 1
            if Input.IsCanceled then Canceled += 1 end
        end)
        Button.FocusLost:Connect(function() Lost += 1 end)
    )", "Viewport") == 1, Lui_GetLastError(Runtime));
    auto Hit = [&](double X, double Y, int Expected) {
        int Id = -1;
        Failures += Check(Lui_HitTest(Runtime, 1, X, Y, &Id) == 1 && Id == Expected, "clipped hit test mismatch");
    };
    Hit(50, 50, 3); Hit(109, 50, 3); Hit(110, 50, 0); Hit(95, 85, 5); Hit(199, 149, 0);
    int Id = -1;
    Failures += Check(Lui_HitTest(Runtime, 3, 0, 0, &Id) == 0 && Id == 0 &&
        Lui_HitTest(Runtime, 1, std::numeric_limits<double>::infinity(), 0, &Id) == 0 &&
        Lui_HitTest(Runtime, 1, 0, 0, nullptr) == 0, "invalid hit query accepted");
    int OtherThreadResult = 1;
    std::thread Other([&] { int OtherId; OtherThreadResult = Lui_HitTest(Runtime, 1, 50, 50, &OtherId); });
    Other.join();
    Failures += Check(OtherThreadResult == 0, "hit query ignored scheduler ownership");
    Failures += Check(Lui_PointerInput(Runtime, 3, 0, 0, 1, 75, 0) == 0, "press outside ancestor clip accepted");
    Failures += Check(Lui_PointerInput(Runtime, 3, 0, 0, 1, 0, 0) == 1 &&
        Lui_PointerInput(Runtime, 3, 2, 0, 1, 500, 500) == 1, "captured release outside clip lost");
    Failures += Check(Lui_FocusChanged(Runtime, 3, 1) == 1 &&
        Lui_PointerInput(Runtime, 3, 0, 0, 2, 0, 0) == 1, "visible target rejected input");
    Failures += Check(Lui_RunScript(Runtime, R"(
        Scroll.CanvasPosition = Vector2.new(200, 200)
        assert(Scroll.CanvasPosition.Y == 200 and Scroll.AbsoluteCanvasPosition.Y == 120)
        assert(not Button.IsFocused and Lost == 1 and Ended == 2 and Canceled == 1)
        Scroll.CanvasSize = UDim2.fromOffset(40, 40)
        assert(Scroll.AbsoluteCanvasSize.X == 100 and Scroll.AbsoluteCanvasPosition.Y == 0)
        Scroll.CanvasSize = UDim2.fromOffset(200, 200)
        assert(Scroll.AbsoluteCanvasPosition.Y == 120)
        Scroll.CanvasPosition = Vector2.new(0, 0)
        Copy = Scroll:Clone()
        assert(Copy.CanvasSize.Y.Offset == 200 and Copy.ClipsDescendants)
        Copy.Parent = Window
        assert(Copy.AbsoluteCanvasSize.Y == 200)
        Copy:Destroy(); Copy:Destroy()
        Nested.ClipsDescendants = false
        Nested.Parent = Window
        Nested.Position = UDim2.fromOffset(120, 0)
        NestedButton.Position = UDim2.fromOffset(-40, 0)
        Nested.ZIndex = 1
        NestedButton.ZIndex = 100
        Cover = Instance.new("TextButton", {Parent = Window, Size = UDim2.fromOffset(200, 50), ZIndex = 2})
    )", "ViewportLifecycle") == 1, Lui_GetLastError(Runtime));
    // The clone consumed IDs 7..11; Cover is 12. A descendant cannot escape its sibling stacking context.
    Hit(85, 10, 12);
    Failures += Check(Lui_RunScript(Runtime, "Cover.ZIndex = 0", "Stacking") == 1, Lui_GetLastError(Runtime));
    Hit(85, 10, 6);
    Failures += Check(Lui_RunScript(Runtime, "Nested.ClipsDescendants = true", "Overflow") == 1, Lui_GetLastError(Runtime));
    Hit(85, 10, 12);
    Failures += Check(Lui_RunScript(Runtime, R"(
        Nested.Visible = false
        Cover.ZIndex = 0
        Peer = Instance.new("TextButton", {Parent = Window, Size = UDim2.fromOffset(200, 50)})
    )", "StackingTies") == 1, Lui_GetLastError(Runtime));
    Hit(85, 10, 13);
    Failures += Check(Lui_RunScript(Runtime, "Cover.Parent = nil; Cover.Parent = Window", "ReparentOrder") == 1, Lui_GetLastError(Runtime));
    Hit(85, 10, 12);
    LayoutBackend Backend;
    Failures += Check(Lui_RunScript(Runtime, R"(
        Scroll.Size = UDim2.fromOffset(100, 80)
        Scroll.CanvasSize = UDim2.fromOffset(300, 240)
        Scroll.CanvasPosition = Vector2.new(300, 240)
        assert(Scroll.AbsoluteCanvasPosition.X == 200 and Scroll.AbsoluteCanvasPosition.Y == 160)
        Scroll.Size = UDim2.fromOffset(250, 200)
        assert(Scroll.AbsoluteCanvasPosition.X == 50 and Scroll.AbsoluteCanvasPosition.Y == 40)
        Button:Destroy(); Bottom:Destroy()
        Scroll.CanvasPosition = Vector2.new(0, 40)
        Instance.new("UIPadding", {Parent = Scroll, PaddingLeft = UDim.new(0.1, 0)})
        List = Instance.new("UIListLayout", {Parent = Scroll, Padding = UDim.new(0, 5)})
        A = Instance.new("TextButton", {Parent = Scroll, Size = UDim2.new(0.5, 0, 0, 20), LayoutOrder = 2, ZIndex = 100})
        B = Instance.new("TextButton", {Parent = Scroll, Size = UDim2.fromOffset(50, 30), LayoutOrder = 1})
        assert(A.AbsoluteSize.X == 135 and B.AbsolutePosition.X == 40)
        assert(B.AbsolutePosition.Y == -30 and A.AbsolutePosition.Y == 5)
        List:Destroy()
        Grid = Instance.new("UIGridLayout", {Parent = Scroll, CellSize = UDim2.fromOffset(100, 30), CellPadding = UDim2.fromOffset(10, 5)})
        assert(B.AbsolutePosition.X == 40 and A.AbsolutePosition.X == 150)
        assert(A.AbsolutePosition.Y == -30 and A.AbsoluteSize.X == 100)
        A.Changed:Connect(function() local Offset = Scroll.AbsoluteCanvasPosition; assert(Offset.Y == 40) end)
        A.Text = "callback reads settled geometry"
        Scroll.Sensitive = true
        A.Text = "synthetic viewport private payload"
        Outer = Instance.new("ScrollingFrame", {Parent = Window, Position = UDim2.fromOffset(120, 80),
            Size = UDim2.fromOffset(60, 50), CanvasSize = UDim2.fromOffset(120, 150), CanvasPosition = Vector2.new(10, 20)})
        Inner = Instance.new("ScrollingFrame", {Parent = Outer, Position = UDim2.fromOffset(40, 60),
            Size = UDim2.fromOffset(20, 30), CanvasSize = UDim2.fromOffset(100, 100), CanvasPosition = Vector2.new(5, 10)})
        Item = Instance.new("TextButton", {Parent = Inner, Position = UDim2.fromOffset(20, 20), Size = UDim2.fromOffset(10, 10)})
        assert(Inner.AbsolutePosition.X == 150 and Inner.AbsolutePosition.Y == 120)
        assert(Item.AbsolutePosition.X == 165 and Item.AbsolutePosition.Y == 130)
    )", "CanvasLayouts") == 1, Lui_GetLastError(Runtime));
    const std::string Snapshot = Lui_GetPreviewTreeJson(Runtime);
    Failures += Check(Snapshot.find("synthetic viewport private payload") == std::string::npos,
        "viewport changes leaked sensitive text into diagnostics");
    Lui_SetBackend(Runtime, {&Backend, OnCreate, OnProperty, OnParent, OnArrange, OnDestroy});
    Failures += Check(std::string(Lui_GetLastError(Runtime)).find("Native mapping") != std::string::npos &&
        Backend.Created == 0, "late backend silently accepted viewport features");
    Lui_Destroy(Runtime);
    Runtime = Lui_Create();
    Lui_SetBackend(Runtime, {&Backend, OnCreate, OnProperty, OnParent, OnArrange, OnDestroy});
    Failures += Check(Lui_RunScript(Runtime, R"(
        assert(not pcall(function() Instance.new("ScrollingFrame") end))
        Window = Instance.new("Window", {Visible = true})
        Frame = Instance.new("Frame")
        assert(not pcall(function() Frame.ClipsDescendants = true end))
        assert(not pcall(function() Frame.ZIndex = 1 end))
        assert(Frame.ZIndex == 0 and not Frame.ClipsDescendants)
    )", "UnsupportedNativeViewport") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_HitTest(Runtime, 1, 0, 0, &Id) == 0, "unmapped native hit test was accepted");
    Lui_Destroy(Runtime);
    Runtime = Lui_Create();
    Backend = {};
    Lui_SetBackend(Runtime, {&Backend, OnCreate, OnProperty, OnParent, OnArrange, OnDestroy});
    LuiPresentationCallbacksV1 Presentation{sizeof(Presentation), 1, &Backend, OnPresent};
    auto BadPresentation = Presentation; BadPresentation.AbiVersion = 2;
    Failures += Check(!Lui_SetPresentationCallbacks(Runtime, &BadPresentation) &&
        Lui_SetPresentationCallbacks(Runtime, &Presentation) && !Lui_SetPresentationCallbacks(Runtime, &Presentation),
        "presentation registration boundary failed");
    Failures += Check(Lui_RunScript(Runtime, R"(
        W = Instance.new("Window", {Visible = true, Size = UDim2.fromOffset(200, 150)})
        S = Instance.new("ScrollingFrame", {Parent = W, Size = UDim2.fromOffset(100, 80), CanvasSize = UDim2.fromOffset(200, 300)})
        A = Instance.new("TextButton", {Parent = S, Position = UDim2.fromOffset(0, 70), Size = UDim2.fromOffset(100, 30), ZIndex = 2})
        B = Instance.new("TextButton", {Parent = S, Size = UDim2.fromOffset(20, 20), ZIndex = 1})
        Calls = 0; S.CanvasChanged:Connect(function() Calls += 1 end)
    )", "Presentation") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Backend.Ranks[3] == 1 && Backend.Ranks[4] == 0 && Near(Backend.Clips[3].Height, 80) &&
        Near(Backend.Clips[3].Y, -70), "canonical presentation differed from layout");
    Failures += Check(Lui_ScrollInput(Runtime, 2, 0, 40) == 1, "logical scrolling failed");
    Failures += Check(Lui_RunScript(Runtime, "assert(S.AbsoluteCanvasPosition.Y == 40 and Calls == 2)", "ScrollCheck") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_ScrollInput(Runtime, 2, 0, std::numeric_limits<double>::infinity()) == 0, "invalid scroll accepted");
    Failures += Check(Lui_RunScript(Runtime, R"(
        S.TextSize = 24; S.FontWeight = 'Bold'; S.FontFamily = 'System'
        S.TextColor3 = Color3.fromRGB(255,128,0)
        S.BackgroundColor3 = Color3.new(0.2,0.3,0.4)
        S.BorderSizePixel = 10; S.CornerRadius = 12; S.Opacity = 0.75
        assert(A.TextSize == nil and S.BackgroundColor3.G == 0.3)
        assert(not pcall(function() S.BackgroundColor3.R = 1 end))
        assert(not pcall(function() Color3.new(2,0,0) end))
        assert(not pcall(function() S.Opacity = 2 end))
        assert(not pcall(function() S.FontFamily = 'invalid; CSS' end))
        assert(not pcall(function() S.FontWeight = 'VeryBold' end))
        assert(S.AbsoluteSize.X == 100 and A.AbsolutePosition.Y == 30)
    )", "StyleContract") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Backend.Styles[3].find("\"TextSize\":24") != std::string::npos &&
        Backend.Styles[3].find("\"FontWeight\":\"Bold\"") != std::string::npos,
        "canonical inherited style was not projected");
    Failures += Check(Lui_RunScript(Runtime, R"(
        Outer = Instance.new("ScrollingFrame", {Parent = W, Size = UDim2.fromOffset(100, 100), CanvasSize = UDim2.fromOffset(100, 400)})
        Inner = Instance.new("ScrollingFrame", {Parent = Outer, Size = UDim2.fromOffset(100, 80), CanvasSize = UDim2.fromOffset(100, 100)})
    )", "ScrollChain") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_ScrollInput(Runtime, 6, 0, 60) == 1, "scroll chaining failed");
    Failures += Check(Lui_RunScript(Runtime, "assert(Inner.AbsoluteCanvasPosition.Y == 20 and Outer.AbsoluteCanvasPosition.Y == 40)", "ChainCheck") == 1, Lui_GetLastError(Runtime));
    Lui_Destroy(Runtime);
    return Failures;
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
    const char* WindowResizeScript = R"(
        Responsive = Instance.new("Frame", {Size = UDim2.fromScale(1, 1), Parent = Window})
        Instance.new("UIPadding", {PaddingLeft = UDim.new(0, 10), PaddingRight = UDim.new(0, 10), Parent = Responsive})
        Instance.new("UIGridLayout", {CellSize = UDim2.fromOffset(300, 40), CellPadding = UDim2.fromOffset(10, 10), Parent = Responsive})
        ResponsiveA = Instance.new("TextButton", {Parent = Responsive})
        ResponsiveB = Instance.new("TextButton", {Parent = Responsive})
        ResponsiveC = Instance.new("TextButton", {Parent = Responsive})
        assert(ResponsiveC.AbsolutePosition.X == 10 and ResponsiveC.AbsolutePosition.Y == 50)
    )";
    Failures += Check(Lui_RunScript(Runtime, WindowResizeScript, "WindowResize") == 1, Lui_GetLastError(Runtime));
    const int ResizeRequestsBeforeNativeEvent = Backend.WindowResizeRequests;
    Failures += Check(Lui_WindowResized(Runtime, 1, 1200, 800) == 1, "native window resize was rejected");
    Failures += Check(Lui_RunScript(Runtime,
        "assert(Window.Size.X.Offset == 800 and Window.AbsoluteSize.X == 1200); "
        "assert(ResponsiveC.AbsolutePosition.X == 630 and ResponsiveC.AbsolutePosition.Y == 0)",
        "NativeViewport") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Backend.WindowResizeRequests == ResizeRequestsBeforeNativeEvent,
        "native viewport update requested a window resize");
    Failures += Check(Lui_WindowResized(Runtime, 1, 0, 800) == 0 &&
        Lui_WindowResized(Runtime, 2, 1200, 800) == 0, "invalid viewport update was accepted");
    Failures += Check(Lui_RunScript(Runtime,
        "Window.Size = UDim2.fromOffset(1000, 750); assert(Window.AbsoluteSize.X == 1000)",
        "RequestedWindowSize") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Backend.WindowResizeRequests > ResizeRequestsBeforeNativeEvent,
        "script size assignment did not request a window resize");
    Lui_Destroy(Runtime);
    Failures += ViewportTests();
    Failures += PresentationTests();
    Failures += StyleGeometryTests();
    if (!Failures) std::puts("[LUI:LayoutTest] Layout and headless viewport semantics passed");
    return Failures ? 1 : 0;
}
