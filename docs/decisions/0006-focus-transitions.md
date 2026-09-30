# 0006 — Focus transitions

Date: 2026-09-29

## Context

The backend already reports focus changes, but the runtime could retain `IsFocused = true` after a control was hidden or disabled and could accept focus on multiple objects at once. That made focus state depend on the backend's notification order.

## Decision

At most one `GuiObject` is focused per LUI runtime. When a new object receives focus, the runtime clears any previous focused object and fires its `FocusLost` signal before setting the new object's `IsFocused` state and firing `Focused`. Repeated focus or loss notifications are accepted without repeating a signal. A focus gain is rejected if the target or an ancestor is hidden, or the target is disabled.

Hiding, disabling, or moving a focused object under a hidden ancestor clears focus and fires `FocusLost` immediately. If the same change also ends hover or active pointer input, the runtime fires `FocusLost`, then `MouseLeave`, then `InputEnded` for active pointers in ascending pointer ID order. Destruction clears focus state without a further focus callback, consistent with destruction ending signals.

## Consequences

Headless and native backends share focus state and event ordering. Delayed native `LostFocus` callbacks after a runtime-driven loss are harmless duplicates. Interactive WinUI behavior still requires desktop qualification.
