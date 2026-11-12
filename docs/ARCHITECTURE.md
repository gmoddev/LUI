# Architecture

**Audience: people extending, embedding or studying LUI.** This explains how the
framework is built. For application code and examples, start with the
[SDK documentation](https://gmoddev.github.io/LUI/); these internals are not extra
steps required to create a window or use a supplied service.

Application Luau runs in the shared C++/Luau runtime. LUI owns Instances, reflection, signals, scheduling, module resolution, layout, input, styles and service semantics. Backends render canonical state and return validated input; they do not implement a second application model.

```text
Declared project → captured inputs → shared compiler/resolver → owner generation
                                                               ↓
                                         runtime / layout / input / services
                                                               ↓
                          WinUI 3       GTK4       browser worker + DOM host
                                                               ↓
                                             preview / diagnostics projection
```

## Ownership and profiles

One scheduler owns the VM and Instance tree. Workers queue bounded completions to that owner. Destruction and generation retirement disconnect stale callbacks, cancel requests and retire resources before releasing a generation. Reusable scopes, state and virtual collections follow that ownership model. Geometry uses shared logical units.

WinUI 3 implements the selected Windows x64 profile. GTK4 implements the selected Linux/X11 profile. The experimental browser profile compiles the same runtime/project semantic sources to WASM in a worker, then sends a bounded versioned presentation protocol to a DOM host. Browser input returns through the worker; DOM nodes are private representations. Unsupported providers refuse capabilities explicitly. Its selected acceptance gate passes shared native/browser semantic fixtures, three-engine DOM performance, lifecycle/policy checks and Firefox real IME/Orca sessions. General accessibility usability, frozen-page guarantees and broader platform/provider combinations remain unqualified.

Platform controls, toolkit handles and service implementation details stay private. Native code remains privileged even when its declarations, inputs and completion lifetimes are checked. See the [backend guides](../backends/gtk4/README.md) and [browser host](../backends/browser/README.md) for profile-specific limits.

## Contracts and projects

The supported primitive extension ABI v1 in [LuiExtension.h](../native/abi/LuiExtension.h) is distinct from internal host callbacks in [LuiRuntime.h](../native/abi/LuiRuntime.h). The owned typed service ABI v2 in [LuiNativeServices.h](../native/abi/LuiNativeServices.h) is experimental and unfrozen. Its domain owns bounded requests, cancellation, event ordering, buffers, handles and cooperative retirement/quarantine. Host protocols and native extension ABIs evolve separately.

Declared source, dependency, asset and provider inputs are captured before execution, checking or packaging. The immutable compiler/resolver and module identities are shared by runtime, analyzer, CLI, preview and packaging. Managed manifest parsing lives in [tools/project/AppManifest.cs](../tools/project/AppManifest.cs); prepared-project consumption lives beside it. The WinUI host compiles those shared adapters.

Diagnostic policy is selected before VM creation. Sticky sensitive classification and bounded projections govern diagnostics and preview snapshots. Secret inputs use password presentation. Framework projections do not grant secrecy against application prints, privileged native code or external collectors.

[Schema.cpp](../runtime/reflection/Schema.cpp) drives generated API documentation, Luau definitions and inspector metadata. Public semantics belong in that shared layer before backend mappings are added.

## Build and validation boundaries

[Sources.cmake](../cmake/Sources.cmake) shares runtime/project sources between desktop and browser targets. [Dependencies.json](../cmake/Dependencies.json) owns build dependency pins. Desktop services and browser refusal providers are selected explicitly. Native qualification targets live in [NativeTests.cmake](../cmake/NativeTests.cmake); GTK has its own qualification group.

The Windows, Linux and GTK qualification scripts run the same commands locally and in CI. Shared headless semantics, native mappings, Rust interoperability, generated metadata, managed tooling, preview/editor protocols and relocated packaging provide distinct evidence. Browser toolchains and conformance remain a separate experimental profile. A passing build does not qualify untested devices or input/accessibility sessions.
