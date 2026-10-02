# Networking Foundation A: TCP first slice

The portable runtime now owns a TCP service and a yielding task scheduler. This is an initial transport slice. HTTP, UDP, TLS, dual-family listeners, connection signals, and per-host destination restrictions remain future work.

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

`ListenTcp` binds immediately. Its address defaults to `loopback`; `any` or a numeric local address must be requested explicitly. `Family` is `IPv4` by default or `IPv6`. IPv6 listeners set the IPv6-only socket option explicitly. The service rejects `DualStack` until paired listeners and same-port rollback are implemented. Port zero selects an ephemeral port and exposes the chosen port through the listener.

`ConnectTcp` resolves an address or hostname asynchronously and has a ten-second deadline. Its address defaults to IPv4 loopback. The current connection attempt uses Asio's sequential endpoint selection; parallel dual-family dialing remains future work.

`AcceptAsync`, `ConnectTcp`, `ReadAsync`, `ReadExactAsync`, and `WriteAsync` suspend only a `task.spawn` or `task.defer` coroutine. Calling them from a top-level script or a synchronous signal callback raises an error telling the caller to schedule a task. I/O and DNS happen outside the Luau owner thread; the next `Lui_Pump` delivers each result on that thread. The host must keep pumping while asynchronous work is active.

TCP reads return Luau buffers. `ReadAsync` returns one to the requested maximum bytes, or `nil` on clean EOF. Its default and maximum read size is 64 KiB. `ReadExactAsync` returns the requested number of bytes or raises `UnexpectedEof` if the stream ends early. One connection permits one active read. `WriteAsync` accepts a string or buffer, writes all bytes in order, and resumes after the OS accepts the write or with an error. A write completion does not mean the peer application consumed the bytes. `Shutdown` requests `Read`, `Write`, or `Both`; `Close` is idempotent and cancels pending I/O.

Errors use portable prefixes such as `[LUI:Network] Canceled`, `ConnectionRefused`, `AddressInUse`, `PermissionDenied`, `NameNotFound`, `TimedOut`, `UnexpectedEof`, or `IoError`. They are local Luau errors and structured host logs, never modal dialogs. The first slice caps listeners at 16, connections at 64, active async operations at 128, each read at 64 KiB, and each connection's queued writes at 256 KiB. The current limits are runtime-wide rather than configurable host policy.

## Implementation and qualification

Standalone Asio is fetched at commit `231cb29bab30f82712fcd54faaea42424cc6e710` (release 1.36.0), under the Boost Software License 1.0. It is header-only in this build and uses the platform I/O engine internally. The runtime keeps its sockets private. Worker handlers put bounded result records in the runtime queue; only `Lui_Pump` resumes Luau. Runtime destruction stops and joins the transport worker before closing the VM and unreferences suspended tasks.

`LuiNetworkTests` covers grants, a loopback round trip, endpoints, binary buffer reads, orderly and premature EOF, idempotent close, cancellation of pending reads and accepts, and runtime teardown with a suspended task. Windows and Linux headless CI run this same test. The generated API definitions and reflection schema include only this implemented TCP surface.
