# Foundation 1: canonical UI semantics

Foundation 1 is in progress. The current implementation extends the Foundation 0 proof with shared layout, controls, services, runtime reflection, generated API files, and headless conformance tests. The expanded WinUI controls compile; their interactive behavior still needs desktop qualification.

## Implemented in this increment

- `Position`, `AnchorPoint`, `LayoutOrder`, and read-only derived `AbsolutePosition`/`AbsoluteSize` for GUI objects.
- `UIPadding` and `UIListLayout` with vertical or horizontal fill. LUI computes geometry in logical units and sends resolved bounds to WinUI.
- `Clone()` and `GetDescendants()`, plus cleanup when property initialization fails.
- `TextBox`, `CheckBox`, `Slider`, and `ProgressBar`; `Enabled`, `Checked`, `Minimum`, `Maximum`, `Value`, text, and focus state where applicable.
- `TextChanged`, `CheckedChanged`, `ValueChanged`, `Focused`, and `FocusLost` signals from normalized backend callbacks. Disabled or hidden controls reject new input, and accepted callbacks flush backend changes before returning. Signal callbacks execute through the runtime's single UI thread.
- `app:GetService("WindowService")` with `GetWindows()` and `app:GetService("PlatformService")` with `BackendName` and capability checks. Unsupported capabilities return `false`.
- A runtime [reflection table](../runtime/reflection/Schema.cpp) that gates class creation and property access, and generates [Luau types](../types/LUI.d.luau), [JSON schema](../types/schema.json), and [implemented API reference](API.md).
- Batched property and parent changes: one Luau dispatch sends the final value of each property to the backend before layout.
- A [compatibility policy](COMPATIBILITY.md) for experimental API changes, metadata format versions, and future backend conformance.

## Verify

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Build-Windows.ps1
```

This runs headless runtime, layout, and control conformance tests, regenerates types and API docs, and builds the WinUI host. Use `Lui.WinUI.exe examples/controls.luau` from the generated output folder to inspect the expanded controls on an interactive Windows desktop. The example is also copied into the output folder.

For the current local build, run `out\Foundation1-win-x64-debug\Lui.WinUI.exe` from `out\Foundation1-win-x64-debug`; the default launch loads the Foundation 0 hello example. To open the controls example, pass `examples\controls.luau` as the argument.

## Remaining Foundation 1 work

- Qualify the expanded WinUI controls and focus behavior interactively.
- Add richer input events, sizing constraints, grid layout, accessibility mappings for any custom controls, and additional portable controls where their semantics are clear.
- Expand the shared conformance suite to run against native backends, including focus, disabled state, and accessibility behavior.
- Tighten generated typing so application globals, constructors, service overloads, and read-only properties are represented directly in Luau tooling.

The [full specification](SPEC.md) describes the intended design beyond this increment.
