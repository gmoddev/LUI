# 0092. Compact FFI calls and bounded native work batches

Accepted for the runtime development worktree on November 26, 2026.

The optional Ffi provider keeps ABI v1 and the existing Dispatch method.
Bindings already retain the libffi declaration, argument placement and
library reference. A binding advertises compact calls when its entire
signature uses boolean or finite numeric arguments and returns a boolean,
finite number or void. The provider uses the prepared signature to check
the packet and marshal the values. The result-only reply omits error
fields. CallWithError captures errno and Windows LastError at native return.
The same transport copies raw bytes through the existing bounded pointer
read and write operations. Providers advertise support before the facade
uses these operations. Other calls continue through the structured protocol.

Packed struct descriptions use an explicit maximum field alignment.
Arrays and natural structs that contain packed values inherit the need to
use a pointer in signatures. The provider calculates and checks memory
offsets and leaves by-value ABI classification of natural aggregates to
libffi. Older providers fail packed requests in the facade before creating
a layout. The C fixture checks nested offsets, stride and pointer access.

An explicitly trusted headless project can reuse worker-owned Ffi sessions
through PoolTrusted. The owner and worker grants, VM isolation, quotas,
and resource retirement rules from decision 0090 continue to apply.
The trusted worker bridge admits only scalar calls and bounded byte
operations through the compact protocol. Callback registration and
polling remain owner-affine. Worker replies can be copied directly from
the rooted VM string to an owned native-service buffer. Owner delivery,
cancellation and retirement still settle through the native service domain.

The selected-tool broker can bind one fixed signature and evaluate up to
32 rows in one child under one process timeout. A failed batch returns
no partial results. Native side effects from earlier rows can persist.
Subsequent requests use fresh children. This process boundary provides
fault isolation, and OS confinement requires a separate execution policy.
