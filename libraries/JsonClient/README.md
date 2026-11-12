# JsonClient

An optional HTTP JSON adapter for SDK `0.1.0-preview.2` development. It uses
`@Json/Json` and the existing `HttpService`.

```luau
local JsonClient = require('@JsonClient/JsonClient')
local Client = JsonClient.Open()
task.spawn(function()
    local Data, Response = Client:RequestAsync('http://127.0.0.1:8080/status', {
        Method = 'POST', Value = {Message = 'Hello'}, TimeoutMs = 3000,
    })
    if Response.Success then print('[LUI:JsonClient]', Response.StatusCode) end
end)
```

Importing is pure. `Open()` obtains HttpService using the host's existing
`network.client` grant. Declare JsonClient's dependency on Json in `Requires`
and pin both modules through the ordinary module lock. The SDK's standalone
`json-client` example includes these declarations and a loopback HTTP server.

`RequestAsync(Url, Options?)` returns decoded data and the immutable native HTTP
response. `GetAsync(Url)` is the GET shorthand. Options accept Method, Value,
Headers, TimeoutMs and MaxResponseBytes. A missing Value sends no body.
`false` and `Json.Null` encode as distinct JSON bodies. Content-Type defaults
to application/json for a body when no case-insensitive match is supplied.
Header records are copied before native admission. Native header, URL, method,
deadline, response-size, TLS and host-address validation apply.

An empty body returns nil. JSON null returns Json.Null. Non-success HTTP status
codes return alongside their decoded body. Every nonempty body must be valid
JSON. A decoding failure raises a fixed codec error before a response tuple is
returned. Callers needing raw malformed bodies can use HttpService directly.

The codec caps input/output at 1 MiB, depth 64 and 100,000 nodes by default.
Native response limits may narrow those bounds. Calls run in existing yielding
scheduler tasks. Generation retirement releases native requests and connections.
The adapter creates no persistent native resources of its own. Explicit native
`HttpService:CancelAll()` retains its runtime-wide cancellation semantics.
Transport failures retain native error codes. Codec/option errors omit payloads.

Version is `0.1.0`. The library catalog targets the next SDK and supplies module
types to the generated development reference.
