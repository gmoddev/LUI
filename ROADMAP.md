# Roadmap

The foundations below follow the [LUI specification](docs/SPEC.md#52-short-roadmap). They describe planned work, not completed features. Keep the sequence broad; track concrete implementation tasks in issues when development begins.

## Foundation 0 — Runtime and Windows proof

Build the embedded Luau VM, one UI scheduler, `Instance`, properties, `Signal`/`Connection`, parenting, basic lifecycle, `UDim`/`UDim2`, simple layout, and a minimal WinUI 3 backend for `Window`, `Frame`, `TextLabel`, and `TextButton`.

**Exit criteria**

- The specification's [hello-window example](docs/SPEC.md#foundation-0--runtime-and-windows-proof) runs as a native Windows app without application-side message-loop or backend code.
- `Activated` reaches Luau through the scheduler; callbacks cannot outlive destroyed Instances.
- A headless test covers object identity, parenting, signal disconnect, destruction, and core layout calculations.
- Build instructions pin the Luau and Windows SDK dependencies needed to reproduce the proof.

## Foundation 1 — Canonical UI semantics

Define and test property defaults and validation, lifecycle, layout, input, focus, common controls, basic services, reflection, generated Luau types, and a backend conformance suite. This is the semantic baseline for other backends.

**Exit criteria:** the runtime metadata, generated types, documentation, and conformance expectations agree; layout behavior is testable without a display; public behavior has explicit compatibility rules.

## Foundation 2 — Native integration and production Windows

Add the C++ host API, versioned C extension ABI, capability declaration, worker completion marshaling, dialogs, clipboard, assets, accessibility and DPI qualification, Windows packaging, and deliberate platform interop.

**Exit criteria:** a standalone Windows application can bind a native service, ship with only declared capabilities, handle extension load failure safely, and pass the Windows conformance suite.

## Foundation 3 — Developer experience

Add the CLI, project manifest, preview host and versioned protocol, VS Code Explorer and inspector, source provenance, native preview, and generation-based hot reload.

**Exit criteria:** preview runs outside the VS Code extension process, uses the same object and layout engine, and cleanly discards callbacks and native objects from old reload generations.

## Foundation 4 — Linux backend

Implement GTK4 against the existing backend contract.

**Exit criteria:** Linux passes the shared semantic conformance suite; portable applications do not require GTK-specific code or changed LUI semantics.

## Later

Evaluate macOS, optional direct Win32, richer declarative libraries, visual editing, tray and notification APIs, drag and drop, advanced text, custom surfaces, WebView, terminal, graphics extensions, and AOT execution after the earlier foundations are stable.

## Current next step

Begin Foundation 0 with a small buildable runtime skeleton and a written backend contract. Decide and pin the initial Luau revision, C++ toolchain, Windows App SDK version, dependency acquisition method, and supported Windows baseline as part of that implementation. Do not claim a build or platform guarantee until it is verified.
