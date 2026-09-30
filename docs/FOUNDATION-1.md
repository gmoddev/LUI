# Foundation 1: canonical UI semantics

Foundation 1 is in progress. The current implementation extends the Foundation 0 proof with shared layout, controls, services, runtime reflection, generated API files, and headless conformance tests. The expanded WinUI controls compile and the controls example launches on Windows; interactive event behavior still needs desktop qualification.

## Implemented in this increment

- `Position`, `AnchorPoint`, `LayoutOrder`, and read-only derived `AbsolutePosition`/`AbsoluteSize` for GUI objects.
- `UIPadding`, `UIListLayout` with vertical or horizontal fill, and row-major `UIGridLayout` with automatic column wrapping. A container may have padding and one list or grid layout. LUI computes geometry in logical units and sends resolved bounds to WinUI.
- `UISizeConstraint` clamps a visual object's resolved width and height with nonnegative `MinSize` and optional `MaxSize`. One constraint may be attached to each `Window` or GUI object; clamping occurs before anchor placement and list spacing.
- `Clone()` and `GetDescendants()`, plus cleanup when property initialization fails.
- `TextBox`, `CheckBox`, `Slider`, and `ProgressBar`; `Enabled`, `Checked`, `Minimum`, `Maximum`, `Value`, text, and focus state where applicable.
- `TextChanged`, `CheckedChanged`, `ValueChanged`, `Focused`, and `FocusLost` signals from normalized backend callbacks. Disabled or hidden controls reject new input, and accepted callbacks flush backend changes before returning. Signal callbacks execute through the runtime's single UI thread.
- Focus transitions are owned by the runtime: only one GUI object is focused at a time, previous focus is lost before new focus is announced, and hiding, disabling, or reparenting into a hidden container clears focus.
- `MouseEnter` and `MouseLeave` signals for GUI objects, normalized from pointer enter/exit callbacks. Repeated enter/exit notifications are collapsed; disabling, hiding, or reparenting into a hidden container ends an active hover. The controls example changes the submit button label on hover.
- `InputBegan`, `InputChanged`, and `InputEnded` for pointer presses, moves, releases, and cancellations. Each callback receives a read-only event with `Device`, `PointerId`, and local `Position`. Active presses end if the object becomes hidden, disabled, or moves into a hidden container. The controls example logs button presses and releases.
- `app:GetService("WindowService")` with `GetWindows()` and `app:GetService("PlatformService")` with `BackendName` and capability checks. Unsupported capabilities return `false`.
- A runtime [reflection table](../runtime/reflection/Schema.cpp) that gates class creation and property access, and generates [Luau types](../types/LUI.d.luau), [JSON schema](../types/schema.json), and [implemented API reference](API.md).
- Generated Luau definitions cover application globals, class constructors, services, signals, and read-only fields. The Windows build loads those definitions with the pinned Luau type checker and checks valid and invalid application snippets.
- Batched property and parent changes: one Luau dispatch sends the final value of each property to the backend before layout.
- Backend notifications raised synchronously during Luau execution or a native callback are queued and dispatched at a safe scheduler boundary. Reentrant script, pump, and runtime destruction calls are rejected. See [VM entry decision](decisions/0007-vm-entry-and-backend-events.md).
- Internal backend callbacks report success or failure. Failed creation is rolled back; other failures stop the runtime and surface a structured diagnostic instead of silently continuing. Headless tests inject each failure type. See [backend failure decision](decisions/0008-backend-failures.md).
- A [compatibility policy](COMPATIBILITY.md) for experimental API changes, metadata format versions, and future backend conformance.

## Verify

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Build-Windows.ps1
```

This runs headless runtime, layout, and control conformance tests, regenerates types and API docs, validates the generated definitions, and builds the WinUI host. Use `Lui.WinUI.exe examples/controls.luau` from the generated output folder to inspect the expanded controls on an interactive Windows desktop. The example is also copied into the output folder.

Run `Lui.WinUI.exe examples/grid.luau` from the same folder to inspect grid wrapping and size constraints. The grid example is also copied into the output folder.

The type file infers specific result types for the seven primary constructors. `ProgressBar`, `UIPadding`, `UIListLayout`, `UIGridLayout`, and `UISizeConstraint` share a union result type because the pinned Luau type checker rejects a larger overload intersection. Constructor property tables accept `any`; annotate a table with its generated `ClassNameInit` type when static property checking is needed:

```luau
local Props: WindowInit = {Title = "Typed", Size = UDim2.fromOffset(480, 320)}
local Window = Instance.new("Window", Props)
```

After building on Windows, run `Lui.WinUI.exe` from `backends\winui3\bin\x64\Debug\net9.0-windows10.0.19041.0\win-x64`. The default launch loads the Foundation 0 hello example. Pass `examples\controls.luau` or `examples\grid.luau` to open a Foundation 1 example.

Add `--diagnostics` before or after the script path to open a live diagnostics console. It shows WinUI create/property/layout/input traces, Luau `print()` output, runtime errors, and caught .NET or XAML exceptions. Each line is also saved under `%LOCALAPPDATA%\LUI\Logs` with a timestamp and process ID. The console closes with the app, but the log remains after a crash. Normal launches keep diagnostics off.

```powershell
.\Lui.WinUI.exe --diagnostics examples\controls.luau
```

## Remaining Foundation 1 work

- Qualify the expanded WinUI controls and focus behavior interactively.
- Add keyboard input to the canonical event model, accessibility mappings for any custom controls, and additional portable controls where their semantics are clear.
- Expand the shared conformance suite to run against native backends, including focus, disabled state, and accessibility behavior.
- Improve constructor typing when Luau can accept more overloads without losing static checks on property tables.
- Consolidate method signatures, signals, and container/parent rules into structured reflection metadata; split the runtime into its existing subsystem boundaries before adding more major behavior.
- Add CI that runs the pinned Windows build and headless suite on each proposed change.

The [full specification](SPEC.md) describes the intended design beyond this increment.
