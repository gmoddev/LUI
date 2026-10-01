# 0015 — Extension ABI and host capabilities

Date: 2026-10-01

## Context

Foundation 1's `LuiRuntime.h` is an internal host callback boundary. Extensions need a separate C contract that can load without exposing `lua_State*`, native UI handles, or C++ standard-library types. A failed or incompatible native library must not leave partial services in a live runtime.

## Decision

Use [LuiExtension.h](../../native/abi/LuiExtension.h) for the experimental version 1 extension ABI. Each library exports `LuiExtensionQuery`, `LuiExtensionInit`, and `LuiExtensionShutdown`. Query reports a size-versioned identity and required capability mask before initialization. The host supplies a size-versioned method registration, logging, error, and UI completion table during init. Service methods exchange size-versioned primitive values; strings are borrowed during the call, and returned strings are copied before control returns to Luau. A method executes only on the authoritative scheduler thread. A worker may enqueue a bounded UI completion, which runs in `Lui_Pump` after the active dispatch and cannot reenter Luau directly.

The host declares capabilities once before scripts or extensions. The default mask is zero. A `NativeExtensions` grant is required even to load a library whose query requests no other bits. Loading uses an absolute path and exact ABI version check. Registration is transactional: initializer failure removes its registrations, calls shutdown for any returned context, unloads the library, and leaves the runtime usable. Loading after application execution starts is rejected. A runtime-specific schema lists registered service methods; built-in generated types are not changed by a particular application's extensions.

WinUI reads a versioned manifest as host configuration. It resolves the script under the manifest directory and allows DLL names beside the manifest. This first manifest belongs to trusted applications; it is not a sandbox boundary and does not claim filesystem, network, or process isolation.

## Consequences

The new ABI can expand by a new version or additive size-checked fields. Version 1 currently registers primitive service methods and schedules owner-thread completions only. Object handles, a managed worker pool, higher-level async Luau results, additional capabilities, and generated application extension types require later work. Native libraries remain trusted process code, so ABI validation protects compatibility and ordinary load failures rather than isolating malicious native code.
