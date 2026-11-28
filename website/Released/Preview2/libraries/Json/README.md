# Json 0.1.0

An optional pure Luau JSON codec. It registers no service and allocates no native
resources. Vendor `Json.luau`, declare it in `Modules.Dependencies`, pin its bytes
with the existing module lock and import `require("@Json/Json")`. The library is
absent from applications that do not declare it. See the standalone JSON HTTP example.

`Json.Encode(Value, Limits?)` returns UTF-8 JSON. `Json.Decode(Text, Limits?, Checkpoint?)`
returns Luau values. `Json.Null` is the distinct frozen JSON-null sentinel.
`Json.Array(Values?)` and `Json.Object(Values?)` shallow-copy and mark containers,
preserving empty arrays and objects through round trips. Ordinary empty/string-key
tables encode as objects; dense positive integer-key tables encode as arrays.
Mixed, sparse and unsupported keys, nonfinite numbers, foreign metatables, cycles,
invalid UTF-8 and unsupported values are rejected. Object keys sort on encoding.
Numbers use Luau doubles; this is not a lossless arbitrary-precision number codec.

The optional limits table accepts only `MaxBytes`, `MaxDepth` and `MaxNodes`.
Defaults and hard ceilings are 1,048,576 input/output UTF-8 bytes, 64 value levels
(the root is level one) and 100,000 values/object keys. Positive integer overrides
may only narrow them. VM execution/memory quotas also apply. Internal temporary
allocations are governed by the VM quota; these are not process-wide byte caps.

Decode rejects trailing content, duplicate keys (including escaped equivalents),
malformed number syntax, controls in strings, invalid Unicode/surrogates and
nonfinite numeric overflow. Failures use `[LUI:Json] InvalidJson`, `InvalidUtf8`,
`DuplicateKey`, `InvalidValue`, `InvalidOptions`, `Cycle` or `LimitExceeded` without
including input contents. Expected failures can be handled with `pcall`.

An optional checkpoint function is called during UTF-8 validation and parsing at
bounded byte intervals. It can cooperatively yield or throw to cancel decoding.
No partial value is returned. The input is an immutable string and all parser
state belongs to that call, so interleaved decodes do not share mutable state.
Without the callback, Decode remains synchronous and pure. The byte, depth and
node ceilings apply equally in both modes; a checkpoint does not widen VM quotas.

`Json.Version` is `"0.1.0"`; `Json.RequiredCapabilities` is a frozen empty list.
These are library-owned documentation/data, not new manifest fields or authority.
The SDK stager uses module-lock provenance `Json-0.1.0-preview.2` and exact source bytes.

No individual-module unload is supported. Exports and decoded values retire with
the application's existing VM generation. Source/documentation license: repository MIT.
