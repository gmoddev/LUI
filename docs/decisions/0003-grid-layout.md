# 0003 — Grid layout semantics

Date: 2026-09-29

## Context

Foundation 1 has absolute placement and list layout. Applications also need a portable grid whose geometry can be tested without WinUI. Size constraints can enlarge individual controls, so the grid must leave enough room for them.

## Decision

`UIGridLayout` is a nonvisual child of a `Window` or `Frame`. A container permits either one list layout or one grid layout, alongside one padding component. The grid sorts visual children by `LayoutOrder`, preserving insertion order for ties, and fills rows from left to right.

`CellSize` is a `UDim2` resolved against the padded content box and defaults to 100 by 100 logical units. Negative resolved dimensions become zero. `CellPadding` is a `UDim2` with nonnegative scale and offset components and defaults to zero. Every child gets the resolved cell size instead of its own `Size`; its `Position` and `AnchorPoint` do not affect arrangement. A child's size constraint may enlarge or shrink its actual size. The grid uses the maximum of the base cell size and all constrained child sizes for a uniform slot, so a larger item does not overlap the next slot. It fits as many columns as possible in the content width, with at least one column, and wraps remaining items into later rows.

## Consequences

Grid layout uses the same LUI geometry pass and backend arrangement as other layouts. A slot may overflow a container that is narrower than the slot; clipping remains a separate policy. Resizing the container, changing grid properties, child order, or constraints recalculates columns and positions.
