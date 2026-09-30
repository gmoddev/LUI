# 0013: Invalidate WinUI focus cache when a view cannot receive input

**Status:** accepted, 2026-09-30

The runtime clears `IsFocused` and fires `FocusLost` when a focused object is hidden, disabled, or moved under a hidden ancestor. WinUI focus notifications can arrive after those changes. The backend previously remembered the last focused view ID until another native focus notification arrived, leaving its deduplication state out of step with the runtime.

The WinUI backend now tracks visibility and enabled state for each native view. It walks the parent chain before accepting a focused element and clears its cached ID after relevant property or parent changes when that chain can no longer receive focus. A late native loss is then harmless, and a later native gain after restoration can be forwarded. The runtime remains the authority for the public focus state and event ordering.

The interactive [focus case](../../tests/winui/FocusEdge.luau) covers a text box disabling and hiding itself while focused, followed by restoration and refocus. The Windows desktop run showed one loss for each invalidation, one gain on the other text box, and one gain on each restored refocus. Broader native backend conformance and screen reader qualification remain Foundation 1 work.
