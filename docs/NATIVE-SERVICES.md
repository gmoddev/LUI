# Owned native services

The experimental x64 v2 interface is [LuiNativeServices.h](../native/abi/LuiNativeServices.h).
It coexists with the supported primitive [ABI v1](../native/abi/LuiExtension.h).
V2 adds copied immutable bytes, provider-scoped typed handles, cancellable
requests, bounded progress and a retirement domain. The initial Windows/Linux
x64 profile is implemented; ABI v2 remains experimental rather than a frozen SDK.

## Application calls

An owner registers a named service before application entry. Every v2 method in
the initial profile accepts a Luau buffer, an optional managed handle, a terminal
callback and an optional progress callback:

```lua
local Service = app:GetService("RustProof")
local Request = Service:Echo(buffer.fromstring("binary\000data"), nil,
    function(Result, Handle, Error)
        if Error then
            -- Fixed codes such as Canceled, Closed or ProviderFailure.
            return
        end
        assert(Result and buffer.tostring(Result) == "binary\000data")
    end)
local Won, Code = Request:Cancel()
```

Invocation copies the input before native Start. Mutation of the original Luau
buffer cannot change worker data. Completion copies result bytes into a new Luau
buffer. Callbacks run on a later owner pump, including synchronous native
completion. A callback error produces a fixed framework diagnostic without its
payload. Admission failure raises a fixed code and starts no native operation.
Module evaluation cannot call these effectful methods.

`NativeRequest:Cancel()` returns whether cancellation won and a fixed status.
It does not undo side effects or stop a worker. First settlement wins. Managed
handles offer idempotent `Close()` while owned; closing invalidates future use.
They expose no IDs, native pointers or toolkit objects to ordinary Luau. Cross-
provider, forged, closed and stale handles fail validation.

## Provider ownership

Dynamic providers export `LuiNativeQuery` and `LuiNativeInit`. Query supplies exact
v2 tables, one service identity/version, up to 32 method descriptors, resource
kinds and required grants. No allocation or native work belongs in Query. Init
receives a host table; the returned context remains owned by the provider until
the final Destroy callback. Partial failure uses the same retirement protocol;
shutdown callbacks must accept a null context if Init never created one.

Start borrows arguments until return. A worker retains its session and any input
or handle it uses before that return, and releases each retained reference once.
`CopyBytes` copies source bytes immediately. Views are immutable and valid only
while the caller holds a buffer reference. Complete/Emit borrow producer results;
the host retains only on acceptance. The producer releases after every attempt.
No exception or Rust panic may unwind through C.

Admission reserves the maximum result bytes and a terminal record. Result copies
can consume that reservation without a second byte charge. Progress returns
WouldBlock when its separate event allowance is full. Cancellation suppresses
undelivered progress; a full progress queue cannot consume terminal capacity.
The host accepts records in sequence order and processes bounded batches.
Accepted progress precedes a later provider failure. Cancellation, expiry and
closure suppress undelivered progress. Deadlines are observed by the owner pump;
completion and expiry are linearized when processed, not by a real-time timer.

## Reload and shutdown

A domain belongs to one scheduler thread and survives generation replacement.
Each runtime binding owns a session lease through VM teardown. A trusted host
may retire a registered session while that VM remains alive: calls then fail
Closed, and the session stays valid until the binding and owned values drain.
The default domain is shared by runtimes on that thread. Embedders can create an
explicit domain and attach runtimes before registration. Limits include retiring
sessions: 64 MiB/domain, 16 MiB/provider, 8 MiB/value, 128/32 requests, 256/64
handles and four live plus retiring providers. Empty buffers also have bounded
record counts. Runtime requests share the existing async admission ceiling.

Retirement suppresses old callbacks before VM teardown. BeginShutdown must return
promptly. PollShutdown runs on the owner and returns exactly 1 only after workers
are stopped/joined. Releasing the last worker lease alone is insufficient to
prove that its return instruction has left the provider image. Destroy runs
after quiescence and all host-managed references drain, immediately before image
release. Native cleanup callbacks are always owner-dispatched without registry
locks. A stuck or throwing shutdown provider remains pinned and charged; later
admission fails at the session cap. No force-unload or UI-thread join occurs.

The host runtime image becomes process-resident after the first v2 session to
keep worker return paths safe. Provider images remain independently reclaimable.
After destroying its final runtime, an explicit-domain embedding host must keep
pumping that domain until its counters reach zero before releasing it. An
abandoned nonquiescent domain remains pinned until process exit.

## Declared projects and proof

Sidecars select SchemaVersion 3 / AbiVersion 2 and the `OwnedAsync` method profile.
They declare exact byte limits, kinds and deadlines; arbitrary type text is not
accepted. The checker derives NativeRequest/NativeHandle signatures without
executing native code. Captured loading compares identity, grants and complete
method metadata against Query before Init, using the same pinned Windows image
or sealed Linux memfd mechanism as ABI v1. Old metadata is not reinterpreted.

The independent Rust fixture is in [tests/services/rust](../tests/services/rust/Provider.rs).
With an existing Rust toolchain, configure `-DLUI_BUILD_RUST_PROOF=ON` and build
`LuiNativeServiceTests`, `LuiNativeRuntimeTests` and `LuiOwnedProjectTests`.
CTest includes debug/release interoperability and captured-project loading.
The fixture has no external crates and builds with locked, offline Cargo inputs.
Native libraries remain privileged; quotas and typed errors do not sandbox them
or establish application cryptographic, persistence or secure-deletion guarantees.

## Platform adapters

Installation functions are in [LuiApplicationServices.h](../native/abi/LuiApplicationServices.h).
Declare grants before installation, then install before scripts or verified
project loading. `HostServices` does not grant Storage, Notifications or Tray.
`PlatformService:Supports()` reports whether the relevant adapter was installed.
An unavailable service fails explicitly. Each installed adapter consumes one of
the domain's four live plus retiring provider slots.

| Service | Windows x64 | Linux x64 |
| --- | --- | --- |
| Owned ABI and Rust interoperability | Implemented | Implemented |
| StorageService | Native scoped files | Native scoped files |
| NotificationService / TrayService | Windows shell adapter | Unsupported in the initial GTK profile |

### StorageService

A trusted embedder calls `Lui_InstallStorageV1(Runtime, Root)` with an existing,
absolute, dedicated application directory and the Storage grant. Scripts never
choose a native path. Supplied hosts do not automatically select a storage scope.
The host owns directory permissions and privacy. This is not encrypted storage.

All methods use the callback profile above:

| Method | Input | Handle | Successful output |
| --- | --- | --- | --- |
| Open | Key bytes | nil | Managed key handle |
| Read | Empty buffer | Key handle | File bytes, or nil for missing |
| Write | File bytes | Key handle | No output |
| Remove | Empty buffer | Key handle | No output; missing is allowed |

Keys are 1–64 lowercase ASCII letters, digits, underscores or hyphens. Empty files
return an empty buffer, distinct from missing. Limits are 4 MiB/file, 128 files
and 64 MiB/scope. A held root and exclusive lock bind the worker to that scope.
Linked, hardlinked, nonregular and unexpected entries fail closed, including
FIFOs without blocking. One worker serializes reads and whole-file replacements.

Writes use an exclusive same-directory temporary file, flush and rename; Linux
also syncs the directory. Cancellation before commit preserves the previous
file. Cancellation or an error racing/after commit does not establish rollback:
the write may already have happened. Closing a key prevents new calls; previously
admitted work may finish. Shutdown cancels queued work and joins after thread
exit is observable. Native errors are fixed codes without paths or payloads.

Power-loss durability, hostile local writers, encryption and secure deletion are
outside this contract. A crash-left `.lui-write` causes fail-closed installation;
the trusted owner must inspect/recover the scope before retrying. The framework
does not silently delete unknown or stale data.

### Notifications and tray

The supplied Windows host installs `Lui_InstallWindowsDesktopV1` after grants.
Embedding hosts must pump their normal Windows message loop and the LUI owner.
Native shell objects remain private. No adapter failure opens a modal dialog.

`NotificationService:Show` takes a UTF-8 JSON buffer of at most 1,024 bytes with
`Title` (1–63 UTF-8 bytes) and `Body` (0–255 bytes), nil handle and a callback.
NUL and malformed UTF-8 are rejected. Completion means shell submission was
accepted; OS preferences can suppress display. Actions/read receipts are absent.

`TrayService:Create` takes a 1–127-byte UTF-8 tooltip and returns a managed icon.
`Watch` takes an empty buffer and that icon, emitting empty progress buffers for
activation. One watch/icon lasts at most five minutes; rearm after expiry.
Close removes the icon exactly once and ends the watch with Closed. Native
activation after Close is rejected immediately, even before cleanup pumps.
Floods return WouldBlock. Retirement disconnects the old VM before shell cleanup.
Explorer restart recovery and other desktop sessions are not qualified.

The [native integration example](../examples/native-integration/README.md) uses
synthetic payloads and disables unsupported controls on GTK.

## Adapter qualification

`LuiStorageTests`, `LuiStorageFilesTests` and `LuiDesktopServiceTests` cover the
shared ownership/adapter contracts. Linux `LuiStorageFifoTests` runs separately
with a five-second timeout. `LuiWindowsDesktopQualification` requires an actual
Windows shell and is intentionally outside headless CTest. It checks shell
submission, icon registration, the version-4 callback path, owner delivery and
cleanup. It does not prove human input or notification display/readability.
