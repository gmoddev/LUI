# 0019 — Editor inspector backed by runtime reflection

The VS Code extension consumes the isolated preview host's version 1 JSON stream. It rejects malformed envelopes, unknown classes, invalid bounds, missing parents, parenting cycles, oversized snapshots, and unexpected generations before updating the Explorer. Application Luau stays in the preview process; the extension host only reads protocol data and sends bounded commands.

The host includes the runtime's reflection schema in `hello`. The inspector uses that schema to choose and type the properties present in snapshots. It presents resolved bounds as an explicitly derived value. A separate layout map illustrates logical bounds and does not mimic native controls. Selection is local editor state and clears when a reload changes generations, so reused numeric Instance IDs cannot silently inherit an old selection.

The first editor slice stays read-only except for explicit activation and viewport resize commands. Saving the entry script requests a fresh preview generation; saving the manifest restarts the host. Source provenance, type diagnostics, input simulation, and native preview are subsequent Foundation 3 work.
