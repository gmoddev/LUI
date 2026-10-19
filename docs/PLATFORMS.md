# Platform support

LUI 0.0.1 is experimental. These records describe selected test profiles, not a stable SDK support promise.

| Profile | Tested scope |
| --- | --- |
| Windows x64 / WinUI 3 | Shared semantics, native controls/input, modular projects, clipboard/dialogs, packaging and advanced UI; recorded desktop observations at scale 1 |
| Ubuntu 24.04.5 x64 / GTK 4.14.5 / Xfce X11 VM | Shared/native mappings, modular projects, clipboard/dialogs, relocatable folders, scales 1/2, light/dark/high contrast and live AT-SPI metadata/focus |
| Linux headless | Runtime/project/network/tooling regressions without a native desktop |

## Limits

- Bare-metal Linux input devices, Wayland, fractional/per-monitor DPI and broader distributions remain unqualified.
- Windows third-party IME providers, full CJK/bidi/grapheme editing and cross-device behavior need further qualification.
- Accessibility metadata/focus checks do not establish full screen-reader usability. Virtual collections expose native objects only for resident rows; offscreen item navigation is not qualified.
- Windows/Linux native submission timing has different backend boundaries; it is not a toolkit speed comparison or physical display latency measurement.
- No macOS, browser, Android or iOS backend is currently qualified.
- Autosize, rich text, network image downloads, signed releases and stable SDK certification remain outside this profile.

Host grants determine available services. Explicit unsupported-operation failures are preferable to silently changing portable semantics. See [compatibility](COMPATIBILITY.md), [testing](TESTING.md) and the [roadmap](../ROADMAP.md).
