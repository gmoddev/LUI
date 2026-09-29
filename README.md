# LUI

LUI (Luau UI) is a planned native desktop application framework with Roblox-like Luau semantics. Application code creates `Instance` objects, sets properties, connects signals, and uses services. LUI owns layout, lifecycle, scheduling, and input semantics; platform backends render native controls.

**Status:** repository foundation and design documentation. There is no runnable framework, build, or released package yet. The first implementation target is a small Windows proof using embedded Luau and WinUI 3.

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

This is target API syntax from the design, not an executable example today.

## Design

- [Full product and architecture specification](docs/SPEC.md)
- [Architecture summary and boundaries](docs/ARCHITECTURE.md)
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

These directories mark intended module boundaries. They contain no implementation yet.

## Contributing

Start with [AGENTS.md](AGENTS.md), [AIContext.md](AIContext.md), and the relevant sections of [docs/SPEC.md](docs/SPEC.md). Work against the current milestone in [ROADMAP.md](ROADMAP.md). Record decisions that change public semantics in `docs/decisions/` and update the spec or roadmap in the same change.

## License

No license has been selected. This repository does not currently grant reuse rights beyond those provided by applicable law.
