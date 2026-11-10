# WinUI desktop qualification

## Native temporary text formatting proof (2026-11-03)

`LuiTemporaryTextTests` uses the system `Msftedit.dll` `RICHEDIT50W` control,
`EM_GETOLEINTERFACE`, and `ITextDocument` on a hidden Win32 parent. For every
styled range it calls `ITextFont::Reset(tomApplyTmp)` before setting the foreground
color. The fixture checks exact Unicode text, selection, scroll, modified flag,
one native Undo, a style update while Redo remains available, and one native
Redo. It never clears or suspends Undo during styling; the initial empty-Undo
call occurs only before the synthetic edit.

This is a state/Undo proof only. A later visible Win32 comparison used four
Rich Edit controls in one window: plain black, `Reset(tomApplyTmp)` before red,
`Reset(tomApplyLater)`/red/`Reset(tomApplyTmp)`, and permanent red. Window
Graphics Capture showed both temporary variants as black and the permanent
control as red. The temporary API calls succeeded but did not produce visible
syntax color on this system. The earlier temporary result must not be used as
evidence of displayed highlighting.

A separate suspended-format candidate calls `ITextDocument::Undo(tomSuspend)`,
sets permanent foreground colors, calls `Undo(tomResume)`, then restores the
original Rich Edit modified bit with `EM_SETMODIFY`. The visible comparison
showed red for the styled editor and black after a clear batch. A hidden native
proof passed exact Unicode text, selection, scroll, modified bit, one native
Undo/Redo, style and clear while Redo remained available. The bridge checks
state after a bounded batch of at most 128 ranges. These results qualify the
system Rich Edit control's color and history behavior. WinUI XAML hosting and
native input remained separate gates at that stage.
The WinUI-hosted diagnostic showed three live/hit-testable Rich Edit children
with correct UI Automation text but white, unpainted client regions in Window
Graphics Capture, including an unstyled black baseline. The same host painted
normally beneath a plain Win32 top-level window. A WinUI child-HWND paint path
is therefore unqualified; geometry and UIA alone are insufficient.
An owned `WS_POPUP` Rich Edit host placed above the WinUI window did paint.
Formatting initially appeared black because the host treated Rich Edit's own
formatting `EN_CHANGE` as a text edit and immediately cleared the colors. A
scoped native notification guard fixed that reentrancy. The subsequent Window
Graphics Capture showed red selected text in the suspended-Undo editor, black
in the temporary-format candidate, and red in the permanent positive control.
The same editor returned to black after a clear batch; the captured red-pixel
count changed from 63 to 3, while the positive control stayed red. The TOM
foreground changed from COLORREF 240 to `tomAutoColor` on clear. The popup
fixture retained exact source, native visibility and hit testing throughout.
These are visible paint observations on the selected desktop, not a claim about
every WinUI composition or assistive-technology environment.

The opt-in `CodeEditor` runtime fixture now drives one owned popup Rich Edit
through Luau `TextChanged` and versioned style batches. It passed native typing,
styling, Undo/Redo, typing after LF and a surrogate pair, cross-window redock,
and HWND retirement in the interactive WinUI host. Synthetic native IME start
and end messages reached Luau composition signals. The visual fixture separately
asserted red paint and black after clearing. These fixtures do not exercise an
actual IME candidate, native drag/drop, assistive technology, or all popup
airspace and clipping scenarios. `Supports('CodeEditorStyles')` remains false
while those admission and presentation gates are open.
After native text edits, `CodeEditor.MaxLength` cannot be set below the longest
text seen in that edit history, so Undo cannot restore text beyond the new
limit. The native pointer bridge maps left/right/middle to the shared button
codes and converts HWND pixels to logical coordinates. CodeEditor keys reach
`InputBegan`/`InputEnded`; `Window.Shortcut` is not forwarded from its HWND
because Rich Edit does not expose a reliable post-default handled result. This
is an additional capability gate. Oversized GTK insertions are rejected before
allocation or mutation, and CRLF paste remains a single native Undo step.

The opt-in `--code-editor-observation` fixture holds one live Rich Edit popup
for desktop input and writes its native/runtime text, selection, signal counts
and retirement result. On the selected Windows desktop, UI Automation exposed
the Rich Edit document and selected text; a pointer drag selected text, typing
replaced it, and Ctrl+Z/Ctrl+Y restored and reapplied the edit. Moving the
owner preserved the popup's relative position. A higher `ZIndex` XAML sibling
was present in the accessibility tree but covered by the popup. Hiding only
the editor exposed that sibling, confirming a real popup airspace limit.

The private `LuiWinUITextSetClipV2` prototype subtracts at most 16 validated
host-local physical-pixel rectangles from the popup's Win32 region. It leaves
the single Rich Edit buffer and V1 clipping entry point intact. A fixture with
one fully opaque XAML frame and button showed the button through the cutout
while the editor remained visible; actual pointer input activated the button.
The fixture rejects invalid rectangles/counts/coordinates, reads back the
region, and checks that 128 replacements do not grow GDI objects without
bound. Production cannot use this cutout until the framework supplies exact
opaque occluder geometry and stack order across move, resize, minimize and
redock; transparent or rounded overlays need separate handling. Styling
capability remains false. The runtime fixture manually applies V2 after
redock. Its minimize/restore assertions preserve the editor HWND, text,
selection, scroll and Undo; they do not read back the cutout after restore or
prove automatic region reapplication.

`--code-editor-performance` measures 128 edits to a 32,768-character editor
without styles, followed by 128 edits with 128 color ranges. The reported
boundary is synchronous `WM_CHAR` through owner `Pump`, including native edit,
runtime text admission and style delivery. It excludes compositor paint,
physical display and lexer work. The sequential phases are local measurements,
not a causal comparison or an end-to-end typing-to-paint result. Managed
allocation and post-collection live bytes are recorded separately from process
private bytes, which can grow while the native editor remains alive.

The active user profile exposed only an English keyboard. A process-local
`ImmSetCompositionStringW` and `ImmNotifyIME` attempt returned success but
yielded zero composition bytes, no new preedit signals and no committed text.
Synthetic composition notifications and Undo pass, but actual IME commit,
candidate UI and replacement remain admission gates. These opt-in fixtures
must not be described as a supported editor capability.

The selected worker run compiled source SHA-256
`AA283ABA7109ECD4510591C95BC0B23429231ACDC70D925ACC528E104E369D2B`
as C++17 with `/W4 /WX /O2` and passed. For 128 temporary color ranges over
about 32,000 UTF-16 units, 128 warmed samples measured native range-call time
at p50 66.6, p95 67.2, p99 71.9 microseconds. An independent local build of
the same source with C++17 `/W4 /WX /O2 /EHsc` exited zero and measured
21.0/21.3/21.4 microseconds. These runs exclude WinUI hosting, display latency,
input dispatch, lexer work and allocations. The separate
`--native-text-host-qualification` fixture uses the private `LuiWinUIText.dll`
bridge to apply the same temporary formatting to an actual WinUI-hosted Rich Edit
HWND. It checks exact Unicode text, selection, scroll, modified flag, native
Undo/Redo, clipping/hit testing and cross-window reparent. Its 128-range batch
timing includes the managed-to-native call and TOM work but excludes lexer,
display latency and allocations. It does not qualify real keyboard/IME/UIA use.

The selected local WinUI host run used native bridge source SHA-256
`51484B00E3AB05763369C25CC355C340314724B535F0F0C4E4DEB57C0E462627`
and fixture source SHA-256
`B3CAC2EFD3834BF38BF781BDC0EB8AA76936004A1DBF62F820A81CC17D71178D`.
The bridge compiled as one C++17 file with `/W4 /WX /O2`; the WinUI x64 Release
project compiled with one MSBuild job and zero warnings. The process exited zero
with seven groups passing in `WinUIHostedTemporary04.json`. Its 128-range,
32,010-UTF-16-unit batch over 128 warmed samples measured p50/p95/p99
32.0/35.9/39.3 microseconds. A separate 128-sample synchronous native
`WM_CHAR` edit followed immediately by the same style batch measured
594/803.5/1159.5 microseconds. Both are CPU boundaries without lexer, native event
callback, owner dispatch or physical display latency. The bridge rejects
malformed or overlapping ranges, a batch over 128 spans and a foreign HWND
before styling. Accepted spans are ordered and disjoint, so total styled
range length is bounded by 65,534 UTF-16 units.

An additional offset check (native proof source SHA-256
`ADF70AEB906854D456BEFBDC647246FD2BA01D5BA7E04D24838DFE44CCA6AD2B`)
passed locally with C++17 `/W4 /WX /O2 /EHsc`. After writing `A\nreturn`,
`GetWindowTextW` reads `A\r\nreturn`, while TOM positions `return` at 2. After
an emoji and CRLF, the external UTF-16 start is 20 but the TOM start is 19.
The native proof checks both positions so future styling code cannot silently
use external CRLF offsets as TOM positions. On the selected system Rich Edit,
`EM_SETENDOFLINE(EC_ENDOFLINE_LF)` returned zero; that edit-control message
did not change the Rich Edit newline behavior. A production styled editor needs
an explicit newline/source-preservation policy before connecting Luau spans.

The mixed-ending regression (native proof source SHA-256
`4C3389F5BD862202CA3942DCD6F4FC49D55F651FDA508A97C5C24891432813C8`)
also passed with the same local compiler profile. Writing `A\r\nB\nC\rD`
produces `A\r\nB\r\nC\r\nD` through `GetWindowTextW` and `A\rB\rC\rD`
through `EM_GETTEXTEX(GT_RAWTEXT)`. Mixed original line endings are therefore
not recoverable from this native buffer alone on the selected Windows profile.

For a future opt-in editor with canonical LF source, `CanonicalTextOffsets`
maps at most 256 ordered scalar boundaries in one pass over at most 65,534
UTF-16 units. A surrogate pair counts two TOM units and LF one; CR, NUL and
malformed UTF-16 are rejected. The existing ordinary TextBox contract is
unchanged. The WinUI hosted probe passed again after merging the qualified
window lifecycle head `5692bc8` (report `WinUIHostedTemporaryMerged02.json`);
its standalone scalar fixture maps `A😀\nreturn` scalar `[3,9]` to TOM `[4,10]`.

For native owner wake qualification, publish the normal host and the existing
Rust ABI v2 proof DLL, then run:

```text
Lui.WinUI.exe --owner-wake-qualification --native-provider <absolute proof.dll> --qualification-report <absolute report.json>
```

The Engine-free fixture compares 128 echoes each with requested 16/64ms polling
and wake-enabled 64ms fallback. It measures actual owner intervals and
admission-to-Luau callback, tests busy/terminal/healthy sibling runtimes, sixteen
queued group replacements, owned Rust pixels through FrameSurface and final
native resource quiescence. Its synthetic PNG contains no application data.
The optional [host wake header](../../native/abi/LuiOwnerWake.h) has a separate
versioned contract; existing ABI1/2 and public Luau types remain unchanged.
These results do not qualify Studio FPS, GTK wake dispatch or physical latency.

The shared C++ conformance suite runs in CI without a display. These cases exercise the real WinUI controls and UI Automation tree in an interactive Windows session. Both Luau scripts are loaded by the headless test so CI also catches syntax and startup errors; the native event observations still require a desktop.

Build with `scripts/Build-Windows.ps1`. Run `Lui.WinUI.exe --native-qualification` from the generated output folder for the automatic native mapping probe. It loads `NativeMapping.luau`, checks real WinUI properties, accessibility peer name and help text, parenting, bounds at the current DPI scale, disabled and hidden state, system theme reporting, and native text and slider event round trips. It writes a nonmodal log under `%LOCALAPPDATA%\LUI\Logs`, sets the process exit code, and exits. Windows CI compiles the probe but does not launch a desktop app. For a published folder, copy `NativeMapping.luau` into `tests/winui` inside a separate qualification copy of the folder; application packages omit test scripts.

For input routing, run the generated `Lui.WinUI.exe` with a script path resolved from the repository root. Enable `--diagnostics` to keep an event trace in `%LOCALAPPDATA%\LUI\Logs`.

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

## DSP-1 P2 frame qualification

Build the Windows gate (including LuiWinUIFrameBitmapTests and
LuiWinUIFrameGpuTests) and publish the WinUI
host with LuiRuntime.dll and sibling LuiWinUIFrames.dll. In a desktop session:

```powershell
Lui.WinUI.exe --frame-qualification --qualification-report C:\Sandbox\FrameReport.json --frame-observation
```

Use a dedicated absolute report directory. The optional observation stage keeps
synthetic alternating red/blue frames and the input button live for 30 seconds
after the automatic workload. The fixture writes synthetic PNGs and numeric
aggregates; it does not inspect application pixels. Fixed known-pattern samples
verify channels, alpha composition, row orientation, stride packing, caller
mutation, all scale modes, opacity, clipping, stacking, hiding and reparenting.
It also checks controlled delayed settlement, clear/destroy/reload, injected
allocation/detachment failures, 1920x1080, 1,000 720p frames and a 120-frame burst.
Resource samples include admission/preparation/completion/publication boundaries.

The selected private presenter uses SurfaceImageSource and one explicitly sized
D3D12 UPLOAD allocation. The Windows script generates C++/WinRT projections from
the same restored pinned WinUI package, then enables LUI_BUILD_WINUI_PRESENTER.
Package the resulting helper beside LuiRuntime.dll; ordinary launches register
it without a qualification-only opt-in. A helper built without the presenter,
or a missing/older helper, keeps FrameSurface unsupported.

Additional barriers stop the actual worker halfway through copying mapped rows
and hold the task-private GPU queue before its read. Cancellation/reload must
retain charged bytes until release. Device-loss tests remove only the fixture's
logical D3D12 device, validate removed fences, recreate resources and check pixels.
No physical adapter reset or system TDR is induced. Controlled surface-only loss
notifications also cover prepared, recovering and hidden content. The report
requires native frames/sources/upload bytes/devices and shared charges all zero
after shutdown, and records nine logical removals plus eight expected injected
allocation/detachment/publication/scheduling failures.

The 1,000-frame workload targets 30 Hz with a 35-second upper bound, records its
achieved cadence, and checks published admission/submission/input/layout budgets.
Synthetic captures are separate from desktop observations and physical scanout.
A successful JSON report proves its named assertions; final qualification also
requires immutable-source regressions and independent review. See the
[platform matrix](../../docs/PLATFORMS.md) for supported scope and limitations.
