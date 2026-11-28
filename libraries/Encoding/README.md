# Encoding

Use immutable Luau strings for raw bytes, including NUL and invalid UTF-8. Each function accepts one string and returns a new string. Calls are synchronous, need no capabilities and retain no input.

~~~luau
local Encoding = require("@Encoding/Encoding")
local Bytes = Encoding.HexDecode("00ff")
assert(Encoding.HexEncode(Bytes) == "00FF")
assert(Encoding.Base64Encode(Bytes) == "AP8=")
assert(Encoding.Base64Decode("AP8=") == Bytes)
assert(Encoding.UrlEncode("A+B /") == "A%2BB%20%2F")
assert(Encoding.UrlDecode("A%2BB%20%2F") == "A+B /")
~~~

* `Base64Encode` emits standard padded RFC 4648 Base64. `Base64Decode` requires complete quartets, the standard alphabet and canonical zero pad bits. Whitespace and URL-safe spellings fail.
* `HexEncode` emits uppercase hex. `HexDecode` accepts either case in complete pairs.
* `UrlEncode` percent-escapes all bytes outside the ASCII unreserved set. Use it on an individual component. `UrlDecode` translates percent pairs, preserves literal plus and copies other bytes as supplied.

The raw side is limited to 65,536 bytes. Encoded input is checked against each format's maximum before allocation. The URL decoder also checks actual output length. Failures raise fixed `[LUI:Encoding]` errors catchable by `pcall`. Browser and mobile hosts require separate qualification. No native codec, mutable handle or cryptographic primitive is involved.
