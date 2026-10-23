# Browser host

This experimental host runs LUI's shared C++/Luau runtime in a dedicated wasm32
worker. A semantic DOM projection applies acknowledged batches. The private
BrowserHostV1 protocol is separate from native extension ABI v1/v2.
The selected experimental Linux/X11/software profile passes its acceptance gate;
the [platform matrix](../../docs/PLATFORMS.md) states its tested scope and limits.

## Build and package

Use the exact toolchains in [Toolchain.json](../../tests/browser/Toolchain.json).
After selecting Emscripten in the process environment:

```sh
emcmake cmake -S . -B build/browser -DCMAKE_BUILD_TYPE=Release
cmake --build build/browser --parallel 2 --target LuiBrowserProbe
```

Build `LuiBrowserTool` in a normal native configuration, then run:

```sh
build/native/LuiBrowserTool examples/modular/lui.json build/browser build/ModularWeb
python3 examples/conversation/Prepare.py
build/native/LuiBrowserTool examples/conversation/lui.browser.json build/browser build/ConversationWeb
```

The output must not exist. The offline builder uses shared immutable capture,
path/alias/lock validation and compilation. It emits captured sources/assets,
runtime identities, dependency notices and a separately versioned deployment
descriptor. Desktop package profiles are unchanged. Every source is compiled
again inside the worker; downloaded bytecode is never an input.
Digests establish consistency, not publisher trust against a compromised origin.

Serve the complete output directory over HTTPS or loopback HTTP. Use
`application/wasm` for WASM, JavaScript MIME for modules and JSON MIME for the
descriptor. The tested policy is:

```text
default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; worker-src 'self';
style-src 'self'; img-src blob:; connect-src 'self'; object-src 'none'; base-uri 'none'
```

The page fetches declared same-origin files only, with omitted credentials,
redirect refusal, bounded streamed reads and digest checks. There are no runtime
URL imports, CDN dependencies, service workers or implicit permission prompts.
The host starts Restricted diagnostics before VM creation. DOM UI is intentionally
visible to its embedding origin; it is not a confidentiality boundary.

## Profile

Window, Frame, ScrollingFrame, TextLabel, TextButton, CheckBox, Slider,
ProgressBar, TextBox and ImageLabel have DOM mappings. Layout, scrolling, style,
signals, modules and lifecycle remain owned by the shared runtime.
Text carries observed u64 revisions as two u32 words; DOM UTF-16 selection is
translated to scalar positions. Rejected edits reconcile to canonical state.
Secret editing suppresses composition/selection/copy payloads as required.
Secret multiline editing is refused before assigning text: HTML has no multiline
password control. Collapsed caret changes and selected ranges both reconcile to
the shared scalar text state.
Image decoding accepts captured PNG up to eight bits per channel only, with generation-owned revocable URLs.

All capability grants are false. Raw networking, HTTP/Fetch, hosted endpoints,
native providers, storage, clipboard/dialog services, notifications/tray and a
second Window are refused. Ordinary TextBox editing remains separate from the
unavailable clipboard service. No browser gesture is preserved as a worker grant.

Admission bounds: 256 captured files / 8 MiB per file / 32 MiB aggregate;
256 MiB WASM maximum and 32 MiB VM maximum (manifest may narrow);
4,096 DOM nodes / 16 MiB page property data; 1 MiB / 4,096 staged operations;
256 KiB messages, one frame in flight; 256 events / 1 MiB queued input.
Images admit 32 requests, 2 active decodes, 1 MiB encoded input and 1,048,576
pixels each; decoded cache is at most 16 entries / 32 MiB including pinned data.
Failures clear mounted resources with a fixed nonmodal status. Disposal is
idempotent and retirement has a bounded forced-stop fallback.

## Verify

Set `LUI_PLAYWRIGHT_MODULE` and `PLAYWRIGHT_BROWSERS_PATH` to the pinned installed
Playwright and engine cache. Test runners use an ephemeral loopback server and
close their browsers/server afterward.

```sh
node tests/browser/Protocol.mjs
node tests/browser/Artifacts.cjs build/browser
node tests/browser/Scheduling.cjs build/browser
node tests/browser/Probe.cjs build/browser
node tests/browser/Conformance.cjs build/browser
node tests/browser/Acceptance.cjs build/browser
node tests/browser/Packages.cjs build/browser
node tests/browser/Policy.cjs build/browser
node tests/browser/Performance.cjs build/browser
```

Package tests expect `build/browser/Packages/Modular` and
`build/browser/Packages/Conversation`, generated with the commands above.
`LUI_BROWSER_ENGINES` selects a comma-separated engine subset; default is
Chromium, Firefox and WebKit. `LUI_BROWSER_HEADED=1` runs headed on an available
display. Native `LuiCapturedProjectTests` checks capture/hash/activation parity.

`bash scripts/Qualify-Browser.sh <browser-build> <native-build> [CMake options]`
checks the pinned toolchains, builds native capture parity and Release WASM with
link-time optimization, prepares isolated sample inputs and runs all suites.
Use an available display with `LUI_BROWSER_HEADED=1` for foreground keyboard and
pointer tests. The selected Linux software profile also sets
`LUI_BROWSER_SOFTWARE=1`, `GDK_BACKEND=x11`, `LIBGL_ALWAYS_SOFTWARE=1`,
`MOZ_ENABLE_WAYLAND=0` and `WEBKIT_DISABLE_COMPOSITING_MODE=1`, unsets
`WAYLAND_DISPLAY`, and runs inside task-owned Xvfb (1280x1024x24, `-noreset`),
D-Bus and `xfwm4 --compositor=off` sessions. Run one qualification workload at a
time. Preserve caches; compilation uses two jobs. Debug timings do not qualify
the Release workload budget.

Protocol progress uses acknowledged DOM commits and bounded microtask admission;
it remains independent of compositor callbacks. Physical input tests require
real animation callbacks and ordinary Playwright actionability. The scheduling
fixture deliberately withholds those callbacks. The control regression injects
blur; Acceptance separately disables Chromium focus emulation and changes actual
tabs to check focus-loss cancellation. Firefox/WebKit native tab transitions
remain outside that check. In-page latency brackets DOM event admission through acknowledged DOM,
excluding automation transport; it does not measure physical display latency.
Fixed numeric worker aggregates report core WASM calls, dispatch-target misses
and current/peak linear-memory capacity. Acceptance runs shared native/WASM
semantic inputs, pointer-local coordinates/cancellation, focused collection
anchoring/retirement, nested scrolling, native browser editing and live channel
failures. Artifact checks inspect the emitted import/export and memory sections.
`tests/browser/Modules.json` supplies identical module/cache/isolation/cycle,
initializer-effect and nested task-order inputs to native capture parity and
all browser engines. Each case runs in two fresh generations; native checks also
verify that queued tasks wait for pumping and nested tasks wait for another turn.

For the separate selected real-input/AT sessions, set
`LUI_BROWSER_NATIVE_INPUT=1` in an exclusively task-owned display/D-Bus environment.
At the end of the suite `tests/browser/NativeInput.sh` replaces the task window manager and owns its window manager,
IBus/libpinyin, Orca and speech dispatcher children. It needs `xfwm4`, `xdotool`,
IBus with its GTK modules/libpinyin, Orca 46.1, Python GI/pyatspi and Speech
Dispatcher's dummy module with the ALSA null device. Firefox is the default
selected engine (`LUI_BROWSER_NATIVE_ENGINE` overrides it). The input-provider
and Orca sessions use separate browser contexts. The fixture checks real Pinyin
composition/commit, caret/key delivery, keyboard activation, AT-SPI names/roles/
descriptions/focus and allowlisted Orca speech submissions. Audio quality and
human screen-reader usability are outside this automated session.

High-contrast media emulation is checked on Chromium/Firefox; WebKit's automation
profile does not provide that emulation. Linux Playwright WebKit does not qualify
Safari or mobile. Other OS/IME/AT combinations, Firefox/WebKit native tab switching, frozen-page
deadlines and general WCAG conformance remain unqualified. HTTP and the other
listed services remain explicitly unsupported. The selected acceptance pass
does not qualify frozen-page deadlines, broader providers or a stable SDK.
