# AI context for LUI

Read this before making changes. **Foundations 0, 1, and the defined Windows exit criteria for 2 are complete; Foundation 3 is in progress**. The native Luau runtime, Instance model, WinUI host, portable layout/control/input semantics, and headless conformance tests build. Foundation 0 interactive behavior was verified on 2026-09-29; Foundation 1 mapping and input passed on Windows on 2026-10-01. Foundation 2 adds a versioned primitive extension ABI, C++ host bindings, sandbox VM quotas, platform services, assets, and a self-contained Windows folder. Its published desktop qualification passed on 2026-10-01 at DPI scale 1. The CLI, isolated preview host, and VS Code development extension exist; no other platform backend exists. The [full specification](docs/SPEC.md) states the intended design; [ROADMAP.md](ROADMAP.md) gives milestone order and acceptance criteria.

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

Foundation 2's supported primitive extension contract is [native/abi/LuiExtension.h](native/abi/LuiExtension.h). `LuiRuntime.h` remains the internal host interface and exposes host-only capability declarations, sandbox limits, extension loading, platform callbacks, and runtime-specific extension reflection. The [C++ facade](native/host/Application.hpp) binds primitive methods and signals. Extension services are registered before the first script and do not appear in the static built-in type file. Opaque objects and raw platform handles need a later ABI; high-contrast, multi-DPI, and assistive-technology qualification remain follow-up. See [Foundation 2 notes](docs/FOUNDATION-2.md).

The Windows build checks the generated definitions with the pinned Luau type checker. Direct constructor property tables currently use `any` in overloads; annotate a table with a generated `ClassNameInit` type to statically check its fields.

The examples in the original spec are design illustrations. New code should follow the repository naming and diagnostic conventions in [AGENTS.md](AGENTS.md).

Foundation 3's CLI is in [tools/cli](tools/cli). It reuses the version 1 JSON host manifest for project scaffolding, syntax checking, Windows launching, packaging, and preview launching. `Lui_CheckScript` compiles without executing Luau. [tools/preview-host](tools/preview-host) runs sandboxed application Luau in a separate process and streams the authoritative runtime tree, layout, reflection schema, Luau source provenance, and source-linked runtime errors over a version 1 JSON-lines protocol. Reload destroys the old runtime and starts a new generation. [vscode](vscode) provides an Instance Explorer, read-only layout/property inspector, source navigation, runtime diagnostics, and an explicit Windows native preview launcher; application Luau never runs in the extension host. The user visually confirmed the initial editor view on 2026-10-01. The CLI does not yet typecheck application source. See [Foundation 3 notes](docs/FOUNDATION-3.md).

Networking Foundation A is the completed Foundation 2 TCP follow-on, qualified by [Windows and Linux CI](https://github.com/gmoddev/LUI/actions/runs/37174780149) on 2026-10-03. It includes scheduler-owned yielding tasks, a pinned Asio worker, capability-gated `NetworkService`, paired dual-family listeners, host address/port policy, paced outbound family dialing, connection `Closed` signals, and headless transport tests. [Foundation A notes](docs/NETWORKING-FOUNDATION-A.md) state shipped semantics and limits. [SPEC §53](docs/SPEC.md#53-networking-and-hosted-endpoints-planned), [decisions 0022–0025](docs/decisions/0025-paced-dialing-and-closed-signal.md), and the broader [architecture proposal](docs/proposals/NETWORKING-ARCHITECTURE.md) separate implementation from planned APIs. `HttpService`, `HttpServerService`, UDP, and TLS are not implemented. Foundation 3 type diagnostics are the next roadmap step.
