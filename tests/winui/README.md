# WinUI desktop qualification

The shared C++ conformance suite runs in CI without a display. These cases exercise the real WinUI controls and UI Automation tree in an interactive Windows session. Both Luau scripts are loaded by the headless test so CI also catches syntax and startup errors; the native event observations still require a desktop.

Build with `scripts/Build-Windows.ps1`, then run the generated `Lui.WinUI.exe` with a script path resolved from the repository root. Enable `--diagnostics` to keep an event trace in `%LOCALAPPDATA%\LUI\Logs`.

| Case | Action | Expected observation |
| --- | --- | --- |
| `examples/controls.luau` | Inspect the native accessibility tree | Named text box, slider, and progress bar; disabled states match Luau; native text, checkbox, slider, and button actions update Luau. |
| `examples/grid.luau` | Maximize and restore | Grid wraps to the viewport in logical units; tiles remain equal sizes. |
| `FocusEdge.luau` | Focus Target, type `disable`, Reset, refocus, type `hide`, Reset, refocus | One `A-` on each invalidation, one `B+` on each transfer, one `A+` on each restored focus. |
| `InputQualification.luau` | Inspect Keyboard target in UI Automation | Name is `Keyboard target`; help text is `Type here to verify key and text events`. |
| `InputQualification.luau` | Focus Keyboard target; press and release `A`, then Tab | `K+A`, `Text:a`, `K-A` appear once; focus loss ends any still-active key. Native text entry remains usable. |
| `InputQualification.luau` | Press and release `Press then drag out` | One `P+` and one `P-` with the same pointer ID. |
| `InputQualification.luau` | Press and drag out of the button before release | One `P+` and one `PC` with the same pointer ID; no second end on release. |
| `InputQualification.luau` | Press `Disables on press` | One `D+`, then one `DC`; the control becomes disabled without activating. Reset restores it. |

The status label and optional diagnostics log show the event sequence. Pointer IDs vary per run, so compare pairing and order. UI Automation verifies the platform's accessible name and help text mapping; a later production Windows pass will include assistive technology and high contrast qualification.
