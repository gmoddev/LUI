# Product roadmap

LUI is experimental. The [platform matrix](docs/PLATFORMS.md) distinguishes tested configurations from planned support. These are acceptance gates, not delivery dates.

## Available in the current experimental profile

- Shared Instances, reflection, lifecycle, layout, input and capability enforcement.
- Windows WinUI 3 and selected Linux GTK4 application workflows.
- Declared modular projects, static checking, preview/editor tooling and relocatable application folders.
- Bounded TCP/UDP/HTTP/TLS, native primitive methods/signals and platform services.
- Scrolling/clipping/stacking, portable styles, text/password input, async declared images and virtual collections.

## Next: richer native services

Design owned binary buffers, opaque managed handles and asynchronous requests across a separately versioned C interface. Define cancellation, event ordering, backpressure, typed errors and shutdown/reload ownership before adding service adapters.

Acceptance requires Windows/Linux interoperability with a synthetic Rust service, failure and saturation tests, cancellation races, generation retirement, resource cleanup and continued extension ABI v1 compatibility. Notification/tray and storage adapters need individual platform qualification.

## Browser

Qualify Luau/WASM and a browser renderer against the existing semantics. Document browser permission, network and storage differences. Native raw sockets and arbitrary extensions are not promised in browsers.

## Android and iOS

Select host approaches and qualify each OS independently on real devices, including touch, IME, safe areas, accessibility, suspend/resume, background restrictions and process recreation.

## Stable distribution

Establish signed packages, update authenticity/recovery, dependency provenance, reproducibility, compatibility/migration guidance and a declared support matrix. Accessibility, performance and security qualification accumulate throughout development; experimental platforms do not inherit stable status.

macOS and additional interop/rendering options remain future evaluation work.
