# Public compatibility policy

LUI is currently experimental at version `0.0.1`. The implemented API is listed in [API.md](API.md); the broader [specification](SPEC.md) is a design target. No stable public package is published. The version 1 extension ABI exists for Foundation 2 development but is still experimental until a stable release.

## Semantic contract

- Runtime reflection is the source of truth for implemented classes, properties, methods, and signals. Generated Luau types and API docs come from that table.
- A backend must preserve property defaults, validation, lifecycle, signal order, and resolved layout behavior. A backend change that alters those semantics requires a shared runtime decision and conformance tests.
- An API change updates reflection, generated files, tests, and the public docs in one change. Public property removal or a changed meaning requires a documented migration path before a stable release.
- `InputBegan`, `InputChanged`, and `InputEnded` now accept keyboard as well as pointer events. Applications that read pointer-only fields must narrow on `Event.Device ~= "Keyboard"`. Pointer `InputEnded` adds `IsCanceled` so release and cancellation are distinguishable.
- Platform capabilities are queried through `PlatformService:Supports()`. A missing capability returns `false`; portable APIs may not silently depend on a platform-specific feature.

## Versions

- The CMake project version tracks the experimental application framework. Until a stable release, incompatible changes are permitted but must be recorded in the roadmap or a decision document.
- `schemaVersion` in `types/schema.json` versions the generated metadata format. It is separate from the LUI framework version and must change when metadata consumers need a new parser.
- Schema version 2 replaces name-only method and signal lists with typed entries and adds parent and container metadata. Consumers of version 1 must update their parser before using version 2.
- `native/abi/LuiRuntime.h` is the current internal host callback boundary. Its functions are not the separate extension ABI in `LuiExtension.h`.
- `native/abi/LuiExtension.h` is the separate version 1 C extension contract. The loader requires an exact ABI version and checks structure sizes; an incompatible library is rejected before initialization. Extension-specific services appear in runtime reflection, not the static built-in type/schema files.

Foundation 1 passed its shared semantic tests and Windows desktop qualification on 2026-10-01. Future backend implementations must pass the same semantic expectations before claiming support.
