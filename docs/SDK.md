# LUI preview SDK

The Windows x64 preview SDK is a portable developer bundle. Its build pipeline is experimental; a publicly signed download and general clean-machine support are not yet qualified. Use only an artifact whose publisher and release identity you trust. The adjacent checksum and `lui doctor` verify consistency, not publisher authenticity.

## Start a project

Extract the complete SDK to a permanent folder. Keep all subdirectories together. The bundle includes a self-contained CLI, checker, native WinUI host, isolated layout inspector, packaging tool, types, offline guide and VS Code extension. Application authors do not need the .NET SDK or a compiler. Windows runtime prerequisites and the downloaded-file launch path must pass release qualification before this bundle is advertised as a supported public download.

From PowerShell in the SDK folder:

```powershell
./lui.cmd doctor
./lui.cmd new "$env:USERPROFILE/Projects/MyApp"
./lui.cmd check "$env:USERPROFILE/Projects/MyApp/lui.json"
./lui.cmd run "$env:USERPROFILE/Projects/MyApp/lui.json"
```

Choose a parent folder that exists and a project directory that does not. Click the button in the native window. Edit `Src/Main.luau`, save, close the window and run again. Native restart on save and application-authored automated test commands are planned.

For short `lui` commands, optionally add the SDK's `Cli` directory to your user PATH. The launcher discovers its own SDK; no per-component paths are needed. `--sdk <directory>` or `LUI_SDK` selects an explicit installation. A project's `LuiToolchain.json` requires the exact version and SDK inventory digest. Install versions side by side; reopening a project never upgrades its SDK or downloads dependencies.

## VS Code

Use **Extensions: Install from VSIX** to install `Editor/LUI.vsix`. Open the LUI getting-started walkthrough, then:

1. **LUI: Choose SDK**: select the folder containing `SdkManifest.json`.
2. **LUI: Create Project**: choose a parent folder and enter a name.
3. Open `Src/Main.luau`, use **LUI: Check Types**, then **LUI: Run App**.

Trust is required before application execution or checking. Setup errors appear in the LUI Preview output and status bar. Use **LUI: Stop App** before running edited source again. **LUI: Inspect Layout** starts a separate app generation for the Instance tree and layout map; it does not inspect the live native window.

On a later day, open the project folder and select Run. If the pinned SDK is missing, select that exact installation with Choose SDK. Source and project files stay portable; machine paths are not committed in the pin.

## Check and share

`lui check` checks declared sources without running application code. The starter uses schema 2 and Development diagnostics. Restricted static checking remains unsupported; the SDK does not downgrade policy. New modules must be explicitly declared in `lui.json`.

```text
lui build --output Dist/MyApp
```

Run this from the project folder to create a fresh app folder. Give recipients the whole folder and launch `Start-LUI.cmd`; they do not need the developer SDK. This is folder packaging, not signing, an installer or an updater. Verify the result on a separate target machine. Existing output directories are preserved.

## Build an SDK from source

Framework contributors first run the existing Windows Release qualification. Then, from the repository root:

```powershell
./scripts/Build-SdkWindows.ps1 -NativeDirectory build/windows-x64/Release -OutputDirectory build/SDK-Preview -SdkVersion 0.1.0-preview.1
```

The output directory must be new. Preserve dependency build caches: packaging gathers Luau/Asio and exact managed dependency license material from them. The script publishes managed hosts self-contained without LUI debug metadata, creates an offline VSIX, inventories the bundle and produces a ZIP with an adjacent SHA-256 file. Preserve notices with redistributions. Public release still requires dependency redistribution review, publisher authenticity, normal OS launch, clean-machine app/SDK checks and the declared support matrix.
