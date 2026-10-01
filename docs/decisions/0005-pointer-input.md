# 0005 — Pointer input semantics

Date: 2026-09-29

## Context

`MouseEnter` and `MouseLeave` cover hover but cannot describe a press or pointer movement. WinUI controls may consume routed pointer events internally, and pointer interactions can end through cancellation or capture loss rather than release.

## Decision

All `GuiObject` instances expose `InputBegan`, `InputChanged`, and `InputEnded`. Pointer payloads are read-only `PointerInput` tables with `Device` (`Mouse`, `Pen`, `Touch`, or `Touchpad`), `PointerId`, `Position` (`Vector2` in the target object's local logical coordinates), and `IsCanceled`. Pointer IDs distinguish simultaneous contacts during a running application and are not persistent identifiers.

The internal host callback reports pressed, moved, released, or canceled. An accepted press starts one active pointer on the target; a repeated press with the same ID does not fire twice. Movement fires `InputChanged` even without an active press. Release or cancellation ends an active pointer once; an unmatched end is ignored. Pointer exit, native cancellation, and capture loss cancel an active pointer. `InputEnded.IsCanceled` distinguishes cancellation from release; it is also true when hiding, disabling, or moving a pressed object under a hidden ancestor ends its active pointers at their last known positions, in ascending pointer ID order. Destruction drops active pointers without firing further signals. Invalid phases, devices, coordinates on noncancel events, targets, and input to hidden or disabled objects are rejected.

WinUI uses `AddHandler` with handled events included so standard controls also report pointer activity. Existing control actions such as `Activated` remain separate and continue to work through their native control events. Keyboard input now shares the three input signals through the `InputEvent` union; see [decision 0014](0014-keyboard-input.md).

WinUI can report pointer capture loss during an ordinary button release before LUI receives the routed release. The backend defers capture-loss cancellation until the current UI dispatch finishes. A release then ends the active pointer normally; genuine capture loss still cancels it. The 2026-10-01 native input matrix confirmed normal release, drag-out cancellation, and cancellation when a target disables itself during its press callback.

## Consequences

The runtime owns pointer state, validation, and callback ordering. Headless conformance tests exercise the same entry point as WinUI.
