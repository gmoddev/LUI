# Owner-host service declarations

Schema v2 projects type and validate owner-host ABI v1 methods/signals using captured JSON sidecars. This guide describes Provider Host; the linked schema 2 guide covers native providers. [Project hosting](PROJECT-HOST.md) adds policy-enabled v2 launch/preview/editor and verified native loading.

Add `"HostServices"` to Capabilities and relative `.json` files to ExtensionDeclarations. Keep Extensions empty. A sidecar uses this exact shape:

```json
{
  "SchemaVersion": 1,
  "AbiVersion": 1,
  "Provider": "Host",
  "Services": [{
    "Name": "Telemetry",
    "Methods": [
      {"Name": "Count", "Arguments": ["string"], "Returns": "number"},
      {"Name": "Clear", "Arguments": [], "Returns": "nil"}
    ],
    "Signals": [
      {"Name": "Changed", "Arguments": ["number", "boolean"]}
    ]
  }]
}
```

The checker derives types for `app:GetService("Telemetry")`, methods and signal callbacks. Existing built-in services remain available. Run the existing `check --mode project --diagnostics development` command with generated core definitions. Checking reads metadata and never runs a host callback or DLL. Arbitrary `.d.luau` sidecars and nonprimitive types are refused.

The owner registers methods/signals using existing `Lui_RegisterHostMethod` / `Lui_RegisterHostSignal` or the C++ facade. `Lui_RunProject` compares the complete actual registry with declared metadata before running entry. For example `(Self: Telemetry, Name: string) -> number` matches Count; parameter labels/spacing are ignored. `() -> ()` matches a nil-returning Clear. `Signal<number, boolean>` matches Changed. An absent/extra member or incompatible signature returns RegistrationMismatch without calling it. Native callbacks remain trusted; metadata matching is not proof of callback implementation correctness.

Declarations grant no capabilities: manifest requests and owner grants are required. Restricted checking is refused; policy-enabled Restricted execution is supported. Empty declaration lists retain the untyped host profile. Sidecars require a complete registration contract; every host member must be declared. A preflight mismatch leaves an unstarted runtime and its original host registrations.

## Capture and limits

[Native-provider sidecar schema 2](PROJECT-NATIVE-BINDINGS.md) adds identity/version/capabilities, platform path/hash pins, captured bytes and provenance. Host schema 1 remains unchanged. Types/packages may include both providers; no DLL loads during checking/staging. Execution requires owner trust and verified actual native registrations before entry.

- At most 16 sidecars, 256 KiB each and 1 MiB combined; 64 services / 256 members across the project, 32 primitive arguments per member.
- Types are exactly nil, boolean, number or string. Returns is one primitive; ABI void normalizes to nil.
- Names are portable ASCII identifiers without Lua keywords. Built-in/reserved names, duplicate services and method/signal collisions fail.
- Strict schemas/duplicate-key/path/link/physical-identity checks apply. Captured sidecars share the existing 128 MiB retained-data admission budget.
- Exact sidecar bytes enter source/compiled fingerprints. Canonical sorted registration data has its own fingerprint; ordered argument types remain significant.
- `stage` includes captured sidecars in metadata and the package index; `verify` rejects byte edits. The checker uses frozen sidecars and hashes effective merged definitions. No diagnostic protocol shape or stable extension ABI changes.

See [native provider bindings](PROJECT-NATIVE-BINDINGS.md), [project hosting](PROJECT-HOST.md) and [testing](TESTING.md) for execution, registration validation and qualification requirements.
