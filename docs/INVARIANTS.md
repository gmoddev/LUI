# 50. Canonical invariants

The following invariants are architectural requirements rather than implementation suggestions.

---

## 50.1 Public API invariants

1. **LUI owns public semantics.**  
   Public behavior is defined by LUI rather than inherited accidentally from WinUI, GTK, Win32, or another backend.

2. **Simple operations remain simple.**  
   Creating a window, label, button, text box, or layout must not require lifecycle, thread, renderer, or platform boilerplate.

3. **Platform details stay below the normal API.**  
   Ordinary application code must never require native handles or toolkit objects.

4. **Roblox familiarity is intentional.**  
   Where Roblox semantics translate cleanly to desktop UI, LUI should prefer familiar names and behavior rather than inventing unnecessary concepts.

5. **No framework is required above Instances.**  
   Declarative/component systems are libraries, not mandatory runtime semantics.

6. **Portable code is platform-neutral by default.**  
   Platform-specific behavior requires deliberate use of platform APIs.

7. **Unsupported behavior is explicit.**  
   Backend limitations must not be silently misrepresented as supported semantics.

---

## 50.2 Backend invariants

1. A backend implements LUI semantics; it does not redefine them.

2. Backend-specific UI objects remain private implementation details.

3. Native events are normalized before reaching Luau.

4. Backend object lifetime follows LUI Instance lifetime.

5. Platform-specific features must be capability-queryable.

6. A new backend must not require changes to ordinary application code.

7. Backends must pass the shared conformance suite.

8. LUI layout semantics cannot depend on whichever layout algorithm the native toolkit happens to use.

9. Backend operation failures must reach the runtime. A failed creation is rolled back; other failures stop dispatch before the runtime presents divergent state as healthy.

---

## 50.3 Layout invariants

1. All ordinary layout uses logical device-independent units.

2. `UDim` and `UDim2` have identical semantics across backends.

3. LUI is authoritative for parent-relative layout.

4. Backend measurement may provide intrinsic size data but may not redefine LUI layout rules.

5. Property mutation during one Luau dispatch is batched before expensive backend/layout work wherever practical.

6. Absolute geometry properties are read-only derived values.

7. Layout behavior must be testable without requiring physical display hardware.

---

## 50.4 Lifecycle invariants

1. An Instance has one authoritative LUI identity regardless of backend object representation.

2. Destroy is explicit and idempotent from the application perspective.

3. Destroyed objects may not continue generating application-visible events.

4. Native callbacks may never reference an already-destroyed LUI object.

5. Parenting defines UI ownership unless a class explicitly documents another lifetime.

6. Application shutdown has deterministic ordering.

7. Backend destruction cannot synchronously reenter arbitrary Luau code unless explicitly defined by the LUI lifecycle contract.

---

## 50.5 Threading invariants

1. The Luau VM has one authoritative scheduler thread.

2. UI Instances are mutated only through that scheduler context.

3. Native worker threads never directly enter Luau.

4. Worker completion is marshaled through LUI.

5. Native backend APIs with UI-thread affinity are only called from their valid thread.

6. Application authors do not manually lock UI objects.

7. Application authors do not manually pump OS event loops.

8. Native backend notifications never synchronously reenter an already executing Luau VM. The scheduler dispatches them after the active VM and backend calls finish.

---

## 50.6 Native ABI invariants

1. The stable extension boundary is a C ABI.

2. C++ standard-library types never cross the stable ABI.

3. Internal pointers are not stable handles.

4. Every ABI structure is explicitly versioned or size-versioned.

5. Extensions receive only declared host capabilities.

6. Native object ownership is explicit.

7. Native callbacks cannot directly reenter Luau from arbitrary threads.

8. Raw `lua_State*` integration belongs to an explicitly unstable advanced tier.

9. Native ABI changes cannot silently reinterpret old structures.

10. Extension load failure must fail safely without corrupting the runtime.

---

### Owned native service invariants

- ABI v2 does not change ABI v1's layouts or reinterpret its metadata.
- Worker data owns its references; opaque identities never expose native pointers
  to ordinary Luau. Views remain valid only while an owned buffer reference lives.
- Reserve terminal capacity and maximum result bytes before starting work.
  Progress cannot consume terminal capacity; admission never starts refused work.
- One settlement wins. Accepted progress precedes a later provider failure;
  cancellation, expiry and closure suppress undelivered progress.
- Only the owner delivers Luau callbacks or invokes native cleanup. Retirement
  removes old-generation callbacks before destroying the VM.
- Native images remain pinned until quiescence and owned references drain.
  Stuck providers remain charged; reload cannot reset domain quotas.
- Platform adapters require separate grants and explicit support. Fixed native
  errors exclude private payloads, filesystem paths and provider context.

## 50.7 Security invariants

1. No arbitrary FFI is exposed to ordinary Luau code.

2. Native pointers are opaque unless explicitly crossing a platform-interop boundary.

3. Native extension loading is explicit.

4. Capability access is declared rather than inferred.

5. Sandboxed execution cannot gain native-code execution through normal API surfaces.

6. Sandboxed execution cannot escape filesystem restrictions through path normalization bugs or native helper APIs.

7. Execution and memory limits are enforceable by the host.

8. Untrusted code cannot directly control the UI thread's native message pump.

9. IPC from the editor to preview runtime is treated as untrusted input.

10. Preview execution must never run application Luau inside the VS Code extension process.

11. Development metadata must not accidentally grant application capabilities.

12. Native handles exposed through interop APIs are typed and scope-aware wherever practical.

13. Serialization/deserialization of preview, build, or extension metadata is fail-closed.

---

## 50.8 Theming invariants

1. Default controls use native/system appearance wherever practical.

2. System light/dark/high-contrast changes propagate through the LUI theme system.

3. Explicit application styling overrides appropriate system defaults.

4. Platform-specific visual effects are optional capabilities.

5. Unsupported visual effects have documented fallback behavior.

6. Themes never change the semantic meaning of controls.

7. Accessibility modes take precedence over purely decorative styling when required for correct platform behavior.

8. Applications do not need platform conditionals for ordinary light/dark theming.

---

## 50.9 Accessibility invariants

1. Accessibility is part of class semantics.

2. Native controls retain their native accessibility support.

3. Custom controls must define their accessible role and state.

4. Keyboard-only operation must remain possible for standard interactive controls.

5. Focus state must remain exposed consistently.

6. Application styling must not silently disable required accessibility behavior.

7. Preview tooling may approximate accessibility presentation, but native backend qualification is authoritative.

---

## 50.10 Input invariants

1. LUI exposes semantic input events rather than raw platform message IDs.

2. Standard controls expose high-level actions such as `Activated`.

3. Keyboard, mouse, touch, accessibility activation, and pen input may map to the same high-level action where semantically equivalent.

4. Raw platform input remains behind explicit interop APIs.

5. Input callbacks execute through the LUI scheduler.

6. Focus behavior must remain deterministic from the application's perspective.

---

## 50.11 Reflection invariants

1. Runtime metadata has one authoritative definition.

2. Editor types are generated from runtime metadata.

3. Documentation is generated or validated against the same metadata.

4. Property Inspector schemas derive from the same metadata.

5. Backend conformance data derives from the same semantic definition.

6. A property cannot exist only in runtime code while being absent from reflection.

7. A reflected API cannot claim functionality the runtime does not implement.

---

## 50.12 Tooling invariants

1. Tooling consumes LUI semantics rather than reimplementing them.

2. Preview uses the same object/layout engine as production.

3. Native preview is authoritative where web preview cannot reproduce platform behavior.

4. VS Code never executes arbitrary application code in the extension host.

5. Source provenance must identify the Luau origin of development-mode Instances where available.

6. Preview/editor protocols are versioned.

7. Hot reload must not leave callbacks or native objects belonging to a dead runtime generation active.

---

## 50.13 Build invariants

1. Release applications are self-contained from the user's perspective.

2. Application developers do not need to redistribute a separate Luau installation.

3. Build artifacts contain only required capabilities and extensions.

4. Build mode does not change public LUI semantics.

5. Bytecode format is not a public compatibility contract.

6. Native extension compatibility is checked before application execution.

7. Release builds may strip development metadata without altering application behavior.

---

## 50.14 Compatibility invariants

1. Public semantic behavior is versioned separately from implementation details.

2. Backend changes cannot silently change existing property meaning.

3. Deprecation precedes removal of normal public APIs.

4. Internal native architecture may evolve without requiring ordinary Luau application changes.

5. Stable extension ABI compatibility is explicitly defined rather than assumed.

6. Platform-specific APIs may evolve independently while remaining outside the portable contract.

---
