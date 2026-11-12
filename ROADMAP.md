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

## Dynamic Surface Presentation (DSP-1)

A dedicated FrameSurface adds bounded owned CPU pixels and latest-frame-wins
scheduling. Shared/headless qualification and the Windows native qualification
helper do not close the production fenced-presenter gate. GTK frame presentation
remains unimplemented. No browser/mobile, GPU, zero-copy, 4K or physical latency support
is implied. Completed advanced UI/native-service exits retain their original scope.

## Developer usability

Preview.2 provides a portable Windows SDK, offline VSIX, exact project pins,
workbench and command starters, eight optional modules and four examples.
Native Run, explicit Restart App, source enrollment and actionable diagnostics
support the development loop. The independent inspector visualizes shared layout
and refuses privileged project execution. Versioned docs retain both preview.1
and preview.2 tutorials, practical guides and generated API/library references.

The packaged SDK passes a native app launch, callback and exit, plus a real
VS Code journey on the selected Windows 10 LTSC virtual recipient without a
global .NET SDK or system C++ runtime. Earlier test installations remain in that
OS. Windows and Linux each pass 43 native suites and 59 editor tests. These
automated Linux checks do not qualify a fresh Linux SDK or real Linux editor
recipient. The binary download is an unsigned experimental Windows x64 preview.
Windows 11, physical Windows IME, signing, ordinary downloaded-file trust and
human first-session usability retain independent gates. Application-authored
tests and stronger saved-generation identity remain further development work.
New user-selected document IO, native menus and window customization need explicit
API design before implementation. See [the SDK guide](docs/SDK.md). Published
preview.1 assets, tag and project pins remain unchanged. GTK performance work is
separate from this SDK release.

## Android and iOS

Native Android Views and UIKit are the selected host approaches. Android has
experimental emulator coverage for controls, input, accessibility metadata,
offline storage/recovery and application workloads. Qualify each OS independently
on real devices, including touch, IME, safe areas, accessibility, suspend/resume,
background restrictions and process recreation. Emulator results do not qualify
physical Android devices; the Apple build/device path remains open.

## Stable distribution

Establish signed packages, update authenticity/recovery, dependency provenance, reproducibility, compatibility/migration guidance and a declared support matrix. Accessibility, performance and security qualification accumulate throughout development; experimental platforms do not inherit stable status.

macOS and additional interop/rendering options remain future evaluation work.
