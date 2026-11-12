# Application contract

**Deeper reference for app/framework contracts.** For a task-oriented explanation
and complete examples, use the [versioned API website](https://gmoddev.github.io/LUI/).
This document is useful for precise limits and implementation work, rather than
as a first-app tutorial.

This guide summarizes the current experimental profile. [API.md](API.md) and runtime reflection define implemented names and defaults. [Invariants](INVARIANTS.md) define architectural requirements; the [roadmap](../ROADMAP.md) distinguishes future work from supported behavior.

## UI and lifetime

Application code creates Instances, assigns properties and connects signals. Parenting defines UI ownership, destruction is idempotent and dead objects cannot receive callbacks. All callbacks and mutations run on the owner scheduler thread.

LUI resolves logical geometry, constraints, padding, lists/grids, scrolling, clipping and stacking. Native backends map these results rather than substituting toolkit layout. Styles use explicit geometry; autosizing and rich text are outside the current profile.

TextBox accepts strict UTF-8 without embedded NUL, up to 65,534 UTF-8 bytes. MaxLength may narrow the Unicode scalar limit. Selection offsets/counts are zero-based Unicode scalars, not UTF-16 units or grapheme indices. Preedit is separate from committed text and bounded to 4 KiB. Secret uses native password presentation, sticky sensitive classification and no public selection/preedit; multiline secret input is unsupported.

Declared PNG/JPEG images load asynchronously with cancellation and generation ownership. Inputs are bounded to 1 MiB encoded and 1,048,576 decoded pixels, with two decodes, 32 requests and a pinned-inclusive 16-entry/32 MiB decoded cache. No evaluation-time image download occurs.

## Dynamic CPU frames

FrameSurface is a GuiObject leaf with ScaleMode (Fit, Fill, Stretch), SubmitFrame,
ClearFrame and readonly FrameState. SubmitFrame copies an exact Luau buffer using
validated dimensions/stride. BGRA8Premultiplied is the only format; dimensions are
at most 1920x1080 and stride*height at most 8 MiB. Padding is removed in the copy.
Submission returns true/nil for admission or false with InvalidArgument,
LimitExceeded, WouldBlock, Closed or Unsupported. Admission never promises scanout;
rejection preserves the current frame and state.
One pending frame replaces older pending content; one upload may run per surface.
Ready means shared consumption, never visible scanout. Clear and destruction
invalidate stale uploads; clones start empty. Four live/retained retired surfaces,
three allocations per surface and 64 MiB including reserved native staging bound
ownership across runtime replacement. Supports('FrameSurface') requires a native
adapter; headless CPU tests do not qualify native presentation. A Windows native
qualification helper exists, while the production fenced presenter gate remains
incomplete; GTK frame presentation remains unimplemented. Browser/mobile, GPU
and zero-copy remain unsupported.

## Modular projects

Schema v2 declares exact-case application/dependency sources and pinned vendored content. Preparation captures immutable inputs. Source-bound `require` uses canonical identities, private globals and one non-nil export; false is cached. Initialization effects/yields and cycles are guarded. Reload destroys the previous generation before preparing its replacement.

Read [project hosting](PROJECT-HOST.md), [typechecking](PROJECT-TYPECHECK.md), [host declarations](PROJECT-DECLARATIONS.md), [native provider bindings](PROJECT-NATIVE-BINDINGS.md) and [packaging](PROJECT-PACKAGE.md).

Development editor tools offer explicit Restart App after saving project files.
It retires the current app before fresh SDK validation and launch; application
memory is discarded. Stop cancels pending launches. Development extension 0.1.5
also warns about undeclared source and offers Add File to Project. This stages
only Modules.Files in the unsaved manifest for review and saving. The shared
checker remains authoritative; neither action executes undeclared files.
Development extension 0.1.6 adds persistent process state and conservative restart
hints after editor changes. Running does not certify window readiness or an exact
source snapshot. Stop remains available before spawn and during bounded shutdown.

Development extension 0.1.7 also cancels inspector/type-check preparation when its
workspace or tool settings change. Retired clients cannot overwrite a replacement.
Setup reads accept bounded regular UTF-8 files and report fixed nonmodal guidance
without echoing malformed manifest content.

## Capabilities and diagnostics

Hosts grant capabilities explicitly; manifests can narrow them. VM execution/memory limits do not sandbox privileged native libraries. Policy-enabled projects choose diagnostics before VM creation. Sensitive fields use fixed redacted projections in framework diagnostics; explicit application output can still disclose application data. Development and Restricted modes have different inspection/checking permissions.

Development hosts add bounded fixed-code native/editor diagnostics for missing
grants, invalid capability tokens and startup failures. Runtime operation errors
can leave the window open. The isolated layout inspector remains unprivileged and
directs privileged projects to Run App. This transport carries no private source,
paths or arbitrary native stderr; published preview.1 still has its older generic
exit-code limitation. Refer to the [versioned capability guide](https://gmoddev.github.io/LUI/docs/sdk-0.1.0-preview.1/guides/capabilities/).

## Networking

TCP, UDP, outbound HTTP/HTTPS and hosted HTTP/TLS use runtime-owned transports, bounded admission and owner-thread completion. Access is capability-gated and host address/port policy applies. HTTP direction grants do not imply unrestricted raw sockets. TLS validates outbound identity and requires explicit native-host server credentials. See [TLS dependencies and trust](TLS-DEPENDENCY.md).

## Owned native integration

The experimental x64 ABI v2 coexists with ABI v1. Providers use copied immutable
bytes, session-scoped typed handles and bounded async requests/progress. Only
the owner delivers Luau callbacks. Terminal capacity is reserved before Start;
cancellation and retirement suppress stale work. Nonquiescent native code stays
pinned and charged rather than being force-unloaded. Structured schema 3/ABI 2
declarations derive checker types and match captured registrations before Init.

Trusted embedders explicitly install storage on Windows/Linux. Desktop SDK
projects in next SDK preview.2 can request `Storage.ApplicationId` alongside the Storage
capability; released preview.1 does not admit this field. The desktop owner provisions a scope bound to that identity and the
native project directory. Restarting retains data; copied and packaged roots
have separate scopes. Invalid identities or unavailable storage fail startup
without granting a fallback scope. See [SDK setup](SDK.md).
Separate grants control Windows notifications/tray; GTK reports these unsupported. Native
payloads stay out of framework-owned diagnostics. These interfaces do not
establish encryption, rollback, secure deletion or application protocol security.
See [the complete service contract](NATIVE-SERVICES.md).

## Optional development libraries

The [LuiWorkbench contract](../libraries/LuiWorkbench/README.md) describes
explicitly vendored development modules for the next SDK. Commands, document
buffers/save receipts, workspace presentation and preference adapters reuse
existing public runtime APIs. Module exported types and the library catalog
drive the separate development reference. Importing a module starts no service
or task. Storage remains ordinary unencrypted bytes with explicit host grants
and scope provisioning. These modules are not in SDK 0.1.0-preview.1; native
ABI and the released runtime schema remain unchanged.

The optional [NumericInput composition](../libraries/LuiUI/README.md) adds a
bounded text/step control using existing Instances and private canonical support
sources. Windows headless types, cleanup and owner-input checks pass. Linux and
actual native GUI/input remain unqualified for this component. Its catalog
records public module types and exact SupportFiles without a new native API.

## Qualification evidence

Shared semantics are tested headlessly. Native tests and desktop observations supplement them for the [declared platform profiles](PLATFORMS.md); CI does not certify arbitrary devices, IME providers or screen-reader usability. The [performance guide](PERFORMANCE.md) describes workload limits and measurement boundaries.
