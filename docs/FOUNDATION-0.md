# Foundation 0: Windows proof

Foundation 0 now has a buildable embedded Luau runtime, a small Instance model, headless tests, and a WinUI 3 host. It is a proof of the central interaction, not a production desktop SDK.

## Build

Verified on a Windows x64 worker with Visual Studio 2022 C++ tools, Windows SDK 10.0.26100.0, .NET SDK 9.0.317, and Windows App SDK NuGet 1.8.260804001. The native CMake project pins Luau to commit `c0e346edd89066b44dca174c9f54ce84c746a540` (tag 0.740). CMake fetches that commit unless `-LuauSourceDir` points to a checkout at exactly the pinned revision.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Build-Windows.ps1
```

The script builds the native DLL and headless test, runs the test, then builds the unpackaged WinUI 3 host. It uses `build/windows-x64` for native incremental state and the normal .NET `obj`/`bin` directories for managed output. NuGet and Git access are needed on a fresh machine. The host uses the Windows App SDK runtime; installation and release packaging are later work.

## Run

After a successful build, launch the generated `Lui.WinUI.exe` from `backends/winui3/bin/x64/Debug/net9.0-windows10.0.19041.0/win-x64/`. A verified development build is also copied to `out/Foundation0-win-x64-debug/` in this workspace, with a zip alongside it. It runs the copied `examples/hello.luau` by default. Pass a Luau file path as the first argument to run another script. The host owns the WinUI event loop, so application Luau does not write one.

## Implemented Luau surface

- `Instance.new` for `Window`, `Frame`, `TextLabel`, and `TextButton`, with an optional property table. `Parent` is applied last.
- `Name`, `Parent`, `ClassName`, `Title` for Window, `Text` for text controls, `Visible`, and `Size`.
- `Destroy()`, `GetChildren()`, `FindFirstChild()`, and `IsA()`.
- `Activated` on TextButton, plus `Changed` and `Destroying`, with `Connect()` and `Disconnect()`.
- `UDim.new`, `UDim2.new`, `UDim2.fromOffset`, and `UDim2.fromScale`.
- `task.spawn`, `task.defer`, and `task.delay`, pumped by the host on the UI thread.

Window and Frame are containers. Initial layout resolves child width and height from parent width and height in a headless-testable pass. Controls currently arrange at the parent's origin; positioning and richer layout belong to Foundation 1.

## Boundaries and limitations

- The C callback interface in `native/abi/LuiRuntime.h` is **internal and unstable**. It is not the Foundation 2 extension ABI.
- The WinUI host is an unpackaged development build. It has no installer, self-contained distribution, Linux backend, editor tooling, or hot reload. Reflection and generated typing were added in Foundation 1.
- `task.wait`, automatic sizing, and styling are not implemented yet. Clone, descendant queries, padding, anchors, and focus began in Foundation 1.
- The WinUI bridge compiles and the native semantics pass headless tests. The user verified interactive GUI behavior on 2026-09-29. A hidden launch from the build worker's noninteractive Session 0 exited with `0xc000027b` in `Microsoft.UI.Xaml.dll`; that result is specific to the noninteractive smoke attempt.
- Diagnostic failures go to stderr or trace. They do not open modal dialogs.

The [full specification](SPEC.md) remains the intended API design. [ROADMAP.md](../ROADMAP.md) tracks the broader foundations.
