# Build and run

Clone the public source from `https://github.com/gmoddev/LUI.git`. Run commands from its root. Dependency fetching happens during build preparation; application evaluation never fetches undeclared modules or assets.

## Windows x64

Requirements: Visual Studio 2022 or 2026 with C++ and CMake components, Windows SDK 10.0.26100, .NET 9, Node.js 20 and Perl for the pinned static OpenSSL build. Use PowerShell. The build script discovers the installed Visual Studio CMake executable.

```powershell
./scripts/Build-Windows.ps1
```

Launch the hello example from the managed host output (the platform-specific output layout can vary with MSBuild settings):

```powershell
./backends/winui3/bin/Debug/net9.0-windows10.0.19041.0/win-x64/Lui.WinUI.exe examples/hello.luau
```

The native runtime DLL is copied into the host output by its project. A schema v2 application can be launched with `--project examples/modular/lui.json` instead of a script path.

For a self-contained modular application folder:

```powershell
./scripts/Publish-Windows.ps1 -ManifestPath examples/modular/lui.json -Mode project -OutputDirectory build/package/modular
```

Run `Start-LUI.cmd` in the resulting folder. Select a fresh output path for each staging operation.

## Linux headless

Requirements: CMake 3.24+, a C++17 compiler, Git, OpenSSL 3 development files, .NET 9 and Node.js 20 for managed/editor tests. Python 3 is used by selected fixtures.

```sh
cmake -S . -B build/linux-x64 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-x64 --parallel 2
ctest --test-dir build/linux-x64 --output-on-failure
```

## Linux GTK4

Install GTK 4.10+ development files and pkg-config in addition to the headless requirements. See the [GTK host guide](../backends/gtk4/README.md) for launch and packaging. Native CI uses Xvfb, a private D-Bus session and AT-SPI tooling.

```sh
cmake -S . -B build/gtk4 -DCMAKE_BUILD_TYPE=Debug -DLUI_BUILD_GTK4=ON
cmake --build build/gtk4 --parallel 2
ctest --test-dir build/gtk4 --output-on-failure
```

## Dependencies and generated files

CMake pins Luau, Asio and JSON inputs. [TLS-DEPENDENCY.md](TLS-DEPENDENCY.md) records the OpenSSL provider profile. An existing Luau checkout can be supplied with `-DLUAU_SOURCE_DIR=<directory>`; Windows build scripts require the pinned revision. Preserve incremental dependency caches.

On Windows, `scripts/Generate-Types.ps1 -SchemaDump <LuiSchemaDump.exe> -Root .` regenerates [API.md](API.md), [types](../types/LUI.d.luau) and [schema](../types/schema.json). Build scripts/CI check generated consistency. Include the MIT and third-party notices in distributed folders.

See [testing](TESTING.md), [platform support](PLATFORMS.md) and the [modular example](../examples/modular/README.md) for CLI/editor commands and scope limits.
