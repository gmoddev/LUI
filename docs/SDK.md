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

For next SDK `0.1.0-preview.2`, create a project with optional application
libraries already included:

```powershell
./lui.cmd new "$env:USERPROFILE/Projects/MyWorkbench" --template workbench
./lui.cmd check "$env:USERPROFILE/Projects/MyWorkbench/lui.json"
./lui.cmd run "$env:USERPROFILE/Projects/MyWorkbench/lui.json"
```

The `commands` template is a smaller starting point. Both templates copy the
SDK's verified example sources and vendor dependencies, and pin the exact SDK.
They need no repository checkout or network access. Preview.1 contains only the
basic starter. Workbench notes and layout persist within the project directory's
application scope; a copied or packaged project gets separate data. Close a
running copy before starting another one with the same scope.

The workbench demonstrates two panes, tabs, command search and synthetic notes.
Floating panes and application-authored code execution are not implemented.
Editing, selection, clipboard, undo/redo and restart pass on the selected Windows
and GTK/X11 test profiles. Chinese input also passes on the selected GTK profile.
Real Windows input-method use and fresh Windows 11 installation remain release
gates. Unreleased SDK08 passed the selected clean Windows 10 LTSC SDK journey and
a separate app-only recipient journey without global build/runtime tools. Normal
download/trust and signing remain open; these results apply to the exact candidate
and profiles in the [platform matrix](PLATFORMS.md).

Use **Extensions: Install from VSIX** to install `Editor/LUI.vsix`. Open the LUI getting-started walkthrough, then:

1. **LUI: Choose SDK**: select the folder containing `SdkManifest.json`.
2. **LUI: Create Project**: choose a parent folder and enter a name.
3. Open `Src/Main.luau`, use **LUI: Check Types**, then **LUI: Run App**.

Trust is required before application execution or checking. Setup errors appear in the LUI Preview output and status bar. Save edited project files before Run App. Development extension 0.1.4 adds **LUI: Restart App**: it stops the old app, validates the current SDK pin and runs saved source. Save documents inside the app first because restart discards its memory. Stop App also cancels pending launch/restart. Older tools require Stop App followed by Run App. **LUI: Inspect Layout** starts a separate unprivileged app generation for the Instance tree and layout map; it does not inspect the live native window and refuses privileged capabilities/native extensions. Use Run App for capability-gated projects. Native extensions need an explicitly approved native-host launch with `--allow-native`; Run App does not request native-extension approval.

Development extension 0.1.5 adds **LUI: Add File to Project** for a saved `.luau`
file inside `Modules.Root`. Use its Problems quick fix, editor context menu or
command palette action. Review the unsaved `Modules.Files` change, save the
manifest, then run Check Types. Save or undo existing manifest edits first.
Sources outside the declared list are excluded from checking and execution.
This action preserves explicit declarations and never includes files silently.

Development extension 0.1.6 shows persistent app status after Run. It marks
unsaved project edits and offers restart after edits. Running describes the
owned process, so check the native window too. Click the status to restart a
running app, cancel preparation or retry a failed Stop. Save documents inside
the app before restarting, and restart after edits outside VS Code as well.

Development extension 0.1.7 cancels pending inspection/checking when you remove
the project's workspace folder or change tool settings. Run the command again after the change. Setup
failures use fixed guidance in Output; check the saved UTF-8 manifest, selected
SDK and tool paths, then run `lui doctor` for details.

Service names are not capability tokens: HttpService needs `network.client`, and
HttpServerService needs `network.server` in `lui.json` Capabilities. The host must
grant them and supply the provider. Declared image names go in `Assets` and
`ImageLabel.Source`; AssetService only exposes Has, not a disk loader. The bundled
Luau os library already has time/date/clock/difftime; unrestricted file and
environment access remain unavailable. See the version-bound
[capability guide](https://gmoddev.github.io/LUI/docs/sdk-0.1.0-preview.1/guides/capabilities/)
and [complete practical recipes](https://gmoddev.github.io/LUI/docs/sdk-0.1.0-preview.1/guides/recipes/).

Development source adds fixed capability/startup reasons in native editor output,
without arbitrary stderr or source text. Published preview.1 can still show only
exit code 1. A callback failure can leave its window open while the refused
operation fails. These source improvements do not replace the published SDK ZIP.

Offline capability reference, with exact case:

| Capability | API/setup |
| --- | --- |
| `network.client` | HttpService or outbound networking; a host transport and destination policy are required. |
| `network.server` | HttpServerService or inbound networking; a host transport and bind policy are required. |
| `network.raw` | NetworkService, plus the operation's client/server direction grant. |
| `Clipboard` | ClipboardService and a supported adapter. |
| `Dialogs` | DialogService and a supported picker; a returned path does not grant general file IO. |
| `Storage` | StorageService and an installed app-private scope; preview.2 desktop provisioning needs Storage.ApplicationId too. |
| `Notifications` / `Tray` | Selected Windows shell adapters; GTK reports unsupported. |
| `HostServices` | Trusted registered provider bindings; separate service grants still apply. |
| `NativeExtensions` | Explicitly trusted declared native providers; native code is privileged. |

AssetService, PerformanceService, WindowService, PlatformService and ThemeService
need no additional token for lookup. Keep assets declared and respect each host's
feature limits. Never grant every token as an error-recovery step.

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
./scripts/Build-SdkWindows.ps1 -NativeDirectory build/windows-x64/Release -OutputDirectory build/SDK-Preview -SdkVersion 0.1.0-preview.2 -CppRuntimeDirectory '<VisualStudio>/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT'
```

The output directory and its adjacent `<name>.ManagedBuild` directory must be new. The script isolates managed build files there so ordinary builds cannot leave stale Windows theme resources in the SDK; that directory is outside the distributed bundle. Preserve dependency build caches: packaging gathers Luau/Asio and exact managed dependency license material from them. The script publishes managed hosts self-contained without LUI debug metadata, creates an offline VSIX, inventories the bundle and produces a ZIP with an adjacent SHA-256 file. Preserve notices with redistributions. Public release still requires dependency redistribution review, publisher authenticity, normal OS launch, clean-machine app/SDK checks and the declared support matrix.

Supply the release x64 Microsoft C++ runtime directory from the licensed Visual
Studio installation. The build checks it against cmake/WindowsCppRuntime.json;
an older or different payload is refused. The pinned DLLs ship beside each SDK
executable, with hashes and Microsoft notices. Apps built from the SDK retain
those files and notices. These app-local copies receive fixes through updated
SDK/application packages; they do not require a separate runtime installer.
Keep the complete `THIRD-PARTY-LICENSES` folder with shared apps. It includes
LUI's license, native dependency notices, and license material for the exact
managed packages and bundled .NET runtime. The SDK also keeps a root copy in
`ThirdPartyNotices`; packages with a license URL retain that reference and
their copyright metadata.

Development source also offers [NumericInput](../libraries/LuiUI/README.md), a
bounded text field with step buttons. Stage its example with
`python scripts/Stage-LibraryExample.py --example numeric-input --output build/NumericInputApp`.
The stager captures its public module and private support sources in an ordinary
module lock. Windows headless checks pass; native GUI/Linux qualification remains
open. It is not included in prepared SDK12 or the released preview.1 download.

The qualified Windows native source-build profile uses MSVC 19.44.35228. CMake
refuses Release builds with the exact MSVC 19.37.32822.0 x64 toolchain because its
generated interpreter unwind metadata breaks error recovery; Debug remains
available. Other toolchains need their own passing native regression results.
