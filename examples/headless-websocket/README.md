# WebSocket clients, from minimal to advanced

This example targets the unreleased preview.3 candidate. It is not part of the
published preview.2 SDK. The native implementation is being integrated and
qualified on this branch; donor results do not qualify a candidate package.

WebSocket uses the same application runtime in graphical and headless hosts.
It needs only `network.client`, explicit host authority and a scheduled task.
Connect, send and receive use the existing yielding async methods:

```lua
task.spawn(function()
    local Socket = app:GetService("WebSocketService"):ConnectAsync("wss://example.com/socket")
    Socket:SendAsync("Hello")
    local Message = Socket:ReceiveAsync()
    if Message then
        print(Message.Data)
    end
    Socket:Close()
end)
```

`ReceiveAsync` returns one immutable `{Data: string, Binary: boolean}` message,
or nil after an orderly close. Messages received before the first call wait in
a bounded inbox, so an early peer message cannot race listener attachment.
Use normal `pcall` for expected connection/transport/protocol failures. `Close`
starts a bounded graceful handshake; `Cancel` aborts immediately. Await a final
`ReceiveAsync` when a finite tool needs to observe clean peer closure.

Run the local synthetic peer in one terminal:

```text
python examples/headless-websocket/Fixture.py
```

Build `LuiHeadlessHost` and run the minimal app in another terminal:

```text
LuiHeadlessHost --project examples/headless-websocket/minimal.lui.json --grant network.client --http-origin http://127.0.0.1:18081 --diagnostics development
```

Use `advanced.lui.json` for binary data, subprotocol selection, cancellation and
smaller limits. Use `lui.json` for HTTP configuration, lazy hash-pinned optional
Json, and JSON messages over the same WebSocket service. The latter imports
Json through the existing resolver and lock; the minimal app has no dependency.
The CLI also accepts `run <project> --mode headless --host <executable>` with the
same host grant and origin options. No external account or Internet is needed.

HTTP origin authority maps `ws` to `http` and `wss` to `https`; it is exact in
scheme, host and port. The project further narrows traffic to loopback port
18081. Do not grant `network.raw` merely to use WebSocket. A capability in the
project is a request, never a self-grant. Real hosts should set their own exact
service origins and TLS trust policy. There is no certificate bypass or redirect
following.

For binary sending, pass a buffer, or `SendAsync(Bytes, true)` for a binary string.
Received Data always remains an immutable string; Binary preserves its wire
message type. Advanced options stay on the same `ConnectAsync(Url, Options)`:
timeouts, inbox/outbox bounds, subprotocols, request headers, Origin and an existing
HTTP cancellation token. Lower limits narrow runtime ceilings. Native queues
are bounded; rejected sends are not silently dropped and a flooded inbox closes
the connection. Applications decide retries, reconnection, JSON schemas and
credentials. Ping/pong framing, masking and fragments belong to the runtime.

`State`, `Subprotocol`, `CloseCode` and `CloseReason` expose the selected protocol
and terminal state without native handles. Peer reasons and message payloads are
untrusted application data; framework diagnostics contain stable error codes
and numeric metrics. Avoid printing credentials or private messages. Default
headless diagnostics are Restricted; these examples explicitly opt into prints.
The initial profile admits eight retained socket handles, including closed ones
until the VM releases them. Drop obsolete references; retaining closed handles
can produce `ResourceExhausted` on a later connect. Closing does not force a VM
collection. Whole-runtime retirement releases the generation's resources.

This example exercises the native client; its Python fixture is a test peer.
The candidate's separate websocket-echo example uses LUI's hosted RouteWebSocket
API. Browser networking, HTTP/2/3 upgrades, compression and automatic reconnect
remain outside this native profile. See the candidate networking guide for limits
and the separately recorded qualification scope.
