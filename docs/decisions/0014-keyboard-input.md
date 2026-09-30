# 0014: Canonical keyboard input

**Status:** accepted, 2026-09-30

## Context

Foundation 1 already exposes `InputBegan`, `InputChanged`, and `InputEnded` for pointers. Native controls handle text entry and activation, but Luau also needs portable physical key events without exposing WinUI virtual key values. A key can repeat, and focus can leave before its native release arrives.

## Decision

The three input signals carry a read-only `InputEvent` union. Pointer events retain `Device`, `PointerId`, and local logical `Position`, and gain `IsCanceled`. Keyboard events have `Device = "Keyboard"`, a canonical `Key`, and `IsRepeat`. Applications distinguish the payloads by `Device`; text composition remains the `TextBox.TextChanged` contract.

The canonical keys are `A`–`Z`, `0`–`9`, `F1`–`F12`, `Enter`, `Escape`, `Tab`, `Space`, `Backspace`, `Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown`, `ArrowLeft`, `ArrowRight`, `ArrowUp`, `ArrowDown`, `Shift`, `Control`, `Alt`, and `CapsLock`. Other native keys do not enter the portable event stream yet. WinUI maps its virtual keys to these names and includes events handled by native controls.

A first press fires `InputBegan` with `IsRepeat = false`; a repeat fires `InputChanged` with `IsRepeat = true`; a paired release fires `InputEnded` with `IsRepeat = false`. A duplicate press on an active key counts as a repeat. An unpaired repeat or release is ignored. Only a focused, input-eligible GUI object can begin or repeat a key. Focus loss ends active keys after `FocusLost` and before a new object's `Focused`. Hiding or disabling ends keys after focus, hover, and active pointer cancellations; destruction drops them without further callbacks. Key ends are ordered by canonical key name when multiple keys are active.

## Consequences

The runtime owns pairing and ordering, and headless conformance tests cover it. Backends map platform keys and route native events to the nearest LUI view. The experimental generated type changed from pointer-only input signals to a discriminated `InputEvent` union; applications using pointer fields must check `Device` first. IME text, character layout, system shortcuts, and unmapped keys stay under native control.
