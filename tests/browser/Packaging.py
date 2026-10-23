"""Offline browser packaging refusal tests, using the actual native tool."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

Tool, Build = map(lambda Value: str(Path(Value).resolve()), sys.argv[1:3])
Fixture = Path(__file__).resolve().parent / "fixture"

with tempfile.TemporaryDirectory(prefix="LuiBrowserPackaging-") as Temporary:
    Root = Path(Temporary)
    App = Root / "App"
    shutil.copytree(Fixture, App)
    Original = json.loads((App / "lui.json").read_text())

    def Run(Name, Success):
        Result = subprocess.run([Tool, str(App / "lui.json"), Build, str(Root / Name)], capture_output=True, timeout=30)
        assert (Result.returncode == 0) == Success, Name
        if not Success:
            assert not (Root / Name / "lui.browser.json").exists(), Name

    Run("Valid", True)
    Data = json.loads((Root / "Valid/lui.browser.json").read_text())
    assert Data["Profile"] == "BrowserHostV1" and len(Data["Content"]) == 3
    assert (Root / "Valid/THIRD-PARTY-LICENSES/PicoSHA2.txt").is_file()
    Result = subprocess.run([Tool, str(App / "lui.json"), Build, str(Root / "Valid")], capture_output=True, timeout=30)
    assert Result.returncode != 0

    for Capability in ["Clipboard", "Dialogs", "network.client", "network.raw", "network.server", "Storage", "Notifications", "Tray", "NativeExtensions", "HostServices"]:
        Manifest = {**Original, "Capabilities": [Capability]}
        (App / "lui.json").write_text(json.dumps(Manifest))
        Run("Refused-" + Capability, False)

    Manifest = json.loads(json.dumps(Original))
    Manifest["Modules"]["Files"].append("Unused.luau")
    (App / "lui.json").write_text(json.dumps(Manifest))
    (App / "Src/Unused.luau").write_text("local = invalid")
    Run("UnusedSyntaxFailure", False)
    (App / "Src/Unused.luau").unlink()
    Run("MissingSource", False)
    (App / "lui.json").write_text(json.dumps(Original))
    Run("Recovered", True)
print("[LUI:BrowserTest] Offline capture, all-source compilation, capability refusals and exclusive output passed")
