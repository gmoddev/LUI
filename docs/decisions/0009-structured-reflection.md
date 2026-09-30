# 0009 — Structured reflection metadata

Date: 2026-09-30

## Context

The first reflection table listed method and signal names but left their signatures in the type generator. The runtime separately listed which classes exposed each signal, and parenting rules named Window, Frame, and UISizeConstraint directly. These duplicate definitions could diverge as the public API grows.

## Decision

Reflection now records method signatures, signal types, service methods and properties, and each class's parent rule and child container capability. Runtime method and signal lookup follows class inheritance through that metadata. Parenting checks read the same parent rule and container capability. Generated Luau types and API documentation consume the structured entries. The JSON metadata format advances to schema version 2.

The runtime still maps a reflected method to its native implementation, and property handlers still implement value validation. Reflection defines which public members and parent relationships exist; it does not replace the code that performs them.

## Consequences

Adding a method, signal, or container requires a schema entry and an implementation, with generated types and docs derived from the entry. Consumers of `types/schema.json` must parse version 2. The framework remains experimental, so this metadata change does not carry a stable compatibility promise.
