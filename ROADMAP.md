# Product roadmap

LUI is experimental. The [platform matrix](docs/PLATFORMS.md) distinguishes tested configurations from planned support. These are acceptance gates, not delivery dates.

## Available in the current experimental profile

- Shared Instances, reflection, lifecycle, layout, input and capability enforcement.
- Windows WinUI 3 and selected Linux GTK4 application workflows.
- Declared modular projects, static checking, preview/editor tooling and relocatable application folders.
- Bounded TCP/UDP/HTTP/TLS, native primitive methods/signals and platform services.
- Scrolling/clipping/stacking, portable styles, text/password input, async declared images and virtual collections.
- Experimental owned native buffers, managed handles, async requests/progress and safe retirement; scoped Windows/Linux storage and Windows notifications/tray.

## Owned native services

The separately versioned x64 ABI v2 implements owned binary buffers, opaque managed handles, asynchronous requests, cancellation, ordered progress, backpressure, fixed errors and cooperative retirement. ABI v1 remains supported. V2 remains experimental and unfrozen; see [the service guide](docs/NATIVE-SERVICES.md).

The independent Rust fixture covers Windows/Linux debug/release interoperability, captured loading and stuck shutdown. Storage is explicitly installed by a trusted embedder. Windows supplies notification/tray integration; GTK reports them unsupported. Application cryptography, data protection and protocols require application-specific acceptance.

## Qualified experimental browser profile

The experimental shared-runtime worker and DOM host now have captured modular
and conversation deployments and a completed selected Linux/X11/software
acceptance pass in Chromium/Firefox/Playwright WebKit. Shared native/browser
module/cycle/isolation/task fixtures, full native regressions, published DOM
latency ceilings, lifecycle stress, CSP/refusals and selected Firefox real IME
and Orca sessions pass. [The browser guide](backends/browser/README.md) and
[platform matrix](docs/PLATFORMS.md) state exact scope and remaining limits.
HTTP, native raw sockets, arbitrary extensions and other uninstalled browser
services remain explicitly unavailable.

## Next: Android and iOS

Select host approaches and qualify each OS independently on real devices, including touch, IME, safe areas, accessibility, suspend/resume, background restrictions and process recreation.

## Stable distribution

Establish signed packages, update authenticity/recovery, dependency provenance, reproducibility, compatibility/migration guidance and a declared support matrix. Accessibility, performance and security qualification accumulate throughout development; experimental platforms do not inherit stable status.

macOS and additional interop/rendering options remain future evaluation work.
