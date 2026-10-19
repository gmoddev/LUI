# Internal compiled project host interface

The shared component prepares schema v2 declared inputs and compiles every source without executing application code. The internal owner-thread `Lui_RunProject` executes policy-enabled prepared projects. CLI, preview, WinUI and editor consume the same native project contract under [Project hosting](PROJECT-HOST.md). Project checking and content staging never execute application or native provider code.

## C++ component

[Owner-host sidecars](PROJECT-DECLARATIONS.md) bind typed projects to captured ABI v1 primitive registrations. Declared registries must match before entry; mismatch executes no application callback. Declarations grant no capabilities. Native providers additionally require captured verified loading through `Lui_LoadProjectNative`. Source descriptors carry captured sidecar hashes and canonical registration metadata/digests without changing the C struct ABI.

`runtime/project/Project.h` provides:

- `Prepare(Manifest, Options, Error)` → immutable source `Snapshot`.
- `Compile(Sources, Options, Error)` → immutable `CompiledSnapshot` with all declared bytecode.
- `Snapshot::Resolve(Importer, Request, Error)` → captured declared source; no disk lookup.

Compile consumes the retained source snapshot. Editing/deleting files after capture does not change it. Source-only preparation version 1 digests remain unchanged. Compiled preparation version 2 adds the pinned Luau profile and artifact hashes. The snapshot owns both captured sources and bytecode; release all shared references to free that data. `ValidateImports` uses Luau's parser and shared resolution to reject known literal import errors/cycles, including unused sources and deferred functions, while preserving lexical shadowing. It collects valid edges despite other syntax/import errors so those errors cannot mask a known cycle. Compile records `Imports: LiteralBuiltinV1` in its profile. Dynamic/aliased imports and sources rebinding global require use runtime checks. [Project-wide checking](PROJECT-TYPECHECK.md) provides literal export/type-alias inference.

## C host ABI v1

Include `native/abi/LuiProject.h` and link the runtime. `AbiVersion` is `LUI_PROJECT_ABI_VERSION`; initialize `StructSize` to the exact native struct size. Managed adapters must match native pointer width/alignment. Unsupported versions, sizes, or flags are refused.

The basic lifecycle is:

1. Initialize `LuiProjectOptionsV1`, including size/version; all other fields can be zero for defaults.
2. Call `Lui_ProjectPrepare` with a length-delimited UTF-8 manifest path and an output handle. Success owns one reference; failure sets that output to null.
3. Initialize `LuiProjectSummaryV1` or `LuiProjectSourceV1` size/version, then query summary, sorted source index, or importer-relative resolution.
4. Retain a live owned handle before transferring it to another thread; release each owned reference once. Final release invalidates all borrowed views.

Preparation is synchronous and worker-callable, with no VM involved. The optional cancellation callback runs on that same thread, must not enter a VM, and must remain valid until preparation returns. Options and their callback/context are not retained afterwards. Cancellation is observed between reads/compiler calls and before result publication, not inside a running compiler invocation. The host owns reload coalescing and stale-result suppression; this interface does not create a session scheduler.

Summary includes entry identity, source digest, compiled digest, preparation version, source count, canonical descriptor, combined bytecode bytes, and retained-data charge. Source views contain canonical identity/owner, logical and staged relative paths, hashes, and length-delimited captured source/bytecode. Borrowed textual metadata is also NUL terminated; bytecode must always be read by length. Indexed sources are sorted by identity. Query failure leaves its output struct untouched.

Every concurrent query needs a retained live reference; final release must not race a query. Null retain/release are harmless. Foreign/stale handles, duplicate releases, invalid caller pointers, and overlapping input/output buffers are host programming errors.

## Limits and diagnostics

Default source bounds remain manifest 256 KiB, lock 512 KiB, 33 owners, 256 files, 8 MiB per source, and 32 MiB aggregate source. Compiled bounds add 16 MiB per artifact, 64 MiB aggregate bytecode, and 128 MiB retained-data admission charge. Zero C options select defaults; positive values can narrow them. Raising a ceiling fails `InvalidOptions`. Compile's C++ budget options apply to compiled/retained data; source admission bounds apply during `Prepare`.

Retained-data accounting charges string capacities and native structures with conservative container/host bookkeeping allowances. It is an admission budget, not a process-wide memory limit. The compiler's transient allocations cannot be preempted or capped by this interface. Retained charge can vary by platform/library; digests exclude it.

Errors use generic `[LUI:Project] Code` strings, including `CompileFailed`, `LimitExceeded`, and `Canceled`. Error buffers are NUL terminated if capacity is nonzero, truncate to fit, and permit null/zero to omit diagnostics. A null pointer with positive capacity is rejected. Failed compiler diagnostics are discarded so they cannot expose source fragments. No modal dialog is used.

**Returned source, bytecode and descriptor are trusted host data.** They are not redacted diagnostic projections. Preparation captures policy; runtime policy is selected before VM creation. Do not publish trusted views into logs or preview; use the policy-enabled projection APIs.

## Experimental native execution

Create the runtime with exact-size/version `LuiDiagnosticPolicyV1` and `Lui_CreateWithPolicy`, then initialize `LuiProjectRunOptionsV1` with its exact size/version and Flags=0. Legacy development owners use the explicit DEVELOPMENT_DIAGNOSTICS flag and cannot run Restricted projects. Reserved must be zero; depth zero selects 64 and positive values narrow it. Call `Lui_RunProject` on the fresh owner runtime with a live prepared handle; the runtime retains inputs after caller release.

Manifest capabilities cannot exceed owner grants and narrow effective grants. Native projects first require explicit owner trust, `Lui_LoadProjectNative`, actual captured platform image/identity/registry verification and the exact compiled digest. Native code is privileged; VM quotas do not sandbox it. Assets must be declared and host-registered; network policy must match owner configuration. VM defaults are 16 MiB/10000 interrupts, or manifest limits; stricter owner limits remain. Preflight rejection does not start the application. Execution failure retires the VM and empties the tree; destroy its handle.

Entry/module globals are private. `require` captures its defining file identity and uses only prepared bytes. Exports must be exactly one non-nil value; false is cached. Failed initializers remain failed for that VM, and dynamic cycles poison their participants even when caught. Module initialization rejects effects/yields before admission; return UI/service factory functions and call them after require returns. Pure value constructors are permitted. Initializers share their caller's execution quota.

Bootstrap buffers native presentation and scheduled tasks until entry succeeds, then commits native creation/properties/layout. Entry/commit failure destroys attempted native objects and discards VM/cache/callbacks/work. External host effects/prints cannot be rolled back. On project shutdown, old Destroying callbacks are suppressed. Reload requires destruction of the old runtime before preparation of the replacement; do not keep the old VM alive behind a preparation error. A new VM owns fresh exports, references and IDs. `Lui_RunScript` cannot mix unrelated chunks into a configured project.

Global require returns `unknown` in the legacy checker. The [project checker](PROJECT-TYPECHECK.md) infers literal export/type-alias types through the pinned analyzer/shared resolver. Without a configured project, require raises `ProjectNotConfigured`. Managed `PreparedProject` supplies the v2 WinUI/preview adapter; editor protocols carry native identities and projected payloads.

## Qualification

`LuiCompiledProjectTests` covers C and C++ consumers, immutable inputs, all-source compilation, deterministic fingerprints, bytecode limits/cancellation, malformed host inputs, bounded errors, borrowed lifetime, and concurrent retained queries. `LuiProjectTests` retains the source/filesystem/lock corpus. `LuiModuleTests` covers execution/cache/binding, guards, literal/dynamic cycles, quotas, owner policy and generation cleanup. All three are mandatory Windows/Linux CI targets. See [testing](TESTING.md) and [platform limits](PLATFORMS.md).
