# Testing

The public source includes the same runtime, backend and tool tests used for the experimental profile. Follow [BUILD.md](BUILD.md) to install prerequisites.

## Automated coverage

- Headless CTest suites cover lifecycle, layout, text, collections, extensions, host services, sandboxing, networking, projects and privacy.
- `scripts/Build-Windows.ps1` runs the Windows native suites, generated-file validation, manifest/package/diagnostic tests, CLI/preview/editor checks and WinUI build.
- `.github/workflows/ci.yml` defines Windows, Linux headless/tooling and GTK native jobs. GTK includes scale 1/2, live accessibility metadata, CLI launch and relocated package launch.
- `node --test tests/editor/EditorTests.js tests/editor/TypeTests.js tests/editor/ProjectTests.js` runs editor tests; integration portions need the built preview/runtime/CLI/checker paths supplied through the environment variables shown in CI.

Use focused CTest selection while iterating, then run the relevant full platform gate before claiming support. Native text, images, clipping and input mappings supplement shared tests; one backend passing does not qualify another.

## Desktop observations

Use [Windows fixtures](../tests/winui/README.md) and the [GTK desktop fixture](../tests/gtk4/desktop/README.md). Record OS, architecture, toolkit, display server, scale, themes and input providers. Exercise resizing, navigation, Unicode editing, clipboard/dialog cancellation, accessibility and close/relaunch. Use synthetic input and keep screenshots/logs in ignored build output.

The [conversation workload](../examples/conversation/README.md) exercises 10,000 messages and bounded virtual rows. [Performance budgets](PERFORMANCE.md) distinguish owner work and native submission from physical display latency.

Known gaps remain in [PLATFORMS.md](PLATFORMS.md). Automated tests or accessibility metadata do not certify physical devices or full screen-reader usability.
