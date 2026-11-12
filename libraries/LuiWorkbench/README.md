# LuiWorkbench (next SDK development)

Small optional Luau modules for editors and tools. Target: SDK
`0.1.0-preview.2` development. Nothing is added to the released preview.1 SDK.
The modules currently use existing runtime APIs and explicitly declared vendors.
Importing a module starts no services, tasks, windows or subscriptions.

```luau
local Commands = require('@LuiWorkbench/Commands')
local Registry = Commands.New()
local Remove = Registry:Register({
    Id = 'File.Save', Label = 'Save document', Category = 'File',
    Shortcut = {Key = 'S', Control = true},
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

`Dispatch` ignores repeats and every shortcut while `TextEditing` is true.
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
