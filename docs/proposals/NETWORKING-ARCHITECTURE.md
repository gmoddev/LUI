> **Status: supplied design proposal, 2026-10-01.** This document is preserved for planning. It does not describe implemented LUI APIs or override [SPEC.md](../SPEC.md) and [the networking decision](../decisions/0022-networking-scope.md). Platform claims and numeric defaults require qualification before implementation. The illustrative private-network address was changed to the documentation-only `192.0.2.10` address for the public repository.

# LUI Networking, Sockets, and Hosted Endpoint Architecture

## 1. Purpose

LUI should provide first-class networking suitable for normal desktop applications without requiring native extensions.

The networking stack should support three progressively higher-level surfaces:

```text
                     Application Luau
                            │
             ┌──────────────┼───────────────┐
             ▼              ▼               ▼
        HttpService   HttpServerService  NetworkService
             │              │               │
      HTTP client +      HTTP server      TCP / UDP
      JSON helpers       + routing        primitives
             │              │               │
             └──────────────┼───────────────┘
                            ▼
                       LUI NetCore
                            │
                   async transport layer
                            │
                 ┌──────────┴──────────┐
                 ▼                     ▼
              Windows                Linux
              Winsock                 POSIX
               IOCP                   epoll
```

Networking belongs to the **LUI runtime/platform-services layer**.

It must not belong to:

- WinUI;
- GTK;
- the UI backend contract;
- VS Code preview rendering;
- application-specific native plugins.

The same networking API should work identically whether the application's UI backend is WinUI, GTK, or eventually macOS.

---

# 2. Core design position

LUI should expose three levels.

### `HttpService`

For outbound HTTP:

```lua
local HttpService = app:GetService("HttpService")

local Response = HttpService:GetAsync(
    "https://example.com/api/status"
)
```

It also owns portable JSON and URL encoding helpers.

### `HttpServerService`

For hosting application endpoints:

```lua
local HttpServerService = app:GetService("HttpServerService")

local Server = HttpServerService:CreateServer({
    Address = "loopback",
    Port = 8080,
})

Server:Route("GET", "/status", function(Request)
    return {
        StatusCode = 200,
        Body = "OK",
    }
end)

Server:Start()
```

### `NetworkService`

For applications that actually need raw networking:

```lua
local NetworkService = app:GetService("NetworkService")

local Listener = NetworkService:ListenTcp({
    Address = "192.0.2.10",
    Port = 8074,
})
```

This exposes TCP/UDP semantics without exposing operating-system socket handles.

---

# 3. Platform research

## Windows

Windows networking should ultimately use Winsock 2.2. Microsoft's current guidance is to use protocol-independent address resolution, IPv6-capable APIs, and asynchronous I/O for servers; IO completion ports are the high-performance asynchronous mechanism used by Winsock server implementations.

Conceptually:

```text
NetworkService
      ↓
NetCore
      ↓
Winsock 2
      ↓
overlapped I/O
      ↓
IOCP
```

A Windows server performs the familiar:

```text
socket
  ↓
bind
  ↓
listen
  ↓
AcceptEx / accept
  ↓
async recv/send
```

but none of this is visible to Luau.

---

## Linux

Linux uses normal POSIX sockets:

```text
socket
  ↓
bind
  ↓
listen
  ↓
accept4
  ↓
nonblocking read/write
```

with an event demultiplexer such as `epoll`.

Linux specifically supports accepting sockets with `SOCK_NONBLOCK` and `SOCK_CLOEXEC` directly through `accept4`, which avoids additional state-changing calls after an accept.

For scalable asynchronous operation:

```text
NetworkService
      ↓
NetCore
      ↓
nonblocking sockets
      ↓
epoll
```

Linux documents nonblocking socket behavior through `O_NONBLOCK` and readiness mechanisms such as `epoll`.

---

# 4. Recommended transport implementation

I would **not** hand-write separate IOCP and epoll engines initially.

Use a pinned version of **standalone Asio** internally.

Asio already maps asynchronous networking onto:

```text
Windows
    → overlapped I/O + IOCP

Linux
    → epoll

future macOS/BSD
    → kqueue
```

Its current implementation documentation confirms IOCP on Windows and epoll on Linux.

Architecture:

```text
                    LUI NetCore
                        │
                  standalone Asio
                        │
          ┌─────────────┴─────────────┐
          ▼                           ▼
       Windows                      Linux
       Winsock                       sockets
       IOCP                          epoll
```

This is preferable to maintaining two highly sensitive asynchronous networking engines inside LUI.

LUI still owns all public networking semantics.

Asio is only an implementation detail.

---

# 5. Do not expose native sockets

Normal Luau never receives:

```text
SOCKET
HANDLE
OVERLAPPED*
file descriptor
sockaddr*
epoll fd
IOCP handle
```

A TCP connection is:

```text
TcpConnection
```

not:

```text
int fd
```

Native handles may eventually be available through an explicit unsafe/platform interop layer, but not through `NetworkService`.

---

# 6. Binary data

Raw network APIs should use Luau's native `buffer` type.

Luau already has an implemented fixed-size byte-buffer type and corresponding C API, making it appropriate for network packets without inventing a LUI-specific byte-array object.

Example:

```lua
local Data = Connection:ReadAsync(4096)

if Data then
    print(buffer.len(Data))
end
```

Writes accept:

```text
buffer
string
```

Strings are convenient for text protocols.

Buffers are preferred for binary protocols.

---

# 7. Address semantics

This must be canonicalized by LUI rather than inherited from the operating system.

Listeners accept:

```lua
{
    Address = "loopback",
    Port = 8080,
}
```

or:

```lua
{
    Address = "any",
    Port = 8080,
}
```

or a specific numeric address:

```lua
{
    Address = "192.0.2.10",
    Port = 8080,
}
```

IPv6 literals are also supported:

```lua
{
    Address = "::1",
    Port = 8080,
}
```

---

# 8. Listener address definitions

## `loopback`

Safe default.

Equivalent logically to:

```text
127.0.0.1
::1
```

The service is reachable only by the current machine.

---

## `any`

Explicitly exposes the listener on all available interfaces.

Equivalent logically to:

```text
0.0.0.0
::
```

This should **never be the default**.

Example:

```lua
HttpServerService:CreateServer({
    Address = "any",
    Port = 8080,
})
```

is a deliberate decision to expose the server beyond localhost.

---

## Specific IP address

Example:

```text
192.0.2.10
```

means:

> Bind only to the local interface carrying exactly `192.0.2.10`.

If that address is not locally assigned, bind fails.

Windows reports the equivalent of `WSAEADDRNOTAVAIL` for an invalid local bind address, while Linux similarly reports `EADDRNOTAVAIL` if the requested address is not local.

LUI maps both to:

```text
NetworkError.AddressNotAvailable
```

There is no fallback to another interface.

---

# 9. IPv4 and IPv6 must not inherit OS defaults

This is an important portability problem.

Windows IPv6 sockets default to `IPV6_V6ONLY = true`.

Linux normally defaults to `IPV6_V6ONLY = false`, controlled through its `bindv6only` system setting.

Therefore LUI must **never rely on the operating-system default**.

For:

```lua
Address = "any"
Family = "DualStack"
```

LUI should internally use:

```text
IPv4 socket:
    0.0.0.0:PORT

IPv6 socket:
    [::]:PORT
    IPV6_V6ONLY = true
```

This creates deterministic behavior on Windows and Linux.

Likewise:

```lua
Address = "loopback"
Family = "DualStack"
```

becomes:

```text
127.0.0.1:PORT
[::1]:PORT
```

with separate sockets.

---

# 10. Address-family configuration

For semantic addresses:

```lua
Family = Enum.NetworkFamily.DualStack
```

should be the default.

Options:

```text
IPv4
IPv6
DualStack
```

For explicit literals:

```text
192.0.2.10
```

implies IPv4.

```text
2001:db8::1
```

implies IPv6.

Supplying a contradictory `Family` is an error.

---

# 11. Port semantics

Valid ports:

```text
0–65535
```

### Port `0`

Means:

> Ask the operating system to allocate an available ephemeral port.

The actual port becomes available through:

```lua
Listener.Port
```

or:

```lua
Server.Port
```

For dual-stack listeners, LUI must ensure both underlying listeners use the **same selected port**.

---

# 12. Example `192.0.2.10:74`

This should be valid LUI syntax:

```lua
local Listener = NetworkService:ListenTcp({
    Address = "192.0.2.10",
    Port = 74,
})
```

but it has different OS permission behavior.

Linux defines ports below 1024 as privileged and requires the process to have `CAP_NET_BIND_SERVICE` in the applicable user/network namespace.

Therefore an ordinary Linux user would receive something equivalent to:

```text
NetworkError.PermissionDenied

Unable to bind 192.0.2.10:74.
The operating system denied permission.
```

Windows does not use the Unix `<1024` privilege model for raw Winsock listeners.

For maximum cross-platform portability, ordinary applications should generally choose:

```text
1024–65535
```

unless deployment specifically provisions low-port capability.

---

# 13. TCP listener API

Canonical initial API:

```lua
local Listener = NetworkService:ListenTcp({
    Address = "loopback",
    Family = Enum.NetworkFamily.DualStack,
    Port = 8080,

    Backlog = 128,
    MaxConnections = 256,
})
```

Properties:

```text
TcpListener

IsListening: boolean [readonly]
Port: number [readonly]
BoundEndpoints: {NetworkEndpoint} [readonly]

AcceptAsync(): TcpConnection
Close(): ()
```

`Close()` is idempotent.

---

# 14. Why `AcceptAsync`, not `ClientConnected`

Do not make the fundamental TCP API:

```lua
Listener.ClientConnected:Connect(...)
```

The accept operation itself represents backpressure.

Use:

```lua
task.spawn(function()
    while Listener.IsListening do
        local Connection = Listener:AcceptAsync()

        task.spawn(function()
            handleConnection(Connection)
        end)
    end
end)
```

This gives the application explicit control over how quickly connections are accepted.

A convenience signal can be added later, but it must not become the fundamental transport semantic.

---

# 15. TCP is a byte stream

This distinction is critical.

Do not expose a fundamental event named:

```text
DataReceived
```

because TCP does **not** preserve message boundaries.

If a peer writes:

```text
"HELLO"
"WORLD"
```

the receiving application may observe:

```text
"HELLOWORLD"
```

or:

```text
"HE"
"LL"
"OWORLD"
```

or any other valid stream segmentation.

LUI must not encourage application developers to assume packet framing.

Instead:

```lua
local Data = Connection:ReadAsync(4096)
```

means:

> Resume when between 1 and 4096 bytes are available, or return `nil` when the peer performs an orderly end-of-stream.

---

# 16. TcpConnection

```text
TcpConnection

LocalEndpoint: NetworkEndpoint [readonly]
RemoteEndpoint: NetworkEndpoint [readonly]
IsOpen: boolean [readonly]

ReadAsync(MaxBytes: number?): buffer?
ReadExactAsync(Bytes: number): buffer
WriteAsync(Data: string | buffer): ()
Shutdown(Direction): ()
Close(): ()

Closed: Signal
```

---

# 17. Read semantics

```lua
local Data = Connection:ReadAsync()
```

Default maximum:

```text
64 KiB
```

Rules:

- Returns a non-empty `buffer` when bytes are received.
- Returns `nil` after a clean remote EOF.
- Throws on connection reset or other network errors.
- Never blocks the LUI UI/VM thread.
- Maximum requested read size is bounded by runtime policy.
- Multiple simultaneous reads on one connection are rejected.

`ReadExactAsync(1024)` waits for exactly 1024 bytes.

If EOF occurs first:

```text
NetworkError.UnexpectedEof
```

---

# 18. Write semantics

```lua
Connection:WriteAsync(Data)
```

means:

> Write the complete supplied byte sequence to the transport stream or fail.

Completion does **not** mean the peer application consumed the data.

It only means LUI completed transport submission according to the connection's write contract.

Writes are ordered.

Given:

```lua
task.spawn(function()
    Connection:WriteAsync("A")
end)

task.spawn(function()
    Connection:WriteAsync("B")
end)
```

the runtime serializes complete write operations rather than interleaving their bytes.

Queued output has a hard byte limit.

---

# 19. Backpressure

Every connection must have bounded pending output.

Initial proposed default:

```text
MaxQueuedWriteBytes = 4 MiB
```

If the application attempts to exceed it:

```text
NetworkError.BackpressureLimit
```

The runtime must never accumulate arbitrary pending data because the peer stopped reading.

A slower peer must apply pressure back to the Luau task.

---

# 20. TCP close semantics

### Remote graceful close

```lua
ReadAsync()
```

returns:

```lua
nil
```

after all already-received bytes have been consumed.

### Remote reset

Throws:

```text
NetworkError.ConnectionReset
```

### Local `Close()`

Cancels pending operations.

Waiting tasks resume with:

```text
NetworkError.Cancelled
```

`Close()` itself is idempotent.

---

# 21. Half-close

Support:

```lua
Connection:Shutdown(Enum.NetworkShutdown.Write)
```

Semantics:

```text
Read
Write
Both
```

This maps to TCP half-close semantics where supported.

Example:

```lua
Connection:WriteAsync(Request)
Connection:Shutdown(Enum.NetworkShutdown.Write)

local Response = Connection:ReadAsync()
```

---

# 22. UDP

UDP should also exist, but retain datagram semantics.

```lua
local Socket = NetworkService:BindUdp({
    Address = "loopback",
    Port = 9000,
})
```

API:

```text
UdpSocket

LocalEndpoint

ReceiveFromAsync(): Datagram
SendToAsync(Endpoint, Data): ()
Close(): ()
```

Datagram:

```text
Data: buffer
RemoteEndpoint: NetworkEndpoint
```

Unlike TCP:

> One UDP send corresponds to one datagram.

LUI should preserve that distinction.

---

# 23. UDP size limits

Initial LUI UDP should limit ordinary datagrams to:

```text
65,535 bytes
```

and should never silently split one user datagram into multiple UDP messages.

An oversized send produces:

```text
NetworkError.MessageTooLarge
```

Application-level fragmentation remains an application/protocol concern.

---

# 24. Connecting TCP clients

```lua
local Connection = NetworkService:ConnectTcp({
    Host = "example.com",
    Port = 443,
    Timeout = 10,
})
```

Host may be:

```text
hostname
IPv4 literal
IPv6 literal
```

Unlike listener binding, DNS names are appropriate for outbound connections.

Windows explicitly recommends protocol-independent address lookup using `getaddrinfo`, while POSIX provides the equivalent API.

---

# 25. IPv4/IPv6 connection selection

When a hostname resolves to both IPv4 and IPv6, LUI should implement a **Happy Eyeballs v2-style connection strategy** rather than waiting sequentially for one address family to fail.

RFC 8305 describes concurrent/staggered connection attempts so broken IPv6 or IPv4 paths do not impose long user-visible delays.

This remains an internal behavior.

Luau simply sees:

```lua
NetworkService:ConnectTcp(...)
```

---

# 26. HttpServerService

Most applications should never touch raw TCP.

For REST APIs, callbacks, device-control endpoints, local control planes, etc.:

```lua
local HttpServerService =
    app:GetService("HttpServerService")
```

Example:

```lua
local Server = HttpServerService:CreateServer({
    Address = "loopback",
    Port = 8080,
})

Server:Route("GET", "/status", function(Request)
    return {
        StatusCode = 200,
        Headers = {
            ["Content-Type"] = "application/json",
        },
        Body = '{"ok":true}',
    }
end)

Server:Start()
```

---

# 27. HttpServer lifecycle

```text
HttpServer

IsRunning
Port
BoundEndpoints

Route(Method, Path, Handler)
Start()
StopAsync(Options?)
Close()
```

Lifecycle:

```text
Created
  ↓
routes registered
  ↓
Start()
  ↓
Running
  ↓
StopAsync()
  ↓
Stopped
```

Routes may not be mutated while the server is running in the initial implementation.

This keeps dispatch deterministic.

Dynamic route mutation can be added later if justified.

---

# 28. Bind defaults

The secure default is:

```lua
Address = "loopback"
```

Not:

```lua
Address = "any"
```

Therefore:

```lua
HttpServerService:CreateServer({
    Port = 8080,
})
```

is local-only.

Making an application network-visible requires explicitly writing:

```lua
Address = "any"
```

or a specific non-loopback local address.

---

# 29. HTTP version

Initial server:

```text
HTTP/1.1
```

only.

Do not implement HTTP/2 or HTTP/3 in the first networking foundation.

HTTP/2 multiplexing and HTTP/3/QUIC introduce substantially different transport behavior; RFC 9110 explicitly treats HTTP/1.1, HTTP/2, and HTTP/3 as different protocol versions sharing common HTTP semantics.

The public request/response model should nevertheless avoid assumptions that prevent HTTP/2 later.

---

# 30. HTTP parser

Do **not** write a hand-rolled HTTP parser.

Recommended implementation:

```text
Asio
  ↓
strict HTTP/1 parser
  ↓
LUI request model
```

A reasonable lightweight candidate is a pinned `llhttp`, which produces embeddable C parser output and is intended as a maintainable successor to Node's older HTTP parser.

Alternatively, Boost.Beast is technically strong and provides HTTP/1 and WebSocket parsing on top of Boost.Asio, but it requires the broader Boost stack rather than standalone Asio.

For LUI's existing dependency philosophy, my preference is:

> **standalone Asio + pinned strict HTTP parser**

rather than importing Boost solely for networking.

---

# 31. HTTP request model

Handler input:

```text
HttpRequest

Method: string
Path: string
RawTarget: string
HttpVersion: string

Headers: HttpHeaders
Body: buffer

RemoteEndpoint: NetworkEndpoint
LocalEndpoint: NetworkEndpoint
```

Request objects are immutable.

---

# 32. Headers

HTTP field names are case-insensitive and headers may legally appear more than once.

Therefore do not expose request headers as a simple:

```lua
[string]: string
```

table.

Use:

```lua
Request.Headers:Get("Content-Type")
Request.Headers:GetAll("Set-Cookie")
Request.Headers:Has("Authorization")
```

`Get()` returns the canonical combined value only where safe.

`GetAll()` preserves multiple field occurrences.

---

# 33. Request bodies

Initial HTTP server uses **bounded buffered bodies**.

Example:

```lua
local Body = Request.Body
```

where:

```text
Body: buffer
```

Default maximum request body:

```text
8 MiB
```

Applications needing large streaming uploads can use a later streaming-request API.

Do not expose unbounded request buffering.

---

# 34. Response model

Handler returns:

```lua
{
    StatusCode = 200,

    Headers = {
        ["Content-Type"] = "text/plain",
    },

    Body = "Hello",
}
```

Body accepts:

```text
nil
string
buffer
```

If no `Content-Length` or `Transfer-Encoding` is explicitly supplied, LUI determines correct framing automatically.

Applications should almost never manually manage HTTP framing headers.

---

# 35. JSON convenience

Because `HttpService` already owns JSON:

```lua
local HttpService = app:GetService("HttpService")
```

a route can simply do:

```lua
Server:Route("GET", "/status", function(Request)
    return {
        StatusCode = 200,
        Headers = {
            ["Content-Type"] = "application/json",
        },
        Body = HttpService:JSONEncode({
            ok = true,
            version = "1.0",
        }),
    }
end)
```

A later convenience helper can reduce this to:

```lua
return HttpServerService:JSON({
    ok = true
})
```

without changing the underlying response model.

---

# 36. Routing semantics

Initial routing should be intentionally simple.

```lua
Server:Route("GET", "/status", Handler)
Server:Route("POST", "/launch", Handler)
```

Initial route paths are **exact matches**.

No implicit regex.

No filesystem-style normalization.

No hidden wildcard language.

So:

```text
/users/123
```

does not match:

```text
/users/124
```

Parameterized routing can be added later as a library feature:

```text
/users/{id}
```

after its normalization and conflict semantics are deliberately specified.

Do not make routing syntax part of Foundation 2 unnecessarily.

---

# 37. Automatic HTTP responses

Canonical behavior:

```text
valid request + matching route
    → execute route

valid request + no matching path
    → 404

known path + wrong method
    → 405

malformed HTTP
    → 400

target too long
    → 414

request body too large
    → 413

headers too large
    → 431

server overloaded
    → 503
```

These errors occur before Luau where possible.

---

# 38. Strict HTTP parsing

LUI should intentionally choose stricter server behavior where RFC flexibility can create ambiguity.

RFC 9112 explicitly notes request-smuggling risk when different recipients interpret framing differently. It permits a server to reject a message containing both `Content-Length` and `Transfer-Encoding`, and requires closing the connection after handling such ambiguity.

LUI should take the strict option:

```text
Content-Length + Transfer-Encoding
    → 400
    → close connection
```

Also reject:

- malformed `Content-Length`;
- conflicting duplicate `Content-Length`;
- obsolete folded headers;
- invalid request-line framing;
- unsupported transfer codings;
- malformed chunk framing.

Do not try to be lenient.

---

# 39. Keep-alive

HTTP/1.1 persistent connections should be supported.

RFC 9112 requires correct message framing and complete consumption of request content before a persistent connection can safely carry another request.

LUI processes:

```text
one request
    ↓
one handler
    ↓
one complete response
    ↓
next request
```

per HTTP/1.1 connection.

Initial implementation should **not concurrently pipeline handlers on one HTTP/1.1 connection**.

That avoids response reordering complexity.

Different connections remain concurrent.

---

# 40. Handler execution

Each request executes as an LUI scheduled coroutine/task:

```text
network worker
     ↓
request parsed
     ↓
scheduler event
     ↓
Luau request task
     ↓
handler may yield
     ↓
response
     ↓
NetCore write
```

A handler may therefore do:

```lua
Server:Route("GET", "/data", function(Request)
    local Response =
        HttpService:GetAsync("https://example.com/data")

    return {
        StatusCode = 200,
        Body = Response.Body,
    }
end)
```

without blocking WinUI or GTK.

---

# 41. The network worker never enters Luau

This is non-negotiable.

```text
IOCP thread
epoll thread
DNS worker
TLS worker
```

may never execute:

```text
lua_pcall
Luau callbacks
Instance mutation
```

They create bounded completion records.

Those records enter:

```text
LUI scheduler queue
```

and the scheduler resumes the appropriate coroutine.

This also naturally fixes the VM-reentrancy class of issue identified in the current Foundation 1 runtime.

---

# 42. Internal async primitive

All async services should use one runtime primitive.

Conceptually:

```text
AsyncOperation<T>

Pending
  ↓
Completed(T)

or

Pending
  ↓
Failed(NetworkError)

or

Pending
  ↓
Cancelled
```

This same facility later powers:

```text
HTTP
filesystem
dialogs
processes
database extensions
DNS
sockets
```

Luau does not need to see `Future<T>` unless later desired.

The public API simply yields:

```lua
local Connection =
    NetworkService:ConnectTcp(...)
```

while the current coroutine is suspended.

---

# 43. HTTP server bounds

Initial proposed defaults:

```text
Backlog                    128
MaxConnections             256
MaxActiveRequests          128

MaxRequestLine             8 KiB
MaxHeaderBytes             32 KiB
MaxHeaderCount             100
MaxRequestBody             8 MiB

HeaderTimeout              10 seconds
BodyTimeout                30 seconds
HandlerTimeout             30 seconds
KeepAliveIdleTimeout       30 seconds
TLSHandshakeTimeout        10 seconds

MaxQueuedWriteBytes
per connection             4 MiB
```

All are configurable within host-defined maxima.

Sandbox policy can impose stricter maxima.

---

# 44. Resource exhaustion behavior

The runtime must never grow:

```text
connections
pending accepts
request buffers
response buffers
queued writes
pending scheduler events
```

without a hard bound.

If the connection limit is reached:

```text
HTTP server:
    send 503 if practical
    close

raw TCP:
    reject/close accepted connection
```

No unbounded queues.

---

# 45. Error model

Networking should expose portable error categories.

Initial set:

```text
NetworkError

AddressInUse
AddressNotAvailable
PermissionDenied

HostNotFound
NetworkUnreachable
HostUnreachable

ConnectionRefused
ConnectionReset
ConnectionAborted
UnexpectedEof

TimedOut
Cancelled
Closed

MessageTooLarge
BackpressureLimit
ResourceLimit

TlsHandshakeFailed
CertificateRejected

ProtocolError
Unsupported
Unknown
```

Native error information may be attached diagnostically:

```text
NativeCode
Platform
```

but application logic should normally test the portable `Code`.

---

# 46. Error handling example

```lua
local Success, Result = pcall(function()
    return NetworkService:ListenTcp({
        Address = "192.0.2.10",
        Port = 74,
    })
end)

if not Success then
    print(Result)
end
```

A Linux permission failure and an analogous Windows permission failure should present the same high-level category:

```text
PermissionDenied
```

while diagnostic metadata remains platform-specific.

---

# 47. Socket reuse semantics

Do not expose raw `SO_REUSEADDR` semantics directly.

Windows and Linux differ significantly here.

Microsoft recommends `SO_EXCLUSIVEADDRUSE` for server listeners to prevent another process from hijacking an already-bound port and explicitly warns against normal server use of `SO_REUSEADDR`.

Linux commonly uses `SO_REUSEADDR`; an active listener still blocks reuse, while the option helps with normal restart behavior. `SO_REUSEPORT` has separate multi-listener load-distribution semantics.

Canonical implementation:

```text
Windows:
    SO_EXCLUSIVEADDRUSE = true
    SO_REUSEADDR = false

Linux:
    SO_REUSEADDR = true
    SO_REUSEPORT = false
```

These differing native settings implement the same LUI semantic:

> A normal LUI listener exclusively owns its requested address/port while running.

---

# 48. Firewall behavior

**Binding a socket and being reachable through a firewall are separate operations.**

Windows Firewall blocks unsolicited inbound traffic by default unless traffic matches an allow rule.

Therefore:

```lua
Server:Start()
```

means:

> LUI successfully created and bound the listener.

It does **not** mean:

> every other machine can reach it.

LUI must not automatically create firewall rules.

That would be a surprising privileged side effect.

---

# 49. Firewall configuration

If firewall management is added later it should be explicit:

```lua
local FirewallService =
    app:GetService("FirewallService")
```

or handled during deployment/installation.

Never:

```text
NetworkService:ListenTcp()
    secretly modifies host firewall
```

Windows firewall rules can scope inbound access by program, protocol, port, addresses, and profiles, reinforcing why this should remain an explicit deployment concern.

Linux firewall configuration likewise remains outside normal socket creation.

---

# 50. TLS architecture

TLS belongs above TCP:

```text
TCP
 ↓
TLS
 ↓
HTTP
```

Do not make HTTPS a completely separate networking architecture.

Internally:

```text
TcpTransport
     │
     ├── PlainTransport
     │
     └── TlsTransport
```

---

# 51. TLS provider boundary

Define a private provider interface.

Possible implementations:

```text
Windows:
    Schannel

Linux:
    OpenSSL

portable fallback:
    OpenSSL
```

Windows provides TLS through the Schannel SSP, including modern TLS versions.

Linux kernel TLS is not itself a complete handshake implementation; kernel documentation explicitly notes that the handshake remains a user-space concern, which is another reason not to design LUI around kTLS directly.

kTLS may later be used as an optimization underneath a user-space TLS implementation.

---

# 52. Server TLS API

TLS credentials should be opaque LUI objects rather than a collection of backend pointers.

Example:

```lua
local TlsService = app:GetService("TlsService")

local Credential = TlsService:LoadServerCredential({
    CertificateFile = "cert.pem",
    PrivateKeyFile = "key.pem",
})

local Server = HttpServerService:CreateServer({
    Address = "any",
    Port = 8443,
    TLS = Credential,
})
```

Future credential sources may include:

```text
PEM
PKCS#12
Windows certificate store
system keychain
hardware-backed key
```

without changing `HttpServerService`.

---

# 53. TLS defaults

Canonical security defaults:

```text
TLS 1.2 minimum
TLS 1.3 permitted/preferred

certificate/key required for server mode
secure cipher selection delegated to provider policy
handshake timeout enforced

no SSL
no TLS 1.0
no TLS 1.1
```

Microsoft has deprecated TLS 1.0 and TLS 1.1 on modern Windows, reinforcing that LUI should not treat them as normal new-application defaults.

---

# 54. Network capabilities

LUI's capability model should distinguish:

```text
network.client
network.server
network.raw
```

### `network.client`

Allows:

```text
HttpService outbound calls
ConnectTcp
DNS resolution
```

### `network.server`

Allows:

```text
HttpServerService
TCP listen
UDP bind for inbound traffic
```

### `network.raw`

Allows:

```text
direct NetworkService TCP/UDP primitives
```

A normal HTTP server does not need `network.raw`.

---

# 55. Trusted vs sandboxed applications

Trusted native desktop applications can receive broad network capabilities from their application configuration.

Sandboxed applications/modules may have restrictions such as:

```text
only loopback
specific hosts
specific ports
no listeners
HTTP-only
no raw sockets
```

Example future manifest policy:

```toml
[capabilities.network]
client = true

[capabilities.network.server]
listen = [
    "127.0.0.1:8080",
]
```

Do not expose these restrictions as clutter in every Luau API call.

The host enforces them underneath the simple API.

---

# 56. DNS safety

`ListenTcp` and `BindUdp` should initially accept only:

```text
semantic address tokens
numeric IP addresses
```

not arbitrary DNS names.

Why?

A listener should bind to a deterministic local interface.

For outbound operations:

```lua
ConnectTcp({
    Host = "server.example.com",
})
```

DNS resolution is expected.

This cleanly separates:

```text
local binding
```

from:

```text
remote destination discovery
```

---

# 57. HTTP server handler failures

Unhandled Luau error:

```lua
error("something broke")
```

must not crash the network worker or listener.

Behavior:

```text
log structured runtime error
return 500 Internal Server Error
do not expose stack trace to remote client
```

Development diagnostics may show the stack locally.

Production HTTP responses must not leak local source paths or native errors.

---

# 58. Server shutdown

```lua
Server:StopAsync({
    GracePeriod = 5,
})
```

does:

```text
stop accepting new connections
        ↓
allow active requests to finish
        ↓
after grace period:
    cancel remaining handlers
    close connections
        ↓
Stopped
```

`Server:Close()` is immediate.

---

# 59. HttpService relationship

`HttpService` should use the same networking core.

```text
              NetCore
                 │
        ┌────────┴────────┐
        ▼                 ▼
   HttpService      HttpServerService
      client              server
```

This prevents separate implementations from disagreeing about:

- DNS;
- TLS;
- timeouts;
- errors;
- proxy handling;
- scheduler integration;
- resource accounting.

---

# 60. Future WebSockets

WebSocket should eventually layer on HTTP/TCP rather than exist as an unrelated socket implementation.

```text
TCP
 ↓
TLS optional
 ↓
HTTP Upgrade
 ↓
WebSocket
```

Potential API:

```lua
Server:WebSocket("/events", function(Socket)
    ...
end)
```

But WebSocket should be a later networking milestone.

Boost.Beast is evidence that HTTP and WebSocket naturally share an Asio-style transport model, though LUI need not use Beast itself.

---

# 61. Future Unix/local IPC

The public architecture should allow later:

```text
Unix domain sockets
Windows named pipes
```

through a higher-level:

```text
LocalTransportService
```

or appropriate extension.

Do not force them into `NetworkEndpoint`, because they are not IP network addresses.

---

# 62. Reflection integration

The networking surface must be registered in LUI's canonical reflection system.

Reflection should describe:

```text
NetworkService
TcpListener
TcpConnection
UdpSocket
NetworkEndpoint

HttpService
HttpServerService
HttpServer
HttpRequest
HttpHeaders

TlsService
TlsCredential
```

including:

```text
methods
properties
signals
async/yielding status
parameter types
return types
capability requirement
```

This drives:

```text
runtime validation
generated Luau types
documentation
VS Code completion
capability inspection
```

---

# 63. Suggested repository placement

```text
runtime/
├── async/
│   ├── Operation.*
│   └── CompletionQueue.*
│
└── services/
    ├── NetworkService.*
    ├── HttpService.*
    ├── HttpServerService.*
    └── TlsService.*

network/
├── core/
│   ├── NetCore.*
│   ├── Endpoint.*
│   ├── Errors.*
│   └── Limits.*
│
├── transport/
│   ├── TcpListener.*
│   ├── TcpConnection.*
│   └── UdpSocket.*
│
├── http/
│   ├── Parser.*
│   ├── Server.*
│   ├── Request.*
│   ├── Response.*
│   └── Headers.*
│
└── tls/
    ├── TlsProvider.*
    ├── openssl/
    └── schannel/
```

Networking must **not** live under:

```text
backends/winui3
backends/gtk4
```

---

# 64. Networking invariants

## Transport

1. LUI owns networking semantics; native socket APIs are implementation details.
2. No native socket handle is exposed through the normal Luau API.
3. TCP is always modeled as a byte stream.
4. UDP is always modeled as datagrams.
5. IPv4/IPv6 behavior is explicit and cannot depend on OS defaults.
6. A specific bind address never silently falls back to another address.
7. Listener ownership semantics remain equivalent across operating systems.

## Scheduler

8. Network worker threads never enter Luau.
9. Network workers never mutate Instances.
10. Every Luau-visible completion enters through the authoritative scheduler.
11. Async networking suspends only the invoking task/coroutine, never the UI thread.
12. Backend or network callbacks cannot synchronously reenter an executing Luau VM.

## Resources

13. All connection, request, buffer, and write queues are hard-bounded.
14. Slow peers apply backpressure rather than causing unbounded allocation.
15. Listener overload has deterministic rejection behavior.
16. Destruction and cancellation release all native operations.

## Security

17. Listener default exposure is loopback only.
18. `Address = "any"` must always be explicit.
19. LUI never silently changes firewall rules.
20. HTTP framing is parsed strictly.
21. Ambiguous request framing is rejected.
22. Request/header/body sizes are bounded before application dispatch.
23. Unhandled server errors never expose stack traces to remote peers.
24. Sandboxed code cannot acquire network capabilities it was not granted.
25. Raw networking requires a stronger capability than ordinary HTTP use.
26. TLS certificate validation cannot be silently disabled in production defaults.

## HTTP

27. HTTP parser behavior is runtime-owned and backend-independent.
28. HTTP/1.1 requests on one connection are processed in deterministic order.
29. Route callbacks run through the scheduler.
30. A malformed request must never reach user route code.
31. Persistent connections are reused only after the prior request body has been fully resolved.
32. Response framing is generated by LUI unless explicitly and safely overridden.

---

# 65. Short implementation roadmap

## Networking Foundation A — Async runtime + TCP

Implement:

```text
common async-operation primitive
Asio transport runtime
NetworkEndpoint
NetworkError

ListenTcp
ConnectTcp
AcceptAsync
ReadAsync
ReadExactAsync
WriteAsync
Shutdown
Close

scheduler completion bridge
hard queue limits
Windows + Linux headless tests
```

Qualification must run on both Windows and Linux.

---

## Networking Foundation B — HTTP

Implement:

```text
HttpService integration
strict HTTP/1.1 parser
HttpServerService
exact routing
buffered requests
bounded responses
persistent connections
timeouts
graceful shutdown
```

Add malformed-request and request-smuggling regression suites.

---

## Networking Foundation C — UDP + TLS

Implement:

```text
UDP sockets
TLS provider contract
server credentials
HTTPS
certificate validation
TLS timeout/resource qualification
```

---

## Later

Add only when justified:

```text
WebSocket
streaming HTTP bodies
HTTP/2
HTTP/3 / QUIC
route parameters
multipart helpers
proxy support
mTLS
network-interface enumeration
multicast
local IPC
```

---

# 66. Example — local application API

```lua
local HttpServerService =
    app:GetService("HttpServerService")

local HttpService =
    app:GetService("HttpService")

local Server = HttpServerService:CreateServer({
    Address = "loopback",
    Port = 8074,
})

Server:Route("GET", "/status", function(Request)
    return {
        StatusCode = 200,

        Headers = {
            ["Content-Type"] = "application/json",
        },

        Body = HttpService:JSONEncode({
            ok = true,
            application = "LUI Example",
        }),
    }
end)

Server:Route("POST", "/launch", function(Request)
    Launcher:Launch()

    return {
        StatusCode = 204,
    }
end)

Server:Start()

print("Listening on port", Server.Port)
```

No WinSock.

No `epoll`.

No threads.

No HTTP parsing.

No message framing.

No firewall manipulation.

---

# 67. Example — LAN-bound server

If the machine owns:

```text
192.0.2.10
```

then:

```lua
local Server = HttpServerService:CreateServer({
    Address = "192.0.2.10",
    Port = 8074,
})

Server:Route("GET", "/health", function()
    return {
        StatusCode = 200,
        Body = "healthy",
    }
end)

Server:Start()
```

means exactly:

> Listen for TCP HTTP traffic sent to `192.0.2.10:8074`.

It does not listen on:

```text
127.0.0.1
192.168.x.x
another NIC
IPv6
```

unless separately requested.

---

# 68. Example — raw protocol

```lua
local NetworkService =
    app:GetService("NetworkService")

local Listener = NetworkService:ListenTcp({
    Address = "192.0.2.10",
    Port = 8074,
})

task.spawn(function()
    while Listener.IsListening do
        local Client = Listener:AcceptAsync()

        task.spawn(function()
            local Success, Error = pcall(function()
                while Client.IsOpen do
                    local Data = Client:ReadAsync(4096)

                    if Data == nil then
                        break
                    end

                    Client:WriteAsync(Data)
                end
            end)

            Client:Close()

            if not Success then
                print("[LUI:Network]", Error)
            end
        end)
    end
end)
```

This exposes the power of sockets without leaking platform mechanics.

---

# 69. Architectural conclusion

Networking should follow exactly the same philosophy as LUI's UI architecture:

```text
APPLICATION SURFACE

simple Luau
    ↓
NetworkService / HttpService / HttpServerService

────────────────────────────────

LUI SEMANTICS

connections
endpoints
HTTP
timeouts
errors
backpressure
capabilities
resource limits

────────────────────────────────

IMPLEMENTATION

Asio
    ↓
┌──────────────┬──────────────┐
│              │              │
Windows       Linux        future macOS
Winsock       sockets       BSD sockets
IOCP          epoll         kqueue
```

The application should be able to host a real network API without knowing anything about Winsock, `epoll`, file descriptors, IOCP, HTTP framing, or worker threads.

At the same time, the abstraction must **not** pretend networking is simpler than it really is internally.

The canonical rule should be:

> **Simple Luau networking surface; bounded, asynchronous, protocol-correct native machinery underneath.**

For LUI specifically, I would put **Networking Foundation A into Foundation 2**, because the async completion primitive, capability enforcement, native integration, and production-host boundaries all belong there. HTTP hosting can follow immediately once that transport/scheduler contract is proven.