# Testing

**Audience: framework contributors and integration authors.** These checks test
LUI itself. Ordinary app authors can start with Check Types, Run App and the
[SDK development loop](https://gmoddev.github.io/LUI/docs/sdk-0.1.0-preview.2/guides/workflow/).
This preview does not provide an application-authored `lui test` command.

The public source includes the same runtime, backend and tool tests used for the experimental profile. Follow [BUILD.md](BUILD.md) to install prerequisites.

## Automated coverage

- Headless CTest suites cover lifecycle, layout, text, collections, extensions, host services, sandboxing, networking, projects and privacy.
- `scripts/Build-Windows.ps1` runs the Windows native suites, generated-file validation, manifest/package/diagnostic tests, CLI/preview/editor checks and WinUI build.
- `.github/workflows/ci.yml` defines Windows, Linux headless/tooling and GTK native jobs. GTK includes scale 1/2, live accessibility metadata, CLI launch and relocated package launch.
- `node --test tests/editor/EditorTests.js tests/editor/TypeTests.js tests/editor/ProjectTests.js` runs editor tests; integration portions need the built preview/runtime/CLI/checker paths supplied through the environment variables shown in CI.
- `bash scripts/Qualify-Browser.sh` checks pinned browser toolchains, builds native capture parity and optimized WASM, prepares isolated packages, and runs protocol, emitted-artifact, scheduling, controls/100 lifecycle cycles per engine, shared acceptance, packages, policy and in-page performance suites. The [browser guide](../backends/browser/README.md) specifies the selected display/software environment and optional real Firefox IME/Orca sessions. Browser checks are run on the selected test worker; these are separate from hosted native CI.

Use focused CTest selection while iterating, then run the relevant full platform gate before claiming support. Native text, images, clipping and input mappings supplement shared tests; one backend passing does not qualify another.

`tests/browser/Modules.json` supplies eight identical cache/isolation/cycle,
initializer-effect, require-binding and task-order cases to native capture parity
and every browser engine, each in two fresh generations. Native assertions also
check that queued work waits for a pump and nested tasks wait for another turn.

## Desktop observations

Use [Windows fixtures](../tests/winui/README.md) and the [GTK desktop fixture](../tests/gtk4/desktop/README.md). Record OS, architecture, toolkit, display server, scale, themes and input providers. Exercise resizing, navigation, Unicode editing, clipboard/dialog cancellation, accessibility and close/relaunch. Use synthetic input and keep screenshots/logs in ignored build output.

The [conversation workload](../examples/conversation/README.md) exercises 10,000 messages and bounded virtual rows. [Performance budgets](PERFORMANCE.md) distinguish owner work and native submission from physical display latency.

Known gaps remain in [PLATFORMS.md](PLATFORMS.md). Automated tests or accessibility metadata do not certify physical devices or full screen-reader usability.

## Owned native services

Enable the Rust proof as described in [BUILD.md](BUILD.md). Native service tests
cover ownership, quotas, cancellation/settlement races, redaction and retirement.
Storage suites exercise actual scoped-file behavior on Windows/Linux. Linux's
separate FIFO rejection test has a five-second timeout. Desktop bridge tests
exercise deferred side effects, bounded activation and immediate close refusal.

`LuiWindowsDesktopQualification` is an explicit desktop executable outside
headless CTest. Run it with an actual Windows shell to verify notification
submission, tray registration, deferred activation and removal. `--observe`
briefly shows a status window without requesting focus. Activation is sent to
its own callback window; the fixture injects no mouse or keyboard input. It
does not qualify notification display, accessibility or Explorer recovery.
