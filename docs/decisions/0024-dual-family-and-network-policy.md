# 0024: Paired TCP listeners and host network policy

Status: accepted for Networking Foundation A.

## Context

The first TCP slice had one acceptor per listener and capability grants with no address or port restrictions. The planned `DualStack` option needs deterministic same-port behavior on Windows and Linux. A host also needs a way to narrow network access without exposing policy mutation to Luau.

## Decision

`DualStack` is available for the semantic addresses `loopback` and `any`. It binds an IPv6-only acceptor first, then an IPv4 acceptor at the selected port. A fixed-port failure closes both. For port zero, a second-bind address collision retries the whole pair at most 16 times. Other errors fail immediately. Both endpoints are reported, and one family being unavailable fails the entire listener. A listener has one pending Luau accept waiter; each family may have one native accept pending. A second accepted connection can wait in a bounded internal queue. Closing a listener cancels its waiter once and closes queued sockets.

The host-only `Lui_SetNetworkPolicy` takes a versioned `LuiNetworkPolicyV1` before scripts. It supports client/server loopback-only flags and inclusive client destination/server listener port ranges. Zero/zero means no range restriction. A restricted server port range rejects port zero because the OS could select a disallowed port. Outbound port checks run before DNS; loopback policy accepts numeric loopback addresses or `localhost`, rejects other names before DNS, and filters `localhost` results before dialing. The WinUI manifest exposes the same fields under `NetworkPolicy`. Policy only narrows capability grants, and invalid or late configuration fails without changing the policy.

## Consequences

The paired listener does not silently degrade to one family. Hosts that require dual-family availability must handle a bind error. Network policy is intentionally limited to loopback scope and port ranges; CIDR and hostname allowlists require a separate contract. Outbound endpoint selection remains sequential until parallel dialing is implemented.
