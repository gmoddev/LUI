# 0017 — First CLI and shared project manifest

Date: 2026-10-01

## Context

Foundation 2 already has a versioned JSON manifest, validated by the WinUI host and Windows packager. Foundation 3 needs a project entrypoint and repeatable commands before preview or editor work can depend on them. The specification's TOML project example is a future design target.

## Decision

Use the existing version 1 JSON manifest as the first project manifest, conventionally `lui.json`. The CLI, host, and packager compile the same `AppManifest` source, so capability and path rules remain aligned. `lui new` scaffolds a project with no grants. `lui check` validates the manifest and invokes the native Luau compiler without executing the script; it does not claim type checking. `lui run` checks before launching the Windows host. `lui build` checks before staging from an explicitly supplied published WinUI host into an empty output directory.

Expose a host-internal `Lui_CheckScript` function to compile source and return diagnostics without creating Instances or running callbacks. It does not alter the stable extension ABI. The CLI loads the native runtime by explicit path or `LUI_RUNTIME`, and handles expected failures on stderr without application dialogs.

## Consequences

The first CLI requires a native runtime for syntax checks and an existing WinUI host for launch or packaging. `build` stages a folder; it does not compile the native host, sign an installer, or add cross-platform UI support. The TOML project format, full Luau type diagnostics, isolated preview host, editor protocol, inspector, source provenance, and hot reload remain separate Foundation 3 work. A later project manifest format will have a distinct version and migration path.
