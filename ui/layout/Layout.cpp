#include "Layout.h"
#include "../../runtime/internal/Diagnostics.h"
#include "../../runtime/internal/State.h"

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
    Value->Bounds = Bounds;
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

    double Left = Padding ? Resolve(Padding->PaddingLeft, Bounds.Width) : 0;
    double Right = Padding ? Resolve(Padding->PaddingRight, Bounds.Width) : 0;
    double Top = Padding ? Resolve(Padding->PaddingTop, Bounds.Height) : 0;
    double Bottom = Padding ? Resolve(Padding->PaddingBottom, Bounds.Height) : 0;
    BoundsValue Inner{Bounds.X + Left, Bounds.Y + Top,
        std::max(0.0, Bounds.Width - Left - Right), std::max(0.0, Bounds.Height - Top - Bottom)};

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

}
