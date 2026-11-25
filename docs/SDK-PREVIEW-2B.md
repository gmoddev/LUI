# LUI preview.2b

Preview.2b is a smaller package of the preview.2 API. It shares one private .NET
runtime between the SDK tools and uses the Windows UI dependency without the
unused machine-learning packages. No global .NET installation or compiler is
required. Existing preview.1/2 downloads and project pins stay unchanged.

Extract the complete archive into a permanent folder. Keep Cli, Tools, Editor,
Libraries, Examples, notices and SdkManifest.json together. On Linux, keep Native
too and extract the tar.gz with executable permissions intact.

On Windows x64:

```powershell
.\lui.cmd doctor
.\lui.cmd new ..\MyApp
.\lui.cmd check ..\MyApp\lui.json
.\lui.cmd run ..\MyApp\lui.json
```

On experimental Linux x64:

```sh
./lui doctor
./lui new ../MyApp
./lui check ../MyApp/lui.json
./lui run ../MyApp/lui.json
```

Linux targets Ubuntu 24.04 x64 with GTK 4.14 and X11. Install GTK 4.10 or later,
OpenSSL3 and the native libraries listed in LinuxRequirements.json. This is a
relocatable folder, not an AppImage. Other distributions, Wayland, physical input
and assistive technology remain unqualified. See the release page for the exact
checks performed on these artifacts.

In VS Code, install Editor/LUI.vsix, open your project folder, and run LUI: Choose
SDK to select this extracted SDK. Use Run App and explicit Restart App after saving.
The preview.2 API reference still applies:
https://gmoddev.github.io/LUI/docs/sdk-0.1.0-preview.2/learn/first-project/
Older tutorial downloads pin their original SDK; they do not silently upgrade.
Create a new project with this SDK to obtain its exact preview.2b pin.

Run `lui build --output NEW_DIRECTORY` from a project to share an app folder.
Recipients launch Start-LUI.cmd on Windows or Start-LUI.sh on Linux and keep the
complete folder and notices together. Windows SDK tools now share the Cli folder;
component paths are discovered through SdkManifest.json.

This is an unsigned experimental preview. Checksums verify unchanged bytes, not
publisher identity. Ordinary downloaded-file trust, signing, broader OS/input and
human usability checks remain open. Use only a publisher you trust.
