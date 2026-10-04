# 0030 — Bounded UDP datagrams

Status: accepted, 2026-10-04.

## Decision

Implement `NetworkService:BindUdp(UdpBindOptions): UdpSocket` on the existing private Asio worker and owner scheduler completion bridge. Ordinary Luau receives no native handle. Bind requires `network.server` and `network.raw`; every `SendToAsync` additionally requires `network.client`. Host server address/port policy applies at bind, and client destination address/port policy applies before send. A server that replies over UDP must declare both direction grants.

Options are `Port` (required, integer 0–65535), `Address` (default `loopback`), `Family` (`IPv4` or `IPv6`), and `MaxDatagramBytes` (integer 1–65507, default 65507). Numeric bind addresses infer their family; contradictory families fail. Semantic `loopback`/`any` default to IPv4. IPv6 sockets explicitly use IPv6-only. Paired dual-family UDP is deferred; applications can bind separate family sockets. Bind is immediate, with no interface fallback and no address reuse option. Port zero exposes the actual assigned port through `LocalEndpoint`.

`UdpSocket` is read-only and exposes `IsOpen`, `LocalEndpoint`, `ReceiveFromAsync`, `SendToAsync`, and idempotent `Close`. A receive returns a read-only `UdpDatagram` containing an owned mutable `Data: buffer` and read-only `RemoteEndpoint: NetworkEndpoint`. An empty packet returns an empty buffer, never EOF. One receive consumes one datagram; concurrent receive waiters fail with `ReceivePending`.

Send accepts a numeric same-family endpoint and binary `string | buffer`, copying the payload before yielding. DNS destination resolution, unspecified/multicast/limited-broadcast destinations, IPv4-mapped IPv6 addresses, multicast membership, broadcast mode, socket options, and UDP connection mode are outside this slice. Directed broadcast is not enabled. Sends are initiated in admission order, one at a time per socket. Completion means the local transport accepted the datagram; delivery and arrival order are not guaranteed, and LUI adds no retries, reliability, segmentation, or reassembly.

## Bounds and truncation

The proposal's illustrative 65535-byte payload ceiling is replaced with a conservative 65507-byte ceiling for both supported families. Ordinary IPv4's maximum 65535-byte packet includes at least 20 bytes of IP header and 8 bytes of UDP header ([RFC 791](https://www.rfc-editor.org/rfc/rfc791), [RFC 768](https://www.rfc-editor.org/rfc/rfc768)). The OS, address family, IP options, and path MTU can further reduce usable payload. Oversized admission and native message-size failures throw `[LUI:Network] MessageTooLarge`; an oversized send is never split into multiple user datagrams. IP fragmentation remains OS behavior.

A receive allocates `MaxDatagramBytes + 1` bytes. A copied count above the limit or a native message-size error discards the entire result and throws `MessageTooLarge`. This detects POSIX truncation as well as Windows' explicit truncation error, without publishing partial payloads. The socket remains available for the next datagram. Platform references: [Linux UDP receive behavior](https://man7.org/linux/man-pages/man7/udp.7.html), [Windows recvfrom](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-recvfrom), and [Asio async receive lifetime](https://think-async.com/Asio/asio-1.19.0/doc/asio/reference/basic_datagram_socket/async_receive_from/overload1.html).

Per runtime: at most 16 retained UDP handles. Closed handles count until collected. Per socket: one pending receive, at most 32 queued/active sends and 256 KiB queued/active payload bytes. Empty sends count toward the operation ceiling. The shared runtime ceiling of 128 await references includes native results awaiting scheduler delivery; completions drain at most 64 per pump. UDP does not introduce a second completion queue. Size/quota/policy failures occur before registering a waiter. Receive has no automatic deadline; close or runtime destruction ends the wait.

## Lifecycle

Close marks the socket closed on the owner thread and posts native close to the worker. Pending operations that have not settled complete once with `Canceled`; already queued terminal results keep their settled outcome. New operations fail with `Closed`. Recoverable datagram errors do not implicitly close the socket. Finalization closes and removes the owner handle while private operation captures keep native memory alive. Runtime destruction stops and joins the worker before releasing the VM and outstanding references. No worker invokes Luau or shows UI.

## Qualification

`tests/network/Udp.cpp` runs inside `LuiNetworkTests` on both CI platforms. It covers IPv4/IPv6, endpoint identity, binary/empty/maximum payloads, immutable records, mutable owned buffers, send buffer copying, both oversize-detection paths and recovery, grant and policy denials, invalid options/destinations, bind conflicts, concurrent receive rejection, close cancellation, retained-handle and outstanding-operation limits, and destruction with active receives. Generated definitions validate positive UDP code and reject record mutation and invalid option types. Platform qualification is recorded in [Foundation C notes](../NETWORKING-FOUNDATION-C.md).
