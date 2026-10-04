# 0025: Paced TCP dialing and connection closure signal

Status: accepted for Networking Foundation A.

## Context

Asio's sequential endpoint connect could wait on one address family while another usable address was available. The TCP resource also exposed `IsOpen` but had no callback for closure. Both behaviors were listed as remaining Foundation A work.

## Decision

After one asynchronous resolver result, the runtime filters endpoints under host policy, alternates IPv4 and IPv6 while preserving each family's order, and keeps at most 16 candidates. It starts one attempt immediately and subsequent attempts 250 ms apart, with at most two active sockets. The first successful attempt wins and closes the others. A ten-second deadline covers resolution and dialing. This is inspired by [RFC 8305](https://www.rfc-editor.org/rfc/rfc8305.html), but does not implement its separate A/AAAA query timing.

`TcpConnection.Closed` is a no-argument signal delivered on the owner thread after explicit `Close`, observed remote EOF, or a terminal read/write error. It fires at most once; `IsOpen` is false before delivery. A peer closure becomes visible when an application read or write observes it. `Shutdown` by itself does not close the resource. A still-open connection collected by Luau or runtime teardown releases its callbacks without firing the signal. Signal callback errors are local logs. Subscriptions may be disconnected idempotently, and each connection permits up to 64 active listeners.

## Consequences

The dialer has bounded sockets and results, and one winner is materialized into Luau. DNS answer timing still depends on the platform resolver; a later transport iteration may split queries if measurements show a need. A read that observes EOF closes the connection resource, so writes after that observation fail with `Closed`.
