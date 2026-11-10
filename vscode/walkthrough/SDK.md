# Choose your SDK

Extract the Windows x64 preview SDK to a permanent folder. Use **LUI: Choose SDK** and select the folder containing `SdkManifest.json`.

The SDK supplies the runtime, checker, native host and layout inspector together. App authors do not compile the framework or install the .NET SDK. Follow the SDK's system dependency and integrity instructions before running it.

The extension checks the installation using `lui doctor`. It never downloads or upgrades an SDK when a project opens.
