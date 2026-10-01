# AI context for LUI

Read this before making changes. **Foundations 0 and 1 are complete**: the native Luau runtime, Instance model, WinUI host, portable layout/control/input semantics, and headless conformance tests build. The user verified Foundation 0 interactive behavior on 2026-09-29; the native Foundation 1 mapping and input matrix passed on Windows on 2026-10-01. Foundation 2 is in progress with a versioned experimental extension ABI, host capability grant, and WinUI manifest. No CLI, production package, or other platform backend exists. The [full specification](docs/SPEC.md) states the intended design; [ROADMAP.md](ROADMAP.md) gives milestone order and acceptance criteria.

## Project intent

Build a native desktop framework where ordinary Luau application code uses Roblox-like Instances, properties, signals, services, and `task` scheduling. LUI defines portable semantics and uses native platform controls. WinUI 3 is first, GTK4 follows, and macOS remains a future consideration.

## Nonnegotiable design constraints

- Keep normal public APIs independent of WinUI, Win32, GTK, native pointers, and platform event loops.
- Keep the Luau VM and UI object tree on one authoritative scheduler thread; marshal native worker results back to it.
- Make LUI's layout and lifecycle semantics authoritative across backends.
- Keep native extension compatibility at a versioned C ABI with explicit ownership and capabilities.
- Treat accessibility, input/focus, reflection, and conformance as core design work.
- Keep editor preview execution outside the VS Code extension host and treat protocol input as untrusted.
- Do not add arbitrary FFI to ordinary Luau code.

## Working method

1. Read [AGENTS.md](AGENTS.md) and the relevant spec sections before editing.
2. Choose the smallest coherent part of the current roadmap foundation; do not imply unimplemented APIs are available.
3. Define semantics and tests at the runtime boundary before coupling an API to a backend.
4. Update documentation when changing public behavior. Capture settled decisions in `docs/decisions/`.
5. State exactly what was built and verified, including platform and tooling limitations.

Foundation 0's internal C host callbacks are in `native/abi/LuiRuntime.h`. They are not the stable extension ABI described for Foundation 2. The runtime reflection table in `runtime/reflection/Schema.cpp` gates implemented classes, properties, methods, signals, and parenting, and generates `types/LUI.d.luau`, `types/schema.json`, and `docs/API.md`. See [Foundation 1 progress](docs/FOUNDATION-1.md) for current scope and remaining qualification.

Foundation 2's experimental extension contract is [native/abi/LuiExtension.h](native/abi/LuiExtension.h). `LuiRuntime.h` remains the internal host interface and now exposes host-only capability declaration, extension loading, and runtime-specific extension reflection. Extension service methods are registered before the first script and do not appear in the static built-in type file. See [Foundation 2 notes](docs/FOUNDATION-2.md).

The Windows build checks the generated definitions with the pinned Luau type checker. Direct constructor property tables currently use `any` in overloads; annotate a table with a generated `ClassNameInit` type to statically check its fields.

The examples in the original spec are design illustrations. New code should follow the repository naming and diagnostic conventions in [AGENTS.md](AGENTS.md).
