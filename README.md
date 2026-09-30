# LUI

LUI (Luau UI) is a planned native desktop application framework with Roblox-like Luau semantics. Application code creates `Instance` objects, sets properties, connects signals, and uses services. LUI owns layout, lifecycle, scheduling, and input semantics; platform backends render native controls.

**Status:** Foundation 0 is complete: the native runtime and WinUI 3 host build, headless tests pass, and interactive behavior was verified by the user on 2026-09-29. Foundation 1 is in progress. This is not a released package or production SDK.

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

The [generated Luau definitions](types/LUI.d.luau) describe the implemented globals, classes, and services. The Windows build validates them with the pinned Luau type checker.

## Design

- [Full product and architecture specification](docs/SPEC.md)
- [Architecture summary and boundaries](docs/ARCHITECTURE.md)
- [Foundation 0 build and scope](docs/FOUNDATION-0.md)
- [Foundation 1 progress and remaining work](docs/FOUNDATION-1.md)
- [Implemented API reference](docs/API.md)
- [Public compatibility policy](docs/COMPATIBILITY.md)
- [Roadmap and milestone acceptance criteria](ROADMAP.md)
- [AI and contributor context](AIContext.md)
- [Agent instructions](AGENTS.md)

LUI's public semantics are backend independent. The first backend is WinUI 3 on Windows; GTK4 on Linux follows once the semantics are stable. macOS is a future target. The core uses one authoritative Luau and UI scheduler thread. Native workers marshal completions to that thread. Extensions use a versioned C ABI.

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

Implementation currently lives in `runtime/` (including reflection and input), `ui/layout/`, `native/abi/`, `backends/winui3/`, `tests/`, and `examples/`. Other directories mark planned module boundaries.

## Contributing

Start with [AGENTS.md](AGENTS.md), [AIContext.md](AIContext.md), and the relevant sections of [docs/SPEC.md](docs/SPEC.md). Work against the current milestone in [ROADMAP.md](ROADMAP.md). Record decisions that change public semantics in `docs/decisions/` and update the spec or roadmap in the same change.

## License

No license has been selected. This repository does not currently grant reuse rights beyond those provided by applicable law.
