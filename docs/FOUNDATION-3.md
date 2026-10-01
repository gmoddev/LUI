# Foundation 3 — developer experience

Foundation 3 is in progress. Its first slice provides a CLI and uses the version 1 JSON manifest shared by the Windows host and packager. The manifest can be named `lui.json`; pass another path explicitly for an existing app such as [the dashboard](../examples/dashboard/dashboard.manifest.json).

## Commands

Run the CLI from the repository with `dotnet run --project tools/cli/Lui.Cli.csproj -- <command>`:

```powershell
dotnet run --project tools/cli/Lui.Cli.csproj -- new build/MyApp
dotnet run --project tools/cli/Lui.Cli.csproj -- check build/MyApp/lui.json --runtime build/windows-x64/Debug/LuiRuntime.dll
dotnet run --project tools/cli/Lui.Cli.csproj -- run build/MyApp/lui.json --host backends/winui3/bin/x64/Debug/net9.0-windows10.0.19041.0/win-x64
```

`new` requires an empty target directory and creates `lui.json` plus `src/main.luau`. The manifest starts with no capabilities. `check` validates the manifest and compiles a UTF-8 entry script of up to 8 MiB without running it. It reports syntax errors, not type errors. The native runtime path may instead be supplied by `LUI_RUNTIME`. `run` is Windows-only; it checks source, passes the absolute manifest to the WinUI host, and waits for the window to close.

To stage a self-contained folder, first publish the Windows host as described in [Foundation 2](FOUNDATION-2.md). Then run:

```powershell
dotnet run --project tools/cli/Lui.Cli.csproj -- build build/MyApp/lui.json --host build/published-host --output build/package/MyApp
```

`build` requires a published host directory with `Lui.WinUI.exe` and `LuiRuntime.dll`; it stages only the declared script, assets, extensions, and host dependencies. The output directory must be empty. If extension DLLs are built outside the manifest directory, pass `--extensions <directory>`. This command does not build the native runtime, publish the host, sign an installer, or typecheck source.

## Verification and next work

The runtime headless suite checks that source compilation accepts valid code, rejects invalid syntax, does not execute code, and remains usable after an error. CLI tests cover scaffolding, shared manifest validation, syntax diagnostics, Windows package staging, and launch argument construction. Windows CI builds the CLI and runs both test sets; Linux CI runs the portable native checker and CLI scaffold/check tests.

Next: run preview in a separate process over an explicitly versioned protocol, using the same runtime and object tree. Then build the editor Explorer and inspector, source provenance, native preview command, and generation-based reload.
