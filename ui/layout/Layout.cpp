#include "Layout.h"
#include "../../runtime/internal/Diagnostics.h"
#include "../../runtime/internal/State.h"
#include "../../runtime/reflection/Schema.h"
#include "../style/Style.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace LuiLayout {

static double Resolve(Dimension Value, double ParentExtent) {
    return ParentExtent * Value.Scale + Value.Offset;
}

static VectorValue ApplySizeConstraint(LuiRuntime* Runtime, const Node* Value, VectorValue Size) {
    for (int Id : Value->Children) {
        const Node* Child = Runtime->Nodes.at(Id).get();
        if (Child->ClassName != "UISizeConstraint") continue;
        Size.X = std::max(Size.X, Child->MinSize.X);
        Size.Y = std::max(Size.Y, Child->MinSize.Y);
        if (Child->HasMaxSize) {
            Size.X = std::min(Size.X, Child->MaxSize.X);
            Size.Y = std::min(Size.Y, Child->MaxSize.Y);
        }
        break;
    }
    return Size;
}

VectorValue ResolveConstrainedSize(LuiRuntime* Runtime, const Node* Value, double ParentWidth, double ParentHeight) {
    return ApplySizeConstraint(Runtime, Value,
        {std::max(0.0, Resolve(Value->Size.X, ParentWidth)),
         std::max(0.0, Resolve(Value->Size.Y, ParentHeight))});
}

void ArrangeNode(LuiRuntime* Runtime, Node* Value, BoundsValue Bounds) {
    if (Runtime->BackendFailed) return;
    const BoundsValue PreviousBounds = Value->Bounds;
    Value->Bounds = Bounds;
    LuiStyle::Project(Runtime, Value);
    BoundsValue Content = Bounds;
    if (Value->ClassName == "ScrollingFrame") {
        const VectorValue PreviousSize = Value->AbsoluteCanvasSize, PreviousPosition = Value->AbsoluteCanvasPosition;
        // Canvas dimensions are explicit; descendants never grow them implicitly.
        Value->AbsoluteCanvasSize = {
            std::max(Bounds.Width, std::min(1e9, Resolve(Value->CanvasSize.X, Bounds.Width))),
            std::max(Bounds.Height, std::min(1e9, Resolve(Value->CanvasSize.Y, Bounds.Height)))};
        Value->AbsoluteCanvasPosition = {
            std::clamp(Value->CanvasPosition.X, 0.0, Value->AbsoluteCanvasSize.X - Bounds.Width),
            std::clamp(Value->CanvasPosition.Y, 0.0, Value->AbsoluteCanvasSize.Y - Bounds.Height)};
        Content.X -= Value->AbsoluteCanvasPosition.X;
        Content.Y -= Value->AbsoluteCanvasPosition.Y;
        Content.Width = Value->AbsoluteCanvasSize.X;
        Content.Height = Value->AbsoluteCanvasSize.Y;
        if (PreviousSize.X != Value->AbsoluteCanvasSize.X || PreviousSize.Y != Value->AbsoluteCanvasSize.Y ||
            PreviousPosition.X != Value->AbsoluteCanvasPosition.X || PreviousPosition.Y != Value->AbsoluteCanvasPosition.Y ||
            PreviousBounds.Width != Bounds.Width || PreviousBounds.Height != Bounds.Height)
            Runtime->CanvasSignals.push_back(Value->Id);
    }
    if (Runtime->Backend.Arrange) {
        Runtime->BackendError.clear();
        if (!Runtime->Backend.Arrange(Runtime->Backend.Context, Value->Id,
            Bounds.X, Bounds.Y, Bounds.Width, Bounds.Height,
            Value->ClassName == "Window" && !Value->HasViewportSize ? 1 : 0)) {
            FailBackend(Runtime, "Arrange", Value->Id);
            return;
        }
    }

    Node* Padding = nullptr;
    Node* List = nullptr;
    Node* Grid = nullptr;
    std::vector<Node*> VisualChildren;
    for (int Id : Value->Children) {
        Node* Child = Runtime->Nodes.at(Id).get();
        if (Child->ClassName == "UIPadding") Padding = Child;
        else if (Child->ClassName == "UIListLayout") List = Child;
        else if (Child->ClassName == "UIGridLayout") Grid = Child;
        else if (Child->ClassName == "UISizeConstraint") continue;
        else VisualChildren.push_back(Child);
    }

    double Left = Padding ? Resolve(Padding->PaddingLeft, Content.Width) : 0;
    double Right = Padding ? Resolve(Padding->PaddingRight, Content.Width) : 0;
    double Top = Padding ? Resolve(Padding->PaddingTop, Content.Height) : 0;
    double Bottom = Padding ? Resolve(Padding->PaddingBottom, Content.Height) : 0;
    BoundsValue Inner{Content.X + Left, Content.Y + Top,
        std::max(0.0, Content.Width - Left - Right), std::max(0.0, Content.Height - Top - Bottom)};

    if (List || Grid) std::stable_sort(VisualChildren.begin(), VisualChildren.end(),
        [](const Node* LeftNode, const Node* RightNode) { return LeftNode->LayoutOrder < RightNode->LayoutOrder; });
    if (Grid && !VisualChildren.empty()) {
        const VectorValue Cell{
            std::max(0.0, Resolve(Grid->CellSize.X, Inner.Width)),
            std::max(0.0, Resolve(Grid->CellSize.Y, Inner.Height))};
        const VectorValue Gap{
            Resolve(Grid->CellPadding.X, Inner.Width),
            Resolve(Grid->CellPadding.Y, Inner.Height)};
        std::vector<VectorValue> ChildSizes;
        ChildSizes.reserve(VisualChildren.size());
        VectorValue Slot = Cell;
        for (Node* Child : VisualChildren) {
            VectorValue ChildSize = ApplySizeConstraint(Runtime, Child, Cell);
            Slot.X = std::max(Slot.X, ChildSize.X);
            Slot.Y = std::max(Slot.Y, ChildSize.Y);
            ChildSizes.push_back(ChildSize);
        }
        size_t Columns = 1;
        const double HorizontalPitch = Slot.X + Gap.X;
        if (HorizontalPitch > 0 && std::isfinite(HorizontalPitch)) {
            const double Capacity = std::floor((Inner.Width + Gap.X) / HorizontalPitch);
            if (std::isfinite(Capacity) && Capacity >= 1)
                Columns = static_cast<size_t>(std::min(Capacity, static_cast<double>(VisualChildren.size())));
        }
        for (size_t Index = 0; Index < VisualChildren.size(); ++Index) {
            const size_t Column = Index % Columns;
            const size_t Row = Index / Columns;
            ArrangeNode(Runtime, VisualChildren[Index], {
                Inner.X + Column * HorizontalPitch,
                Inner.Y + Row * (Slot.Y + Gap.Y),
                ChildSizes[Index].X,
                ChildSizes[Index].Y});
        }
        return;
    }
    double Cursor = 0;
    for (Node* Child : VisualChildren) {
        const VectorValue Resolved = ResolveConstrainedSize(Runtime, Child, Inner.Width, Inner.Height);
        double Width = Resolved.X;
        double Height = Resolved.Y;
        double X = Inner.X + Resolve(Child->Position.X, Inner.Width) - Child->AnchorPoint.X * Width;
        double Y = Inner.Y + Resolve(Child->Position.Y, Inner.Height) - Child->AnchorPoint.Y * Height;
        if (List) {
            X = Inner.X + (List->FillDirection == "Horizontal" ? Cursor : 0);
            Y = Inner.Y + (List->FillDirection == "Vertical" ? Cursor : 0);
            Cursor += (List->FillDirection == "Vertical" ? Height : Width) +
                Resolve(List->Padding, List->FillDirection == "Vertical" ? Inner.Height : Inner.Width);
        }
        ArrangeNode(Runtime, Child, {X, Y, Width, Height});
    }
}

static BoundsValue Intersect(BoundsValue Left, BoundsValue Right) {
    const double X = std::max(Left.X, Right.X), Y = std::max(Left.Y, Right.Y);
    return {X, Y, std::max(0.0, std::min(Left.X + Left.Width, Right.X + Right.Width) - X),
        std::max(0.0, std::min(Left.Y + Left.Height, Right.Y + Right.Height) - Y)};
}

static bool Contains(BoundsValue Bounds, double X, double Y) {
    return Bounds.Width > 0 && Bounds.Height > 0 && X >= Bounds.X && Y >= Bounds.Y &&
        X < Bounds.X + Bounds.Width && Y < Bounds.Y + Bounds.Height;
}

bool HasViewportIntersection(const LuiRuntime* Runtime, const Node* Value) {
    BoundsValue Visible = Value->Bounds;
    bool HasClip = false;
    for (const Node* Parent = Value; Parent->ParentId;) {
        Parent = Runtime->Nodes.at(Parent->ParentId).get();
        if (Parent->ClipsDescendants || Parent->ClassName == "ScrollingFrame" || (HasClip && Parent->ClassName == "Window")) {
            HasClip = true;
            Visible = Intersect(Visible, Parent->Bounds);
        }
    }
    // Preserve the legacy explicit-ID input profile outside an explicit viewport.
    return !HasClip || (Visible.Width > 0 && Visible.Height > 0);
}

bool ContainsViewportPoint(const LuiRuntime* Runtime, const Node* Value, double X, double Y) {
    bool HasClip = false;
    for (const Node* Parent = Value; Parent->ParentId;) {
        Parent = Runtime->Nodes.at(Parent->ParentId).get();
        HasClip = HasClip || Parent->ClipsDescendants || Parent->ClassName == "ScrollingFrame";
        if ((Parent->ClipsDescendants || Parent->ClassName == "ScrollingFrame" || (HasClip && Parent->ClassName == "Window")) &&
            !Contains(Parent->Bounds, X, Y)) return false;
    }
    return true;
}

static int HitNode(const LuiRuntime* Runtime, const Node* Value, double X, double Y) {
    if (Value->Destroyed || !Value->Visible || !Value->Enabled) return 0;
    const bool Inside = Contains(Value->Bounds, X, Y);
    if ((Value->ClipsDescendants || Value->ClassName == "ScrollingFrame") && !Inside) return 0;
    std::vector<const Node*> Children;
    for (int Id : Value->Children) {
        const Node* Child = Runtime->Nodes.at(Id).get();
        if (LuiSchema::IsA(Child->ClassName, "GuiObject")) Children.push_back(Child);
    }
    std::stable_sort(Children.begin(), Children.end(),
        [](const Node* Left, const Node* Right) { return Left->ZIndex < Right->ZIndex; });
    for (auto Child = Children.rbegin(); Child != Children.rend(); ++Child)
        if (const int Id = HitNode(Runtime, *Child, X, Y)) return Id;
    return Inside && Value->ClassName != "Window" ? Value->Id : 0;
}

int HitTest(const LuiRuntime* Runtime, const Node* Window, double X, double Y) {
    if (!Contains(Window->Bounds, X, Y)) return 0;
    return HitNode(Runtime, Window, X, Y);
}

static std::vector<Node*> PaintChildren(const LuiRuntime* Runtime, const Node* Value) {
    std::vector<Node*> Children;
    for (int Id : Value->Children) {
        Node* Child = Runtime->Nodes.at(Id).get();
        if (LuiSchema::IsA(Child->ClassName, "GuiObject")) Children.push_back(Child);
    }
    std::stable_sort(Children.begin(), Children.end(), [](const Node* Left, const Node* Right) {return Left->ZIndex < Right->ZIndex;});
    return Children;
}

static void PresentNode(LuiRuntime* Runtime, Node* Value, BoundsValue Clip, int Rank, bool Visible) {
    if (Runtime->BackendFailed || Value->Destroyed) return;
    Visible = Visible && Value->Visible && Value->Enabled;
    const bool Clips = Value->ClipsDescendants || Value->ClassName == "ScrollingFrame" || Value->ClassName == "Window";
    if (Clips) Clip = Intersect(Clip, Value->Bounds);
    const BoundsValue Local{Clip.X - Value->Bounds.X, Clip.Y - Value->Bounds.Y, Clip.Width, Clip.Height};
    const BoundsValue VisibleBounds = Intersect(Clip, Value->Bounds);
    Runtime->BackendError.clear();
    ++Runtime->PresentationCalls;
    if (!Runtime->Presentation.Present(Runtime->Presentation.Context, Value->Id, 1 | (Clips ? 2 : 0),
        Local.X, Local.Y, Local.Width, Local.Height, Rank,
        Visible && VisibleBounds.Width > 0 && VisibleBounds.Height > 0 ? 1 : 0)) {
        FailBackend(Runtime, "Present", Value->Id); return;
    }
    const auto Children = PaintChildren(Runtime, Value);
    for (size_t Index = 0; Index < Children.size(); ++Index)
        PresentNode(Runtime, Children[Index], Clip, static_cast<int>(Index), Visible);
}

void PresentTree(LuiRuntime* Runtime, Node* Window) {PresentNode(Runtime, Window, Window->Bounds, 0, true);}

int GetWindowPresentation(const LuiRuntime* Runtime, const Node* Window, LuiWindowPresentationV1* Snapshot) {
    struct Pending {
        const Node* Value;
        BoundsValue Clip;
        bool Visible, Eligible, OpaqueAncestors;
    };
    std::vector<Pending> Stack{{Window, Window->Bounds, true, true, true}};
    std::vector<LuiPresentationEntryV1> Entries;
    size_t Scheduled = 1, Scanned = 0;
    auto Finite = [](const BoundsValue& Value) {
        return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Width) &&
            std::isfinite(Value.Height) && Value.Width >= 0 && Value.Height >= 0 &&
            std::isfinite(Value.X+Value.Width) && std::isfinite(Value.Y+Value.Height);
    };
    while (!Stack.empty()) {
        const Pending Current = Stack.back(); Stack.pop_back();
        const Node* Value = Current.Value;
        if (Value->Destroyed) continue;
        if (!Finite(Value->Bounds) || !Finite(Current.Clip)) return 0;
        if (Entries.size() == Snapshot->Capacity ||
            Value->Children.size() > LUI_WINDOW_PRESENTATION_MAX_ENTRIES - Scanned)
            return 2;
        Scanned += Value->Children.size();
        BoundsValue Clip = Current.Clip;
        if (Value->ClipsDescendants || Value->ClassName == "ScrollingFrame" || Value == Window)
            Clip = Intersect(Clip, Value->Bounds);
        const BoundsValue VisibleBounds = Intersect(Clip, Value->Bounds);
        const auto& Geometry = Value->StyleGeometry;
        const bool Visible = Current.Visible && Value->Visible && Geometry.Has(StyleGeometryValue::VisibleOpacity);
        const bool Eligible = Current.Eligible && Value->Visible && Value->Enabled;
        const bool OpaqueAncestors = Current.OpaqueAncestors && Geometry.Has(StyleGeometryValue::UnitOpacity);
        const bool Opaque = (Value->ClassName == "Frame" || Value->ClassName == "ScrollingFrame") &&
            OpaqueAncestors && Geometry.FillsOpaqueRectangle();
        const bool Intersects = VisibleBounds.Width > 0 && VisibleBounds.Height > 0;
        const uint32_t Flags = (Visible && Intersects ? LUI_PRESENTATION_VISIBLE : 0) |
            (Eligible && Intersects ? LUI_PRESENTATION_INPUT_ELIGIBLE : 0) |
            (Visible && Intersects && Opaque ? LUI_PRESENTATION_OPAQUE_RECTANGLE : 0);
        Entries.push_back({Value->Id, Value->ParentId, static_cast<uint32_t>(Entries.size()), Flags,
            Value->Bounds.X, Value->Bounds.Y, Value->Bounds.Width, Value->Bounds.Height,
            Clip.X, Clip.Y, Clip.Width, Clip.Height});
        const auto Children = PaintChildren(Runtime, Value);
        if (Children.size() > LUI_WINDOW_PRESENTATION_MAX_ENTRIES - Scheduled) return 2;
        Scheduled += Children.size();
        for (auto Child = Children.rbegin(); Child != Children.rend(); ++Child)
            Stack.push_back({*Child, Clip, Visible, Eligible, OpaqueAncestors});
    }
    std::copy(Entries.begin(), Entries.end(), Snapshot->Entries);
    Snapshot->Count = static_cast<uint32_t>(Entries.size());
    return 1;
}

}
