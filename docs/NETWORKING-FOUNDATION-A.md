# Networking Foundation A: TCP

The portable runtime now owns a TCP service and a yielding task scheduler. Networking Foundation A now includes paired listeners, host policy, paced outbound address-family dialing, and connection lifecycle signals. HTTP, UDP, and TLS remain future work.

## Use

The host must declare `network.server` and `network.raw` to listen, or `network.client` and `network.raw` to connect. The WinUI manifest uses those exact strings in its `Capabilities` array. The default grant set is empty; the isolated preview host still rejects privileged manifests.

```lua
local Network = app:GetService("NetworkService")
local Listener = Network:ListenTcp({ Address = "loopback", Family = "IPv4", Port = 0 })

task.spawn(function()
    local Peer = Listener:AcceptAsync()
    local Bytes = Peer:ReadAsync(4096)
    if Bytes then
        Peer:WriteAsync(Bytes)
    end
    Peer:Close()
end)

print("[LUI:Network] Listening on " .. Listener.Port)
```

`ListenTcp` binds immediately. Its address defaults to `loopback`; `any` or a numeric local address must be requested explicitly. `Family` defaults to `IPv4` for semantic addresses. A numeric IPv6 literal implies `IPv6`; an explicit contradictory family is rejected. `DualStack` accepts `loopback` or `any` and creates an IPv6-only and an IPv4 listener on one shared port. Both must bind or the whole operation fails. Port zero selects an ephemeral port; a collision on the second bind is retried up to 16 times. `BoundEndpoints` reports both actual addresses and the shared port. If IPv6 or IPv4 is unavailable, `DualStack` fails without falling back to one family.

The host can add a `NetworkPolicy` object to a version 1 WinUI manifest. It narrows the declared grants and cannot be changed after scripts start:

```json
{
  "NetworkPolicy": {
    "ClientLoopbackOnly": true,
    "ServerLoopbackOnly": true,
    "ClientPortMin": 8000,
    "ClientPortMax": 8999,
    "ServerPortMin": 8000,
    "ServerPortMax": 8999
  }
}
```

Each min/max pair must both be zero or omitted for unrestricted ports, or both be in `1..65535` with min no greater than max. The client range applies to the remote destination port; the server range applies to the local listener port. A restricted server port range rejects port zero; the host must choose a port in range. Server loopback policy rejects `any` and non-loopback numeric bind addresses. Client loopback policy accepts loopback numeric addresses or `localhost`; it rejects other names before DNS and filters `localhost` results after resolution. Port policy is checked before binding or resolution. Denials raise `[LUI:Network] PolicyDenied`. The host-only `Lui_SetNetworkPolicy` API uses a versioned `LuiNetworkPolicyV1` struct; omitted policy leaves granted network access unrestricted by these additional rules.

`ConnectTcp` resolves an address or hostname asynchronously and has a ten-second deadline. Its address defaults to IPv4 loopback. After resolution, it keeps at most 16 candidate endpoints, alternates IPv4 and IPv6 in the resolver's first-family order, and starts attempts 250 ms apart. At most two sockets dial at once. The first successful connection wins; other attempts are canceled. Failure of all candidates reports the last portable connection error. This is informed by [Happy Eyeballs v2](https://www.rfc-editor.org/rfc/rfc8305.html), but resolution still waits for Asio's combined result before dialing, so it does not implement the RFC's separate A/AAAA query timing.

`AcceptAsync`, `ConnectTcp`, `ReadAsync`, `ReadExactAsync`, and `WriteAsync` suspend only a `task.spawn` or `task.defer` coroutine. Calling them from a top-level script or a synchronous signal callback raises an error telling the caller to schedule a task. I/O and DNS happen outside the Luau owner thread; the next `Lui_Pump` delivers each result on that thread. The host must keep pumping while asynchronous work is active.

TCP reads return Luau buffers. `ReadAsync` returns one to the requested maximum bytes, or `nil` on clean EOF. Its default and maximum read size is 64 KiB. `ReadExactAsync` returns the requested number of bytes or raises `UnexpectedEof` if the stream ends early. One connection permits one active read. `WriteAsync` accepts a string or buffer, writes all bytes in order, and resumes after the OS accepts the write or with an error. A write completion does not mean the peer application consumed the bytes. `Shutdown` requests `Read`, `Write`, or `Both`; `Close` is idempotent and cancels pending I/O.

`TcpConnection.Closed` is a no-argument signal. It fires once on explicit `Close`, observed remote EOF, or a terminal read/write error. `IsOpen` is false by the time the callback runs. Delivery is queued to the next owner-thread `Lui_Pump`; callbacks never run on the network worker. A remote close cannot be observed until an application read or write detects it. `Shutdown` alone does not fire `Closed`. Collection of a still-open connection or runtime teardown disconnects remaining callbacks without firing the signal. A returned subscription has an idempotent `Disconnect` method; a disconnected callback is not called. Each connection permits up to 64 active `Closed` listeners. Callback errors are logged with `[LUI:Network]` and do not open a dialog.

Errors use portable prefixes such as `[LUI:Network] Canceled`, `ConnectionRefused`, `AddressInUse`, `PermissionDenied`, `PolicyDenied`, `NameNotFound`, `TimedOut`, `UnexpectedEof`, or `IoError`. They are local Luau errors and structured host logs, never modal dialogs. The runtime caps listeners at 16, connections at 64, active async operations at 128, each read at 64 KiB, and each connection's queued writes at 256 KiB. These resource limits are runtime-wide and are separate from the host address/port policy.

## Implementation and qualification

Standalone Asio is fetched at commit `231cb29bab30f82712fcd54faaea42424cc6e710` (release 1.36.0), under the Boost Software License 1.0. It is header-only in this build and uses the platform I/O engine internally. The runtime keeps its sockets private. Worker handlers put bounded result records in the runtime queue; only `Lui_Pump` resumes Luau. Runtime destruction stops and joins the transport worker before closing the VM and unreferences suspended tasks.

`LuiNetworkTests` covers grants, a loopback round trip, endpoints, binary buffer reads, orderly and premature EOF, idempotent close, cancellation of pending reads and accepts, paired IPv4/IPv6 binding and rollback, policy denials, local and remote close signals, hostname fallback, and runtime teardown with a suspended task. The [Windows and Linux headless CI run](https://github.com/gmoddev/LUI/actions/runs/37174780149) passed on 2026-10-03. The generated API definitions and reflection schema include only this implemented TCP surface.
