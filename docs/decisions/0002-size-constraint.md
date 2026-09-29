# 0002 — Size constraint semantics

Date: 2026-09-29

## Context

Foundation 1 resolves `Size` in LUI, but applications need portable minimum and maximum bounds for controls and windows. Native toolkits may report preferred sizes later; those measurements cannot define final layout independently.

## Decision

`UISizeConstraint` is a nonvisual child of a `Window` or GUI object. One constraint is allowed per visual object. `MinSize` defaults to `(0, 0)` and `MaxSize` defaults to `nil`, meaning unbounded. Both use logical units and must be finite and nonnegative. When present, `MaxSize` must be at least `MinSize` on each axis. Construction validates the pair together, independent of table iteration order. Later invalid assignments fail without changing the current values.

LUI resolves the visual object's `Size`, clamps each axis, then uses the clamped size for anchors, list spacing, `AbsoluteSize`, child layout, and backend arrangement. The same rule applies to the root window.

## Consequences

A constraint can make a child larger than its parent's content box; clipping remains a separate policy. A native backend receives the final bounds and does not apply another size policy. Intrinsic measurement can feed a future layout stage without changing this clamping rule.
