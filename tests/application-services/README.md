# Application-service journeys

These tests use synthetic records and fresh app-private storage. Run desktop
checks in an isolated test desktop: they open native application and picker
windows. The supported candidate profiles are described in the
[Windows guide](../../docs/SDK-CANDIDATE-3.md) and
[Linux guide](../../docs/SDK-LINUX-CANDIDATE-3.md).

Stage each application outside the SDK using the candidate's packaged CLI,
modules and lock files:

```sh
python tools/application-services/Prepare.py --sdk /path/to/sdk --app file-desk --output /path/to/apps/file-desk
python tools/application-services/Prepare.py --sdk /path/to/sdk --app network-desk --output /path/to/apps/network-desk
python tools/application-services/Prepare.py --sdk /path/to/sdk --app window-desk --output /path/to/apps/window-desk
```

`Baseline.py --sdk /path/to/sdk --projects /path/to/apps --output /path/to/report`
uses public owner ingress, actual storage/networking and a trusted test picker.
It checks app behavior and retirement counters without a display. It does not
exercise an OS picker, native clipboard or physical input. Published preview.2
app sources and baseline receipts are retained at checkpoint `b0eb303`; current
app sources require preview.3.

On the qualified Linux X11 profile, use the existing GTK, AT-SPI, pyatspi,
xfwm4, xdotool, Xvfb and session-bus tools:

```sh
xvfb-run -a dbus-run-session -- /usr/bin/python3 tests/application-services/NativeGtk.py --sdk /path/to/sdk --projects /path/to/apps --output /path/to/fresh-report/Report.json
```

This drives all three packaged native apps through accessibility and software
X11 input, including clipboard, storage restart, HTTP/WebSocket traffic,
cancel/retry, shared editors, Ctrl+S and workspace restoration. It uses a fresh
HOME and XDG data/config/cache profile and retires only its own processes. It does not
qualify Wayland, hardware input, OS trust prompts or Windows.

In an isolated **interactive Windows desktop**, run:

```powershell
& ./tests/application-services/NativeWindows.ps1 -Sdk C:/Test/SDK -OutputDirectory C:/Test/FreshNativeReport
```

This creates and checks a public SDK app, verifies the real clipboard, cancels
Open/Save/Folder immediately and after a short delay, reopens each picker, then
closes the parent after starting another request. The observer requires a visible,
responsive success window, exit0, no callback diagnostic and unchanged SDK
inventory. It does not assert chooser visibility or select a file. Windows native filesystem/provider tests
cover actual bytes separately; GTK's `SelectedDialogs` backend test exercises
the real OS chooser selection, reads, folder listing and save.

Native checks require the candidate's desktop dependencies. Missing displays or
tools are qualification prerequisites, not successful skipped journeys. Keep
private SDKs and reports immutable after qualification; choose fresh output paths.
