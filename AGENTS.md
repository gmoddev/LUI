# Agent instructions

These instructions apply throughout this repository. Read [AIContext.md](AIContext.md), [ROADMAP.md](ROADMAP.md), and relevant sections of [docs/SPEC.md](docs/SPEC.md) before implementation.

## Naming and diagnostics

- Use **PascalCase**, not camelCase, by default for identifiers you introduce. Preserve required external names and ABI spellings when compatibility demands them.
- The explicit exception is `self = setmetatable({}, ...)`.
- Name a function that returns an existing object or creates it if missing `GetObject()` (or a specific form such as `GetFolder()`), not `GetOrCreateObject()`.
- Prefix prints and logs with `[System:SubSystem]`, for example `[LUI:Scheduler]`.
- Handle expected failures without modal dialogs or foreground errors that could interrupt a user's game. Prefer structured logs and nonintrusive diagnostics; only show application UI when the application contract explicitly calls for it.

## Architecture

- Public Luau semantics belong to LUI, not to a native backend.
- Keep backend handles and toolkit objects private except in explicit platform interop modules.
- Perform Luau callbacks and Instance mutation on the authoritative scheduler thread.
- Make object destruction safe and idempotent. Disconnect callbacks for dead objects and prevent native callbacks from referencing them.
- Keep layout in logical units and test it without a physical display.
- Register public APIs in reflection metadata so types, docs, and inspector data can derive from one source.
- Version any stable ABI or editor protocol and validate input at boundaries.

## Change discipline

- Follow the milestone order in [ROADMAP.md](ROADMAP.md) unless a task explicitly changes priorities.
- Add tests for semantics or backend behavior when implementing them. Do not claim cross-platform support from Windows-only verification.
- Keep the full spec as the design source; revise it when the intended contract changes and document the rationale in `docs/decisions/`.
- Avoid silently turning a proposed example into a promise of implemented functionality.
