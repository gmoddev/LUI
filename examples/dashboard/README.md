# Launcher dashboard demo

This local LUI sample is inspired by the navigation and discovery flow of the Modrinth App. It uses fictional projects and makes no network requests, downloads, or Minecraft launches.

The demo exercises native buttons, text input, filters, pagination, dynamic Instance creation and destruction, delayed scheduler work, progress bars, a slider, a checkbox, accessibility names, and the system theme service. Open **Discover**, search for a project, select a card, and choose **Install to library** to watch a simulated download. **Play** shows installed projects; **Settings** offers session-only controls.

Build a self-contained folder with:

```powershell
.\scripts\Publish-Windows.ps1 -ManifestPath .\examples\dashboard\dashboard.manifest.json -OutputDirectory .\build\package\my-dashboard -CppRuntimeDirectory '<VisualStudio>/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT'
```

Then run `Lui.WinUI.exe --manifest dashboard.manifest.json` from the new package folder. Choose a new output directory for each build. The sample uses native control styles and a fixed layout.
