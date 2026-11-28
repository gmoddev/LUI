# Compression

Use `Encode(Format, Bytes, MaxOutput, Complete)` or `Decode` with the same arguments. Formats are `gzip`, `zlib`, raw `deflate`, and `zstd`. Supply a buffer of at most 1 MiB and an explicit output ceiling between zero and 1 MiB. The worker checks the limit as it produces output. Decoding requires one complete member and rejects trailing bytes, truncated inputs and invalid checksums.

~~~luau
local Compression = require("@Compression/Compression")
local Request = Compression.Encode("gzip", buffer.fromstring("hello"), 128, function(Bytes, Failure)
    assert(Bytes and not Failure)
end)
~~~

The call returns a `NativeRequest`. Call `Request:Cancel()` while pending. The runtime delivers a result buffer or a fixed failure code to the callback on the owner thread. Arguments outside the public bounds raise a catchable error. A worker admits up to eight outstanding jobs and checks cancellation at 16 KiB intervals. Each job has a 30-second deadline. The worker retires with its owning runtime.

The Windows and Linux headless owner requires the option `--providers Compression` and a project declaring both `HostServices` and `NativeExtensions`. Copy the target SDK's `Compression.json` declaration and its `Native` image at the declared path. The module uses zlib 1.4 under the zlib license and Zstandard 1.6.0 under BSD-3-Clause. The Zstandard decoder accepts one ordinary frame without dictionaries and enforces a 1 MiB window. Its image and license can be distributed with just the applications that use it.
