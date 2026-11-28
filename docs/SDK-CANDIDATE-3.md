# LUI SDK preview.3 candidate

This is an **unpublished experimental candidate**, prepared for qualification.
It is not the published preview.2 SDK, and its bytes or inventory must not be
substituted for preview.2 release assets. Local inventory checks establish
consistency, not publisher authenticity. Signing and automatic updates are not
provided by this candidate.

The general-runtime development checkout adds six individually selectable source
libraries. The asset-report and app-tests examples run headless. The timestamp
viewer uses the same UTC parser in a native window. These examples and their vendored libraries are included in this candidate.
Each library's README documents imports, limits, errors and ownership.

Extract the whole folder. Keep its Cli, Tools, Editor, Libraries,
Examples, notices and SdkManifest.json together. On Windows x64, run:

```powershell
.\lui.cmd doctor
.\lui.cmd new ..\MyWorkbench --template workbench
```

From the sibling project, use the candidate's `lui.cmd check`, `run` and `build
--output <new-directory>`. Projects pin the exact inventory; another candidate
with the same version label but different bytes requires a deliberate SDK choice.
There is no implicit downloader. A self-contained SDK recipient needs no global
.NET SDK or C++ toolchain.

This candidate adds native HTTP cancellation tokens, bounded WebSocket clients
and hosted routes, a separate headless component, explicit optional command
shortcuts while editing and selected document/folder access. Copy complete Examples folders outside the SDK and keep
their manifests, vendor files and locks together. For the finite echo example:

```powershell
# From a sibling copy of Examples/websocket-echo; port18083 must be available.
C:\LUI-SDK-0.1.0-preview.3\lui.cmd check
C:\LUI-SDK-0.1.0-preview.3\lui.cmd run --mode headless --grant network.client,network.server --http-origin http://127.0.0.1:18083 --diagnostics development
```

Headless mode accepts explicit network grants and exact-origin or public-only
outbound policy. It creates no UI. The default timeout is30 seconds; an open
listener is pending work. Exit0 means completed/retired,1 means failure,124 means
timeout and130 means interruption. Fixed failure advice omits private source and
raw stderr. Development prints are application disclosure.

Source integration checks cover Windows/Linux protocol, real networking,
capability denial, cancellation, resource retirement and CLI startup/failure paths.
SDK packaging, editor recipient, native UI, physical keyboard/IME/accessibility
and performance are separate qualification gates. Do not infer Linux SDK or GUI
qualification from this Windows archive. GTK performance work is independent.

Examples/selected-files uses the optional LuiWorkbench Files module. Its Dialogs
manifest and candidate desktop host provide native Open file/Save as/Open folder
pickers,4MiB bounded document buffers and read-only single-folder listing/child
reads. Ordinary apps receive no native paths. Save as replaces one selected data
file; it does not preserve original ACLs/metadata or undo a committed write on
cancel. New filesystem/desktop gates are recorded separately from earlier SDK
inventories. Preview.2's path-only picking contract is unchanged.

The project checker now catches unknown and readonly property keys in direct
literal Instance.new initializers. Dynamic tables/classes, value compatibility
and legacy single-file checking retain runtime validation. Properties:any is not
a promise of fully checked constructor inputs.

The public docs retain preview.1/2 contracts. Candidate pages are labeled
unreleased and kept separate. Read SDK/API source headers only when embedding or
extending the framework; ordinary apps use public Luau services and optional
libraries. Preserve LICENSE.txt, ThirdPartyNotices, Dependencies.json,
ManagedDependencies.json and CppRuntime.json when redistributing a qualified app.

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
