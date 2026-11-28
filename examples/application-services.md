# Three applications using the public SDK

The immutable baseline at `b0eb303` targets published **0.1.0-preview.2**. Current
apps use the **unreleased preview.3 candidate**. Private candidate packaging and
owner journeys pass for the networking checkpoint; the expanded file candidate
and native recipient journeys are being qualified. Nothing is published.
Application code has no private API imports.

Start with an extracted SDK and Python 3. Each command creates a separate project,
copies dependencies and lock hashes from that SDK, and runs its packaged checker:

```powershell
python tools/application-services/Prepare.py --sdk C:/LUI-SDK-0.1.0-preview.3 --app file-desk --output C:/Apps/FileDesk
python tools/application-services/Prepare.py --sdk C:/LUI-SDK-0.1.0-preview.3 --app network-desk --output C:/Apps/NetworkDesk
python tools/application-services/Prepare.py --sdk C:/LUI-SDK-0.1.0-preview.3 --app window-desk --output C:/Apps/WindowDesk
```

From any generated project, use the SDK's ordinary `lui doctor`, `lui check`,
`lui run` and `lui build --output <new-directory>` commands. No C++ build, global
.NET SDK, downloaded modules or source-checkout runtime is needed. Copy this file,
the chosen application's `Src/Main.luau`, and `Prepare.py` with their repository
layout to reproduce preparation on another SDK recipient. Native Linux recipient
support requires a matching Linux SDK; a Windows archive is not portable to Linux.

**File Desk** reads a selected text file, saves a copy through Save as, lists a
selected folder and reads a named child file. It also copies/pastes and saves a
bounded app-private scratchpad through Preferences. No native paths enter the app.
It preserves edits made during asynchronous reads, protects dirty close, and
reports dismissal, cancellation and unavailable providers in its status area.
Read selections/folders are read-only; a Save as destination grants writes to
that entry only. Cancellation cannot undo a committed save. The editor limit is
8,192 UTF-8 bytes; the underlying service supports buffers up to4MiB. The SDK2
baseline at b0eb303 displays a path and has no document IO; retain that evidence.

**Network Desk** runs an ephemeral loopback HTTP server, sends real JSON traffic,
tests invalid-JSON recovery and makes a certificate-validated HTTPS request to
example.com. The slow-request button holds a real TCP connection without blocking
the scheduler; Cancel affects only the action's token. The server and
connections close with the window. Network grants are explicit; `network.raw` is
needed only for the cancellation fixture. The candidate adds real WebSocket echo
through the same server. The generated headless manifest runs the same application
without UI on fixed loopback port18082:

```powershell
lui run headless.lui.json --mode headless --grant network.client,network.server --http-origin http://127.0.0.1:18082 --diagnostics development
```

This finite journey verifies HTTP receipts, token isolation and WebSocket messages,
then closes its server. Port18082 must be available; change the code, manifest port
policy and explicit origin together if needed. Source/owner checks do not replace
candidate SDK-recipient qualification.

**Window Desk** edits one shared note in up to four windows, with command-backed
actions, Ctrl+S/Ctrl+N inside editors, an application context panel, system theme changes and
workspace restoration. It uses the existing Commands, Documents, Workspace and
Preferences modules. The context panel is ordinary LUI UI, not an OS menu. Workspace
restoration covers open-view count, theme and pane state; window coordinates are
not public in preview.2. The candidate opts Save/New into the command registry's
editor policy and preserves native high-contrast colors. The SDK2 baseline still
uses main-window buttons while editing. Saved notes have an 8,192-byte UTF-8 storage
limit. Save note and Save workspace report completion separately. Closing protects
unsaved note edits and pending writes; workspace changes are saved only on request.
