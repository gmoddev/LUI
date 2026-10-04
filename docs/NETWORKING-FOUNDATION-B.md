# Networking Foundation B — HTTP

**Status:** HTTP scope implemented; Windows/Linux headless qualification is in progress. Bounded parsing, validated serialization, outbound `HttpService`, and hosted `HttpServerService` are available.

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

The internal response serializer owns framing, validates fields and reason phrases, preserves duplicate application fields, suppresses content for HEAD/204/205/304, and emits fixed lengths for ordinary responses. [Decision 0028](decisions/0028-outbound-http-service.md) records the outbound slice.

## Hosted service

The host grants `network.server`; no `network.raw` or `network.client` is required to serve requests. The latter is needed only if a handler also makes outbound HTTP calls. Binding uses TCP's host address/port policy and paired IPv4/IPv6 behavior. `CreateServer` binds immediately; port zero exposes the selected `Port` before `Start`. Acceptance begins only after `Start`, and routes are frozen then.

```lua
local Server = app:GetService("HttpServerService"):CreateServer({Port = 8080})
Server:Route("GET", "/status", function(Request)
    return {
        StatusCode = 200,
        Headers = {{Name = "Content-Type", Value = "text/plain"}},
        Body = "OK",
    }
end)
Server:Start()
-- Call Server:Close() when the application stops hosting.
```

`CreateServer` options: required `Port` (0–65535); optional `Address` (default `loopback`), `Family` (`IPv4`/`IPv6`/`DualStack`), `TimeoutMs` (1–60000, default 10000), `MaxConnections` (1–32, default 16), and `MaxRequestBytes`/`MaxResponseBytes` (0–8 MiB, default 1 MiB each). Numeric addresses must agree with Family. `any` is an explicit public bind; no fallback or firewall rule is added. Host port restrictions also apply to port-zero selection as in TCP: a required nonzero range rejects zero.

The server exposes read-only `Port`, `BoundEndpoints`, and `IsListening`, plus `Route`, `Start`, and `Close`. Start is idempotent; Close is terminal and idempotent. Finalization closes resources. At most four retained handles are allowed per runtime; closed retained handles count until collected. HTTP and raw TCP bindings share the 16-listener ceiling. Every server has at most 128 routes and every runtime at most 32 hosted session/handler slots. Excess connections are closed. Suspended retired handlers retain slots until their underlying await finishes or the runtime is destroyed, so repeated timeouts cannot grow the task queue without bound.

Routes match an exact case-sensitive method and encoded path before `?`. There is no path decoding, normalization, dynamic matching, implicit HEAD fallback, or route mutation after Start. Unknown routes produce 404. Registration rejects duplicate/invalid routes. Request tables and their field/endpoint tables are immutable: `Method`, `Path`, `RawTarget`, `HttpVersion`, binary string `Body`, ordered `Headers`, separate `Trailers`, `LocalEndpoint`, and `RemoteEndpoint`.

Handlers run on the authoritative scheduler as tasks and may yield on supported network operations. They return one `HttpReplyOptions` table: optional `StatusCode` (200–599, default 200), `StatusMessage` (default empty reason phrase), ordered `Headers` pairs, and binary string/buffer `Body`. Framing fields remain runtime-owned. Invalid replies and unhandled errors generate a local `[LUI:HttpServer] HandlerFailed` diagnostic, a generic 500 response, and closure; private exception details never enter that response. HEAD and body-free status rules are preserved.

Only a fully parsed bounded request reaches a handler. Bad framing/unsupported requests yield generic 400; parser limit failures yield 413 and close. Each connection handles one request and writes one response before dispatching the next. Reads pause during handling/writing; retained pipeline input is capped to a 16 KiB read suffix. Connections close after at most 100 requests. This provides deterministic persistence and backpressure with one buffered reply per connection.

The per-request worker deadline includes idle input, parsing, scheduler delay, handler wait, and writing. It closes expired connections without promising a timeout reply. Close/expiry suppresses queued handlers and prevents suspended handler coroutines from resuming application code. Underlying awaits may finish later; their references stay valid until completion, and no stale response is written. Already executing Luau is not forcibly preempted; hosts should apply VM execution quotas for untrusted applications. Independent application tasks spawned by a handler are outside server cancellation. Runtime destruction joins the worker and releases resources without callbacks. See [decision 0029](decisions/0029-hosted-http-service.md).

## Verification

`LuiHttpTests` covers fragmentation at every split, single-byte reads, binary bodies, persistent boundaries, response body exceptions, all-prefix EOF truncation, deterministic byte mutations, malformed-request regressions, and resource limits. Its transport fixture sends two requests through the existing asynchronous TCP service and feeds bounded reads into the native parser. Build and run the `LuiHttpTests` target with CMake/CTest. [Build-Windows.ps1](../scripts/Build-Windows.ps1) and Linux CI include it.

The client fixture uses a bounded native loopback peer with a separate `network.client`-only Luau runtime. It verifies serialization, duplicate fields, binary buffers, informational responses, chunked trailers, EOF bodies, HEAD, malformed replies, truncation, limits, host policy, cancellation, and destruction. Type tests verify the reflected request/response contracts. No external network access is needed.

Hosted fixtures run with `network.server` alone, then add `network.client` for yielding-handler tests. They verify immutable binary requests, chunk trailers, duplicate replies, exact paths, HEAD/status rules, ordered persistent requests with an async handler and Instance mutation, framing rejection before route dispatch, generic failures, response/admission/server/request-count bounds, partial/handler deadlines, close during read/await, and runtime destruction. Both public services are generated from reflection and checked with positive/negative Luau type cases.

This is headless verification. No application endpoint is opened persistently, no firewall rules are changed, and no native UI is needed.

## Next implementation

Finish Windows/Linux qualification of this hosted slice, then start Foundation C with bounded UDP datagrams and the portable TLS provider design. HTTPS, streaming, proxies, broader HTTP profiles, JSON helpers, and outbound connection pooling remain follow-up.

## Exit audit

The implementation covers outbound and hosted HTTP on the shared worker, independent direction grants, strict bounded framing, fixed response serialization, ordered persistence, yielding owner-thread handlers, deadlines, backpressure, generic failures, and safe shutdown. `LuiHttpTests` and type tests provide the headless gates. Hosted Windows/Linux CI qualification is pending; this audit makes no claim of a Linux UI backend, full HTTP interoperability, TLS, or physical desktop testing.
