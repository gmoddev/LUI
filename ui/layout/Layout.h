#pragma once

struct LuiRuntime;
struct Node;
struct VectorValue;
struct BoundsValue;

namespace LuiLayout {

VectorValue ResolveConstrainedSize(LuiRuntime* Runtime, const Node* Value, double ParentWidth, double ParentHeight);
void ArrangeNode(LuiRuntime* Runtime, Node* Value, BoundsValue Bounds);

}
