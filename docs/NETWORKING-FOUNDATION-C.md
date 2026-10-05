# Networking Foundation C — UDP and TLS

Status: UDP qualified; TLS/HTTPS implemented and Windows native suites passed. Foundation C's combined Windows/Linux CI exit audit is pending.

## Shipped UDP surface

The host must declare `network.raw` and `network.server` before binding; declare `network.client` as well to send or reply. The default grants are empty. Existing version 1 host network policy restricts server bind and client destination addresses/ports.

```luau
local Network = app:GetService("NetworkService")
local Socket = Network:BindUdp({Port = 0, MaxDatagramBytes = 4096})

task.spawn(function()
    local Datagram = Socket:ReceiveFromAsync()
    print("[Demo:Udp] Received", buffer.len(Datagram.Data), "bytes")
    Socket:SendToAsync(Datagram.RemoteEndpoint, Datagram.Data)
    Socket:Close()
end)
```

Bind is synchronous and defaults to IPv4 loopback. Set `Family = "IPv6"` for IPv6 loopback, or use a numeric local address whose family is inferred. `Address = "any"` explicitly binds all local interfaces in the chosen family. `Port = 0` requests an ephemeral port; `Socket.LocalEndpoint` gives its numeric address and actual port. UDP family choices are IPv4 and IPv6; paired dual-family UDP remains deferred.

`ReceiveFromAsync()` returns `{Data: buffer, RemoteEndpoint: NetworkEndpoint}`. The record and endpoint are read-only; the owned buffer can be edited. Empty datagrams are valid. `SendToAsync(Endpoint, Data)` accepts a numeric same-family destination and a binary string or buffer, copied before suspension. Async methods require a `task.spawn` or `task.defer` coroutine. Sends preserve admission order locally, with no guarantee of network delivery or arrival order.

Socket properties `IsOpen` and `LocalEndpoint` are read-only. Close is terminal and idempotent; it cancels unsettled operations once. Finalization and runtime destruction also release resources. Already queued terminal results preserve their settled outcome. Network errors use catchable `[LUI:Network] Code` strings and produce no modal dialogs.

## Limits

| Resource | Bound |
| --- | --- |
| Retained UDP handles per runtime | 16, including closed handles until collection |
| Pending receives per socket | 1 |
| Queued/active sends per socket | 32, including empty datagrams |
| Queued/active send payload per socket | 256 KiB |
| `MaxDatagramBytes` | Default 65507; configurable 1–65507 |
| Shared network await references/results | 128 per runtime |
| Scheduler completion drain | At most 64 per pump |

Oversized sends fail with `MessageTooLarge`. Oversized received datagrams are consumed and rejected with that error; partial data is never returned, and the socket remains open. The OS and path MTU may reduce usable send size. LUI never splits a send or retries a failed datagram. Receive waits have no built-in deadline; applications close the socket when canceling a wait.

DNS destinations, connected UDP, multicast/broadcast configuration, IPv4-mapped IPv6, paired dual-family UDP, and general socket options remain unsupported. [Decision 0030](decisions/0030-bounded-udp-datagrams.md) states the complete policy, ownership, truncation, and lifecycle contract.

## Qualification

The incremental Windows worker build and all 12 native suites passed on 2026-10-04, including UDP and generated type checks. [Windows and Linux CI](https://github.com/gmoddev/LUI/actions/runs/37189268030) passed at `7fbbe5d03ecf82e70f8efef61eefec7c1882c3d7` on the same date. CI also passed CLI, preview/editor checks, and generated API/schema consistency. UDP tests cover packet boundaries, empty/binary/maximum payloads, both oversized receive paths and recovery, IPv6, buffer ownership, cancellation, bind failure, quotas, grants, host policy, and teardown. This is headless networking qualification and makes no Linux UI claim. This UDP audit does not complete Foundation C's TLS requirements.

## Shipped TLS and HTTPS

HTTPS URLs now work with `HttpService:RequestAsync` and `GetAsync`. The default port is 443. Client calls still require only `network.client`, obey host policy, and share the HTTP parser, worker, deadline, cancellation barrier, and 32-call/result ceiling. TLS always verifies certificate chain, dates, server purpose, and the original URL DNS/IP identity before writing an HTTP request. DNS requests send SNI. TLS 1.2 is the minimum and TLS 1.3 is supported; there is no Luau verification bypass or plaintext retry.

The private provider uses OpenSSL. Windows builds pinned 3.5.9 statically in the CMake dependency cache and imports trusted OS roots; Linux uses distribution OpenSSL 3 and system trust paths. Windows also checks the Disallowed stores. No certificate store is modified. Validation does not currently fetch AIA or live revocation data. Certificates are bounded to a 128 KiB peer list and eight intermediate certificates. Tickets/session caching, compression, and renegotiation are disabled. [Decision 0031](decisions/0031-portable-tls-and-https.md) records the exact provider and trust profile.

### Host credentials

Before scripts/network use, a native host may call `Lui_SetTlsOptions` with a size/version-checked `LuiTlsOptionsV1`. Its optional PEM CA roots replace system trust roots; an optional PKCS#12 server certificate/key and length-delimited password configure server mode. Inputs are synchronously parsed, caller buffers need not remain alive, and configuration succeeds only once. Trust/credential byte limits are 256 KiB each, with a 1024-byte password limit. Invalid inputs fail nonintrusively with a stable `[LUI:Tls]` error, without logging secret data.

Once the host has supplied credentials, application Luau can create a secure server:

```luau
local Server = app:GetService("HttpServerService"):CreateServer({Port = 8443, TLS = true})
Server:Route("GET", "/health", function(Request)
    return {Body = "ready"}
end)
Server:Start()
```

`network.server` is sufficient. Missing credentials fail before bind with `TlsCredentialRequired`; `TLS` must be Boolean. Each accepted TLS handshake retains an existing hosted connection/session slot and uses `TimeoutMs`, followed by a fresh per-request deadline after handshake. Plain HTTP remains the explicit default. The current CLI/WinUI manifest does not load TLS credentials; use the native host API for secure server credentials.

### Shutdown and qualification

Abrupt TLS EOF fails close-delimited responses as `UnexpectedEof`. Complete length/chunk-framed responses settle before shutdown cleanup; cleanup remains bounded by the existing deadline and slot, and its cancellation/timeout preserves the already authenticated response. Servers attempt close_notify within their request deadline. Server close, client cancellation, and runtime destruction abort unsettled work and prevent late route delivery. Provider calls and host credential parsing are not individually preempted by async timers.

All 12 Windows native suites passed on 2026-10-04, including trusted binary HTTPS and hosted IPv4/IPv6 TLS, DNS SNI/IP identity, untrusted/wrong-name/expired certificates, TLS 1.2 and legacy refusal, handshake stalls, truncated streams, shutdown stalls, close/cancellation, owner delivery, and runtime destruction. Fixtures are explicitly public synthetic credentials and require no OS installation. Generated definitions validate the server's `TLS` option. Windows/Linux CI qualification is pending for this TLS revision.

## Deferred extensions

Schannel-specific provider integration, mTLS, live revocation/AIA policy, named credentials, hardware/platform key stores, PEM private-key loading, CLI/WinUI credential manifests, direct Luau TLS sockets, WebSocket, HTTP/2, HTTP/3, streaming bodies, and outbound pooling remain future work. These do not enter the current Foundation C profile; its exit audit covers bounded UDP and the defined TLS/HTTPS provider, client/server credentials, and lifecycle.
