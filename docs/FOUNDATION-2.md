# Foundation 2: native integration and production Windows

Foundation 2 is in progress. The first increment provides an experimental version 1 C extension ABI, an explicit host capability grant, a native loader, and a WinUI application manifest. It proves that a C library can register a service method and that Luau can call it without receiving `lua_State*` or a backend handle. This is not yet a production Windows package or a sandbox.

## Implemented boundary

- [LuiExtension.h](../native/abi/LuiExtension.h) defines size-versioned C values, extension metadata, a host function table, and query/init/shutdown exports. Version 1 supports `nil`, Boolean, finite number, and bounded string method values. Strings are borrowed during a call; the host copies returned strings immediately.
- The host must declare `NativeExtensions` through `Lui_DeclareCapabilities` before running a script. The default grant is zero. Only the host can call this API; application Luau cannot grant itself native code access. `PlatformService:Supports("NativeExtensions")` reflects the grant.
- `Lui_LoadExtension` takes an absolute path before the first application script. It checks the grant, ABI version, extension identity, required capabilities, and entry points. A failed initializer rolls back method registrations and unloads the library, leaving the runtime usable. A successfully loaded extension shuts down when the runtime is destroyed.
- Extensions can register service methods with Luau signatures. The runtime exposes them through `app:GetService` and [runtime extension reflection](../native/abi/LuiRuntime.h) without putting toolkit objects or `lua_State*` in the extension ABI. Static generated types cover built-in services; application-specific extension types still need a future generation step.
- The host table's `ScheduleUi` entry accepts completions from native worker threads. A bounded queue drains in `Lui_Pump` on the runtime owner thread; pending completions are discarded during shutdown. A completion cannot reenter an active Luau dispatch. The C sample starts a worker and the extension test verifies its completion runs on the owner thread.
- The WinUI host accepts `--manifest <path>`. Schema version 1 names the application script, declared capabilities, and DLLs beside the manifest. Manifest parsing rejects unknown capability names, duplicate DLLs, paths escaping the manifest directory, and extension loading without the grant. Errors go to the nonmodal LUI log.
- WinUI now destroys its runtime when the last native window closes, allowing registered extensions to shut down before the host exits.

## Build and verify

The [Windows build script](../scripts/Build-Windows.ps1) runs the Foundation 0 and 1 suites, the extension ABI test, and the manifest validation test before building WinUI. The extension test loads real C DLLs, invokes a native service from Luau, checks dynamic reflection, rejects incompatible ABI and failed initialization, and verifies rollback. Linux CI also builds and runs the extension tests.

To stage the sample after a Windows build:

```powershell
.\scripts\Stage-NativeExample.ps1
cd .\build\native-example\win-x64
.\Lui.WinUI.exe --manifest native-service.manifest.json
```

The sample [manifest](../examples/native-service.manifest.json) grants `NativeExtensions` and loads only `LuiSampleExtension.dll`; the [Luau example](../examples/native-service.luau) calls `NativeMath:Add` and `NativeMath:Echo`. `--diagnostics` can be added for a live trace. Manifest launches save a log under `%LOCALAPPDATA%\LUI\Logs` without opening a console by default.

## Remaining work

| Area | Next qualification |
| --- | --- |
| Native integration | Add a C++ host facade, object handles, additional registration kinds, and higher-level async results or signals on top of the owner-thread completion queue. |
| Application capabilities | Add explicit grants and enforcement for filesystem, network, clipboard, dialogs, process and shell services, plus execution and memory limits for sandboxed applications. The current grant controls only extension loading. |
| Windows services | Implement dialogs, clipboard, assets, system theme changes, platform interop, and standalone packaging. |
| Windows qualification | Run the staged native service example on an interactive desktop, then qualify accessibility, assistive technology, high contrast, and DPI behavior. |

The [extension ABI decision](decisions/0015-extension-abi-and-capabilities.md) records the current contract. The [specification](SPEC.md) remains the design source for later Foundation 2 features.
