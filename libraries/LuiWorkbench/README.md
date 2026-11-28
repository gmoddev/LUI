# LuiWorkbench (next SDK development)

Small optional Luau modules for editors and tools. Target: SDK
`0.1.0-preview.3` candidate. This version is not in published preview.1/2 SDKs.
The modules currently use existing runtime APIs and explicitly declared vendors.
Importing a module starts no services, tasks, windows or subscriptions.

## Nested desktop layouts

`DockModel.New()` owns the split/tab graph and application panel identities. `DockHost.New(Model, Options)` presents it inside a caller-owned Window and Frame. The existing two-pane `Workspace` module remains available. [Contract](../../docs/decisions/0095-retained-dock-layout.md) · [Signal analysis example](../../examples/dock-workspace/Src/Main.luau) · [Field notebook example](../../examples/dock-editor/Src/Main.luau)

```luau
local Model=require("@LuiWorkbench/DockModel").New()
local Desk=require("@LuiWorkbench/DockHost").New(Model,{Window=Main,Root=Body})
assert(Desk:Register("Draft",{Title="Notes",Content=Editor,Focus=Editor,MinWidth=180}))
assert(Desk:Register("Plot",{Title="Signal",Content=Plot}))
assert(Desk:Restore({Version=1,Floats={},Main={Id="Desk",Axis="X",Ratio=.65,
    First={Id="Editors",Tabs={"Draft"},Active="Draft"},Second={Id="View",Tabs={"Plot"},Active="Plot"}}}))
```

Drag a tab six logical units to reveal the local cross. Its center adds a tab. Its arms create local splits. Outer arrows split the window root. The tab strip accepts reordering. Escape, capture loss and focus loss cancel the gesture. Neutral space opens a native float. **Redock** and the floating window's normal close both return its contents.

Use `Show`, `Hide`, `Activate`, `Move`, `Float`, `Redock`, `SetTitle` and `Unregister` for commands. `GetLocation(Id)` returns group ID, window ID and one-based slot. `IsVisible(Id)` tests active tab membership. Preflight/commit and `SetRatio` are available on the standalone model. When mounted, mutate through the host to keep chrome and gesture state consistent. `Cancel()` retires a pending pointer transaction.

`Snapshot()` returns plain data suitable for a versioned preference entry. Register available panels before `Restore(Data)`. Omitted panels remain hidden. Check the returned boolean before accepting a preference. Invalid records and unknown references leave the last committed layout intact. Use `@Json/Json` to serialize records as in the signal analysis example. Save the latest snapshot at shutdown to capture current native window bounds.

`OnAttachment(Id,WindowOrNil,Active)` binds or suspends a view-specific sink. `OnGeometry(Id,Window,Content)` runs after arranging active content. `OnChanged()` signals committed layout changes. `OnWindow(Id,Window,Added)` connects and retires per-window commands or metrics. Supply `CanInteract()` to guard modal or project transitions. Applications with a resize wake can set `TrackResize=false` and call `RefreshGeometry()`. Otherwise each mounted host observes native client size every 120 ms. Native proportional layout responds between observations.

`Dispose()` releases dock chrome, floats, callbacks and the resize observer. Supplied controls return to their original parent, size, position and visibility. The caller owns their contents and application state. Dispose before destroying the main window or caller-owned parents.

```luau
local Commands = require('@LuiWorkbench/Commands')
local Registry = Commands.New()
local Remove = Registry:Register({
    Id = 'File.Save', Label = 'Save document', Category = 'File',
    Shortcut = {Key = 'S', Control = true},
    AllowWhileEditing = true,
    Enabled = function() return HasEdits end,
    Execute = SaveDocument,
})
Window.Shortcut:Connect(function(Input) Registry:Dispatch(Input) end)
local Rows = Registry:List('save', 12)
Registry:Execute(Rows[1].Id)
Remove() -- idempotent; cannot unregister a later replacement
Registry:Dispose()
```

`Commands.New()` supports 128 commands. IDs are unique and at most 128 UTF-8
bytes; labels 256; categories 128; queries 256. Search matches every
whitespace-separated token in label/ID/category, case-insensitively for ASCII,
in stable ID order. `List(Query?, Limit?)` defaults to 12 rows, maximum 128.
Rows and copied shortcut bindings are immutable. A shortcut is an exact portable
Key plus optional Control/Shift/Alt booleans. Conflicting bindings are refused.
Definitions and bindings must be plain tables; metatables are refused. Only the
four validated shortcut fields are retained. Unregister handles retain an opaque
identity rather than callbacks, so holding one after disposal does not retain
the registered callback's application data.

Enablement is checked again at execution. Predicates that fail disable the
command. Execute returns `(Success, Reason?)`: Missing, Disabled, Busy, Retired,
or Failed. Application callback details are not returned or logged. Invocation
is serialized until its callback returns, including across scheduler yields.
Registration during an invocation and recursive listing are refused. Listing
from a command may build a palette without releasing the invocation fence.
Disposal and unregister remain available. Callbacks must finish or use existing cancellation-aware LUI
services. The registry does not own or cancel tasks started by a command.

`Dispatch` ignores repeats. While `TextEditing` is true it ignores commands by
default. Set `AllowWhileEditing = true` on a command such as Save to allow its
explicit Control shortcut in a focused editor. This opt-in requires a shortcut
with `Control = true` and no Alt modifier, protecting plain typing and AltGr.
Shift is optional. The flag is copied at registration; changing the original
definition afterward has no effect. Listed rows report the effective boolean.
Reserved text editing chords cannot opt in, even when a local undo stack is
empty: Control with A/C/V/X/Y/Z, Insert/Delete/Backspace, Home/End, arrow keys,
PageUp/PageDown, Enter/Return or Tab (with or without Shift). Register such an
application command without the flag to keep it available outside editing.
Commands without the flag retain preview.2 behavior. The runtime still suppresses
shortcuts during composition, and native editor-handled keys remain native.
It uses the existing unhandled Window.Shortcut signal; it cannot consume native
input. Bind palette buttons and menus to Execute as well. Native shortcut
availability follows the host's existing shortcut mapping. Dispose drops all
callbacks and rejects subsequent mutations; unsubscribe the window signal too.

## Documents

```luau
local Documents = require('@LuiWorkbench/Documents')
local Store = Documents.New()
Store:Open('Notes', 'Notes.luau', '-- hello')
Store:SetText('Notes', Editor.Text)
local Receipt = Store:BeginSave('Notes')
-- Write Receipt.Text through an authorized application service.
-- After completion, on the scheduler thread:
Store:CompleteSave(Receipt, true)
local Closed, Reason = Store:Close('Notes') -- false, 'Dirty' if edits remain
```

Open returns an immutable view; an existing ID returns its current buffer.
Get/List return immutable snapshots, never the mutable store. SetText increments
the revision only when bytes change. Dirty compares current bytes to the last
successfully saved bytes, so undoing to the saved content becomes clean.
Maximum 32 documents, 65,534 UTF-8 bytes per buffer, no embedded NUL, ID 128 and
label 256 bytes. Text ownership is bounded; no filesystem paths are resolved.

`BeginLoad(Id)` captures a clean document's identity and revision before an
application-owned asynchronous read. `CompleteLoad(Receipt, Text)` installs the
text as a clean baseline while preserving its label and tab order. Pass nil for
a failed read. Empty text is a successful empty document. Completion consumes
the receipt and returns `(Applied, Reason?)`, with Stale, Retired, Failed,
InvalidText or RevisionLimit on refusal.

Only one load may be pending per document. Loading while dirty or saving is
refused. Editing, including editing then undoing, makes a pending result stale.
Beginning a save, closing/reopening the ID, canceling the load or disposing the
store retires its receipt. `CancelLoad(Receipt)` reports whether it retired an
active load. The application owns and cancels its actual service request.

One BeginSave receipt per document may be pending. It captures the exact revision
and immutable submitted bytes. CompleteSave accepts only the original receipt
object once. Success updates the saved baseline, preserving any later edits;
failure leaves it unchanged. CancelSave retires the receipt and returns whether
it was pending. Cancel the associated application IO separately. Close refuses
SavePending even with Discard, then refuses Dirty unless Discard is explicitly
true. Close/reopen cannot reuse an earlier receipt. Move uses a final 1-based
position in the global tab order. Dispose retires receipts and releases buffers;
the application owns any pending service request and must cancel it separately.

All calls belong on the runtime scheduler thread. Pure model semantics can run
headlessly on Windows/Linux; UI and platform qualification are separate. No
native ABI, Instance reflection or global service changes are needed: exported
types in each module are the declarations consumed by project analysis.

Stage a standalone command example with:

```text
python scripts/Stage-LibraryExample.py --example commands --output build/CommandsApp
cd build/CommandsApp
lui check
lui run
```

The stager captures source bytes and generates ordinary module-lock hashes.
The application owns signal connections and must dispose models at shutdown.

## Workspace and Split

```luau
local Workspace = require('@LuiWorkbench/Workspace')
local Layout = Workspace.New()
Layout:Add('Notes', 'Left')
Layout:Add('Preview', 'Right')
Layout:Dock('Notes', 'Right', 1)
Layout:Activate('Preview')
local LeftWidth, RightWidth, IsSplit = Layout:Layout(800)
local Bytes = Layout:Encode()
local Restored, Failure = Layout:Decode(Bytes)
```

Workspace owns presentation IDs only. Add registers up to 32 unique UTF-8 IDs
(128 bytes each) in Left or Right; default Left. Remove unregisters an ID and
repairs active selection. Dock moves a known ID to a side and optional final
1-based position, default end. Activate selects the visible tab in its side.
Snapshot returns a deeply frozen Version 1 layout. Restore validates the entire
snapshot before mutation: exact known identity set, unique dense arrays, valid
active tabs, finite Ratio in 0.1–0.9, no unknown fields or metatables. It returns
false, InvalidLayout on incompatible data, preserving current presentation.
It never opens documents from stored IDs.

SetRatio stores presentation intent. Layout(Width) returns logical widths and
whether to display two panes; at least 240 units per pane plus an 8-unit divider
are required. With two populated groups below 488 units, it returns a single
left-sized viewport. The view must show its selected tab there, including a tab
from the Right group. A single populated group receives the full width.
Encode/Decode use the bounded `LUIW1` length-prefixed UTF-8 format, maximum 8192
bytes, with no native paths, document contents or executable objects. Truncated,
extra, unknown-version and incompatible identity data are refused atomically.

`Split.New()` provides Begin(Input, WindowX, Width, Ratio),
Move(Input, WindowX, Width), Finish(Input), Cancel() and Dispose(). Begin accepts
one primary mouse contact at a time. Use window-relative logical X for both
Begin and Move. Movement returns a clamped ratio; matching primary release
commits it. Native cancellation, invalid coordinates or a resized viewport
returns the original ratio and retires the gesture. Other contacts/buttons
cannot finish it. Call Cancel on other presentation changes and apply its
returned original ratio. Dispose retires without invoking callbacks. The view
owns the existing input signal connections; no native capture API is introduced.

## Preferences

```luau
local Preferences = require('@LuiWorkbench/Preferences')
local Store = Preferences.New(app:GetService('StorageService'), 'workspace_layout')
local Operation = Store:Read(function(Bytes, Failure)
    if Bytes and not Failure then Layout:Decode(buffer.tostring(Bytes)) end
end)
-- Later, after the read completes:
Store:Write(buffer.fromstring(Layout:Encode()), function(_, Failure)
    -- Report Failure through application status UI.
end)
Operation:Cancel()
Store:Dispose()
```

Construction is lazy. Keys must contain 1–64 lowercase ASCII letters, digits,
underscores or hyphens. Read/Write admit one operation at a time per store;
inputs are copied at admission and bounded to 8192 bytes. Missing content is
nil without error; an empty buffer is a present empty value. Decode and validate
application schemas before applying stored bytes. Oversized reads return
ByteLimit; native failure codes pass through, and synchronous unavailable
service calls return Unavailable. Completion exceptions produce only the fixed
`[LUI:Preferences] CompletionFailed` diagnostic.

Operations expose idempotent Cancel (true only for the current operation).
Cancel/Dispose retire callbacks before canceling the native request and closing
the owned handle. Late Open completions close their handle. No implicit retries
occur. Cancellation racing a native write commit does not establish rollback;
the native scoped storage contract applies. Dispose is idempotent and starts no
new work. The application must cancel independently owned document save receipts.

This is ordinary, unencrypted preference storage. It cannot store credentials
securely or provide transactions across keys. The owner must grant Storage and
install a dedicated scope. Next SDK preview.2 desktop project hosts provision
one when the manifest includes both the Storage capability and
`"Storage": {"ApplicationId": "com.example.notes"}`. The ID and project directory
identity determine the scope: another project with the same ID gets separate
data. Packaged and development copies also have separate data. See
[native storage](https://github.com/gmoddev/LUI/blob/main/docs/NATIVE-SERVICES.md#storageservice) for locations,
failure behavior and migration limits. Other embedders still install the provider
explicitly. Published preview.1 does not support this manifest field.

## Selected files (preview.3 candidate)

`File:ByteLength(Callback)` returns the current length as an exact integer up to 2^53−1. `File:ReadRange(Position, Count, Callback)` reads from a zero-based absolute byte offset. Request 1–65,536 bytes at a time. A successful short buffer ends at EOF and an empty buffer means the offset is at or beyond EOF. Each callback receives a value and optional fixed error code. Each operation can be canceled. A sequence of reads can observe external changes between chunks. The whole-file `Read` and atomic `Write` methods keep their 4 MiB ceiling.

A writable file returned by `Picker:SaveFile` supports `File:BeginWriter(function(Writer, Failure) ... end)`. Each writer prepares an empty replacement. Call `Writer:Write(Buffer, Callback)` for up to 65,536 bytes, `Writer:Seek(Offset)` for an idle zero-based cursor, and `Writer:Position()` for the next byte. `Writer:Commit(Callback)` publishes the draft. `Writer:Close()`, picker disposal, or canceling a pending writer operation aborts it. Each callback receives a failure code or nil. A writer retains its destination independently of closing the original file. A successful commit or failed write/commit retires the wrapper. Four writers may be open per FileService, each with a maximum logical length of 128 MiB. A freshly committed empty writer truncates or creates the file.

```luau
File:BeginWriter(function(Writer, Failure)
    if not Writer or Failure then return end
    local Draft = Writer
    Draft:Write(buffer.fromstring("complete document"), function(WriteError)
        if WriteError then return end
        Draft:Commit(function(CommitError)
            if CommitError then print("[LUI:Files] Save failed") end
        end)
    end)
end)
```

Each commit checks the revision captured when its draft opened. A concurrent reader sees a complete old or new file at the selected name. A check followed by rename does not provide compare-and-swap against an external writer. An error or cancellation after rename can leave the replacement visible. Windows flushes the new file; Linux flushes it and the containing directory. Power-loss durability depends on the device and filesystem. On Linux an anonymous temporary file is preferred. A named fallback on a filesystem without anonymous files can leave a private draft after a hard crash. Windows marks its staged handle delete-pending until rename. The packaged `atomic-copy` example grants one explicit destination with `--write-file`.

`Files.Reader(File, Offset?)` creates an independent cursor, initially at zero unless supplied. `Reader:Read(Count, Callback)` advances by the bytes delivered on success. `Position()` reports the next byte and `Seek(Offset)` repositions an idle reader. Canceling a read preserves the old position and suppresses its callback. `Reader:Close()` retires the cursor. The caller still owns and closes the file.

The headless `file-summary` example takes an exact read-only file selected by the operator using `--read-file`. Desktop applications continue to use the native picker.

Use `require('@LuiWorkbench/Files')` for documents selected by the user. This is
separate from Preferences, which owns app-private state. The manifest needs
Dialogs and the desktop host must provide FileService. Start with the complete
`selected-files` SDK example or stage it with Stage-LibraryExample.py.

```luau
local Files = require('@LuiWorkbench/Files')
local Picker = Files.New(app:GetService('FileService'))
Picker:OpenFile(function(File, Failure)
    if not File then return end -- Dismissal is nil/nil; inspect Failure for errors.
    File:Read(function(Bytes, ReadFailure)
        File:Close()
        if Bytes then
            local Text = buffer.tostring(Bytes)
            -- Validate encoding/size before assigning text to an editor.
        end
    end)
end)
-- Call Picker:Dispose() when the owning window/application is destroyed.
```

OpenFile gives a read-only File with Name/Read/Close. SaveFile(SuggestedName,
Complete) asks for one writable destination; use Write(buffer, Complete).
OpenFolder returns a read-only Folder: List gives Name/IsDirectory/Size records,
Open(Name, Complete) reads one regular child file, and Close releases the grant.
No native paths, folder writes, recursive traversal or persisted grants.

Every asynchronous method returns an Operation. Cancel returns true once while
active and suppresses its callback; it cannot undo an already committed write.
Dispose suppresses callbacks, cancels requests and closes owned resources.
Resource Close and Dispose are idempotent. Dismissal returns nil/nil; native
failures are fixed strings such as CapabilityDenied, LimitExceeded, Closed,
DeadlineExceeded or ProviderFailure. Admission refusal maps to Unavailable.
Callback errors are contained and newly delivered resources are closed.

Limits are4MiB per buffer,32 provider resources/requests,256 listed entries and
255-byte portable UTF-8 leaf names, plus native domain quotas. IO deadlines are
30s; pickers120s. Close promptly: Windows selected handles can prevent rename.
Symlinks, reparse paths, hardlinked files, devices and direct UNC paths are refused.
Mounted/mapped network filesystems are unqualified, not detected portably.
Save as uses a new data file and does not preserve ACLs/metadata. Cancellation or
an error after replacement cannot guarantee the original file remains. Keep
editor text when a save is uncertain. Native FileService's buffer/handle methods
are an advanced provider interface, not the normal app workflow.

Windows WinUI3 and Linux GTK4.10+ are the intended initial hosts; actual desktop
qualification is recorded separately from source/owner checks. Browser/mobile
and other desktop/file system profiles remain unsupported.

## Integrated example

```text
python scripts/Stage-LibraryExample.py --example workbench --output build/WorkbenchApp
cd build/WorkbenchApp
lui check
lui run
```

The example combines command buttons/palette, native window shortcuts, multiple
CodeEditor buffers, dirty tabs, explicit redocking, draggable split, inspection,
virtualized 1,000-note navigation, deferred loading, app-local themes and clean
shutdown. Notes use distinct ordinary storage keys; the storage-qualified test
host demonstrates durable restart/restoration. Preview.2 desktop launch requests
an explicit scope; a provisioning failure stops startup. Embedders without
storage can still run the example, and the palette has an explicitly labeled
discard-and-close command for unsaved buffers. Saved layouts restore only when
their known document set matches. Notes are synthetic and editing never executes
their contents. No real engine, messaging identity or application data is used.

## Native workload qualification

Framework contributors can stage a fresh workbench and reuse the desktop
workload counters and limits for 200 mixed transitions:

```text
python scripts/Stage-LibraryExample.py --example workbench --output build/WorkbenchNative
```

With a built Windows native host, run:

```powershell
& $NativeHost --project build/WorkbenchNative/lui.json --workbench-qualification --qualification-report build/WorkbenchWindows.json
```

With the GTK test target built, use a fresh storage directory:

```sh
mkdir build/WorkbenchStorage
GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none xvfb-run -a dbus-run-session -- build/gtk4/backends/gtk4/LuiGtkTests --workbench build/WorkbenchNative/lui.json "$PWD/build/WorkbenchStorage" "$PWD/build/WorkbenchGtk.json"
```

Reports include startup before project preparation, native edit samples, owner
response, layout, native submission, working set and retained editor retirement.
Windows scrolling measures explicit C API admission; GTK uses its native
controller. Neither timing includes physical input/display or driver pacing.
Real keyboard, pointer, clipboard and IME journeys require separate qualification.

With `--write-directory DIRECTORY`, a packaged headless app's existing OpenFolder request receives direct-child write authority. An embedded host can grant it with Lui_CompleteWritableFolderV1. Ordinary native picker completion and `--read-directory` grant listing and reads. Folder:OpenOutput(Name, Callback) returns a writable file, creating or truncating only on a successful write or writer commit. Folder:CreateDirectory, RemoveDirectory and RemoveFile take one leaf name and a completion receiving an optional error. RemoveDirectory requires an empty directory. Folder:Rename(From, To, Callback) moves a file or directory within the same parent and refuses an occupied destination. No operation recursively removes data. All names are validated and relative to the anchored selection. Direct mutations address current names, so coordinate independent editors. Linux uses directory-relative operations with no final symlink traversal. Windows uses protected directory and entry handles. If an independent program swaps an entry concurrently, the Linux operation acts on the name it finds at the system call. It never follows a replacement link to its target. Mutations can finish before cancellation is observed, and a directory sync failure can report an error after publication. Inspect the directory before retrying.

`Files.Copy(Source, Destination, Callback)` uses a 64 KiB reader and a temporary seekable writer to publish up to 128 MiB. Cancel suppresses the completion and discards an uncommitted draft. Both file handles remain caller-owned. Manually closing either terminates the copy with Closed. Disposing either picker cancels the copy and suppresses completion. Keep source bytes stable while copying; independent edits between chunk reads can appear in the copy. `Examples/folder-copy` demonstrates a complete application.
