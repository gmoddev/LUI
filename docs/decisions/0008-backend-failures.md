# 0008 — Backend operation failures

Date: 2026-09-30

## Context

The internal backend callbacks previously returned no status. WinUI caught and logged exceptions, leaving the runtime unaware that creation, a property update, parenting, layout, or destruction had failed. The LUI Instance tree could then diverge from its native representation.

## Decision

Every internal backend callback returns success or failure. A backend may report diagnostic detail to the runtime before returning failure. A failed creation destroys any partial native object, removes the new LUI Instance, and fails the Luau call. Failed property, parent, arrange, or destroy operations mark the runtime as failed. The runtime records a structured diagnostic, drops pending backend changes and events, rejects subsequent scripts and input, and still permits destruction for cleanup. It does not continue presenting the divergent tree as a working application.

WinUI catches exceptions at the callback boundary, reports them through this contract, and keeps diagnostics nonmodal. The C callback table remains an internal, unstable host contract; this change makes no promise about the future extension ABI.

## Consequences

Headless tests inject each operation failure and verify that it reaches the runtime. A process using a failed runtime must create a new runtime to recover. Interactive WinUI failure behavior remains to be qualified on a desktop.
