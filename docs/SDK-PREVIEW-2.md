# Preview.2 release candidate notes

This is an unreleased preview candidate. Editing workflows pass on the selected
Windows and Linux test profiles. SDK08 passed selected clean Windows 10 LTSC SDK
and separate app-only recipient journeys. Fresh Windows 11, real Windows input
methods, ordinary downloaded-file trust and signing still need qualification.
The published preview.1 download remains unchanged.

Development extension 0.1.7 binds pending inspection/type checking to its
workspace and tool settings, cancels stale work and suppresses retired-client
events. Bounded UTF-8 setup reads and fixed guidance prevent malformed manifest
text from appearing in setup errors. Source review and Windows/Linux integration
checks pass. Prepared SDK13 also passes the two-process real VS Code journey on
the selected Windows10LTSC virtual recipient without a global SDK/system CRT.
Fresh Windows11, physical input, ordinary download/trust and signing remain open.

Development extension 0.1.6 adds persistent process status and a conservative
restart hint after project edits. It distinguishes preparation, startup, running,
unsaved edits, stopping and Stop refusal. Running does not promise window
readiness; restart after edits made outside VS Code too.

Development extension 0.1.5 adds an undeclared-source warning and Add File to
Project quick fix. It stages only Modules.Files for review and saving, preserving
explicit source declarations and all other manifest text.

Development extension 0.1.4 adds explicit Restart App with saved-file checks,
owned-process retirement and cancellation of pending launches by Stop App.
Save documents inside your app before restarting; its memory is discarded.
Native apps do not restart automatically on save. SDK12 retains extension 0.1.3;
this source change does not update that prepared archive.

The extension includes actionable capability and startup diagnostics
and safely handles failed SDK discovery and project creation without exposing
malformed tool output.
Overlapping discovery requests for the same SDK and project share one integrity
scan. Later operations run a fresh scan; project pins and permission checks remain
in effect.
The development libraries now include revision-bound document load receipts,
the bounded Json codec and JsonClient over the existing HTTP service. A staged
json-client example demonstrates a loopback API and native status window.
The runtime safely defers quota-error unwinding until collection has finished.
Service names are not permission tokens: HttpService needs network.client and
HttpServerService needs network.server. The native host sends bounded fixed-code
reasons without private source or arbitrary stderr. Inspect Layout explains its
privileged-project restriction and directs you to Run App. A callback failure can
leave the window open while that operation fails. The older preview.1 extension
and native host do not gain these changes from a documentation update.

Start with `lui new MyApp`, or try `lui new MyWorkbench --template workbench`.
The workbench starter includes tabs, two editor panes, searchable commands,
themes, synthetic notes and saved layouts. The smaller `commands` template
introduces command buttons and keyboard shortcuts. Both include their libraries
and work offline after you extract the SDK.

Eight optional public modules are prepared in development source: Commands,
Documents, Workspace, Split, Preferences, Json, JsonClient and NumericInput.
SDK12 retains the earlier seven-module set. NumericInput composes existing text
and step buttons, with private support sources captured in the content lock.
Its targeted Windows headless checks pass; Linux/native GUI qualification remains
pending. The modules help organize commands, track unsaved
edits, arrange two panes, resize the divider, save ordinary preferences and
exchange bounded JSON through the existing HTTP service. They are vendored
Luau source modules; importing an unused module starts no background work.

Desktop applications can request their own persistent storage with the Storage
capability and `Storage.ApplicationId` in `lui.json`. Data stays separate for
different project directories. Saving and reopening the same project preserves
it; copying or packaging the project creates a separate scope. Another running
copy may hold the scope lock. Startup reports a storage failure without silently
switching to temporary data.

The workbench allows 32 open documents and 64 KiB minus two bytes per document;
its ordinary preference saves are limited to 8 KiB. Larger synthetic navigation
collections are virtualized. Floating panes, arbitrary docking trees, code
execution inside the workbench, encrypted storage and storage migration are not
included. Editing, selection, clipboard, undo/redo, pane resizing, saving and
reopening pass on the selected Windows and GTK/X11 test profiles. Chinese input
passes on the selected GTK profile. A 200-transition workbench workload passes
the existing desktop performance and resource limits on each profile. These
tests measure application response and native submission, excluding physical
input/display latency. Real Windows input methods, fresh Windows 11 installation,
downloaded-file launch and signing remain release gates.

Projects retain their exact SDK version and inventory pin. Reopening a project
never silently upgrades its SDK or downloads dependencies. Read the included
[SDK guide](SDK.md) for setup, VS Code, checking and packaging.
