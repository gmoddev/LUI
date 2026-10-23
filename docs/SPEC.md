# Application contract

This guide summarizes the current experimental profile. [API.md](API.md) and runtime reflection define implemented names and defaults. [Invariants](INVARIANTS.md) define architectural requirements; the [roadmap](../ROADMAP.md) distinguishes future work from supported behavior.

## UI and lifetime

Application code creates Instances, assigns properties and connects signals. Parenting defines UI ownership, destruction is idempotent and dead objects cannot receive callbacks. All callbacks and mutations run on the owner scheduler thread.

LUI resolves logical geometry, constraints, padding, lists/grids, scrolling, clipping and stacking. Native backends map these results rather than substituting toolkit layout. Styles use explicit geometry; autosizing and rich text are outside the current profile.

TextBox accepts strict UTF-8 without embedded NUL, up to 65,534 UTF-8 bytes. MaxLength may narrow the Unicode scalar limit. Selection offsets/counts are zero-based Unicode scalars, not UTF-16 units or grapheme indices. Preedit is separate from committed text and bounded to 4 KiB. Secret uses native password presentation, sticky sensitive classification and no public selection/preedit; multiline secret input is unsupported.

Declared PNG/JPEG images load asynchronously with cancellation and generation ownership. Inputs are bounded to 1 MiB encoded and 1,048,576 decoded pixels, with two decodes, 32 requests and a pinned-inclusive 16-entry/32 MiB decoded cache. No evaluation-time image download occurs.

## Modular projects

Schema v2 declares exact-case application/dependency sources and pinned vendored content. Preparation captures immutable inputs. Source-bound `require` uses canonical identities, private globals and one non-nil export; false is cached. Initialization effects/yields and cycles are guarded. Reload destroys the previous generation before preparing its replacement.

Read [project hosting](PROJECT-HOST.md), [typechecking](PROJECT-TYPECHECK.md), [host declarations](PROJECT-DECLARATIONS.md), [native provider bindings](PROJECT-NATIVE-BINDINGS.md) and [packaging](PROJECT-PACKAGE.md).

## Capabilities and diagnostics

Hosts grant capabilities explicitly; manifests can narrow them. VM execution/memory limits do not sandbox privileged native libraries. Policy-enabled projects choose diagnostics before VM creation. Sensitive fields use fixed redacted projections in framework diagnostics; explicit application output can still disclose application data. Development and Restricted modes have different inspection/checking permissions.

## Networking

TCP, UDP, outbound HTTP/HTTPS and hosted HTTP/TLS use runtime-owned transports, bounded admission and owner-thread completion. Access is capability-gated and host address/port policy applies. HTTP direction grants do not imply unrestricted raw sockets. TLS validates outbound identity and requires explicit native-host server credentials. See [TLS dependencies and trust](TLS-DEPENDENCY.md).

## Owned native integration

The experimental x64 ABI v2 coexists with ABI v1. Providers use copied immutable
bytes, session-scoped typed handles and bounded async requests/progress. Only
the owner delivers Luau callbacks. Terminal capacity is reserved before Start;
cancellation and retirement suppress stale work. Nonquiescent native code stays
pinned and charged rather than being force-unloaded. Structured schema 3/ABI 2
declarations derive checker types and match captured registrations before Init.

Storage is explicitly installed by trusted embedders on Windows/Linux. Separate
grants control Windows notifications/tray; GTK reports these unsupported. Native
payloads stay out of framework-owned diagnostics. These interfaces do not
establish encryption, rollback, secure deletion or application protocol security.
See [the complete service contract](NATIVE-SERVICES.md).

## Qualification evidence

Shared semantics are tested headlessly. Native tests and desktop observations supplement them for the [declared platform profiles](PLATFORMS.md); CI does not certify arbitrary devices, IME providers or screen-reader usability. The [performance guide](PERFORMANCE.md) describes workload limits and measurement boundaries.
