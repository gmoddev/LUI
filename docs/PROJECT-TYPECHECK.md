# Project-wide Luau checking

Schema v2 projects can now be checked through the shared native resolver and pinned Luau analyzer. This checks captured sources without running the application, module initializers, or native DLLs. Existing schema v1 `check` and `--mode syntax` behavior remains supported.

## Run

Build `LuiTypeCheck` and the developer CLI, then use:

```powershell
dotnet run --project tools/cli/Lui.Cli.csproj -- check ./lui.json --mode project --diagnostics development --checker ./build/windows-x64/Debug/LuiTypeCheck.exe
```

Linux uses `./build/linux-x64/LuiTypeCheck`. `LUI_TYPECHECK` can provide the checker path; definitions default to `LUI.d.luau` beside it. `--definitions` selects another owner-supplied definition file. Add `--format json` for version 2 project diagnostics. Text output uses canonical IDs such as `app:Screens/Home.luau` and one-based positions.

Project mode passes the manifest to native shared preparation. It uses no managed import resolver, discovery, download or runtime execution. `--runtime` is refused. Explicit `--diagnostics development` is mandatory; Restricted manifests are refused. See [project hosting](PROJECT-HOST.md) for policy-enabled launch, preview and packaging.

The native command is:

```text
LuiTypeCheck LUI.d.luau --project lui.json --diagnostics development [--generation 12]
```

It writes one complete JSON transaction. Exit 0 means no diagnostics/truncation, 1 means source/project diagnostics, and 2 means setup/policy/budget failure with generic stderr and no JSON. The CLI returns 1 for either diagnostics or setup failure.

## Import typing

- Direct literal builtin imports use the declared module's inferred table/function/scalar exports and exported type aliases.
- Relative/alias/explicit-extension spellings resolve through the same owner identities as runtime loading.
- Dynamic or aliased require calls return `unknown`; refine/assert before use. A constant string stored in a variable remains dynamic in this initial profile.
- Lexical local/parameter shadows use ordinary Luau inference. Sources writing global require use runtime resolution and receive no builtin literal inference.
- Every declared source is checked, including unused modules and deferred literal imports. Known literal cycles fail project validation.
- Known zero/multiple/nil/nullable module results are reported. Unknown/variadic returns retain runtime validation.

Analysis does not prove initializer purity or discover dynamic native service types. Host/native types come from validated declaration metadata; verified native execution is a separate owner-runtime operation. User type-function bodies and nocheck inputs are refused; other Luau modes remain. Constructor tables need generated ClassNameInit annotations for precise field checking.

## Version 2 transaction

[Native sidecar schema 2](PROJECT-NATIVE-BINDINGS.md) supplies primitive native service types alongside Host schema 1. Preparation captures/hashes listed platform binaries without Query/Init/loading. Restricted checking remains refused; no application or DLL executes in analysis.

[Owner-host sidecars](PROJECT-DECLARATIONS.md) add methods/signals to reflected App/GetService definitions from captured primitive metadata, without executing callbacks/DLLs. DefinitionsDigest hashes merged definitions; no-sidecar transactions preserve earlier fingerprints. Native providers use the same validated metadata pipeline.

Top-level fields are `version`, `generation`, `profile`, `digest`, `sourceDigest`, `definitionsDigest`, `entry`, `diagnosticsMode`, `redacted`, `truncated`, `sources`, and `diagnostics`.

`sources` is sorted by canonical identity and contains `id`, staged relative `path`, SHA-256 `hash`, captured `bytes`, and `lines`. No absolute project root is emitted. Each diagnostic has `source`, `kind`, `code`, `message`, and `range`. Kinds are syntax, type, import, module, or project. Source ranges use zero-based UTF-16 columns and exclusive ends; project-level cycle diagnostics have null source/range.

The analysis fingerprint hashes the immutable source digest, exact definitions hash, Luau revision and ProjectTypesV1 profile. Edits create a new transaction; files changed after capture cannot alter that transaction. The analysis fingerprint is not a compiled bytecode/package digest. Generation is a safe nonnegative JSON integer, default 0 for the CLI; editor adapters must validate generation/digest/source mapping before navigation when implemented.

CLI validates protocol shape, duplicates, hashes, IDs, portable paths, aggregate bounds, ranges, flags and exit status. Native analysis computes ranges from captured bytes. Diagnostic messages use safe templates for syntax/type errors; they do not pretty-print literals or user type payloads. Development source IDs/maps remain explicit (`redacted:false`); Restricted checking is refused. The v2 editor consumes prepared mappings for multi-source Problems and source navigation, invalidates old work on project edits, and checks saved declared files on open/save.

## Bounds and qualification

Shared preparation enforces 256 declared files, 8 MiB per source, 32 MiB aggregate source, locked dependencies and opened-file/path/case checks. Definitions are capped at 8 MiB. Output admits 256 diagnostics, 2048 UTF-8 bytes per message and 1 MiB serialized JSON. Analysis has a 10-second per-module budget and 60-second native cancellation/process deadline; the CLI terminates an overdue child. Parser/analyzer transient memory is not process-wide capped.

`LuiProjectTypeTests` covers literal/alias/table/factory/type-alias inference, wrong imported types, dynamic unknown/refinement, shadowing, unused errors, syntax/return/cycle failures, frozen bytes, UTF-16 ranges, policy/type-function/nocheck refusal, budgets and truncation. CLI tests cover real version 2 transactions, a Unicode host path, protocol rejection and legacy coexistence. See [testing](TESTING.md) and [platform limits](PLATFORMS.md).
