# Platform support

LUI 0.0.1 is experimental. These records describe selected test profiles, not a stable SDK support promise.

| Profile | Tested scope |
| --- | --- |
| Windows x64 / WinUI 3 | Shared semantics, native controls/input, modular projects, clipboard/dialogs, packaging and advanced UI; recorded desktop observations at scale 1 |
| Ubuntu 24.04.5 x64 / GTK 4.14.5 / Xfce X11 VM | Shared/native mappings, modular projects, clipboard/dialogs, relocatable folders, scales 1/2, light/dark/high contrast and live AT-SPI metadata/focus |
| Linux headless | Runtime/project/network/tooling regressions without a native desktop |
| Experimental BrowserHostV1 / Ubuntu 24.04 x64 / X11 software VM | Pinned Chromium 156.0.8078.4, Firefox 157.0 and Playwright WebKit 27.2: captured modular/conversation applications, controls, scalar text/selection, lifecycle, CSP/refusals and bounded in-page DOM latency; selected Firefox IBus/libpinyin and separate Orca 46.1 sessions |

## Limits

Native service ABI v2 is experimental on Windows/Linux x64, with an independent
Rust debug/release proof. Scoped StorageService uses native file operations on
both systems and requires explicit trusted-host installation. Notifications and
tray are implemented for the selected Windows shell; GTK reports unsupported.
Windows checks cover shell submission, icon registration, deferred activation
and cleanup, not notification display/readability or Explorer restart recovery.
See [the service contract](NATIVE-SERVICES.md).

- Bare-metal Linux input devices, Wayland, fractional/per-monitor DPI and broader distributions remain unqualified.
- Windows third-party IME providers, full CJK/bidi/grapheme editing and cross-device behavior need further qualification.
- Accessibility metadata/focus checks do not establish full screen-reader usability. Virtual collections expose native objects only for resident rows; offscreen item navigation is not qualified.
- Windows/Linux native submission timing has different backend boundaries; it is not a toolkit speed comparison or physical display latency measurement.
- The browser profile passed its selected Linux/X11/software acceptance gate and remains experimental. Shared native/browser module-cache, isolation, cycle, initializer and task-order fixtures pass across two fresh generations per engine. Real IME/AT evidence covers the selected Firefox sessions only. Orca speech submissions use a dummy/null output sink; audio quality, human usability and combined Orca/Pinyin operation are unqualified. High-contrast media emulation is checked on Chromium/Firefox; the WebKit automation profile lacks that emulation.
- Acceptance checks Chromium tab focus loss with automation focus emulation disabled, retained focused collection anchors and focus cleanup on row retirement. Firefox/WebKit native tab transitions, frozen-page deadlines, other browser OS/IME/AT combinations, nonresident collection navigation and general screen-reader collection usability remain unqualified. Linux Playwright WebKit does not qualify Safari or mobile.
- No macOS, Android or iOS backend is currently qualified.
- Autosize, rich text, network image downloads, signed releases and stable SDK certification remain outside this profile.

Host grants determine available services. Explicit unsupported-operation failures are preferable to silently changing portable semantics. See [compatibility](COMPATIBILITY.md), [testing](TESTING.md) and the [roadmap](../ROADMAP.md).

The [browser guide](../backends/browser/README.md) records its service matrix and
repeatable test environment. HTTP, raw networking, WebSocket, storage,
clipboard/dialog services, notifications/tray, native providers and multiple
windows are unavailable. Ordinary text editing is separate from the clipboard
service. Single-line passwords are supported; Secret multiline editing fails
closed before assigning text. DOM acknowledgments do not prove physical paint.
