# 0088 — Immutable runtime image sources

`AssetService:FromEncoded(Data)` constructs a resource from a PNG or JPEG
buffer. `AssetService:FromPixels(Data, Width, Height, Stride, Format)`
constructs one from `BGRA8Premultiplied` pixels. Both return a resource and
nil on acceptance, or nil and a fixed failure code. The source owns a copy.
Raw inputs use exact stride times height byte length, four-byte aligned stride
and packed storage. Encoded inputs are sampled for dimensions before allocation.
The native decoder validates the complete image when a control requests it.

`ImageLabel.ImageResource` binds a runtime source while `Source` retains its
string contract for declared assets. Assigning one source selects that mode.
Setting `ImageResource` to nil clears a runtime binding. Constructor
initializers specify a single source. Clones preserve existing bindings.
Each resource exposes read-only `Width`, `Height` and `IsClosed`. `Close()`
releases the handle's source lease and prevents new bindings. Existing
bindings and their clones retain immutable pixels until detached. A handle
that becomes unreachable releases its lease during Luau collection.

For a selected file, read the authorized document with the existing
FileService or Files wrapper, then pass its buffer to `FromEncoded`.
This keeps picker cancellation, file identity and grants in FileService.
The image service takes bytes and accepts no filesystem paths. A label's
existing `LoadState` and `LoadChanged` report final presentation readiness.
Replacing or destroying a label retires its request token. Multiple labels
with the same resource share the generation-local cache entry and flight.

The source quota is 64 MiB and 1,024 immutable source records per owner
thread, including source leases from retiring generations. PNG/JPEG data
is limited to 1 MiB. Each side is at most 4,096
pixels and total pixels at most 1,048,576. Strided raw input is limited to
8 MiB and the packed copy to 4 MiB. Source storage remains charged until
the last managed binding or native decode lease releases it. A separate
32 MiB presentation cache and existing async staging limits apply.
Inactive native textures own their pixels, so cache retention cannot
exhaust the quota for new runtime sources. Each successful raw miss makes
one native copy and subsequent bindings reuse the resulting texture.
Diagnostics count host references; toolkit and GPU allocations are separate.

The private bridge is additive. Its versioned load callback shares the
declared-image cancel callback and context. The opaque source outlives owner
retirement if a host holds an explicit lease. WinUI and GTK use the existing
desktop adapters and support `PlatformService:Supports("RuntimeImages")`.
Headless runtimes can create and inspect resources and report unavailable
presentation. Browser and mobile hosts have no native mapping for these
resources in this checkpoint. FrameSurface remains the update path for
continuous pixel streams.
