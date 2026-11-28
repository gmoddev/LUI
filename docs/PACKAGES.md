# Packages

The Preview.5 CLI restores reusable Luau packages into LUI's
existing schema-v2 module loader, native module lock, type checker and
standalone builder.

```powershell
lui package init Catalog/Greeter/1.0.0 --name Greeter --version 1.0.0 --license MIT
lui package verify Catalog/Greeter/1.0.0
lui package pack Catalog/Greeter/1.0.0 --output Greeter.luipkg
```

Keep one catalog entry per name and version. Use the directory while editing
locally, or place the archive at `Catalog/Greeter/1.0.0.luipkg`. Each
`LuiPackage.json` lists its modules and their dependencies.

```json
{
  "SchemaVersion": 1,
  "Name": "Greeter",
  "Version": "1.0.0",
  "Sdk": "*",
  "License": "MIT",
  "LicenseFile": "LICENSE.txt",
  "Modules": ["Main.luau"],
  "Dependencies": {"Format": "^2.1.0"}
}
```

Import the entry with `require("@Greeter/Main")`. Export types through declared
Luau modules for the existing checker. Optional `Types` files ending in
`.d.luau` stay in the archive and local cache. The restored application carries
a notice asset with package versions, license identifiers and declared license
text.

Put the application's own modules under a separate root such as `Src`.
Use `lui package add Greeter --version '^1.0.0'` beside `lui.json`.
The command creates or edits `LuiDependencies.json`. Its direct requests
take this form.

```json
{"SchemaVersion": 1, "Dependencies": {"Greeter": "^1.0.0"}}
```

```powershell
lui package restore lui.json --source Catalog
lui package inspect lui.json
lui check lui.resolved.json
lui run lui.resolved.json
lui build lui.resolved.json --mode headless --output Dist/Hello
```

The resolver chooses one version per alias, tries the highest compatible
version first and backtracks within a fixed budget. Requests support exact
three-component versions, caret compatibility, tilde patch ranges and
`*`. Existing manually vendored modules and their lock entries participate
in the generated native lock.

The package's `Sdk` field accepts an exact SDK version or `*`. Restore uses
the matching installed SDK and checks the project's `LuiToolchain.json`
when present. Select an installation with `--sdk` as for other SDK commands.

Keep `lui.json`, `LuiDependencies.json`, `LuiPackages.lock.json` and the
toolchain pin in version control. Restore again without `--source` to reuse
the exact cached bytes. If the cache is missing, supply a catalog containing
the locked content. For a deliberate version or local source update, pass
`--update true`. For a changed generated manifest, also select a new sibling
filename with `--output` or pass `--replace-output true`.

`lui package remove Greeter` edits the dependency requests. Run
`lui package update lui.json --source Catalog --replace-output true`
after reviewing a requested change. Use `--locked true` in automation to
require an existing lock. A failed resolution leaves the runnable
manifest in place. An interruption between lock and manifest publication
can leave the older output in place with the newer lock. Rerun frozen
restore with explicit replacement to regenerate that output.

### Package sources

Work from a local directory or an exact `.luipkg` archive in a catalog.
To import an HTTP-distributed archive, obtain its SHA-256 through a
trusted channel and use `lui package fetch <https-url> --sha256 <hash> --source Catalog`.
Fetch uses ordinary TLS validation, rejects redirects, bounds the transfer,
and validates the entire archive before making it available to restore.

To reuse a package from a Git repository, first obtain a trusted clone
and full commit ID. Use `lui package import-git Repo --revision <commit>
--git <absolute executable> --source Catalog`. Add `--subdir <path>` for
a package inside a repository. Import reads declared regular blobs
from the selected commit and captures their bytes in a local archive.
It uses a selected Git executable and reads commit data without checking
out a working tree. Restore uses local sources and the lock, so it
works with the network disconnected.

### Optional and platform profiles

`lui package add Theme --optional true` declares an optional root.
Include it with `--include Theme` on restore. Package authors can declare
`OptionalDependencies` and list optional Luau files in a
`FeatureModules` object keyed by the optional alias. Keep imports of
that alias in those feature-specific files. For example,
`"OptionalDependencies": {"Theme": "^1.0.0"}` with
`"FeatureModules": {"Theme": ["RichGreeting.luau"]}` makes the
additional entry available when the consumer selects Theme. Include
all explicitly selected aliases on subsequent restore and inspect.

Package authors can limit a release with `"Platforms": ["LinuxX64"]`.
`lui package add PlatformTool --platform LinuxX64` creates an
application dependency for that target. Restore skips incompatible
versions during a fresh solve. Frozen restore keeps its existing versions
and reports an incompatible selection. Projects with multiple profiles
can choose separate `--lock <name.lock.json>` and `--output <name.json>`.

`Capabilities` and `TrustedNative` express requirements. Restore checks them
transitively against the application's declarations. Trusted-native packages
also require `--trusted-native true` during restore. Execution applies the
existing application and host grants independently.

### Packages with native providers

List provider sidecars in `NativeProviders` and include the provider's
capabilities in the package descriptor.

```json
{
  "SchemaVersion": 1,
  "Name": "MathBindings",
  "Version": "1.0.0",
  "Sdk": "*",
  "License": "MIT",
  "LicenseFile": "LICENSE.txt",
  "Modules": ["Main.luau"],
  "Dependencies": {},
  "Capabilities": ["NativeExtensions"],
  "TrustedNative": true,
  "NativeProviders": ["Math.json"]
}
```

Use an existing native provider declaration such as the SDK's
`Examples/digest/Crypto.json`. Paths in its bindings are relative to the
package root. Include each listed binary and its SHA-256, together with the
license and required notices.
Windows x64 uses `.dll` and Linux x64 uses `.so`. A package can carry both.
The existing package bounds apply to the whole package (32 MiB) and to
individual binaries (8 MiB).

Add the provider's exact name to the application's authored `Extensions`
array and declare its required capabilities. Restore with
`lui package restore lui.json --source Catalog --trusted-native true`.
The resulting manifest includes a sidecar for the current platform. Package
verification checks the matching PE or ELF x64 library header before
staging. The native loader still validates provider identity, ABI, and
host policy at execution. The
lock covers all packaged platform files. When sharing a project across
platforms, generate the appropriate manifest on each host.

The standard checker and builder validate the restored provider contract and
capture its selected binary. Select the provider in the existing host at
launch. The stock headless host accepts its pinned standard providers and
experimental Ffi with their existing options. A custom provider needs a
compatible host with explicit native trust. Operating-system library
dependencies remain part of the native provider's deployment requirements.

Content hashes verify consistency with the local lock. Establish publisher
authenticity independently. ZIP and directory sources admit declared regular
files within portable path and allocation limits. Package commands execute
no install hooks.

For a project restored by an earlier private SDK, restore from the trusted
catalog or cache again with the new SDK and explicitly replace the generated
manifest. Rebuild standalone output from that manifest to use the new
generated metadata hash checks on qualified native hosts.

For `VersionConflict`, inspect direct and transitive constraints for the
reported alias. `PackageLockChanged` means the dependency request or selected
SDK compatibility changed. `PackageIntegrityMismatch` means the cached bytes
differ from their pin. Supply a verified catalog and restore in a clean
project copy.
