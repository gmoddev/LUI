# 0004 — Hover input semantics

Date: 2026-09-29

## Context

The specification calls for portable `MouseEnter` and `MouseLeave` signals. Native pointer events can be repeated or arrive while a control is being hidden, disabled, or moved in the tree.

## Decision

Every `GuiObject` exposes `MouseEnter` and `MouseLeave`. A backend reports pointer enter or exit through the internal `Lui_HoverChanged` host function. The runtime tracks whether each object is currently hovered and fires a signal only when that state changes. It accepts an enter only while the object and its ancestors are visible and the object is enabled. An exit can clear existing hover even after the object becomes hidden or disabled. Hiding or disabling an object, or moving a hovered object under a hidden ancestor, clears affected hover and fires `MouseLeave` immediately. Destroying an object ends its signals without synthesizing a leave event.

`MouseEnter` and `MouseLeave` have no event payload. They describe a pointer crossing an object's hit area, regardless of whether the pointer is a mouse, pen, or other pointing device. The WinUI backend maps `PointerEntered` and `PointerExited` to these signals.

## Consequences

Duplicate native notifications do not create duplicate Luau events. Other backends can use the same transition function without exposing platform message types. Key, press, release, and motion events remain later Foundation 1 work.
