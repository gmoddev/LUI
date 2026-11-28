# 0093. Prepared aggregates and scoped FFI callbacks

[Decision 0094](0094-ffi-handles-and-native-bindings.md) extends this
checkpoint's transport and ABI coverage to opaque handles, exact words,
Microsoft x64 packed callback arguments and natural aggregate variadic tails.

The optional trusted provider prepares leaf offsets for bounded aggregate
signatures composed of numbers and booleans. The Luau facade prepares the
matching field traversal at binding time. Compact messages contain the
fields in declaration order. The provider validates length, type and
numeric range, then writes the values into zeroed, per-call ABI storage.
Pointer-bearing and exact-word layouts continue to use structured messages
and their existing handle retention. Scoped calls retain their own scratch
storage across native execution.

The V1 extension method registration and public value layout stay intact.
An owner host may discover a versioned, private Ffi provider export when
loading its existing trusted native image. The scoped dispatch borrows a
callback for one native call. It runs a protected Luau function on the
already executing owner stack. The provider grants the callback access to
that scope only while executing on the same thread and in the same session.
Reentrant FFI dispatch fails at the provider boundary. A foreign-thread,
unscoped or retired callback returns its declared fallback immediately.
An error, yield or invalid result also selects the fallback. A computed
callback carries no VM reference in the pinned native trampoline.

Fixed Microsoft x64 calls select packed POD transfer using the exact C ABI
size and computed memory offsets. The independent oracle exercises both
an eight-byte register case and an eleven-byte indirect case. System V
AMD64 accepts explicit packing with the natural field alignment and
requires a pointer or C wrapper for layout-changing packed values.
The System V variadic profile also accepts natural aggregate tails.
Microsoft x64 variadic tails retain the promoted scalar and pointer
profile. Layout-changing packed callbacks, unions and bitfields require
native wrappers. Top-level C arrays continue to use pointers.

Trusted worker sessions may use compact aggregate and scalar packets with
the same checks as the owner and retain their existing admission, quotas,
and affine retirement. Callback registration stays with the owner.
Managed services retain their operation-specific permissions. Trusted
FFI runs with the process's authority. A broker provides a child-process
fault boundary, with OS confinement supplied by a separately selected
execution policy.
