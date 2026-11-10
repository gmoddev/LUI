# LUI Preview for VS Code

For the developer bundle, follow [the SDK guide](../docs/SDK.md): install the
included VSIX, choose the extracted SDK once, create a project, check and run.
The public download and clean-machine SDK release gates remain open. The
source-build instructions below remain available for framework contributors.

The extension shows an Instance tree, resolved layout diagram, read-only properties and source navigation. Application code runs in separate preview/native processes. The diagram inspects logical layout; native preview provides actual platform controls.

Build the runtime, `tools/preview-host/Lui.PreviewHost.csproj`, CLI and `LuiTypeCheck`, then open an application folder in a trusted VS Code workspace. Set `lui.manifestPath` if it is not `lui.json`. Repository outputs are discovered automatically; external builds can use `lui.previewHostPath`, `lui.runtimePath`, `lui.cliPath`, `lui.checkerPath`, `lui.definitionsPath` and `lui.nativeHostPath`.

## Commands

- **LUI: Choose SDK** / **Create Project** configure the matched SDK and scaffold a pinned modular application.
- **LUI: Inspect Layout** / **Stop Preview** manage the isolated inspector host.
- **LUI: Run App** / **Stop App** launch/close the real WinUI or GTK application. Stop and run again after source changes.
- **LUI: Check Types** checks saved declared sources. Saving a modular project source invalidates older generation/check/navigation results.

The inspector supports activation, resize, focus/blur, pointer phases and portable key press/repeat/release for supported GUI Instances. Focus before sending keys; Reset input cancels presses. These commands exercise signals, not native text editing/IME/shortcuts. Pointer signals do not automatically activate buttons. Reload suppresses stale input.

Errors appear in Problems, the inspector and LUI Preview Output without modal popups. Diagnostic visibility depends on owner policy. Development checking is explicit; Restricted static checking and privileged native providers in isolated preview are refused.

Project checking resolves literal imports and declared primitive service types using the shared native resolver. It is not an autocomplete language server or an unsaved-buffer checker. See [project typechecking](../docs/PROJECT-TYPECHECK.md), [project hosting](../docs/PROJECT-HOST.md) and the [modular example](../examples/modular/README.md).

Use an Extension Development Host with `vscode/` as its development path to run from source. The extension uses Node.js built-ins and has no npm dependencies; the preview host requires .NET 10.
