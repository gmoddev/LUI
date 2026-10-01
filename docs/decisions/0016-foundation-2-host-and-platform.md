# 0016 — Host bindings, sandbox limits, and Windows services

Date: 2026-10-01

## Context

The first Foundation 2 increment could load a native DLL but did not give C++ hosts a concise binding API, bound Luau execution, provide common Windows services, or produce a standalone folder.

## Decision

Keep the C extension boundary primitive and size-versioned. Add typed C++ host method and signal adapters above the same runtime registration path. C++ callbacks and signal emission run on the owner thread. Native workers marshal through the extension completion queue. Extend the version 1 host table by appended, size-checked signal entries; old binaries see the original prefix.

The host declares capability grants before a script. Sandbox configuration is host-only and precedes scripts and extensions. It freezes Luau globals, applies an allocator-enforced VM heap ceiling, and uses Luau interrupt safepoints for each top-level dispatch. A sandboxed runtime cannot load a native extension; host services need `HostServices`. These controls do not promise process isolation or a process-wide memory ceiling. Compilation, C++ host code, and backend work remain trusted host responsibilities.

Expose clipboard and file picking as explicit, capability-gated services. Reads and pickers return through callbacks after asynchronous platform completion, so the WinUI thread does not block on a picker or clipboard read. Cancellation is `nil, nil`; failure is `nil, Error`. The runtime validates completion IDs, bounds queues, and drops pending callbacks at shutdown. The file picker is invoked only by application code and uses the owning WinUI window.

Expose the system theme as `ThemeService` with `Light`, `Dark`, `HighContrast`, and an owner-thread `ThemeChanged` signal. WinUI determines the current system state and reports changes to the runtime. Package image assets by manifest filename; Luau sees logical names and cannot assign arbitrary filesystem paths to `ImageLabel.Source`.

Publish Windows as a self-contained folder. The packager copies the host dependencies and only the manifest's script, extensions, and assets. It rejects path escapes, duplicate names, collisions, and nonempty output folders. The package is not a signed installer.

Treat version 1 of the C extension ABI as the supported primitive method and signal contract. Its size-checked table prefix remains compatible with older version 1 extensions. Keep opaque native objects, properties, enum registration, and raw window-handle interop out of this ABI; they need a separate versioned design with lifetime and capability rules. Capability-gated Windows services provide the explicit platform boundary for this foundation.

The unpackaged WinUI release uses `EnableMsixTooling`, `WindowsAppSDKSelfContained`, and .NET self-contained publishing. Trimming, single-file, and ready-to-run publishing are disabled. The local interactive probe found that the previous self-contained folder exited with `0xC000027B` after native layout; enabling the unpackaged WinUI tooling property made the published probe pass. Preserve a published-folder desktop qualification in the release process.

## Consequences

Host methods and signals have a narrow primitive conversion set. Native object handles, enum registration, general container conversion, and stable async result objects require a later ABI and type system increment. Future filesystem, network, process, and shell services need distinct grants and enforcement; they are not exposed today. Interactive high contrast, assistive technology, and multi-DPI qualification remain separate from the passed 100% DPI desktop probe.
