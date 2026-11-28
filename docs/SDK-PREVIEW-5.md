# LUI 0.1.0-preview.5

Preview.5 is an experimental SDK for Windows x64 and Linux x64. The CLI
can check, stage and package a Luau project with its declared modules,
assets and capabilities. GTK4 is required for Linux desktop applications.
The headless host has no GTK dependency.

## Start or migrate a project

1. Download the archive and matching checksum from the project's release page.
   Compare the archive's SHA-256 using `Get-FileHash` on Windows or
   `sha256sum` on Linux. Both archives are unsigned. A matching digest
   checks bytes against that release record. Windows can still show an
   unknown-publisher or downloaded-file warning. Review the source and
   origin before deciding whether to run it. Keep platform protections enabled.
2. Extract the complete SDK into a fresh, short path. On Linux preserve
   executable permissions. Run its CLI with `doctor --format json`.
3. Keep your previous SDK and make a copy of your project. Run
   `lui new Scratch --sdk <new-sdk>`, review the generated
   `LuiToolchain.json` and migrate the SDK pin in the project copy.
   Review provider and package selection before regenerating locks.
4. Check and build with the pinned SDK. Test frozen restore with a
   populated cache. Launch the resulting standalone application outside
   the SDK directory and exercise its normal workflows.

## Since the public Preview.2b SDK

- Select filesystem, networking, subprocess and worker services through
  declared capabilities. Add SQLite, compression, cryptography, regex
  or TOML only in applications that need them.
- Use local or archived packages, immutable source imports, platform
  selection and offline frozen restore. Each native payload needs a
  matching platform, approved digest and explicit host authority.
- Dock retained controls in ordered tabs and nested splits, float and
  redock panels, or use the existing workspace unchanged. See the staged
  `dock-editor` and `dock-workspace` projects. Wayland controls global
  window placement. Same-window docking and explicit redocking remain available.
- Present immutable images and atlas regions or submit bounded frames to
  a `FrameSurface`. Keep producer backpressure and release superseded
  frames. Native publication, compositor presentation and physical display
  are distinct observations.
- Trusted FFI supports prepared calls, generated bindings, scoped
  synchronous callbacks and explicitly admitted headless worker pools.
  These capabilities remain experimental. The supported ABI profiles are
  Microsoft x64 and System V AMD64. Use a C pointer wrapper for
  layout-changing packed values on System V. A blocked in-process call
  holds its thread. The broker supplies a separate child and timeout
  boundary under the same OS account.

The SDK includes an offline VS Code extension, a package guide and five
developer journeys under `Examples/DeveloperJourneys`.
