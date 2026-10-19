# Contributing

Read the [architecture](docs/ARCHITECTURE.md), [invariants](docs/INVARIANTS.md), [compatibility policy](docs/COMPATIBILITY.md) and relevant API/project guides before changing behavior.

- Introduce PascalCase identifiers; preserve required external names and ABI spellings. Use `GetObject()`, rather than `GetOrCreateObject()`.
- Prefix diagnostics with `[LUI:Subsystem]`. Expected failures use structured, nonmodal diagnostics.
- Keep public semantics in the shared runtime. Native toolkit objects remain private.
- Run Instance mutations and Luau callbacks on the authoritative scheduler thread.
- Make destruction idempotent and prevent callbacks into dead objects or retired generations.
- Register public APIs in reflection; regenerate types/schema/API documentation together.
- Add focused tests for changed semantics and native mappings. State the platforms actually tested.
- Preserve copyright/license notices, dependency pins and useful technical comments.

Use the [build](docs/BUILD.md) and [testing](docs/TESTING.md) guides. Keep binaries, credentials, personal data and local logs out of contributions. Use synthetic fixtures. An API change should update its tests, public documentation and compatibility guidance together.

Public pull requests are reviewed and integrated into the maintained source before publication. Keep changes focused and include reproduction steps, expected behavior and verification evidence.
