# Modular workspace example

Synthetic two-screen application using declared modules, reusable buttons and shared state. Home increments a counter; Library selects a fictional project; navigation preserves state within a generation. Reload starts fresh. A classified synthetic text field remains visible in the native app but appears as `[Redacted]` in ordinary diagnostics/inspector output.

Build with `scripts/Build-Windows.ps1`. From the repository root:

```powershell
dotnet tools/cli/bin/Debug/net10.0/lui.dll check examples/modular/lui.json --mode project --diagnostics development --checker build/windows-x64/Debug/LuiTypeCheck.exe
dotnet tools/cli/bin/Debug/net10.0/lui.dll run examples/modular/lui.json --mode project --host backends/winui3/bin/x64/Debug/net10.0-windows10.0.19041.0/win-x64
dotnet tools/cli/bin/Debug/net10.0/lui.dll preview examples/modular/lui.json --mode project --host tools/preview-host/bin/Debug/net10.0/lui-preview-host.dll --runtime build/windows-x64/Debug/LuiRuntime.dll
```

For VS Code set `lui.manifestPath` to this manifest. Use **LUI: Choose SDK** for a matched installation, or configure individual build paths as a framework contributor. **LUI: Inspect Layout** and **LUI: Check Types** inspect/check declared project sources; **LUI: Run App** launches native controls. Saving a project file reloads the inspector generation; native execution must be stopped and rerun. Source controls navigate prepared module IDs.

Compose a runnable folder using a published WinUI host:

`scripts/Publish-Windows.ps1 -ManifestPath examples/modular/lui.json -Mode project -OutputDirectory <new-directory>` builds and publishes a self-contained Windows host, then stages the modular application. The underlying CLI composition command is:

```powershell
dotnet tools/cli/bin/Debug/net10.0/lui.dll build examples/modular/lui.json --mode project --host <published-host-directory> --project-tool build/windows-x64/Debug/LuiProjectTool.exe --output <new-output-directory>
```

Run `Start-LUI.cmd` in the result. `lui verify <output>/Application --project-tool <tool>` verifies declared content. Normal v2 launches do not save logs; the native host's explicit `--diagnostics-log` enables bounded projected logs. This example needs no privileged capabilities or native providers.
