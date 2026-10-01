# 0018 — Isolated preview protocol and reload generations

The editor launches `lui-preview-host` as a separate process. Application Luau runs in a sandboxed VM there. Version 1 JSON lines on standard input and output carry commands and full tree snapshots; standard output contains no unframed runtime logs. The host serializes the same Instance nodes and resolved layout that the native backend uses.

Each reload destroys the existing runtime, including callbacks and scheduled work, before incrementing the generation and running source again. Every stateful command identifies its expected generation, preventing stale editor events from targeting reused numeric Instance IDs. The first protocol uses full snapshots for simple recovery and deterministic state; incremental updates can be added later under explicit compatibility rules.

The initial host rejects privileged manifest capabilities and native extensions. It bounds input lines, queued commands, source, VM resources, and serialized trees. The host process boundary contains preview crashes away from the editor process. Exact native rendering remains the responsibility of a later native preview command.
