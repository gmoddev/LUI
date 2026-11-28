# LUI Linux SDK preview.3 candidate

This is an **unpublished experimental Linux x64 candidate**. It is not the
published Windows preview.2 SDK. Its inventory establishes local consistency,
not publisher authenticity, signing or automatic updates. Qualification results
must name the exact inventory; a successful build is not recipient qualification.

Extract the complete `.tar.gz` archive with executable permissions intact. Keep
Cli, Tools, Native, Editor, Libraries, Examples and SdkManifest.json
together. Run:

```sh
./lui doctor --format json
./lui new ../MyWorkbench --template workbench
cd ../MyWorkbench
/path/to/LUI-SDK/lui check lui.json --sdk /path/to/LUI-SDK
/path/to/LUI-SDK/lui run lui.json --sdk /path/to/LUI-SDK --mode project
```

CLI/preview are self-contained .NET applications: recipients need no .NET SDK,
global .NET runtime or C++ compiler. Native system libraries remain prerequisites.
LinuxRequirements.json lists the intended Ubuntu24.04 x64/GTK4.14/X11 profile:
GTK4.10+, OpenSSL3, compatible glibc/libstdc++, ICU, zlib and libgcc. This is a
relocatable folder, not an AppImage. Wayland, other distributions/desktops and
physical input/assistive technology require separate evidence. GTK performance
work is independent. Missing display/library failures remain nonmodal.

Copy complete Examples projects outside the SDK, including vendor files and
locks. `selected-files` needs a visible desktop window and its Dialogs manifest.
It reads a user-selected file, saves a copy and lists a selected read-only folder
through the optional Files module. It never accepts a native path from app code.
Buffers are bounded to4MiB; links/reparse paths, hardlinked files, devices and
direct UNC paths are refused. Mounted network filesystems are unqualified. Save
as replaces a data entry; it does not preserve ACLs/metadata or undo a committed
save when canceled. Keep editor text if completion is uncertain.

The finite WebSocket example uses no display. From a sibling copy of
Examples/websocket-echo, with loopback port18083 available:

```sh
/path/to/LUI-SDK/lui check --sdk /path/to/LUI-SDK
/path/to/LUI-SDK/lui run --sdk /path/to/LUI-SDK --mode headless --grant network.client,network.server --http-origin http://127.0.0.1:18083 --diagnostics development
```

Headless requires explicit grants and outbound policy, creates no UI, defaults
to30s and considers an open listener pending work. Exit0 means completed/retired,
1 failure,124 timeout and130 interruption. Development prints disclose app data.

Project checking adds unknown/readonly literal constructor key diagnostics.
Dynamic initializers, value compatibility and legacy single-file checking still
require runtime validation. This is not full Properties:any input typing.
The optional Commands module supports explicit Ctrl+S/Ctrl+N while editing;
ordinary text editing and composition retain precedence.

Projects pin the exact SDK inventory. Another candidate with the same version
requires a deliberate SDK choice; there is no downloader. For an app-only folder,
use `lui build lui.json --sdk /path/to/LUI-SDK --output NEW_DIRECTORY`, then run its
Start-LUI.sh. Keep LICENSE.txt, Native/LICENSE, ThirdPartyNotices,
Dependencies.json, ManagedDependencies.json and LinuxRequirements.json with the
SDK; preserve notices carried into app folders. CppRuntime.json is Windows-only.

The source includes the Linux-compatible offline VS Code extension in Editor.
Install it and choose this SDK explicitly. SDK check/build/headless results,
native UI/clipboard/pickers and actual VSCode edit-save-run are separate gates;
consult the recorded candidate qualification before treating a profile as ready.

## Native data applications

Copy a full example directory from `Examples` and run `lui check` there. Use `lui run --mode headless --stdio true` for byte-preserving stdin, stdout and stderr. The `json-lines` example normalizes JSON records. Supply `--read-file PATH` to `file-summary` to count bytes and line feeds in bounded reads.

A Release native build can select any combination of `LUI_STANDARD_COMPRESSION`, `LUI_STANDARD_CRYPTO` and `LUI_STANDARD_SQLITE` with CMake. Each option defaults to OFF. The SDK packager reads the selected build settings and checks each staged image against the digest used to build its host. Only selected providers appear in `Libraries`, together with their corresponding examples. Keep the target-specific declaration, native image, module lock and listed licenses with each distributed application.

Run `digest` with `--providers Crypto`, `codec` with `--providers Compression -- encode gzip`, and `records` with `--providers SQLite --grant Storage --storage-root DIRECTORY -- list`. These options follow the headless stream options above. Create the private storage directory before running the records example. Each example includes the commands, limits and cleanup behavior in its own README.
## Persistent services and standalone applications

Build a portable headless application with `lui build lui.json --mode headless --sdk SDK_DIRECTORY --output NEW_DIRECTORY`. Run its `Start-LUI.exe` (Windows) or `Start-LUI.sh` (Linux) directly. That launcher uses native-host switches such as `--stdio` and repeated `--provider NAME`. The development CLI uses `--stdio true` and `--providers NAME,OTHER`.

| SDK example | Operator selection | Behavior |
| --- | --- | --- |
| atomic-copy | `--write-file DESTINATION` | Streams stdin into an atomic replacement. |
| folder-copy | `--write-directory DIRECTORY` | Copies between two direct children using bounded memory. |
| live-records | `--providers SQLite --database-root PRIVATE_DIRECTORY` | Commits one notebook operation at a time across cooperating processes. |
| regex-filter | `--providers Regex -- PATTERN` | Writes matching input lines. |
| toml-config | `--providers Toml` | Validates TOML input with Config and applies defaults. |
| tool-check | `--tool EXECUTABLE --tool-cwd DIRECTORY -- ARGUMENT` | Runs a trusted native tool with literal arguments and bounded output. |

The headless operator can grant individual parent environment keys to Context with `--context true --env-keys NAME`. Child environment keys use the separate `--child-env-keys NAME` switch. The child starts with an empty environment. The selected program runs with the OS user's filesystem and networking permissions. See the packaged Process documentation for platform-specific descendant behavior. Args parses application arguments. Messages encodes small isolated values and provides a bounded mailbox. The desktop `message-viewer` example validates and displays a message interactively.

Native Release builds can additionally select `LUI_STANDARD_REGEX` and `LUI_STANDARD_TOML`. Each generated application carries its selected provider and redistribution notices.

## Parallel computation and development tools

The Windows x64 and Linux x64 candidates include six standalone projects and three optional libraries for this workflow. Create a pinned command-line project with `lui new MyTests --template spec-cli --sdk <sdk>` or `lui new MyWorkers --template parallel-data --sdk <sdk>`. The other example folders can be copied together with their module lockfiles. Use the SDK's normal `check`, `stage`, `verify`, and `build` commands.

| Example | Run it | What to observe |
| --- | --- | --- |
| `parallel-data` | Headless with standard I/O | Two isolated worker VMs calculate a checksum and exit. |
| `parallel-viewer` | Desktop project mode | Edit the text field while the batch count advances, then stop both workers. |
| `source-inspect` | Headless with standard I/O, the packaged `Tools/LuiTypeCheck` executable and an explicit working directory | Send a Luau document on stdin for syntax diagnostics, literal imports and compiled size. Use the Windows checker’s `.exe` suffix on Windows. |
| `tool-session` | Headless with standard I/O, a selected Git executable and repository working directory | Send one Git object query per input line and receive a reply from the same child. |
| `tool-pipeline` | The same Git selection | Two concurrent Git stages calculate a fingerprint of the tracked path list. |
| `spec-cli` | Headless with standard I/O and application arguments after `--` | List named cases with `--list`, select a literal substring with `--filter`, or use `--repeat 10` for timings. |

The Workers, Process, Language, Spec and Random directories under `Libraries` include API signatures, limits and examples. CPU workers use independent VM memory and share the host's process. A selected executable runs under the operator's OS account. Its working directory and explicit environment grant are part of the invocation.
