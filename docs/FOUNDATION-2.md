# Foundation 2: native integration and production Windows

The Foundation 2 Windows exit criteria passed on 2026-10-01. A self-contained release folder launched its declared native service, the service responded through Luau, a missing DLL failed with exit code 1 and a saved nonmodal log, and the interactive native qualification passed at the desktop's 100% DPI scale. The portable native tests pass on Windows; Linux CI tracks the same portable contract separately.

## Implemented

- The [version 1 extension ABI](../native/abi/LuiExtension.h) loads a DLL only after a host grants `NativeExtensions`. It validates identity, capabilities, entry points, and structure sizes; a failed initializer rolls back its methods and signals and unloads the library. Methods exchange bounded primitive values. Extensions can register service signals and emit them on the owner thread. A worker schedules an owner-thread completion through the bounded queue before it emits a signal. An extension compiled against the original table prefix remains compatible with the appended signal entries.
- The [C++ host facade](../native/host/Application.hpp) binds ordinary C++ functions or lambdas as typed Luau service methods, plus primitive service signals. It handles Boolean, finite numeric, and string arguments and returns. Registration appears in runtime reflection. Unsupported types fail at compile time. Host code runs on the scheduler thread and must finish before application teardown.
- A host may call `Lui_ConfigureSandbox` before running scripts. It freezes the Luau environment, limits VM heap allocations, and stops long-running Luau dispatches at interrupt safepoints. Sandboxed runtimes deny native DLL loading, and host service access requires the `HostServices` grant. The memory limit is a **Luau VM heap** limit; it is not an operating-system process memory limit. Native host methods, backend work, and compilation happen outside that quota. No ordinary Luau filesystem, network, process, or shell service exists yet.
- The WinUI manifest accepts `NativeExtensions`, `HostServices`, `Clipboard`, and `Dialogs` grants and optional sandbox limits. Normal application Luau cannot grant itself capabilities. `PlatformService:Supports()` reports active grants backed by implemented services.
- `ClipboardService:WriteText(Text)` writes text. `ClipboardService:ReadText(Callback)` completes asynchronously with `(Text, Error)`; a missing text format gives `nil, nil`. `DialogService:OpenFile(Callback)` opens a Windows file picker after application code requests it and completes with `(Path, Error)`; cancellation gives `nil, nil`. The runtime queues completions for the owner thread, bounds them, and discards pending requests on shutdown. Both services require their own grants.
- `ThemeService.CurrentTheme` is `Light`, `Dark`, or `HighContrast`. `ThemeChanged` fires from the owner-thread pump when the Windows system theme changes. The WinUI host detects the current system settings; native controls retain their platform theme behavior.
- Manifest `Assets` are filenames beside the manifest. The runtime registers only those names. `AssetService:Has(Name)` checks the declaration, and `ImageLabel.Source` accepts only a declared name. WinUI resolves it to the packaged file. Arbitrary paths and URLs are not accepted as image sources.
- [Publish-Windows.ps1](../scripts/Publish-Windows.ps1) builds Release, publishes a self-contained WinUI host, and stages only the manifest's declared application script, DLLs, and assets alongside the host dependencies. It rejects collisions and a nonempty output directory. The unpackaged WinUI publish enables MSIX tooling and disables trimming, single-file, and ready-to-run publishing; the published desktop qualification caught a WinUI startup failure without that configuration. This is a folder distribution, not an installer or signed release.
- The host suppresses OS error dialog boxes; expected errors and startup failures use the saved LUI log without forcing a dialog over another application. `--diagnostics` remains an opt-in visible console.

## Build and package

The [Windows build script](../scripts/Build-Windows.ps1) runs headless semantics, layout, conformance, extension, C++ host, sandbox, theme, platform, asset, manifest, package, and generated type tests, then compiles WinUI. Linux CI runs the portable native tests.

To build the sample release folder on a Windows build machine:

```powershell
.\scripts\Publish-Windows.ps1 -ManifestPath .\examples\native-service.manifest.json
.\build\package\win-x64\Lui.WinUI.exe --manifest native-service.manifest.json
```

The sample [manifest](../examples/native-service.manifest.json) grants only `NativeExtensions` and includes only `LuiSampleExtension.dll` and the [Luau script](../examples/native-service.luau). For a development build, [Stage-NativeExample.ps1](../scripts/Stage-NativeExample.ps1) still stages the Debug host. Manifest launches save a log under `%LOCALAPPDATA%\LUI\Logs`.

## Exit audit

| Criterion | Evidence |
| --- | --- |
| Standalone Windows app binds a native service | The Release folder's `LUI Native Service` window displayed `NativeMath:Add(2, 3) = 5`; its button called the service from Luau and updated the label. The window closed cleanly. |
| Only declared application capabilities and files ship | The sample manifest grants only `NativeExtensions`; package tests verify declared-file staging and collision rejection. The 2026-10-01 Release folder contained the manifest, script, sample DLL, WinUI host, and host dependencies without test scripts. |
| Extension load failure is safe | Replacing the declared DLL name with a missing name exited with code 1, logged the failure under `%LOCALAPPDATA%\LUI\Logs`, and showed no error dialog. Headless tests cover failed initializer rollback and a legacy ABI-prefix DLL. |
| Windows conformance | The Release build passed the native semantic, layout, input, extension, host, sandbox, platform, theme, asset, manifest, package, reflection, and generated type tests. `--native-qualification` passed WinUI control mapping, native event round trips, accessibility peer name/help text, theme reporting, and logical bounds at DPI scale 1. |

## Scope limits and follow-up qualification

| Area | Required check |
| --- | --- |
| Accessibility and DPI | The automatic probe passed at DPI scale 1 in the dark theme. Repeat under another DPI scale, high contrast, and an assistive technology before claiming broad configuration coverage. |
| Native object model | Add opaque object handles, properties, and enums to a later ABI revision. Version 1 supports primitive methods and signals. Capability-gated platform services are the Foundation 2 escape hatch; arbitrary native window handles remain a future design target. |
| Security scope | Define any future filesystem, networking, process, and shell services with explicit grants and restricted roots or targets. The current sandbox does not claim containment of a hostile native host or a process-wide memory ceiling. |

The [extension decision](decisions/0015-extension-abi-and-capabilities.md) and [host, sandbox, and platform decision](decisions/0016-foundation-2-host-and-platform.md) record the present contract. The [specification](SPEC.md) remains the design source for APIs that are not implemented.
