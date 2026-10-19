# LUI

LUI is a native application framework for Luau. Build interfaces with Instances, properties, signals and services; LUI owns layout, scheduling and lifecycle while WinUI 3 and GTK4 render native controls.

**Experimental, version 0.0.1.** Windows x64 and a selected Ubuntu GTK/X11 profile have been tested. LUI is not a stable SDK or a signed release. See [platform support and limitations](docs/PLATFORMS.md).

```lua
local Window = Instance.new("Window", { Title = "Hello" })
local Button = Instance.new("TextButton", {
    Text = "Click",
    Parent = Window,
})
Button.Activated:Connect(function()
    print("[LUI:Example] Hello")
end)
Window.Visible = true
```

## Get started

1. Follow the [build guide](docs/BUILD.md).
2. Run [hello.luau](examples/hello.luau) or the [modular application](examples/modular/README.md).
3. Explore the [launcher dashboard](examples/dashboard/README.md) and the [10,000-item conversation](examples/conversation/README.md).

## Features

- Roblox-like Luau Instances, signals, services and generated types.
- Logical layout, clipping, scrolling, stacking, styles and system themes.
- Native text editing, bounded Unicode selection/composition and password controls.
- Owned reusable state, scopes and virtual collections; asynchronous declared images.
- Declared modules, project-wide checking, native preview, VS Code tooling and application folders.
- Capability-gated native integration, clipboard/dialogs and bounded TCP/UDP/HTTP/TLS services.

Capability availability depends on host grants and platform support. Tests and examples use synthetic data. Application execution never downloads undeclared modules or image assets.

## Documentation

- [Implemented API](docs/API.md) and [Luau types](types/LUI.d.luau)
- [Architecture](docs/ARCHITECTURE.md), [invariants](docs/INVARIANTS.md) and [compatibility](docs/COMPATIBILITY.md)
- [Application contract](docs/SPEC.md) and [project host](docs/PROJECT-HOST.md)
- [Testing](docs/TESTING.md), [performance](docs/PERFORMANCE.md) and [platform support](docs/PLATFORMS.md)
- [Product roadmap](ROADMAP.md) and [contributing](CONTRIBUTING.md)

## Repository layout

| Path | Responsibility |
| --- | --- |
| `runtime/`, `ui/` | VM, scheduling, Instances, services, input, layout and styles |
| `backends/` | Native platform hosts and preview adapters |
| `native/` | C interfaces and C++ host facade |
| `tools/`, `vscode/` | CLI, checking, packaging and editor tools |
| `types/`, `docs/` | Generated API definitions and technical guides |
| `tests/`, `examples/` | Conformance, integration and synthetic applications |

## License

LUI source and documentation use the [MIT License](LICENSE). Dependencies retain their own licenses and notices; see [third-party notices](third_party/licenses) and the notices distributed with each dependency.
