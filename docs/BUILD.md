# Build and run

Clone the public source from `https://github.com/gmoddev/LUI.git`. Run commands from its root. Dependency fetching happens during build preparation; application evaluation never fetches undeclared modules or assets.

## Windows x64

Requirements: Visual Studio 2022 or 2026 with C++ and CMake components, Windows SDK 10.0.26100, .NET SDK 10.0.402, Python 3.12+, Node.js 24.22.1 and Perl for the pinned static OpenSSL build. Use PowerShell 7. The script discovers Visual Studio's CMake and builds managed projects with the separately installed dotnet CLI.

```powershell
./scripts/Build-Windows.ps1
```

Launch the hello example from the managed host output (the platform-specific output layout can vary with MSBuild settings):

```powershell
./backends/winui3/bin/x64/Debug/net10.0-windows10.0.19041.0/win-x64/Lui.WinUI.exe examples/hello.luau
```

The native runtime DLL is copied into the host output by its project. A schema v2 application can be launched with `--project examples/modular/lui.json` instead of a script path.

For a self-contained modular application folder:

```powershell
./scripts/Publish-Windows.ps1 -ManifestPath examples/modular/lui.json -Mode project -OutputDirectory build/package/modular
```

Run `Start-LUI.cmd` in the resulting folder. Select a fresh output path for each staging operation.

## Linux headless

Requirements: CMake 3.24+, a C++17 compiler, Git, OpenSSL 3 development files, .NET 10 and Node.js 24 for managed/editor tests. Python 3.12+ is used by qualification scripts and selected fixtures. Install the exact SDK/Node versions in global.json and .node-version; qualification rejects other versions.

```sh
cmake -S . -B build/linux-x64 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-x64 --parallel 2 --target LuiNativeQualification
ctest --test-dir build/linux-x64 --output-on-failure
```

## Linux GTK4

Install GTK 4.10+ development files and pkg-config in addition to the headless requirements. See the [GTK host guide](../backends/gtk4/README.md) for launch and packaging. Native CI uses Xvfb, a private D-Bus session and AT-SPI tooling.

```sh
cmake -S . -B build/gtk4 -DCMAKE_BUILD_TYPE=Debug -DLUI_BUILD_GTK4=ON
cmake --build build/gtk4 --parallel 2 --target LuiGtkQualification
ctest --test-dir build/gtk4 -R '^LuiGtkTests$' --output-on-failure
```

For the full Linux headless/tooling gate, run `bash scripts/Qualify-Linux.sh`; it includes the pinned Rust fixture. For native GTK mapping, scales 1/2, live AT-SPI and relocated package gates, install Xvfb/D-Bus/AT-SPI test dependencies and run `bash scripts/Qualify-Gtk.sh`. Both accept a build directory followed by extra CMake configure arguments to reuse dependency caches. Windows full qualification uses `./scripts/Build-Windows.ps1 -NativeServiceProof`.

## Dependencies and generated files

[Dependencies.json](../cmake/Dependencies.json) pins Luau, Asio, JSON and the Windows OpenSSL source. [global.json](../global.json) and [.node-version](../.node-version) select SDK 10.0.402 and Node 24.22.1 for qualification. [TLS-DEPENDENCY.md](TLS-DEPENDENCY.md) records the OpenSSL provider profile. An existing Luau checkout can be supplied with `-DLUAU_SOURCE_DIR=<directory>`; Windows build scripts require the pinned revision. Preserve incremental dependency caches.

On Windows, `scripts/Generate-Types.ps1 -SchemaDump <LuiSchemaDump.exe> -Root .` regenerates [API.md](API.md), [types](../types/LUI.d.luau) and [schema](../types/schema.json). Build scripts/CI check generated consistency. Include the MIT and third-party notices in distributed folders.

See [testing](TESTING.md), [platform support](PLATFORMS.md) and the [modular example](../examples/modular/README.md) for CLI/editor commands and scope limits.

## Owned native service proof

Install Rust 1.98.0 through rustup as pinned in rust-toolchain.toml, then configure with
`-DLUI_BUILD_RUST_PROOF=ON`. `LuiRustProof` builds the crate in debug and release
with locked offline inputs and two Cargo jobs. `LuiNativeRuntimeTests` and
`LuiOwnedProjectTests` depend on that target. CTest runs interoperability,
captured-project and isolated quarantine cases. Windows's full script enables
this with `-NativeServiceProof`. No crate downloads are required.

## Experimental browser profile

The [browser host guide](../backends/browser/README.md) describes captured
native-free applications, Release WASM builds, deployment/CSP, limits and the
pinned Chromium/Firefox/WebKit test profile. After selecting the pinned Emscripten
SDK, Node and Playwright installations, run:

```sh
bash scripts/Qualify-Browser.sh build/browser build/browser-tools-native
```

Foreground keyboard/pointer and selected real IME/Orca sessions need the isolated
display/D-Bus/software environment documented in the host guide. Runtime native
services, HTTP and multiple windows remain unsupported. This experimental
profile does not qualify Safari/mobile or general screen-reader usability.
