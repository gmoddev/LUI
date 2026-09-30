# 0011: Accessible names and descriptions

Native WinUI controls supply roles and states, but a text box or slider without visible control text can remain unnamed in the accessibility tree. Foundation 1 needs portable metadata for those controls before treating native accessibility as qualified.

`GuiObject` now exposes `AccessibilityLabel` and `AccessibilityDescription`, both empty by default. A label overrides the native accessible name, and a description provides additional help text. Empty values preserve native naming behavior from visible text where the platform provides it. These strings are owned by LUI, included in reflection and generated types, copied by `Clone()`, and sent to the backend as ordinary property changes. WinUI maps them to UI Automation name and help text. Other backends must implement equivalent semantics before claiming accessibility conformance.
