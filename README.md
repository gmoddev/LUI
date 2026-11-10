# LUI

LUI is an application framework for Luau. Build interfaces with Instances, properties, signals and services; LUI owns layout, scheduling and lifecycle while WinUI 3, GTK4 and an experimental shared-runtime WASM/DOM host render the interface.

**Experimental, version 0.0.1.** Windows x64, a selected Ubuntu GTK/X11 desktop profile and the selected Chromium/Firefox/Playwright WebKit browser profile have passed their qualification gates. LUI is not a stable SDK or a signed release. See [platform support and limitations](docs/PLATFORMS.md).

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

Visit the **[LUI documentation website](https://gmoddev.github.io/LUI/)** for your
first project, a modular project-library tutorial and the searchable API reference.

Try the [Windows preview download](https://github.com/gmoddev/LUI/releases/tag/sdk-v0.1.0-preview.1)
and follow the [SDK guide](docs/SDK.md) to create your first app with VS Code.
This is an early, unsigned preview; first-time setup on a fresh Windows PC is
still being tested.

To build the framework from source:

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
- Experimental owned native buffers/handles/async services, scoped storage and Windows notifications/tray.

Capability availability depends on host grants and platform support. Tests and examples use synthetic data. Application execution never downloads undeclared modules or image assets.

## Documentation

Suggestions and bug reports are welcome. We are not accepting pull requests or
code contributions for now because the project is changing quickly. See the
[contribution guide](CONTRIBUTING.md) for details.

- [Implemented API](docs/API.md) and [Luau types](types/LUI.d.luau)
- [Architecture](docs/ARCHITECTURE.md), [invariants](docs/INVARIANTS.md) and [compatibility](docs/COMPATIBILITY.md)
- [Application contract](docs/SPEC.md) and [project host](docs/PROJECT-HOST.md)
- [Owned native services](docs/NATIVE-SERVICES.md) and [native integration example](examples/native-integration/README.md)
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
