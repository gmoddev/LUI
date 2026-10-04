# 0027 — Bounded HTTP/1.1 message framing

**Date:** 2026-10-04

## Decision

Begin Networking Foundation B with the internal `LuiHttp` C++ component in `runtime/network/http/`. It contains a transport-independent incremental parser for one request or response. The implementation uses the existing pinned Asio address parser for numeric IPv6 Host validation; no new dependency is added. HTTP services, serializers, routing, and deadlines remain separate implementation work. The parser does not run Luau or expose a new public ABI.

`Feed` returns a status, consumed byte count, and typed error. It retains only the current bounded line and parsed message. A message becomes accessible through `GetResult()` after complete framing and trailer validation. Completion consumes exactly one message; the caller owns the remaining input. Completed and failed parsers consume no further bytes. Errors are terminal: the future connection layer must close the connection, discard the parser, and never resynchronize or dispatch the partial message. Each message needs a fresh parser. The connection layer must process requests and responses in order and enforce timeouts independently.

## Initial profile

- HTTP/1.1 only; exact CRLF and single request-line separators.
- Requests use encoded origin-form or `OPTIONS *`. No path decoding or normalization happens here. Invalid URI bytes/percent escapes are rejected.
- Requests require exactly one nonempty Host. Hosts accept ASCII letters/digits/dots/hyphens or bracketed numeric IPv6, optionally with a decimal port through 65535. Userinfo, commas, scoped literals, and IPvFuture are unsupported.
- Content-Length is unsigned decimal with checked conversion. Duplicate fields are rejected even if identical; comma lists are rejected. Transfer-Encoding accepts exactly one `chunked` field. Combining length and transfer framing fails in either order.
- Fixed-length and chunked bodies preserve binary bytes. Only chunk framing is removed; content compression is not decoded. Chunk extensions are syntax-checked, bounded, and ignored. Trailer names are restricted to `content-digest`, `repr-digest`, and `server-timing`; values remain opaque. Unknown trailers are rejected, and admitted trailers stay separate from headers. No digest verification is implied.
- HEAD responses and 304 responses end at headers even when a representation length exceeds the body cap. Informational responses and 204 responses prohibit framing headers and have no body. Informational messages consume only their own bytes. A 205 response must resolve to empty content using ordinary framing. The caller supplies the originating request method for response parsing.
- Ordinary responses with neither length nor transfer framing require clean EOF and disable persistence. EOF before fixed/chunked completion fails. Requests never use EOF to delimit a body.
- Connection token lists are validated; `close` disables persistence. Nominating framing fields as connection options is rejected. `KeepAlive` describes protocol eligibility; callers must also verify that the transport remains open before reusing it.
- CONNECT tunnels, protocol upgrades, HTTP/1.0, proxy request forms, other transfer codings, and request expectations (including `100-continue`) are unsupported in this profile. It is not a complete general-purpose HTTP implementation.

These choices use [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html) and [RFC 9110](https://www.rfc-editor.org/rfc/rfc9110.html) for framing, while deliberately restricting optional syntax and features. The metadata trailer choices follow [RFC 9530](https://www.rfc-editor.org/rfc/rfc9530.html) and [Server Timing](https://www.w3.org/TR/server-timing/). Expanding this profile requires explicit decisions and regression cases before a service advertises the extra behavior.

## Bounds

| Stored/input category | Default | Maximum internal configuration |
| --- | --- | --- |
| Start/field/chunk line, including CRLF | 8 KiB | 64 KiB |
| Headers plus trailers, including terminators | 32 KiB | 1 MiB |
| Header plus trailer fields | 100 | 1024 |
| Decoded body | 8 MiB | 64 MiB |
| Chunk metadata, data terminators, and trailers | 32 KiB | 1 MiB |
| Chunks, including terminal zero | 65,536 | 1,048,576 |

Limits are checked during input consumption. A declared fixed/chunk body exceeding the remaining allowance fails before body allocation/acceptance. Metadata and chunk count have separate limits so tiny chunks cannot bypass the body cap. Lower internal bounds support conformance tests; a zero body limit is valid. These are internal defaults, not a released Luau configuration contract or an exact allocator-memory quota.

## Verification

`LuiHttpTests` checks every two-part split and byte-at-a-time delivery of valid fixtures, binary bodies, response exceptions, separate trailers, persistent-message boundaries, early EOF, limits at and past boundaries, and malformed framing/header/chunk cases. A deterministic mutation corpus compares whole-input and fragmented outcomes. The TCP fixture sends consecutive messages through the real capability-gated `NetworkService` and feeds seven-byte reads into the parser on the scheduler thread. Both Windows and Linux build scripts run this target.

No request reaches a Luau HTTP handler in this slice because those handlers and services are not implemented. The TCP fixture qualifies parser/transport composition, not a hosted HTTP server.
