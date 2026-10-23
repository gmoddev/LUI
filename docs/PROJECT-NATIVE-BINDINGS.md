# Native provider and platform content bindings

For the experimental owned service interface, use sidecar SchemaVersion 3 /
AbiVersion 2 with Provider Native. Preserve the Extension identity, version,
capabilities, provenance and platform path/hash bindings described below.
Declare exactly one service with the same name as Extension.Name, a
ResourceKindCount and structured Methods; no Signals or arbitrary type strings:

```json
{
  "Name": "Echo",
  "Profile": "OwnedAsync",
  "MaxInputBytes": 1024,
  "MaxResultBytes": 1024,
  "InputKind": 0,
  "ResultKind": 0,
  "DeadlineMilliseconds": 30000
}
```

The checker derives the buffer/optional handle/callback signature and returns
NativeRequest. Kind 0 means no required handle; positive kinds must fit the
service's ResourceKindCount. Query metadata must match the declaration before
Init. NativeExtensions and HostServices are both required by this native owned
provider profile. Schema 1/2 and ABI v1 are unchanged. See [owned native
services](NATIVE-SERVICES.md) and the [executable fixture](../tests/services/Project.cpp).

Schema v2 preparation/checking/staging captures and hashes declared native bytes and generates primitive types without loading a library. [Project hosting](PROJECT-HOST.md) adds actual owner-trusted verified Windows/Linux loading before project execution; isolated preview continues to refuse privileged providers.

In a manifest, Extensions is a list of provider names, for example `"Extensions": ["NativeMath"]`. List the sidecar under ExtensionDeclarations and request NativeExtensions plus every capability it requires. HostServices is needed for Host sidecars; it is not automatically granted by native metadata. Legacy schema v1 Extensions remains a library-path list.

Native sidecars use this exact schema 2 shape (the zero digests below are placeholders, not usable pins):

```json
{
  "SchemaVersion": 2,
  "AbiVersion": 1,
  "Provider": "Native",
  "Extension": {
    "Name": "NativeMath",
    "Version": 1,
    "RequiredCapabilities": ["NativeExtensions"],
    "Provenance": {"Kind": "LocalVendor", "Id": "Release-1"},
    "Bindings": [
      {"Platform": "WindowsX64", "Path": "Native/Math.dll", "Sha256": "0000000000000000000000000000000000000000000000000000000000000000"},
      {"Platform": "LinuxX64", "Path": "Native/libMath.so", "Sha256": "0000000000000000000000000000000000000000000000000000000000000000"}
    ]
  },
  "Services": [{
    "Name": "MathService",
    "Methods": [{"Name": "Add", "Arguments": ["number", "number"], "Returns": "number"}],
    "Signals": [{"Name": "Completed", "Arguments": ["number"]}]
  }]
}
```

Use real lowercase SHA-256 fingerprints of exact binary bytes. Version is a positive uint32. Git provenance requires a full 40/64-digit lowercase revision. Capability names are the existing known grants; NativeExtensions is mandatory. Every native provider must appear exactly once in the manifest and sidecars. A service belongs to one provider, and built-in/duplicate/member/type rules from [host declarations](PROJECT-DECLARATIONS.md) apply. Host schema 1 and Native schema 2 may coexist in one project.

Only WindowsX64/.dll and LinuxX64/.so bindings are selected here, at most one per target per provider. All listed targets are required and verified on every host. No current-platform shortcut, registry discovery or download occurs. Unsupported targets fail. These labels and hashes do not prove a file is a compatible executable image or validate transitive loader dependencies.

## Checking, staging and native integration

Existing `check --mode project --diagnostics development`, `stage` and `verify` commands consume the shared captured inputs. The checker infers native service methods/signals without native execution. Staging uses captured bytes even after input changes/deletion and emits schema 2 / DeclaredContentV2 for native content. Its NativeBindings records include identity/platform/path/hash/length/version/capabilities/provenance. Projects without native content keep schema 1 / DeclaredContentV1. Verification rejects edited/missing binary bytes and mismatched metadata/index.

Internal `Snapshot::GetNativeBinaries()` exposes immutable captured content. `MatchNative` is a data matcher for owner observations of identity, ABI/version, capabilities, platform/hash and complete registration. A matching invented receipt cannot prove an OS image. `Lui_LoadProjectNative` produces observations from Query/Init of actually loaded captured pinned bytes and matches them before allowing entry.

At most 16 sidecars/providers, two bindings per native provider, 16 MiB per binary / 64 MiB combined, and 128 MiB combined retained project/package admission. C++ owners may narrow binary bounds; current C preparation structs use defaults. Primitive registration limits remain 64 total services / 256 members / 32 arguments. Existing strict path/link/case/collision and captured physical-identity checks apply. Parser/compiler temporaries are not process-wide memory capped.

Hash/provenance metadata grants neither capabilities, publisher authenticity nor license permission. Policy-enabled execution supports Development and Restricted; static Restricted checking remains refused. Actual loading uses captured pinned images and observed Query/Init registrations, with rollback and shutdown before unload. OS/transitive dependencies remain owner trust; sibling private dependencies need a later deployment profile. See [platform limits](PLATFORMS.md).
