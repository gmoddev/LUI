#pragma once

struct LuiRuntime;
struct Node;
struct VectorValue;
struct BoundsValue;
struct LuiWindowPresentationV1;

namespace LuiLayout {

VectorValue ResolveConstrainedSize(LuiRuntime* Runtime, const Node* Value, double ParentWidth, double ParentHeight);
void ArrangeNode(LuiRuntime* Runtime, Node* Value, BoundsValue Bounds);
bool HasViewportIntersection(const LuiRuntime* Runtime, const Node* Value);
bool ContainsViewportPoint(const LuiRuntime* Runtime, const Node* Value, double X, double Y);
int HitTest(const LuiRuntime* Runtime, const Node* Window, double X, double Y);
void PresentTree(LuiRuntime* Runtime, Node* Window);
int GetWindowPresentation(const LuiRuntime* Runtime, const Node* Window, LuiWindowPresentationV1* Snapshot);

}
