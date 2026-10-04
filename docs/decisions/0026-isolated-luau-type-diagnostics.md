# 0026 — Isolated Luau type diagnostics

## Decision

Foundation 3 adds `LuiTypeCheck`, a separate executable linked to the same pinned Luau analyzer that validates generated definitions. It loads reflection-generated `LUI.d.luau`, reads one UTF-8 entry script from stdin, and performs static analysis without creating a VM or executing application code. The CLI validates the shared application manifest and source before launching it. The editor launches the CLI; neither application code nor the analyzer runs inside the extension host.

`lui check` now checks types by default. `--mode syntax --runtime <library>` explicitly retains the previous compiler-only check. Native launch and package staging retain their existing syntax check. No fallback reports type success when tooling or definitions are missing.

The version 1 result has `version`, manifest-relative `source` (CLI only), `diagnostics`, and `truncated`. Each diagnostic has `kind` (`type` or `syntax`), `message`, and `range.start`/`range.end`, with zero-based lines and UTF-16 columns and an exclusive end. Conversion happens at the native tooling boundary because Luau supplies UTF-8 byte columns. Both CLI and editor validate result bounds and ranges. Text CLI errors use one-based positions. Success is exit 0; source diagnostics are exit 1. CLI setup/process failures also return 1 with stderr and no JSON result; the native executable returns 2 for operational failures.

Source and definitions are each limited to 8 MiB. The analyzer reports at most 256 diagnostics, each at most 2048 UTF-8 bytes, with explicit truncation. Luau module analysis has a 10 second deadline; the CLI bounds the entire child transaction to 30 seconds. The editor allows 35 seconds, limits stdout to 4 MiB and stderr to 64 KiB, and cancels the CLI process tree when results become obsolete. These are input, time, and output limits, not an OS memory sandbox. Analyzer Windows failures suppress OS error dialogs, and clients hide console windows.

VS Code diagnostics use a separate collection from preview runtime errors. **LUI: Check Types** works without starting preview. Automatic checks occur on open and save of the manifest entry script, require workspace trust, and can be disabled with `lui.autoCheck`. Edits cancel pending analysis and clear outdated results; unsaved buffers wait for a save. Earlier jobs cannot publish after cancellation. Tool failures go to the Output channel without modal dialogs.

## Scope and rationale

The runtime currently runs a single manifest entry script. Initial static analysis follows that scope and does not resolve filesystem modules or `.luaurc` files. Strict mode is the default; Luau mode directives remain effective. Generated built-in definitions remain the API source. Extension services registered dynamically are not modeled, and constructor overloads accepting `any` still require an explicit `ClassNameInit` annotation to check fields.

This isolates analyzer crashes and expensive work, reuses manifest validation, and provides useful editor diagnostics without introducing an LSP or changing the native runtime ABI. Autocomplete, unsaved-buffer analysis, project/module graphs, and diagnostic code actions remain future work.

## Verification

CLI tests cover reflected globals, invalid assignments, read-only properties, initializer fields, syntax errors, UTF-16 locations after an emoji, invalid UTF-8/NUL source, missing definitions, and a valid `error(...)` call that must never execute. Node tests cover result validation, process options, stale replies, oversized output, workspace trust, Problems publication, clearing on edit, and recovery through the real CLI and analyzer. Windows and Linux CI run the same tooling integration tests.
