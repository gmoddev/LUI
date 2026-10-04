# Networking Foundation B — HTTP

**Status:** in progress. The first slice implements an internal, bounded HTTP/1.1 request/response parser. `HttpService` and `HttpServerService` are not available through `app:GetService` yet.

## Implemented

The C++ component in [runtime/network/http](../runtime/network/http) parses incrementally across arbitrary transport fragments. It returns the exact consumed message boundary, preserves binary bodies, handles fixed lengths and chunked encoding, and keeps admitted trailers separate from headers. Failed or incomplete messages cannot be retrieved as complete messages. A parse failure is terminal and requires connection closure by the future service layer.

It rejects ambiguous length/transfer framing, duplicate lengths, invalid Host fields, obsolete folding, invalid line endings, malformed chunks, unsafe/unknown trailers, and configured limit overruns. Response parsing handles HEAD, informational status, 204/205/304, and EOF-delimited content. Persistent input can carry multiple messages, but each requires a fresh parser; the caller retains the suffix and must enforce ordered dispatch and transport liveness.

The supported profile is deliberately narrower than all HTTP: HTTP/1.1, origin-form or `OPTIONS *`, one chunked transfer coding, and three metadata trailer names. HTTP/1.0, proxy forms, CONNECT, upgrades, and request expectations are rejected. See [decision 0027](decisions/0027-bounded-http-framing.md) for the exact contract and bounds.

## Verification

`LuiHttpTests` covers fragmentation at every split, single-byte reads, binary bodies, persistent boundaries, response body exceptions, all-prefix EOF truncation, deterministic byte mutations, malformed-request regressions, and resource limits. Its transport fixture sends two requests through the existing asynchronous TCP service and feeds bounded reads into the native parser. Build and run the `LuiHttpTests` target with CMake/CTest. [Build-Windows.ps1](../scripts/Build-Windows.ps1) and Linux CI include it.

This is headless verification. No application endpoint is opened persistently, no firewall rules are changed, and no native UI is needed.

## Next implementation

Add validated request/response serialization and the outbound `HttpService` on the shared network worker, with `network.client` enforcement independent of `network.raw`, bounded deadlines, cancellation, and owner-thread completion. Then add loopback-default `HttpServerService`, exact routes, ordered scheduler tasks, bounded response queues, generic handler-failure replies, and shutdown tests. A listener must never dispatch before complete bounded parsing. TLS/HTTPS remains Foundation C; no HTTPS support is implied by this parser.
