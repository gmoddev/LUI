# LUI

LUI (Luau UI) is a planned native desktop application framework with Roblox-like Luau semantics. Application code creates `Instance` objects, sets properties, connects signals, and uses services. LUI owns layout, lifecycle, scheduling, and input semantics; platform backends render native controls.

**Status:** Foundations 0 and 1 and the defined Windows exit criteria for Foundation 2 are complete. Foundation 2 adds a C++ host facade, native extension methods and signals, capability-gated Windows services, VM sandbox limits, packaged assets, and a self-contained Windows folder verified on the desktop at 100% DPI. This is not a signed release or stable SDK.

```lua
local Window = Instance.new("Window", {
    Title = "Hello",
})

local Button = Instance.new("TextButton", {
    Text = "Click",
    Parent = Window,
})

Button.Activated:Connect(function()
    print("[LUI:Example] Hello")
end)

Window.Visible = true
```

This syntax is implemented in the Foundation 0 proof. See [build and run instructions](docs/FOUNDATION-0.md) and the [working example](examples/hello.luau).

The WinUI host supports `--diagnostics` for live traces and a saved crash/error log. Foundation 1 adds native controls, canonical layout, focus, pointer and keyboard input, accessibility metadata, and reflection. See [Foundation 1 run instructions](docs/FOUNDATION-1.md#verify) and the [WinUI desktop qualification cases](tests/winui/README.md).

Foundation 2 uses a version 1 primitive C extension ABI and a host-declared capability manifest. The [native service example](examples/native-service.luau) calls a C DLL through `app:GetService`. The [Foundation 2 guide](docs/FOUNDATION-2.md) covers sandbox limits, clipboard, dialogs, themes, assets, a self-contained Windows release folder, and its exit audit.

The [generated Luau definitions](types/LUI.d.luau) describe the implemented globals, classes, and services. The Windows build validates them with the pinned Luau type checker.

The [launcher dashboard demo](examples/dashboard/README.md) is an interactive Windows sample with discovery, search, project details, simulated installation, and session-only settings. It uses fictional data to exercise the current native controls.

Foundation 3 includes the [developer CLI and isolated preview host](docs/FOUNDATION-3.md): scaffold a project, check its manifest and Luau syntax without running it, launch the Windows host, stage a package, and stream a versioned preview tree from a separate process. The VS Code editor tools remain planned.

## Design

- [Full product and architecture specification](docs/SPEC.md)
- [Architecture summary and boundaries](docs/ARCHITECTURE.md)
- [Foundation 0 build and scope](docs/FOUNDATION-0.md)
- [Foundation 1 canonical baseline](docs/FOUNDATION-1.md)
- [Foundation 2 features and qualification](docs/FOUNDATION-2.md)
- [Foundation 3 CLI and project manifest](docs/FOUNDATION-3.md)
- [Implemented API reference](docs/API.md)
- [Public compatibility policy](docs/COMPATIBILITY.md)
- [Roadmap and milestone acceptance criteria](ROADMAP.md)
- [AI and contributor context](AIContext.md)
- [Agent instructions](AGENTS.md)

LUI's public semantics are backend independent. The first backend is WinUI 3 on Windows; GTK4 on Linux is planned later. macOS is a future target. The core uses one authoritative Luau and UI scheduler thread. Native workers can queue completions for that thread through the versioned C extension ABI.

## Repository layout

| Path | Planned responsibility |
| --- | --- |
| `runtime/` | Luau VM, scheduler, Instances, signals, reflection, lifecycle, services |
| `ui/` | Layout, input, focus, style, theme, accessibility |
| `backends/` | WinUI 3, GTK4, and preview adapters |
| `native/` | Stable C ABI, C++ host API, extensions |
| `tools/` | CLI, preview host, packager, generators |
| `vscode/` | Editor extension |
| `types/` | Generated Luau definitions |
| `tests/` | Runtime, layout, conformance, integration tests |
| `docs/` | Specification and architecture records |

Implementation currently lives in `runtime/` (including reflection, input, extensions, and platform services), `ui/layout/`, `native/abi/`, `native/host/`, `backends/winui3/`, `tools/packaging/`, `tests/`, and `examples/`. Other directories mark planned module boundaries.

## Contributing

Start with [AGENTS.md](AGENTS.md), [AIContext.md](AIContext.md), and the relevant sections of [docs/SPEC.md](docs/SPEC.md). Work against the current milestone in [ROADMAP.md](ROADMAP.md). Record decisions that change public semantics in `docs/decisions/` and update the spec or roadmap in the same change.

## License

No license has been selected. This repository does not currently grant reuse rights beyond those provided by applicable law.
