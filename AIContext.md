# AI context for LUI

Read this before making changes. **Foundation 0 is complete**: the native Luau runtime, small Instance model, WinUI host, and headless tests build, and the user verified interactive behavior on 2026-09-29. Foundation 1 is in progress. No CLI, production package, or other platform backend exists. The [full specification](docs/SPEC.md) states the intended design; [ROADMAP.md](ROADMAP.md) gives milestone order and acceptance criteria.

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

Foundation 0's internal C host callbacks are in `native/abi/LuiRuntime.h`. They are not the stable extension ABI described for Foundation 2. The runtime reflection table in `runtime/reflection/Schema.cpp` gates the implemented classes and properties and generates `types/LUI.d.luau`, `types/schema.json`, and `docs/API.md`. See [Foundation 1 progress](docs/FOUNDATION-1.md) for current scope and remaining qualification.

The examples in the original spec are design illustrations. New code should follow the repository naming and diagnostic conventions in [AGENTS.md](AGENTS.md).
