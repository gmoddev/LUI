# LuiUI (next SDK development)

Optional Luau component sources for SDK `0.1.0-preview.2` development. Importing
starts no tasks, services, subscriptions or windows. Windows headless types,
control semantics and owner-input admission pass using the audited SDK12 runtime.
Linux, native GUI, real input methods and assistive technology remain unqualified
for this extraction. Released preview.1 and prepared SDK12 are unchanged.

```luau
local NumericInput = require('@LuiUI/NumericInput')
local Zoom = NumericInput.New(Parent, {
    Value = 100, Minimum = 25, Maximum = 400, Step = 25, Label = 'Zoom percent',
    OnChanged = function(Value) PreviewZoom = Value end,
})
Zoom:SetValue(125)
local Current = Zoom:GetValue()
Zoom:Dispose()
```

NumericInput composes existing Frame, TextBox and TextButton Instances. View,
Input, Decrease and Increase expose these real LUI controls for application
styling. The controller table is frozen. Layout uses a 36-unit height and two
36-unit buttons; applications own sizing and their surrounding responsive layout.

Options must be a plain table with only Value, Minimum, Maximum, Step, Label and
OnChanged. Omitted bounds default to 0–100, step to 1 and value to the minimum.
Bounds/values are finite, with -1e12 <= Minimum <= Maximum <= 1e12; step is positive
and at most 2e12. False values and unknown fields are refused before allocating
controls. Labels accept at most 512 valid UTF-8 bytes without NUL. Options and
callback selection are captured at construction, so later table edits do not
replace component behavior.

SetValue accepts an in-range finite number. Commit parses the current text with
Luau tonumber, clamps a finite number to the bounds and normalizes its display.
Invalid/nonfinite text restores the current value and returns false; successful
admission returns true. No locale-specific numeric parser is supplied. Equal
values normalize text without another OnChanged call. Step buttons clamp at the
bounds and are disabled there. Commit and step actions leave active IME preedit
alone. An explicit application SetValue can replace the text during composition.

Rendering and callbacks tolerate reentrant value changes or disposal. A newer
value wins; retired controls receive no later component callback. OnChanged
failure follows the existing State delivery contract with a fixed error. Dispose
is idempotent, disconnects owned connections and destroys all four owned controls.
Reparenting a part does not transfer its ownership. Destroying an ancestor or any
component part retires the controller too. Failed construction cleans up admitted
controls and reports ConstructionFailed without the original exception text.
GetValue retains the last scalar after
retirement; setters and Commit refuse it. All calls belong on LUI's scheduler
thread. The helper creates no competing scheduler or native lifetime model.
Callbacks run inline in the current scheduler task. Keep them brief or use the
application's existing task/service cancellation. Dispose does not cancel a
callback that already entered or work started by application code.
The module Version is 0.1.0.

Scope and State are private support sources copied from the canonical helpers
already vendored by the conversation and workbench examples. NumericInput uses
their existing cleanup and bounded reentrant delivery behavior; no new state or
lifetime API is introduced. The catalog records them as SupportFiles, and the
ordinary project content lock captures their exact bytes.

This initial public module is NumericInput. Earlier examples
may vendor additional LuiUI modules; retain their explicit module lists and
content locks when adapting those projects. No native ABI, reflected core class,
filesystem permission, network capability or GTK performance code changes.
