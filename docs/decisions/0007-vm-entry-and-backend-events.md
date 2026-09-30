# 0007 — VM entry and backend events

Date: 2026-09-30

## Context

Luau can request derived geometry while a script or signal callback is executing. That request flushes properties and layout to a native backend. Native property and layout calls can synchronously raise input or focus notifications. Dispatching those notifications immediately would enter the active Luau VM again through a signal callback.

## Decision

The runtime tracks active Luau calls and backend calls. Backend notifications received during either call are copied into a bounded FIFO queue. The scheduler drains the queue after the active Luau call and backend flush finish. Notifications received while both are idle still dispatch immediately. A queued event is checked against the current Instance and input state when it is drained; notifications for destroyed or ineligible objects are discarded.

The queue holds at most 4096 events. An overflow fails the incoming notification and records a structured error. Reentrant calls to run a script, pump scheduled work, or destroy the runtime are rejected with a diagnostic.

## Consequences

Native callbacks cannot synchronously enter an already executing Luau VM. A backend can report events while applying a property or arranging a control, and signal callbacks execute after the native operation returns. The headless backend tests both callback sites. This contract does not authorize worker threads to call the runtime directly; the owner thread rule still applies.
