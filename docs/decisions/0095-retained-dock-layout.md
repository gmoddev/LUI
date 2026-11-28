# 0095. Optional retained desktop docking

**Status.** Implemented in the optional source library.

Applications opt into two LuiWorkbench modules. Model authoring, persisted data, interaction, content lifetime and native presentation have independent owners. LUI core starts no docking service or timer for applications which never mount a desk.

## Model and persistence

A snapshot has `Version=1`, optional `Main` and a dense `Floats` array. An empty main window omits `Main`. Each node has a stable `Id`. A group contains ordered `Tabs` and an `Active` member. A split contains `Axis` (`X` or `Y`), `Ratio` strictly between zero and one, and children `First` and `Second`. Floating records have `Id`, `Tree`, logical client `Width` and `Height`, optional `OriginGroup`, `OriginSlot` and `Placement={X,Y,Width,Height}` in physical outer-window pixels. Window ID `Main` is reserved.

The model admits up to 256 registered panels, 32 floats, 32 levels and 128 UTF-8 bytes per identifier. Panel placements and node/float IDs are unique. Each active tab must belong to its group. Accepted records are copied into immutable state. Malformed layouts preserve the previous state. Applications migrate legacy records and own their defaults.

Detaching the last tab of a group can collapse its split. The optional float field `OriginSplit={Id,Axis,Ratio,First,Anchor,Panel,Window}` retains the original split ID, orientation, ratio, child side, sibling node, sibling panel and window. Redock reconstructs that edge when the sibling still exists, including after restart. This restores a tool below its previous neighbor even when its group temporarily ceased to exist.

An optional `Hidden` array stores `{Panel,Group,Window,Slot,Split?}` for deliberately hidden panels. The split hint uses the same format. Showing a panel restores a surviving tab slot or split edge. Passing an explicit group overrides the hint. When the original window and anchor have both closed the panel returns to the main window. Unregistered content leaves no persisted placement hint.

`PrepareMove` returns a proposed layout and leaves the current layout untouched. Only its originating model can commit it while that original layout is still current. Unregistration invalidates pending plans. Removal collapses obsolete singleton splits. Prediction and projection share `Arrange`. Recursive size limits include tab strips and dividers. Undersized native windows divide their available extent without negative rectangles. A splitter projects a transient ratio and commits once on release.

## Projection and native windows

The host retains a supplied content object and one tab per registered ID. Surviving controls leave obsolete chrome before its destruction. Content owners keep selection, buffers, undo history and asynchronous work. They may rebind a native render sink on window migration independently of that content identity.

The presenter tracks the initiating pointer ID and button. Five local cells and four outer cells share hit and draw rectangles. The preview uses the proposed post-removal tree. Tab insertion uses a caret. Native floats have independent contents, selectors and shortcuts. Closing or redocking returns ordered panels to their surviving origin group and slot, with the first main group as fallback. `OnWindow` installs application shortcuts. A focus request still requires normal native focus acknowledgement.

Window metrics are optional. The Windows declaration-backed adapter resolves owned windows, signed screen coordinates, scale and outer bounds. GTK supports compositor-managed top-level creation, resize and redock. Its optional global metrics binding returns unavailable. A local drag and explicit redock remain available when screen mapping is unavailable. Saved floating dimensions always include a logical fallback.

The source checkout includes the model and lifetime suite in `tests/libraries/Docking.luau` and the nine-target native ABI replay in `tests/libraries/DockInput.py`.
