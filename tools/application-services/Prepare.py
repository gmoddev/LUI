"""Stage independent applications using only an installed, inventoried LUI SDK."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess


def Main():
    Arguments = argparse.ArgumentParser(description=__doc__)
    Arguments.add_argument("--sdk", type=Path, required=True)
    Arguments.add_argument("--output", type=Path, required=True)
    Arguments.add_argument("--app", choices=("file-desk", "network-desk", "window-desk"), required=True)
    Options = Arguments.parse_args()
    Sdk = Options.sdk.resolve(strict=True)
    Metadata = json.loads((Sdk / 'SdkManifest.json').read_text(encoding='utf-8'))
    if Metadata.get('SdkVersion') != '0.1.0-preview.3':
        raise SystemExit('This app needs the preview.3 candidate SDK. The SDK2 baseline is commit b0eb303.')
    Output = Options.output.resolve()
    if Output.exists():
        raise SystemExit("Choose a new output directory; existing projects are never overwritten.")
    Template = "json-client" if Options.app == "network-desk" else "workbench"
    Source = Path(__file__).resolve().parents[2] / "examples" / Options.app
    # The SDK owns the manifest, module declarations, vendored bytes and lock hashes.
    shutil.copytree(Sdk / "Examples" / Template, Output)
    shutil.copyfile(Source / "Src" / "Main.luau", Output / "Src" / "Main.luau")
    ManifestPath = Output / "lui.json"
    Manifest = json.loads(ManifestPath.read_text(encoding="utf-8"))
    if Options.app == "file-desk":
        Manifest["Capabilities"] += ["Clipboard", "Dialogs"]
    if Options.app == "network-desk":
        Manifest["Capabilities"] += ["network.raw"]
    else:
        Manifest["Storage"]["ApplicationId"] = "lui.example." + Options.app
    ManifestPath.write_text(json.dumps(Manifest, indent=2) + "\n", encoding="utf-8")
    Headless = None
    if Options.app == 'network-desk':
        Headless = Output / 'headless.lui.json'
        Configuration = json.loads(json.dumps(Manifest))
        Configuration['Capabilities'] = ['network.client', 'network.server']
        Configuration['NetworkPolicy'] = {'ClientLoopbackOnly': True, 'ClientPortMin': 18082, 'ClientPortMax': 18082,
                                          'ServerLoopbackOnly': True, 'ServerPortMin': 18082, 'ServerPortMax': 18082}
        Headless.write_text(json.dumps(Configuration, indent=2) + '\n', encoding='utf-8')
    Cli = Sdk / "Cli" / ("lui.exe" if os.name == "nt" else "lui")
    Flags = 0
    if os.name == "nt":
        ctypes.windll.kernel32.SetErrorMode(0x8003)
        Flags = subprocess.CREATE_NO_WINDOW
    Result = subprocess.run([str(Cli), "check", str(ManifestPath), "--sdk", str(Sdk)],
                            capture_output=True, text=True, creationflags=Flags, timeout=30)
    print(Result.stdout, end="")
    print(Result.stderr, end="")
    if Result.returncode: raise SystemExit(Result.returncode)
    if Headless:
        Result = subprocess.run([str(Cli), 'check', str(Headless), '--sdk', str(Sdk)],
                                capture_output=True, text=True, creationflags=Flags, timeout=30)
        print(Result.stdout, end=''); print(Result.stderr, end='')
    raise SystemExit(Result.returncode)


if __name__ == "__main__":
    Main()
