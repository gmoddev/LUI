# Foundation 1: canonical UI semantics

Foundation 1 is in progress. The current implementation extends the Foundation 0 proof with shared layout, controls, services, runtime reflection, generated API files, and headless conformance tests. The expanded WinUI controls compile and have interactive desktop qualification cases in [tests/winui](../tests/winui/README.md).

## Implemented in this increment

- `Position`, `AnchorPoint`, `LayoutOrder`, and read-only derived `AbsolutePosition`/`AbsoluteSize` for GUI objects.
- `UIPadding`, `UIListLayout` with vertical or horizontal fill, and row-major `UIGridLayout` with automatic column wrapping. A container may have padding and one list or grid layout. LUI computes geometry in logical units and sends resolved bounds to WinUI.
- The layout solver lives in [ui/layout](../ui/layout/Layout.cpp), and input/focus state and event entry points live in [runtime/input](../runtime/input/Input.cpp). Narrow internal interfaces keep runtime state private to the native implementation; the scheduler still controls when layout and backend changes flush.
- Native window content-size changes now update LUI layout bounds. The grid example reflows when the WinUI window is maximized and restored; the requested `Window.Size` remains stable while `AbsoluteSize` follows the viewport. See [window viewport decision](decisions/0010-native-window-viewport.md).
- `UISizeConstraint` clamps a visual object's resolved width and height with nonnegative `MinSize` and optional `MaxSize`. One constraint may be attached to each `Window` or GUI object; clamping occurs before anchor placement and list spacing.
- `Clone()` and `GetDescendants()`, plus cleanup when property initialization fails.
- `TextBox`, `CheckBox`, `Slider`, and `ProgressBar`; `Enabled`, `Checked`, `Minimum`, `Maximum`, `Value`, text, and focus state where applicable.
- `AccessibilityLabel` and `AccessibilityDescription` on GUI objects, mapped to WinUI Automation name and help text. The controls example gives the name field, slider, and progress bar accessible names. See [accessibility metadata decision](decisions/0011-accessibility-metadata.md).
- `TextChanged`, `CheckedChanged`, `ValueChanged`, `Focused`, and `FocusLost` signals from normalized backend callbacks. Disabled or hidden controls reject new input, and accepted callbacks flush backend changes before returning. Signal callbacks execute through the runtime's single UI thread.
- Focus transitions are owned by the runtime: only one GUI object is focused at a time, previous focus is lost before new focus is announced, and hiding, disabling, or reparenting into a hidden container clears focus.
- WinUI samples the focused element for its XAML root and deduplicates asynchronous focus notifications before forwarding transitions. A Windows qualification window confirmed one loss/gain pair between text boxes, Tab navigation past a disabled button, and keyboard activation of the enabled button. See [WinUI focus decision](decisions/0012-winui-focus-notifications.md).
- The WinUI focus bridge tracks effective visibility and enabled state through each view's ancestors. When a focused view becomes ineligible during a property or parent update, it clears its native focus cache so a later restored focus is delivered. The [interactive focus case](../tests/winui/FocusEdge.luau) confirmed disable, hide, restore, and refocus transitions on Windows. See [focus invalidation decision](decisions/0013-winui-focus-invalidation.md).
- `MouseEnter` and `MouseLeave` signals for GUI objects, normalized from pointer enter/exit callbacks. Repeated enter/exit notifications are collapsed; disabling, hiding, or reparenting into a hidden container ends an active hover. The controls example changes the submit button label on hover.
- `InputBegan`, `InputChanged`, and `InputEnded` for pointer and keyboard input. Pointer payloads have `Device`, `PointerId`, local `Position`, and `IsCanceled`; canceled presses are distinct from releases. Keyboard payloads have `Device = "Keyboard"`, canonical `Key`, and `IsRepeat`. Focus loss, hiding, and disabling end active keys and pointers in defined order. See [keyboard input decision](decisions/0014-keyboard-input.md) and [pointer input decision](decisions/0005-pointer-input.md).
- `app:GetService("WindowService")` with `GetWindows()` and `app:GetService("PlatformService")` with `BackendName` and capability checks. Unsupported capabilities return `false`.
- A runtime [reflection table](../runtime/reflection/Schema.cpp) that gates class creation and property access, and generates [Luau types](../types/LUI.d.luau), [JSON schema](../types/schema.json), and [implemented API reference](API.md).
- Structured reflection also defines method signatures, signal types, service members, parent rules, and container capability. Runtime member lookup and parenting use it, and the generated schema is version 2. See [reflection decision](decisions/0009-structured-reflection.md).
- Generated Luau definitions cover application globals, class constructors, services, signals, and read-only fields. The Windows build loads those definitions with the pinned Luau type checker and checks valid and invalid application snippets.
- Batched property and parent changes: one Luau dispatch sends the final value of each property to the backend before layout.
- Backend notifications raised synchronously during Luau execution or a native callback are queued and dispatched at a safe scheduler boundary. Reentrant script, pump, and runtime destruction calls are rejected. See [VM entry decision](decisions/0007-vm-entry-and-backend-events.md).
- Internal backend callbacks report success or failure. Failed creation is rolled back; other failures stop the runtime and surface a structured diagnostic instead of silently continuing. Headless tests inject each failure type. See [backend failure decision](decisions/0008-backend-failures.md).
- A [compatibility policy](COMPATIBILITY.md) for experimental API changes, metadata format versions, and future backend conformance.
- CI runs the pinned Windows build, headless and type tests, and a WinUI compile on changes to `main` and proposed changes. A Linux job compiles and runs the headless suite and checks that reflection emits the same schema.

## Verify

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Build-Windows.ps1
```

This runs headless runtime, layout, and control conformance tests, regenerates types and API docs, validates the generated definitions, and builds the WinUI host. Use `Lui.WinUI.exe examples/controls.luau` from the generated output folder to inspect the expanded controls on an interactive Windows desktop. The example is also copied into the output folder.

Run `Lui.WinUI.exe examples/grid.luau` from the same folder to inspect grid wrapping and size constraints. The grid example is also copied into the output folder.

Run `Lui.WinUI.exe tests/winui/FocusEdge.luau` from the repository root to qualify focus invalidation. Focus the target field, type `disable`, click Reset target, refocus it, type `hide`, click Reset target, then refocus it. The status line should gain one `A-` and one `B+` for each invalidation, and one `A+` for each refocus.

Use the [WinUI desktop qualification matrix](../tests/winui/README.md) for keyboard pairing, pointer release and cancellation, accessibility metadata, focus, controls, and grid reflow. The qualification scripts also load in the headless CI test, which checks syntax and startup behavior without claiming native event verification.

Run `Lui.WinUI.exe --native-qualification` from the generated output folder for the automatic native mapping probe. It records a nonmodal log, checks native controls and event round trips, and exits with a pass/fail process code. The interactive input matrix still requires a Windows desktop session.

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

- Complete the WinUI desktop qualification matrix for keyboard pairing and pointer cancellation, including press during disable. Native UI Automation exposed the configured accessible name and help text on 2026-09-30. Focus, disabled state, text entry, checkbox, slider, button hover, and grid resize have been observed on Windows.
- Run the native mapping probe and finish the interactive keyboard and pointer matrix in a Windows desktop session. Windows CI compiles the probe and checks all qualification scripts for startup errors, but it does not host a native desktop session.
- Improve constructor typing when Luau can accept more overloads without losing static checks on property tables.
- Continue moving object, scheduler, and signals code into internal modules as the runtime grows. Input and focus now have their own internal module.

The [full specification](SPEC.md) describes the intended design beyond this increment.
