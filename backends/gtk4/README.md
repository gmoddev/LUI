# GTK4 native host

GTK host for Linux. GTK >=4.10, a C++17 toolchain, CMake >=3.24, pkg-config and OpenSSL 3 development libraries are required. Dependencies retain their existing pins. The [platform matrix](../../docs/PLATFORMS.md) records tested profiles and limits.

```sh
cmake -S . -B build/gtk4 -DCMAKE_BUILD_TYPE=Debug -DLUI_BUILD_GTK4=ON
cmake --build build/gtk4 --parallel 2 --target LuiGtkHost LuiGtkTests
ctest --test-dir build/gtk4 -R LuiGtkTests --output-on-failure
build/gtk4/backends/gtk4/lui-gtk --project examples/modular/lui.json
dotnet tools/cli/bin/Release/net10.0/lui.dll run examples/modular/lui.json --mode project --host build/gtk4/backends/gtk4
```

Tests require xvfb-run, xauth and dbus-run-session. Normal launching requires a Linux graphical session. `--script examples/hello.luau` runs a single script under Development diagnostic policy. Native projects additionally require explicit `--allow-native`; preparation/checking never load their libraries.

## Native temporary text tag probe (2026-11-03)

`LuiGtkTemporaryTextTests` applies a color tag to one `GtkTextBuffer`, the
buffer type used by `GtkTextView`. The GTK 4 API [states that Undo tracks text
content, not tags or marks](https://docs.gtk.org/gtk4/method.TextBuffer.set_enable_undo.html).
The fixture confirms LF/Unicode text, selection, modified flag, applied tag,
one native Undo, a tag update while Redo remains available, and one native Redo.
It uses `begin_irreversible_action` only to clear synthetic setup history,
never during styling. `ctest -R LuiGtkTemporaryTextTests` uses Xvfb.

Selected Linux worker profile: LuiF4, GTK 4.14.5, GCC C++17 `-O2 -Wall -Wextra
-Werror`, Xvfb/DBus, fatal GTK critical diagnostics. Source SHA-256
`C11AF6006BF97B3606FDAE8C027C1FCAFA69EC606B65DE098B3E0F5C65047242`,
binary SHA-256
`4E471A9EBDD7AC27787784FD5FDC3CD007AD9DFD9E065EB0B11AD102759D34B3`;
both matched before and after the run. The text/selection/Undo/Redo probe
passed. A 128-range restyle over about 32,000 UTF-8 bytes measured p50/p95/p99
313.422/377.553/9594.959 microseconds over 128 warmed samples. Another
two-job build ran concurrently, so these are workload-scoped CPU observations,
not an isolated performance budget or display latency. GTK tag integration into
a public styled editor, IME and assistive-technology usability remain unqualified.

Nine native classes use canonical logical geometry, queued owner-thread signals, generation-safe teardown, themes, declared images and live AT-SPI metadata. GTK logs/startup failures are nonmodal and contain generic messages. ClipboardService uses its capability grant, bounded UTF-8 writes and asynchronous reads (1 MiB, ten seconds, 32 outstanding platform requests), canceled at runtime retirement. DialogService uses GtkFileDialog with its existing grant, one nonmodal chooser per generation, a visible native parent and retirement cancellation. Selection returns a local UTF-8 path; dismissal returns nil/nil. Images must be trusted declared assets.

VS Code **LUI: Open Native Preview** supports Linux schema v2 projects. Set `lui.nativeHostPath` to the GTK executable/build directory or installed prefix, or use discovery at `build/gtk4/backends/gtk4/lui-gtk`. The local workspace must be trusted; no manifest grants native-code trust. Native providers require manual owner launch with `--allow-native` (CLI option: `--allow-native true`). Linux schema v1 native launch is explicitly refused.

## Application folders

Build CLI with .NET 10, then install and compose a schema v2 package:

```sh
cmake --build build/gtk4 --parallel 2 --target LuiGtkHost LuiProjectTool
cmake --install build/gtk4 --prefix "$PWD/build/gtk-prefix" --component GtkHost
dotnet build tools/cli/Lui.Cli.csproj --configuration Release -m:2
dotnet tools/cli/bin/Release/net10.0/lui.dll build examples/modular/lui.json --mode project --host build/gtk-prefix --project-tool build/gtk4/LuiProjectTool --output build/modular-linux
dotnet tools/cli/bin/Release/net10.0/lui.dll verify build/modular-linux/Application --project-tool build/gtk4/LuiProjectTool
build/modular-linux/Start-LUI.sh
```

Alternatively run `sh scripts/Publish-Linux.sh examples/modular/lui.json build/modular-linux` for a Release build. Output must be a new directory. Stage as the intended owner; captured application content uses private owner permissions. A folder handed from root to another user needs ownership transferred to that user.

The resulting folder launches without .NET and supports relocation, including paths with spaces. It includes bin/lui-gtk, lib/libLuiRuntime.so, declared Application content and MIT/dependency notices. GTK 4.10+, OpenSSL 3 and their system dependencies must be installed on the target Linux distribution. This is not an AppImage or signed release. Native providers still need an explicit owner `--allow-native` launch argument.

See the [platform matrix](../../docs/PLATFORMS.md). VM/CI evidence does not qualify bare-metal devices, Wayland, portals or full screen-reader usability.
