# Call a native library

The experimental Windows x64 and Linux x64 SDKs include an optional Ffi provider. A trusted application can use C libraries from Luau. Choose an existing managed service for ordinary file, network, database and process operations. Granting in-process FFI gives the application and the modules it loads the operating-system authority of the host process.

## First call

```luau
local Ffi = require("@Ffi/Ffi")
local Process = Ffi.System("process")
local Symbol = if Ffi.Platform().OS == "Windows" then "GetCurrentProcessId" else "getpid"
local GetPid = Process:Bind(Symbol, {}, "u32")
local Pid = GetPid:Call({})
GetPid:Close()
Process:Close()
```

Create the project with `lui new MyApp --template ffi-platform --sdk <sdk>`. Its root manifest requests HostServices and NativeExtensions, names Ffi and captures the SDK provider declaration. The headless operator also selects `--trusted-ffi true` on `lui run --mode headless`, or `--trusted-ffi` on a standalone launcher. The CLI uses `--stdio true` and the standalone launcher uses `--stdio` for terminal IO. The desktop project host uses its broader `--allow-native` grant. Headless verifies the selected image against its compiled SDK pin. Desktop trusts the application's captured native extension declarations. Treat either grant as native execution. Importing the facade performs no service acquisition. Source-only runs, browser previews and isolated Luau workers have no Ffi service.

## Choose the library and declare its functions

- `System("process" | "math" | "sqlite")` finds a named OS library in System32 or the Linux system library directories. SQLite availability depends on the OS installation.
- `Open(absolutePath, optionalLowercaseSha256)` selects a developer-specified image. Supply a digest to bind the top-level image to exact bytes.
- `Packaged(filename, lowercaseSha256)` loads a single captured asset from the standalone launcher's Application directory. Build the application before using this mode. Put the filename in the manifest's flat Assets list. The fixture examples generate their target-specific binding and digest when assembled.
- Windows loads dependencies with the selected DLL directory and System32 search flags. Linux uses its native ELF dependency resolver, including the operator's environment and the library's runpaths. A hashed Linux image is loaded from a sealed memory file. Dependencies using a relative `$ORIGIN` need a separately supported installation layout. A top-level digest does not authenticate the image's dependencies.
- Closing a Library retires its public handle. Functions and pointer views keep their dependencies alive. The native loader retains up to 64 distinct image selections per provider image until process exit. Static constructors, libraries retaining callbacks and unknown native threads preclude general safe unloading.

Bind each function with its exact C prototype. The qualified conventions are Microsoft x64 and System V AMD64 with fixed parameter lists and up to 16 arguments. The OS loader can verify a symbol exists. The application author must verify its ABI, ownership and lifetime against the header. `CallWithError` returns Value, Errno and Windows LastError captured on the calling thread. Consult each API's success rules. On Windows, different C runtimes can maintain different errno values.

| Native declaration | Luau value |
| --- | --- |
| i8/u8, i16/u16, i32/u32 | Range-checked integer number |
| i64/u64, intptr/uintptr, size | String of exactly 16 hex digits, two's complement for signed values |
| long/ulong | 32-bit number on Windows, 64-bit word on Linux |
| f32/f64 | Finite number, rounded to the declared width |
| bool8/bool32 | Boolean with an explicitly declared 1-byte or 4-byte C representation |
| pointer | Opaque pointer, managed allocation/view, callback token or Ffi.Null |
| utf8 | NUL-terminated UTF-8 copied during the call, with a 16 KiB bound |
| void | Return type only |

Use `Ffi.Word64(high32, low32)` to construct an exact word. Null pointers and null strings use the explicit `Ffi.Null` sentinel so they survive in argument arrays and returned records. Native code that keeps input strings after a call requires separately allocated, retained NUL-terminated storage. Output strings require a valid readable terminator within the bound. Determine buffer lengths from the native API before attempting a copy.

## Structures, memory and ownership

```luau
local Pair = Ffi.Struct({{Name="Left", Type="i32"}, {Name="Right", Type="f64"}})
local Layout = Ffi.Describe(Pair)
local Bytes = Ffi.Allocate(Layout.Size, Layout.Alignment)
Bytes:Set(0, Pair, {Left=3, Right=1.5})
local Right = Bytes:Get(Layout.Offsets[2], "f64")
Bytes:Close()
Pair:Close()
```

Describe reports byte size, alignment and zero-based byte offsets in declaration order. Nest structs and fixed arrays using `Ffi.Array(element, count)`. Naturally aligned structs can be passed by value. Top-level C array parameters decay to pointers.

Pass a second argument to `Ffi.Struct(fields, pack)` to match a C declaration using `#pragma pack`. The supported pack limits are 1, 2, 4, 8 and 16 bytes. Check size, alignment and field offsets against the native declaration. Packed layouts work with `Pointer:Get` and `Pointer:Set`. For fixed C calls, the Microsoft x64 provider supports by-value packed C structs. The System V x64 provider accepts packing that leaves the natural layout intact. Use pointers or a C wrapper for layout-changing packed structs on System V x64. Unions, bitfields, C++ member functions and other calling conventions require a C wrapper.

`Allocate` returns aligned, zero-initialized native storage. `Read` and `Write` copy binary Luau strings in chunks of at most 16 KiB. `Slice` creates a bounded view without copying the underlying storage. Pass the same buffer or a slice as a pointer argument for direct native access. A session admits 1 MiB per allocation, 8 MiB of LUI-owned storage and 1,024 live public handles. Typed writes of pointer-containing values require a native binding that explicitly owns the pointees.

An opaque return value records its source call's dependencies. It can be passed back without exposing its address as a Luau number. `TrustBorrow(size)` asserts that the external address is readable or writable for that span and lifetime. LUI can check offsets within the asserted range. The native owner can independently invalidate it. Use `Adopt(size, releaseFunction)` only when the native contract transfers unique ownership and supplies a fixed `void(void*)` release function. Within a session, observed exact and interior aliases share the registered destructor. The final retained alias releases the allocation. Echoed addresses into known LUI storage, callback code and temporary string storage cannot acquire a second destructor. Independent native frees, unobserved pointers and separate sessions still require coordination. Foreign APIs returning a status from their close function need an explicit call as in the SQLite example. Avoid adopting aliases, calling a destructor twice, or exposing storage to a native library beyond its lifetime.

Every resource supports explicit `Close()`. Closing an individual token twice returns false on the second call. The owner session retires remaining handles at shutdown. A token going out of Luau scope does not promise prompt cleanup. `Stats()` reports live handles and LUI-allocated bytes. Native libraries maintain their own allocations.

## Deliver callbacks on the owner thread

```luau
local Notice = Ffi.Callback({"i32"}, "void", function(Args)
    print(Args[1])
end)
-- Pass Notice to a native function declared with a pointer parameter.
local Delivered, Dropped, Errors = Ffi.Poll()
-- Unregister with the native library and wait for acknowledgement where required.
Notice:Close()
```

The libffi closure copies pointer-free arguments into a bounded owner-session FIFO. The owner calls Poll on its scheduler turn and receives the delivered count, dropped count and individual handler errors. Ordinary workers start with pure APIs. Explicitly trusted workers can use native handles within their own FFI session. Callback registration and Poll are available in the owner profile.

Callback parameters may include scalars and bounded pointer-free structures. The Microsoft x64 profile accepts packed arguments through the same size-based C ABI as fixed calls. System V x64 accepts structures whose packing preserves the natural layout. Pass a constant fallback as the fourth argument for an immediate scalar return from a notification callback.

Use `Ffi.SyncCallback(Parameters, Result, Compute, Fallback)` when a C function
needs a computed result during an owner-thread FFI call. The handler receives
an array of copied arguments and returns a value of the declared scalar
type. Supply a fallback for every non-void result.

```luau
local Double = Ffi.SyncCallback({"i32"}, "i32", function(Args)
    return Args[1] * 2
end, -1)
-- Pass Double to a matching native API that invokes it on the calling thread.
```

The handler executes on the calling Luau stack under protected execution.
A foreign thread, an invocation outside the current owner FFI call, a
closed callback, a handler error, an attempted yield or an invalid result
uses the fallback immediately. A reentrant dispatch into the same FFI
session is rejected. Use the notification API when work belongs on a later
scheduler turn. Synchronous callbacks require a matching owner host and
provider. Worker-local and brokered calls use their own execution profiles.

At most 64 notifications wait per session. Close disables future delivery even if a native caller still holds the function pointer. Retired closures retain their executable memory, immutable signature and fallback through process exit. The cumulative limit is 128 closures per provider image. Reuse a registration for repeated notifications. Native unregistration and any required thread join are the application's responsibility.

## Blocking calls and distribution

Bind a function once and reuse its prepared signature. For high-frequency
updates, benchmark the full Luau call path with representative arguments.
Supporting hosts and providers use compact transport for numbers,
booleans, opaque handles and exact 64-bit words. This includes aggregates
with up to 128 of those fields across the arguments and another 128 in the
result. Strings, larger aggregates and aggregate inputs with custom table
behavior use structured transport. `Read` and `Write` copy raw bytes on
supporting providers. Use a bounded buffer for native APIs that accept
bulk data directly.

A normal call occupies its calling thread until native code returns. The desktop checksum sample starts a bounded native worker, keeps accepting UI input and polls for completion. Cancel signals the library's cooperative cancellation flag. Window retirement joins that job. Trusted headless actors can keep blocking calls off the owner thread. Use FfiBroker when the application needs a process timeout or crash isolation.

Bundle the Ffi facade, Json, Encoding, Ffi.json, pinned platform image and supplied notices only with applications that select them. The callback fixture is example-specific. The supported targets are Windows x64 and Linux x64. ARM64, macOS, mobile and browser execution have no qualified provider. On restricted hosts executable-memory policy may reject callback closures.

The SDK includes `Tools/Generate-FfiBindings.py` for scalar and pointer declarations from a C header. Supply an installed Clang, an explicit x64 target and repeated `--symbol` arguments. For a variadic symbol such as `Log`, supply the specific promoted tail with `--variadic-tail Log=i32,f64`. For a C function whose second argument is a `const char *` with a UTF-8 contract, use `--utf8 Function=2`. Use `result` as a position to copy a `const char *` return value. The SDK still needs an explicit lifetime review for each selected API.

The generated factory accepts an open image and returns the named bindings and a cleanup function. Call the cleanup function after use and manage the image and returned resources separately. On the owner, a factory closes earlier bindings if a later binding fails. A trusted actor whose binding factory fails retires its session through the worker lifecycle.

`Image:BindVariadic(Symbol, FixedCount, Parameters, Result)` prepares a
variadic C signature for one complete argument shape. Include the fixed
parameters and every tail argument in `Parameters`. Declare the tail using
C's promoted types, such as `f64` for a promoted float and `i32` for small
integer types. Both x64 profiles admit promoted scalars, pointers and
naturally aligned structures in the tail. Bind each additional shape
separately and check it against the C prototype.

The experimental headless profile can call Ffi from a trusted actor when
both host flags and `Workers.StartTrusted` or `Workers.PoolTrusted` are
selected. Each actor owns a fresh session and can keep its bindings for
subsequent jobs. Callback registration and polling use the owner profile.
