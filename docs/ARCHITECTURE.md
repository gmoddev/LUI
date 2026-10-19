# Architecture

```text
Application Luau
    ↓
LUI runtime: VM, scheduler, Instances, signals, reflection and services
    ↓
LUI UI semantics: layout, input, focus, styles, themes and accessibility
    ↓
Native backend: WinUI 3 or GTK4
```

## Ownership

LUI defines public behavior and resolves geometry in logical units. Platform controls are private representations of canonical Instances; toolkit behavior cannot silently redefine LUI properties or events.

One scheduler owns the VM and UI tree. Native workers queue bounded completions to that owner. Destruction and generation retirement disconnect stale work before it can reach application callbacks. Reusable scopes/state/collections follow the same ownership model.

## Interfaces

[LuiExtension.h](../native/abi/LuiExtension.h) provides the supported version 1 primitive extension contract. It is distinct from the internal host interface in [LuiRuntime.h](../native/abi/LuiRuntime.h) and captured project interface in [LuiProject.h](../native/abi/LuiProject.h). Host callbacks and toolkit objects are not part of the stable extension ABI.

Reflection in [Schema.cpp](../runtime/reflection/Schema.cpp) drives generated API documentation, Luau definitions and inspector metadata. Backend mappings follow shared conformance tests.

## Projects and diagnostics

Declared inputs are captured before compilation, checking, execution or staging. Module identities and resolution are shared across consumers. Native provider loading requires explicit owner trust, declared capabilities and verified captured inputs. Native code remains privileged.

Diagnostic policy is selected before VM startup. Sensitive classification is sticky; framework snapshots and diagnostics use bounded projections. Secret inputs use native password controls. Explicit application prints and hostile native code are outside an automatic secrecy guarantee.

See the [canonical invariants](INVARIANTS.md), [application contract](SPEC.md), [project host guide](PROJECT-HOST.md) and [platform limits](PLATFORMS.md).
