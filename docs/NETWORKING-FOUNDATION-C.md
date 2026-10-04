# Networking Foundation C — UDP and TLS

Status: UDP implemented; TLS remains pending. Foundation C is in progress.

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

The incremental Windows worker build and all 12 native suites passed on 2026-10-04, including UDP and generated type checks. UDP tests cover packet boundaries, empty/binary/maximum payloads, both oversized receive paths and recovery, IPv6, buffer ownership, cancellation, bind failure, quotas, grants, host policy, and teardown. Windows/Linux CI qualification is pending for this revision. This is headless networking qualification and makes no Linux UI claim.

## Remaining Foundation C work

Define and implement a portable TLS provider boundary with explicit host-owned credentials, validated client connections, bounded handshake/I/O deadlines, and cancellation/shutdown tests on Windows and Linux. Ordinary Luau must never receive native credential handles, and validation must not be silently disabled. HTTPS and TLS server use will build on that provider. UDP completion alone does not satisfy Foundation C's TLS exit requirements. WebSocket, HTTP/2, HTTP/3, streaming bodies, and outbound pooling remain later work.
