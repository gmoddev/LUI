# Experimental Android control host

The Android VM slice projects Window, Frame, ScrollingFrame, TextLabel,
TextButton, CheckBox, Slider, ProgressBar, TextBox and ImageLabel into native
Views, with the shared C++/Luau runtime on a fixed HandlerThread. It supplies
canonical clipping/paint order, basic style/text mapping, bounded PNG decoding,
usable-window insets, cooperative task pumping, stable pointer/key forwarding,
canonical drag/fling and observed text/composition. Physical phone support,
full gesture/IME/accessibility qualification and physical-device performance remain open.
The offline fixture declares only Storage and uses the existing built-in provider
under a dedicated app-private no-backup directory. Network and arbitrary plugins
are refused. The APK requests no permissions. iOS has no implementation here.

The owner thread validates a bundled captured project and its compiler/schema/
source identities. Bundle version 2 uses the private MobileHostV1 profile and
strict base64 source/assets; projection envelopes remain version 1. It sends
bounded copied UTF-8 projections to the main thread.
Acknowledgment, generation, surface epoch, input sequence and target identity
guard native input. Queued input retains its observed frame instead of being
rebased after acknowledgment. Geometry/content observations permit continuing
queued edits and gestures only while their original context remains valid.
Script input revisions win over pending edits. Per-editor edit acknowledgments
protect newer local typing, selection and preedit from older replies. One checkbox action reports its value and one
activation within the same admitted owner dispatch. Activity recreation reprojects canonical state without
restarting the runtime. Pausing detaches input; finishing queues owner retirement.
The five-second host-job watchdog removes the UI and requests cooperative cleanup.
It cannot terminate a native thread: one retained/quarantined session blocks
replacement until retirement actually completes. Never call JNI from the UI.

The selected limits are 128 controls, 192 KiB retained callback data, one
256 KiB complete frame, 256 ordinary inputs / 1 MiB plus 64 reserved terminal
inputs / 64 KiB, 32 active pointer identities, 16 portable keys, 4,096 pending scheduled tasks,
and at most eight native deliveries followed by 32 task entries per 16 ms owner
tick under one 8 ms soft dispatch target.
Delays above one year are refused by this opt-in profile. A callback is not
preempted at 8 ms. VM interrupt limits and the host watchdog remain necessary.
Ordinary desktop/browser pumping is unchanged. Images admit two asset/decode/
publication pipelines, with two decoder workers and an eight-job hard queue.
Publication permits remain held until the main thread consumes a result. Limits
are 4 MiB per decoded image and 32 MiB retained decoded ownership; pending decoded
and Android display-list memory are accounted separately. Retirement cancels
queued work and discards late results without recycling displayed bitmaps.

Use JDK 17, Gradle 8.11.1, AGP 8.9.2, Kotlin 2.1.20, Android SDK 36 revision 2,
Build Tools 36.0.0, NDK 28.2.13676358 and SDK CMake 3.31.6. The exact development
profile is recorded in [Toolchain.json](../../tests/mobile/Toolchain.json).
Android 11/API 30 is the deployment floor. The debug APK includes ARM64 for
phones and x86_64 for emulator checks. Luau Compiler/VM are linked statically;
no Luau JIT, desktop TLS/sockets or extension loader is installed. Native-service
infrastructure is linked for built-in storage; dynamic provider grants are refused.

Automatic system contrast observation uses Android 14/API 34's `UiModeManager`
contrast API. That observation is unavailable on API 30–33; native light/dark
presentation and font scaling remain available. The instrumentation record reports
this provider limit separately from its font-scale assertions. A system text
contrast preference does not imply a qualified application-wide contrast profile.

On a Linux build host with those tools already installed:

```sh
export JAVA_HOME=/path/to/jdk17
export PATH="$JAVA_HOME/bin:$PATH"
export ANDROID_HOME=/path/to/android-sdk
export LUI_GRADLE=/path/to/gradle-8.11.1/bin/gradle
bash scripts/Qualify-Mobile.sh build/mobile
```

Optional `LUI_LUAU_SOURCE` and `LUI_JSON_SOURCE` select trusted pinned dependency
caches; otherwise CMake fetches the repository's exact dependency pins. The gate
generates `Assets/Fixture.json`, `Conversation.json` and `Modular.json` using the host compiler, recompiles the exact
captured source on Android before entry, builds both APKs, runs native ownership
and bounded scheduler/control tests and checks ELF plus APK 16 KiB alignment. It neither installs an SDK nor
publishes or installs an APK. Do not commit generated assets, APKs, caches,
local.properties or signing keys. The generated APK includes LUI/Luau, JSON,
PicoSHA2, Kotlin, JetBrains annotations 13.0 and Android NDK notices. Kotlin's unmodified Apache license and
notice come from its pinned
[2.1.20 license](https://github.com/JetBrains/kotlin/blob/v2.1.20/license/LICENSE.txt)
and [notice](https://github.com/JetBrains/kotlin/blob/v2.1.20/license/NOTICE.txt).
NDK notices are copied from the selected r28c installation, including its static
C++ runtime/toolchain attribution. This is a feasibility dependency inventory,
not the final store distribution review.

On an explicitly selected test device/emulator, install
`app/build/outputs/apk/debug/app-debug.apk` and
`app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk`, then run:

```sh
adb -s SELECTED_SERIAL shell am instrument -w -e class dev.lui.feasibility.HostTest \
  dev.lui.feasibility.test/android.test.InstrumentationTestRunner
```

Fourteen instrumentation tests exercise queued native activation while an acknowledgment is held, button activation, Activity recreation,
state retention, control/assets/scalar text, native clipping/stacking hits,
Window request/actual sizing, rotation, docked keyboard occlusion, native
retirement, rapid edits/backspace and scalar/reversed ranges, InputConnection
composition/script-write precedence and stale connection refusal, pointer ID
reordering/cancel/target retirement, modifier pairing, bounded native clipboard
and password masking/refusals, drag/fling/nested scroll, connected accessibility
hierarchy/actions, focus loss and selected font/contrast preferences. A deliberately
blocked host job uses test-APK-only
reflection, has a bounded release deadline and confirms quarantine prevents
replacement. It adds no script-accessible test command. Use a dedicated test
session and record OS/device/page size, APK/source identities and actual results.
Emulator success cannot establish physical phone, IME or accessibility readiness.
The flat visual projection supplies canonical parent/child relationships to
native accessibility while preserving native control roles/actions. TextSize
uses system-scaled glyphs (sp) within unchanged canonical dp rectangles.
This does not qualify TalkBack usability, general large-font readability,
physical multi-touch/stylus behavior or named keyboard/reader providers.
InputConnection tests are protocol injection, not evidence of a physical IME.
Paste admits one bounded plain-text clip, without URI/rich-content coercion or
a ClipboardService grant. Password copy/cut and public preedit/selection remain
refused. The app uses a privacy cover while paused and secure-window presentation;
OS/keyboard telemetry and general capture protection are outside its guarantees.
Observation stamps include target/ancestor geometry and overlapping eligible hit
regions, retaining relative occlusion order. Unrelated row realization preserves
focused composer/resident-row editing; movement, removal and changed overlap still
invalidate the relevant input context. Canonical hit testing remains shared.

Five additional application tests run with `-e class dev.lui.feasibility.WorkloadTest`.
They select fixed bundled assets through non-exported activities; Intent payloads
cannot select source or paths. A conflicting bundle cannot replace a live owner.
The conversation uses unchanged pinned Scope/State/VirtualList modules with a
phone entry script, 10,000 model items and at most 48 resident rows. The modular
entry reuses the example's shared state, button and screen modules. Both request
a finite 1,000,000-interrupt/32 MiB sandbox; other manifests retain their narrower
limits. Native code is optimized with `-O2 -DNDEBUG` inside the debuggable test APK;
the runtime's quotas, validation and watchdogs remain active.

The application suite measures 200 mixed insert/edit transitions while preserving
native composer focus, resident-row anchoring/retirement, modular navigation,
image pressure/recreation/cancellation, invalid PNGs, timer/input storms and
110 complete generation/recreation/retirement cycles (ten warmups, 100 measured).
Numeric telemetry records acknowledged-native-apply latency, startup after initial
image settlement, whole normal owner-job time/over-eight-ms counts (including
transport/asset work and failures), successful admitted JNI-call time, test-only
`dumpsys gfxinfo` frame summaries/trailing samples,
decoded ownership and sampled process PSS. The latency ring retains 256 numbers.
The trailing frame ring is bounded by Android and is not the full transition
history. Frame durations over a refresh interval are a rendering proxy; neither that
metric nor native acknowledgment measures physical display/input latency.
The unchanged ceilings are five-second startup, 50/250 ms input p95/p99, and a
last-20 retired-PSS median at most 16 MiB above the first-20 median. These selected
emulator measurements do not qualify a physical phone, keyboard or reader.

The offline fixture persists only a bounded numeric preference, after successful
Write completion. It migrates version 1 records to version 2 and refuses malformed
or future records without overwriting them. Startup removes only a verified
uncommitted storage temporary under the provider's scope lock. Checkpoint success
does not depend on a final pause/stop callback; native text/password/preedit and
Activity view state are not serialized. Storage keeps its existing four-MiB file,
128-file and 64-MiB scope bounds. Backup/cloud/device transfer are excluded.
These are app-private bytes, not an application encryption protocol.

The lifecycle test exercises 50 warm home/resume cycles, stopped task pumping,
one-shot task resumption and emulator screen off/unlock. The normal suite also
covers committed-state reload, migration and corrupt-record refusal. For an
external cold-process test, run `dev.lui.feasibility.RecoveryTest#testPrepare`
alone, wait for its `ColdCheckpointReady` instrumentation status, force-stop the
selected test application, then run `dev.lui.feasibility.RecoveryTest#testRecover`
alone. Prepare deliberately waits for external termination and must not be run
as an ordinary passing test. Recover checks a different process, committed state,
discarded partial bytes and empty native password input. Use only the isolated
synthetic fixture: this test replaces its checkpoint. Provider retirement is
polled on the owner with a five-second deadline; pending provider resources keep
the registry quarantined and prevent replacement.

Android's [16 KiB guidance](https://developer.android.com/guide/practices/page-sizes)
requires checking native ELF and package alignment as well as runtime behavior.
The [AGP 8.9 compatibility table](https://developer.android.com/build/releases/agp-8-9-0-release-notes)
documents the Gradle/JDK build requirements; these tools are pinned for this
feasibility slice rather than selected dynamically.
