# Roadmap

The foundations below follow the [LUI specification](docs/SPEC.md#52-short-roadmap). They describe planned work, not completed features. Keep the sequence broad; track concrete implementation tasks in issues when development begins.

## Foundation 0 — Runtime and Windows proof

**Status:** complete. Native runtime and WinUI host build; headless semantics pass; the user verified interactive GUI behavior on 2026-09-29. See [Foundation 0 notes](docs/FOUNDATION-0.md).

Build the embedded Luau VM, one UI scheduler, `Instance`, properties, `Signal`/`Connection`, parenting, basic lifecycle, `UDim`/`UDim2`, simple layout, and a minimal WinUI 3 backend for `Window`, `Frame`, `TextLabel`, and `TextButton`.

**Exit criteria**

- The specification's [hello-window example](docs/SPEC.md#foundation-0--runtime-and-windows-proof) runs as a native Windows app without application-side message-loop or backend code.
- `Activated` reaches Luau through the scheduler; callbacks cannot outlive destroyed Instances.
- A headless test covers object identity, parenting, signal disconnect, destruction, and core layout calculations.
- Build instructions pin the Luau and Windows SDK dependencies needed to reproduce the proof.

## Foundation 1 — Canonical UI semantics

**Status:** complete. Structured reflection, generated and typechecked Luau definitions, API docs, derived geometry, padding/list/grid layout, size constraints, common controls, deterministic focus transitions, hover and pointer input signals, basic services, backend failure reporting, VM reentrancy protection, and headless conformance tests are implemented. The [Foundation 1 exit audit](docs/FOUNDATION-1.md#exit-audit) records the passing Windows native mapping and input qualification on 2026-10-01.

Define and test property defaults and validation, lifecycle, layout, input, focus, common controls, basic services, reflection, generated Luau types, and a backend conformance suite. This is the semantic baseline for other backends.

**Exit criteria:** the runtime metadata, generated types, documentation, and conformance expectations agree; layout behavior is testable without a display; public behavior has explicit compatibility rules.

## Foundation 2 — Native integration and production Windows

**Status:** complete for the defined Windows exit criteria. The [Foundation 2 exit audit](docs/FOUNDATION-2.md#exit-audit) records the version 1 primitive C extension ABI, C++ host facade, owner-thread signals, VM sandbox quotas, capability-gated Windows services, themes, assets, self-contained folder, and passing desktop qualification on 2026-10-01. Networking Foundation A is an added Foundation 2 follow-on, not part of that completed audit. Opaque native objects and raw window handles require a later ABI design; broader DPI, high-contrast, and assistive-technology checks remain qualification follow-up.

Add the C++ host API, versioned C extension ABI, capability declaration, worker completion marshaling, dialogs, clipboard, assets, accessibility and DPI qualification, Windows packaging, and deliberate platform interop.

Include basic system theming (light, dark, high contrast, and change notifications) and capability enforcement for trusted and sandboxed applications, including execution and memory limits and restrictions on services, filesystem access, and native extensions.

**Exit criteria:** a standalone Windows application can bind a native service, ship with only declared capabilities, handle extension load failure safely, and pass the Windows conformance suite.

### Networking Foundation A — async runtime and TCP (planned Foundation 2 follow-on)

Define a shared owner-thread async completion and coroutine-resumption contract, then implement runtime-owned TCP listening, connecting, accept, read, write, shutdown, cancellation, and bounded backpressure. Use an internal, pinned transport library after dependency review. The public Luau service must not expose native socket handles or depend on a UI backend. Add `network.client`, `network.server`, and `network.raw` grants before enabling access. Qualify the same semantics with Windows and Linux headless tests; this does not require a Linux UI backend. See [networking specification](docs/SPEC.md#53-networking-and-hosted-endpoints-planned) and [proposal](docs/proposals/NETWORKING-ARCHITECTURE.md).

### Networking Foundation B — HTTP (planned)

Build outbound `HttpService` and a loopback-default `HttpServerService` on the proven transport. Include strict HTTP/1.1 framing, bounded requests and responses, ordered persistent connections, scheduler-thread route handlers, timeouts, and malformed-request regression tests.

### Networking Foundation C — UDP and TLS (planned)

Add bounded UDP datagrams and a portable TLS provider contract with validated client certificates and explicit server credentials. Qualify shutdown, cancellation, and resource limits on both platforms. WebSocket, HTTP/2, HTTP/3, and streaming bodies remain later work.

## Foundation 3 — Developer experience

**Status:** in progress. The CLI and separate preview host provide versioned tree and layout snapshots with generation-based reload. The VS Code development extension displays an Instance Explorer, a layout map, reflection-backed properties, Luau creation/change locations, source navigation, and source-linked runtime diagnostics. It also launches the real Windows host as a separate native preview. Type diagnostics and richer visual/input tools remain. The user visually confirmed the initial editor view on 2026-10-01. See [Foundation 3 notes](docs/FOUNDATION-3.md).

Add the CLI, project manifest, preview host and versioned protocol, VS Code Explorer and inspector, source provenance, native preview, and generation-based hot reload.

**Exit criteria:** preview runs outside the VS Code extension process, uses the same object and layout engine, and cleanly discards callbacks and native objects from old reload generations.

## Foundation 4 — Linux backend

Implement GTK4 against the existing backend contract.

**Exit criteria:** Linux passes the shared semantic conformance suite; portable applications do not require GTK-specific code or changed LUI semantics.

## Later

Evaluate macOS, optional direct Win32, richer declarative libraries, visual editing, tray and notification APIs, drag and drop, advanced text, custom surfaces, WebView, terminal, graphics extensions, and AOT execution after the earlier foundations are stable.

## Current next step

Begin Networking Foundation A with the shared async operation and scheduler-resumption contract, then the TCP transport and cross-platform headless tests. Continue Foundation 3's Luau type diagnostics after that. Keep the future native object ABI separately versioned.
