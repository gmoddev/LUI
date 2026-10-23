# Schema v2 content staging

`LuiProjectTool` captures and stages declared application/dependency sources, assets, native binaries and metadata without executing Luau or loading a DLL. The CLI forwards to this shared component. `build --mode project` additionally composes a Windows platform host with captured content under `Application/` and `Start-LUI.cmd`; [the modular sample](../examples/modular/README.md) documents self-contained publishing.

## Commands

```powershell
dotnet tools/cli/bin/Debug/net10.0/lui.dll stage app/lui.json --output build/app-content --project-tool build/windows-x64/Debug/LuiProjectTool.exe
dotnet tools/cli/bin/Debug/net10.0/lui.dll verify build/app-content --project-tool build/windows-x64/Debug/LuiProjectTool.exe
```

The output directory must not exist, and its parent must exist. On Linux use the native `LuiProjectTool` executable. Set `LUI_PROJECT_TOOL` or place the tool beside `lui.dll` to omit `--project-tool`. The native equivalents are `LuiProjectTool --stage <manifest> <new directory>` and `LuiProjectTool --verify <staged directory>`.

Success prints a JSON summary with SchemaVersion/Profile, PackageDigest, SourceDigest, CompiledDigest, SourceCount and AssetCount: version 1 / DeclaredContentV1 without native content, version 2 / DeclaredContentV2 with native bindings. Failure is nonmodal with a generic structured code. Each CLI operation has a 60-second child-process limit and bounded output. `stage` compiles all declared sources; it does not perform type analysis. Run `check --mode project --diagnostics development` separately for types. Existing schema v1 `build --host ...` continues its qualified runnable Windows folder scope.

## Contents and validation

[Native provider bindings](PROJECT-NATIVE-BINDINGS.md) capture/stage every pinned WindowsX64/LinuxX64 binary without loading code, including foreign targets. Native packages use schema 2 / DeclaredContentV2 with NativeBindings; other packages retain schema 1. CLI summaries carry matching profiles. Policy-enabled startup verifies selected actual loaded providers separately.

The [owner-host declaration profile](PROJECT-DECLARATIONS.md) captures/stages structured JSON sidecars with exact hashes. Byte edits fail verification even when canonical registration types remain equivalent. Native binary staging never executes code; actual provider loading belongs to the owner runtime.

- Every declared source/dependency is staged, including unused modules. Unlisted files are omitted.
- Declared binary and empty assets preserve exact bytes. Sources use their existing strict UTF-8 rules.
- `lui.json` and the declared lock are canonicalized; sources/assets retain exact bytes.
- `lui.project-index.json` records sorted identities, staged relative paths, SHA-256 hashes/lengths, source/compiled fingerprints, compiler pin and dependency provenance. PackageDigest hashes its exact bytes.
- The writer refuses links and existing leaves, rechecks written hashes, and writes the index last. A failed operation may leave an incomplete directory for inspection; it never deletes it recursively.
- Verification checks the index against newly captured inputs using the shared resolver/compiler. Modified/missing sources, dependencies, assets, metadata or index fail. Index paths never direct reads. Extra files are not discovered.

Native integrations use [Package.h](../runtime/project/Package.h). `VerifyPackage` returns an immutable captured package; use `GetSnapshot()` for compiled input ownership and `GetFiles()` for captured assets/metadata. Never reopen paths after verification. Hashes verify consistency with the index, not publisher authenticity or update trust. Replacing both content and its index can produce another valid package.

Existing 256-source / 8 MiB file / 32 MiB source and compiled limits apply. Assets admit 16 MiB each / 64 MiB aggregate, the index 1 MiB, and combined retained native project/package data 128 MiB. Limits can be narrowed. These are admission bounds, not a process-wide transient memory cap. Trusted package contents are not diagnostic redaction projections; offline Restricted staging does not enable Restricted execution.

Composition reserves host/application namespaces, refuses existing outputs/links/collisions, excludes PDBs and bounds its native staging child. Policy-enabled launching recompiles captured source inputs; this is not an authenticated package/update launcher. Live image decoding retains the trusted declared-file backend contract. LUI source/documentation use MIT; retain dependency notices and review their distribution obligations. See [platform limits](PLATFORMS.md).
