# LUI Preview for VS Code

This development extension shows the live Instance tree, a diagram of resolved logical bounds, and read-only properties. Application Luau runs in separate preview or native host processes. The diagram is for layout inspection; the Windows host provides native rendering.

Build the preview host and native runtime, then open the LUI application folder in VS Code. Run **LUI: Start Preview** from the Command Palette or the LUI sidebar. The default manifest is `lui.json`. If the host and runtime are outside the repository, set `lui.previewHostPath` and `lui.runtimePath` to their absolute paths in VS Code settings. The host requires .NET 9.

Click an Instance in the Explorer or layout map to inspect it. The **Activate** action runs supported button callbacks. Resize a window viewport from the inspector. The Source section opens the Luau line that created or last changed an Instance when available. Saving the entry script reloads the preview into a fresh generation; saving the manifest restarts the host. Errors and Luau prints appear in the **LUI Preview** Output channel. Source-linked runtime errors also appear as VS Code diagnostics and in the inspector.

On Windows, run **LUI: Open Native Preview** to launch the real WinUI window for the same manifest. Set `lui.nativeHostPath` to `Lui.WinUI.exe` or its directory if the host is outside a repository build. The command needs a trusted workspace and the host's `LuiRuntime.dll`. Close the window or run **LUI: Stop Native Preview**. Native preview runs independently of the Explorer and must be reopened after source changes. Launch and exit failures appear in the Output channel and status bar; host logs are under `%LOCALAPPDATA%\LUI\Logs`.

To run this extension from the repository without packaging, open this folder in VS Code and use an Extension Development Host with `vscode/` as the extension development path. The extension uses Node.js built-ins and has no npm dependencies.

## Type diagnostics

Build the CMake `LuiTypeCheck` target and `tools/cli/Lui.Cli.csproj`, then run **LUI: Check Types**. It analyzes the saved manifest entry script against generated LUI definitions and publishes syntax/type errors in Problems. Preview does not need to be running. Repository builds are discovered automatically; otherwise configure `lui.cliPath` (the built `lui.dll` or executable), `lui.checkerPath`, and optionally `lui.definitionsPath` (defaults to `LUI.d.luau` beside the checker).

In a trusted workspace, checks run on entry-script/manifest open and save by default. Set `lui.autoCheck` to false to check manually. Editing cancels pending checks and clears outdated type errors; save to check again. Failures go to **LUI Preview** Output without popups. Preview runtime errors and type errors have independent diagnostic collections.

Initial analysis checks the entry script only, with strict mode as the default and Luau mode directives honored. It does not resolve modules or dynamically registered extension types. Use generated `ClassNameInit` annotations to check constructor table fields. This is a diagnostic tool, not a language server with autocomplete or unsaved-buffer checking. See [Foundation 3](../docs/FOUNDATION-3.md) for limits and the versioned result contract.
