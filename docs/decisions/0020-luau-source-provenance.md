# 0020 — Luau source provenance in preview

**Date:** 2026-10-01

## Context

The preview Explorer showed runtime Instances and layout, but could not identify the Luau statement that created or last changed one. Runtime failures appeared in an Output channel without a source link. The existing preview protocol is version 1 and its tree and diagnostic messages have room for optional fields.

## Decision

The preview host enables source provenance before the entry script runs. The runtime records the nearest Luau frame's chunk source and one-based line at `Instance.new`, clone, parenting, and successful property writes. A last-change location also records the property name. Native changes with no Luau frame retain the previous Luau location. Other hosts leave provenance disabled by default.

The runtime snapshot adds optional `createdAt` and `lastChangedAt` values to each Instance. The host adds an optional `location` to runtime errors and diagnostics when their Luau error prefix names the manifest entry script. The VS Code extension validates these fields and opens files only when the source resolves exactly to that entry script. It presents runtime errors in Problems and clears them when a later generation succeeds.

## Consequences

The version 1 protocol remains compatible with clients that ignore unknown fields. Location lookup and snapshot data add preview overhead. Luau provides line numbers but no column. Locations for constructor property tables can point to the `Instance.new` call rather than an individual table field. A snapshot still has the existing 4 MiB cap. Source navigation and diagnostics remain limited to the manifest entry script until a future module-aware source map is designed.
