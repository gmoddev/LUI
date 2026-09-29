# Architecture at a glance

The [full specification](SPEC.md) is authoritative for intended behavior. This page is a short map for contributors; implementation details still need to be proven.

```text
Application Luau
    ↓
LUI runtime: VM · scheduler · Instances · signals · reflection · services
    ↓
LUI UI semantics: layout · input · focus · theme · accessibility
    ↓
Backend contract
    ├─ WinUI 3 / Windows (first)
    ├─ GTK4 / Linux (later)
    └─ Preview and headless tests
```

## Ownership boundaries

1. **LUI defines public behavior.** Backend controls are private representations of LUI Instances. Native toolkit behavior does not silently redefine properties or events.
2. **LUI owns layout.** Backends can measure intrinsic controls, while LUI resolves `UDim`/`UDim2`, constraints, and final geometry in logical units.
3. **One scheduler owns the VM and UI tree.** Workers cannot enter Luau or mutate Instances directly. They post completions to the LUI scheduler.
4. **Native integration has explicit tiers.** The compatibility boundary is a versioned C ABI with opaque handles. Raw `lua_State*` access is an advanced, unstable tier.
5. **Platform features are queried.** Portable APIs have defined behavior; optional platform effects expose support checks and documented fallbacks.
6. **Reflection is authoritative.** Runtime metadata drives generated types, editor inspection, documentation checks, and conformance data.

## Testing contract

The shared conformance suite should cover parenting, destruction, properties, signals, layout, visibility, focus, activation, disabled state, lifecycle, scheduler behavior, and testable accessibility mappings. Native backend qualification supplements headless tests.

## Design references

- [Canonical invariants](SPEC.md#50-canonical-invariants)
- [Backend contract](SPEC.md#48-backend-contract)
- [Security model](SPEC.md#46-security-model)
- [First implementation target](SPEC.md#53-first-implementation-target)
