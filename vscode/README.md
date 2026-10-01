# LUI Preview for VS Code

This development extension shows the live Instance tree, a diagram of resolved logical bounds, and read-only properties. Application Luau runs only in `lui-preview-host`, a separate process. The diagram is for layout inspection; use the Windows host for native rendering.

Build the preview host and native runtime, then open the LUI application folder in VS Code. Run **LUI: Start Preview** from the Command Palette or the LUI sidebar. The default manifest is `lui.json`. If the host and runtime are outside the repository, set `lui.previewHostPath` and `lui.runtimePath` to their absolute paths in VS Code settings. The host requires .NET 9.

Click an Instance in the Explorer or layout map to inspect it. The **Activate** action runs supported button callbacks. Resize a window viewport from the inspector. Saving the entry script reloads the preview into a fresh generation; saving the manifest restarts the host. Errors and Luau prints appear in the **LUI Preview** Output channel, with errors also shown in the inspector.

To run this extension from the repository without packaging, open this folder in VS Code and use an Extension Development Host with `vscode/` as the extension development path. The extension uses Node.js built-ins and has no npm dependencies.
