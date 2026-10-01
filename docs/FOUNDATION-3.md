# Foundation 3 — developer experience

Foundation 3 is in progress. The CLI, preview host, and VS Code development extension use the version 1 JSON manifest shared by the Windows host and packager. The manifest can be named `lui.json`; pass another path explicitly for an existing app such as [the dashboard](../examples/dashboard/dashboard.manifest.json).

## Commands

Run the CLI from the repository with `dotnet run --project tools/cli/Lui.Cli.csproj -- <command>`:

```powershell
dotnet run --project tools/cli/Lui.Cli.csproj -- new build/MyApp
dotnet run --project tools/cli/Lui.Cli.csproj -- check build/MyApp/lui.json --runtime build/windows-x64/Debug/LuiRuntime.dll
dotnet run --project tools/cli/Lui.Cli.csproj -- run build/MyApp/lui.json --host backends/winui3/bin/x64/Debug/net9.0-windows10.0.19041.0/win-x64
dotnet run --project tools/cli/Lui.Cli.csproj -- preview build/MyApp/lui.json --host tools/preview-host/bin/Debug/net9.0/lui-preview-host.dll --runtime build/windows-x64/Debug/LuiRuntime.dll
```

`new` requires an empty target directory and creates `lui.json` plus `src/main.luau`. The manifest starts with no capabilities. `check` validates the manifest and compiles a UTF-8 entry script of up to 8 MiB without running it. It reports syntax errors, not type errors. The native runtime path may instead be supplied by `LUI_RUNTIME`. `run` is Windows-only; it checks source, passes the absolute manifest to the WinUI host, and waits for the window to close.

To stage a self-contained folder, first publish the Windows host as described in [Foundation 2](FOUNDATION-2.md). Then run:

```powershell
dotnet run --project tools/cli/Lui.Cli.csproj -- build build/MyApp/lui.json --host build/published-host --output build/package/MyApp
```

`build` requires a published host directory with `Lui.WinUI.exe` and `LuiRuntime.dll`; it stages only the declared script, assets, extensions, and host dependencies. The output directory must be empty. If extension DLLs are built outside the manifest directory, pass `--extensions <directory>`. This command does not build the native runtime, publish the host, sign an installer, or typecheck source.

## Isolated preview

Build `tools/preview-host/Lui.PreviewHost.csproj` before using `lui preview`. The CLI launches it in a separate process, passing the manifest and native runtime paths. Its standard input and output carry UTF-8 JSON lines. The host prints `hello` and an initial `fullTree`, then accepts commands. For example:

```json
{"version":1,"type":"activate","generation":1,"id":2}
{"version":1,"type":"reload","generation":1}
{"version":1,"type":"resizeViewport","generation":2,"id":1,"width":800,"height":500}
{"version":1,"type":"shutdown"}
```

`fullTree.nodes` is a sorted array of live runtime Instances with IDs, parent IDs, class, name, title, text, source asset name, accessibility text, visible/enabled/checked/focus state, range values, resolved logical-unit `bounds`, and optional Luau `createdAt` and `lastChangedAt` locations. A location includes the chunk source and one-based line; `lastChangedAt` also names the property. Every message includes `version`, `type`, and `generation`. A command for an old generation yields a `diagnostic`. `reload` destroys the old VM, callbacks, tasks, and tree, then creates a new sandboxed generation. Script or setup failure emits `runtimeError` and an empty `fullTree` for that generation. Console prints become `consoleMessage`; errors become `diagnostic`. Runtime errors tied to the manifest entry script carry an optional `location`. `snapshot` forces a full tree. Tree changes after scheduled work are published automatically.

The initial `hello` also includes the runtime reflection schema, so editor property labels and types come from the same metadata as generated Luau definitions and API docs.

Preview currently accepts only manifests with no privileged capabilities or extensions. It uses the real portable layout engine, but displays no native window. The tree snapshot is limited to 4 MiB; commands to 64 KiB, the input queue to 256 commands, source to 8 MiB, and Luau VM memory and interrupts to capped sandbox limits. The preview host enables source provenance before running Luau; other hosts leave it disabled. Luau frame metadata provides lines, not columns. Native changes without a Luau call site do not replace the last recorded source location. Pointer/keyboard commands are still pending.

## VS Code Explorer and inspector

The development extension lives in [vscode](../vscode/README.md). Run it through an Extension Development Host with `vscode/` as the extension development path, then open an application folder and run **LUI: Start Preview**. Build the .NET preview host and native runtime first. The extension discovers those builds in a repository workspace; for another project, set `lui.previewHostPath` and `lui.runtimePath` to their absolute paths. Set `lui.manifestPath` if the file is not `lui.json`. The extension requires a trusted workspace before it runs application code.

The sidebar shows the live Instance hierarchy, a clickable map of resolved logical bounds, and read-only properties whose available names and types come from runtime reflection. Click or double-click a supported button to select or activate it, and resize the selected window viewport from the inspector. The Source section opens an Instance's creation or last-change line; the Explorer context action opens its creation line. Only locations resolving to the validated manifest entry script can open a file. The tree and selection reset on generation changes; save the entry script to reload automatically. The **LUI Preview** Output channel receives prints and diagnostics. Source-linked runtime errors also appear in the VS Code Problems view and inspector, and clear after a successful new generation. The map depicts layout bounds and does not claim exact native rendering. The current extension does not provide Luau type diagnostics, property editing, or pointer/keyboard interaction.

## Verification and next work

The runtime headless suite checks source compilation, provenance, and authoritative preview snapshots. CLI tests cover scaffolding, shared manifest validation, syntax diagnostics, Windows package staging, and launch argument construction. The preview process test covers the versioned protocol, resolved layout, provenance, activation, generation reset, stale and invalid commands, viewport resize, and cancellation of a delayed callback from an old generation. Node tests validate editor tree, reflection, source locations, and error recovery through the real preview host. Windows and Linux CI run these tests with their native runtimes. The user visually confirmed the initial VS Code Explorer and layout view on 2026-10-01; the new source controls still need visual qualification.

Next: add a native preview command and Luau type diagnostics.
