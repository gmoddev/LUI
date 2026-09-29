# 0001 — Initial layout semantics

Date: 2026-09-29

## Context

Foundation 0 resolved sizes but placed every child at the origin. Foundation 1 needs deterministic geometry that can be tested without WinUI.

## Decision

LUI resolves `UDim` and `UDim2` in logical units. `Position` is relative to the padded parent content box; `AnchorPoint` shifts the child by a fraction of its own resolved size. `UIPadding` insets the content box. A container may have one `UIPadding` and one `UIListLayout`. List layout orders visual children by `LayoutOrder`, preserving insertion order for ties; it places them along the selected axis with `Padding` between items. When a list layout controls a child, its `Position` and `AnchorPoint` do not affect the arranged position. `AbsolutePosition` and `AbsoluteSize` are read-only snapshots of the resolved geometry and force a layout pass when read after mutation.

## Consequences

The same layout pass feeds headless tests and the WinUI backend. Native controls may provide intrinsic measurements later, but the backend does not choose final geometry. Applications needing placement independent of list order should use a container without `UIListLayout`.
