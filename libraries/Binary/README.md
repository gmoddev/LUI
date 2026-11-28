# Binary

Build small binary records and inspect them with the same field layout. Integers use little-endian byte order, independent of the host. The library uses Luau buffers internally and crosses the public boundary with immutable byte strings.

~~~luau
local Binary = require("@Binary/Binary")
local Out = Binary.Writer(64)
Out.U8(1)
Out.U32(125000)
Out.Bytes("red")
local Input = Binary.Reader(Out.Finish())
assert(Input.U8() == 1 and Input.U32() == 125000)
assert(Input.Bytes() == "red" and Input.Remaining() == 0)
~~~

`Writer(MaxBytes?)` returns a frozen record of functions with private mutable storage. Call the functions with dot syntax. `U8`, `U16` and `U32` append an **unsigned integer** in the named width. `Bytes(Value)` appends a two-byte unsigned length followed by exactly those bytes. `Finish()` returns a snapshot. More writes can follow. The hard limit and default are 65,536 total bytes; an application may choose any smaller nonnegative integer. The buffer grows on demand. A length-prefixed field and its prefix must fit together.

`Reader(Input)` accepts a binary string of up to 65,536 bytes and copies it into a private buffer. `U8()`, `U16()`, `U32()` and `Bytes()` consume one complete field and return its value. `Remaining()` returns the number of unread bytes. Applications choose their own record layout, version fields and trailing-byte policy. Truncated fields, overflow, invalid values and exceeded limits raise fixed `[LUI:Binary]` errors catchable by `pcall`. A rejected field leaves the cursor at its old position. Each reader and writer can be garbage-collected independently and owns no native resource. No grants or providers are required. Browser and mobile execution require separate qualification.
