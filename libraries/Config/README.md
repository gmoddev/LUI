# Config

Read a small application settings object. Defaults specify allowed names and types. This pure library depends on the optional Json package. Applications supply their own authorized source of text.

~~~luau
local Config = require("@Config/Config")
local Settings = Config.Decode('{"Limit":12}', {Limit = 10, Verbose = false})
assert(Settings.Limit == 12 and Settings.Verbose == false)
~~~

`Decode(Text, Defaults)` returns a new frozen table. Each value must match the boolean, finite number or string in the defaults. Missing settings inherit their defaults. The caller's table stays available for reuse. Unknown settings, JSON null, duplicate keys, nested objects or arrays, coercions, nonfinite numbers and defaults with metatables fail. Use the Json library directly for documents with nested schemas. Validate application-specific value ranges before using them.

Limits are 32,768 input bytes, 64 settings, 64 bytes per ASCII identifier and 4,096 bytes per UTF-8 string value. Names start with an ASCII letter or underscore and may also contain digits after the first character. Defaults and input share the same value bounds. Fixed `[LUI:Config]` and `[LUI:Json]` errors can be caught with `pcall`. Errors contain no input values. The function has no I/O, log output or native handles. Windows and Linux use the same source. Browser and mobile qualification is separate.
