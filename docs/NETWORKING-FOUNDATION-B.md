# Networking Foundation B — HTTP

**Status:** in progress. Bounded HTTP/1.1 parsing, validated serialization, and outbound `HttpService` are implemented. `HttpServerService` remains planned.

## Implemented

The C++ component in [runtime/network/http](../runtime/network/http) parses incrementally across arbitrary transport fragments. It returns the exact consumed message boundary, preserves binary bodies, handles fixed lengths and chunked encoding, and keeps admitted trailers separate from headers. Failed or incomplete messages cannot be retrieved as complete messages. A parse failure is terminal and requires connection closure by the future service layer.

It rejects ambiguous length/transfer framing, duplicate lengths, invalid Host fields, obsolete folding, invalid line endings, malformed chunks, unsafe/unknown trailers, and configured limit overruns. Response parsing handles HEAD, informational status, 204/205/304, and EOF-delimited content. Persistent input can carry multiple messages, but each requires a fresh parser; the caller retains the suffix and must enforce ordered dispatch and transport liveness.

The supported profile is deliberately narrower than all HTTP: HTTP/1.1, origin-form or `OPTIONS *`, one chunked transfer coding, and three metadata trailer names. HTTP/1.0, proxy forms, CONNECT, upgrades, and request expectations are rejected. See [decision 0027](decisions/0027-bounded-http-framing.md) for the exact contract and bounds.

## Outbound client

The host must declare `network.client` before scripts; HTTP needs no `network.raw` grant. Existing host loopback and destination-port policy applies before resolution and to resolved dial candidates. DNS and paced dual-family dialing use Foundation A's worker and completion bridge. Luau receives no socket object.

```lua
local Http = app:GetService("HttpService")
task.spawn(function()
    local Ok, Response = pcall(function()
        return Http:RequestAsync({
            Url = "http://127.0.0.1:8080/status",
            Method = "GET",
            TimeoutMs = 5000,
            MaxResponseBytes = 65536,
        })
    end)
    if Ok then
        print("[LUI:Example] HTTP status", Response.StatusCode, Response.Body)
    else
        print("[LUI:Example] HTTP failed", Response)
    end
end)
```

The example requires an independently running endpoint. `GetAsync(Url)` uses the default GET options and returns the same response table. Calls must run in `task.spawn` or `task.defer`. `RequestAsync` accepts:

| Option | Contract |
| --- | --- |
| `Url` | Required encoded `http://` URL, at most 8192 bytes. ASCII hostname or bracketed IPv6; port 1–65535, default 80. Credentials, fragments, scoped literals, and HTTPS are rejected. A query without a path becomes `/?query`. |
| `Method` | Case-sensitive HTTP token, default `GET`. CONNECT and nonempty GET/HEAD bodies are rejected. |
| `Headers` | Optional ordered array of `{Name, Value}` pairs, at most 96 caller fields. Duplicate application fields are preserved. Framing and hop-by-hop fields are runtime-owned. |
| `Body` | Optional binary string or buffer, at most 1 MiB. |
| `TimeoutMs` | Integer 1–60000, default 10000. One worker deadline covers DNS, dialing, writing, informational replies, and the final response. Scheduler delivery may occur later. |
| `MaxResponseBytes` | Integer 0–8 MiB, default 1 MiB. Decoded chunk content and EOF-delimited bodies share this bound. |

Every exchange uses a fresh socket and sends `Connection: close`. LUI constructs Host from the URL and Content-Length from buffered bytes. It adds `Accept-Encoding: identity` unless supplied by the caller. Compression is not decoded; response content remains binary bytes. HTTPS, proxies, automatic redirects/retries, cookies, JSON helpers, streaming, and connection pooling are not implemented. HTTP error status codes are returned normally: `Success` is true only for 200–299.

Responses are read-only tables containing `StatusCode`, `StatusMessage`, `Success`, binary string `Body`, ordered `Headers`, and separate ordered `Trailers`. Field names are lowercase; nested field tables are read-only too. HEAD and status-specific no-body rules come from the parser. At most eight informational responses are accepted before the final response. A framing failure closes the socket and never returns partial data; extra bytes already received after the final response cause rejection. No later response is parsed on that socket.

At most 32 HTTP calls may be outstanding per runtime, including completed results waiting for scheduler delivery; Foundation A's shared 128-operation ceiling also applies. Native buffered HTTP memory is bounded separately from the Luau VM quota. Transport, parser, timeout, cancellation, and quota errors are catchable strings beginning `[LUI:Http] Code`; the shared async-context guard uses `[LUI:Network]`. Errors produce no modal UI. `CancelAll()` cancels pending HTTP work through an ordered worker barrier; completed results already queued remain deliverable, and requests submitted after the barrier are unaffected. Runtime destruction closes resources and releases tasks without resuming application code.

The internal response serializer is also implemented for the future hosted service. It owns framing, validates fields and reason phrases, preserves duplicate application fields, suppresses content for HEAD/204/205/304, and emits fixed lengths for ordinary responses. No hosted route API is implied. [Decision 0028](decisions/0028-outbound-http-service.md) records this slice.

## Verification

`LuiHttpTests` covers fragmentation at every split, single-byte reads, binary bodies, persistent boundaries, response body exceptions, all-prefix EOF truncation, deterministic byte mutations, malformed-request regressions, and resource limits. Its transport fixture sends two requests through the existing asynchronous TCP service and feeds bounded reads into the native parser. Build and run the `LuiHttpTests` target with CMake/CTest. [Build-Windows.ps1](../scripts/Build-Windows.ps1) and Linux CI include it.

The client fixture uses a bounded native loopback peer with a separate `network.client`-only Luau runtime. It verifies serialization, duplicate fields, binary buffers, informational responses, chunked trailers, EOF bodies, HEAD, malformed replies, truncation, limits, host policy, cancellation, and destruction. Type tests verify the reflected request/response contracts. No external network access is needed.

This is headless verification. No application endpoint is opened persistently, no firewall rules are changed, and no native UI is needed.

## Next implementation

Add loopback-default `HttpServerService`, exact routes, ordered scheduler tasks, bounded response queues, generic handler-failure replies, and shutdown tests. A listener must never dispatch before complete bounded parsing. TLS/HTTPS remains Foundation C.
