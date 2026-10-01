# 0022 — Networking as a runtime service track

**Date:** 2026-10-01

## Context

The supplied [networking architecture proposal](../proposals/NETWORKING-ARCHITECTURE.md) calls for outbound HTTP, hosted HTTP endpoints, and raw TCP/UDP. LUI's completed Foundation 2 Windows audit covers callback-style platform completion and capability-gated native integration, but not coroutine-suspending async operations, sockets, HTTP, or network grants. Foundation 3 is underway. The proposal would add a substantial runtime capability without coupling it to WinUI or the future GTK backend.

## Decision

Add Networking Foundation A as a planned Foundation 2 follow-on, while preserving the completed Windows exit audit. A first slice defines an owner-thread async operation and coroutine-resumption contract with bounded cancellation and teardown. A then adds TCP over a pinned internal transport dependency and must pass Windows and Linux headless conformance. B adds outbound and hosted HTTP with strict framing and limits. C adds UDP and TLS. The public surface is planned as `NetworkService`, `HttpService`, and `HttpServerService` under `app:GetService`; native socket handles stay private. Network workers post bounded completion records and never enter Luau or mutate Instances.

Plan separate `network.client`, `network.server`, and `network.raw` grants. A raw outbound operation needs both client and raw; a raw listener needs both server and raw. Normal HTTP hosting needs server but not raw. The default listener address is loopback; wider exposure requires an explicit address and host grant. The host declares grants before scripts, and sandboxed code cannot raise them. No grant or service is implemented by this decision.

Standalone Asio is the leading internal transport candidate, subject to an exact version, license, build, and cancellation review before adoption. LUI retains address, error, backpressure, and lifecycle semantics. HTTP/1.1 parsing will reject ambiguous framing, including requests containing both `Content-Length` and `Transfer-Encoding`, before route dispatch. TLS remains a provider boundary rather than an assumed feature of the transport library.

## Open contract questions before implementation

- Define coroutine ownership and cancellation when a script, task, listener, or runtime is destroyed. Existing platform completions call back on the owner thread but do not suspend and resume Luau tasks.
- Define how a dual-family listener with port zero acquires one port and rolls back partial binds, and what `DualStack` does if one family is unavailable. Set `IPV6_V6ONLY` explicitly rather than inheriting OS defaults.
- Qualify the practical maximum UDP payload by address family and MTU. The proposal's 65,535-byte cap is only a policy ceiling, not a promise that every such datagram can be sent.
- Fix numeric limits, portable error mappings, and manifest policy syntax after executable conformance tests. The proposal's values are initial targets, not implemented defaults.

## Evidence and consequences

Luau exposes a native [`buffer` type](https://luau.org/library/) and [C API](https://github.com/luau-lang/site/blob/master/src/content/docs/reference/api.md). [Asio documents](https://think-async.com/Asio/asio-1.36.0/doc/asio/using.html) IOCP and epoll support. [Windows](https://learn.microsoft.com/en-us/windows/win32/winsock/dual-stack-sockets) and [Linux](https://kernel.org/doc/html/v6.7/networking/ip-sysctl.html) differ in IPv6-only defaults. [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html) permits strict rejection of ambiguous HTTP framing, and [RFC 8305](https://www.rfc-editor.org/rfc/rfc8305.html) informs future dual-family client dialing.

The networking proposal remains a reference, while [SPEC §53](../SPEC.md#53-networking-and-hosted-endpoints-planned) records the current intended contract. None of these APIs should enter generated types, reflection, or user-facing examples as implemented behavior until their runtime tests pass. Foundation 3 type diagnostics remain pending while Foundation A begins.
