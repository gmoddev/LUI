"""Qualify local directory and archive packages through the ordinary LUI CLI."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("--cli", type=Path, required=True)
    Parser.add_argument("--dotnet", type=Path)
    Parser.add_argument("--tool", type=Path, required=True)
    Parser.add_argument("--expects-native-hashes", action="store_true")
    Parser.add_argument("--host", type=Path, required=True)
    Parser.add_argument("--native-sdk", type=Path)
    Parser.add_argument("--foreign-native-sdk", type=Path)
    Options = Parser.parse_args()
    if bool(Options.native_sdk) != bool(Options.foreign_native_sdk):
        Parser.error("supply both SDKs to qualify cross-platform native packages")
    Cli = ([Options.dotnet.resolve()] if Options.dotnet else []) + [Options.cli.resolve()]
    Tool, Host = Options.tool.resolve(), Options.host.resolve()
    if os.name == "nt":
        import ctypes
        ctypes.windll.kernel32.SetErrorMode(0x8003)

    def Run(Arguments, Failure=None, Cwd=None):
        Process = subprocess.run(Cli + list(Arguments), cwd=Cwd, capture_output=True, text=True, timeout=25)
        Output = Process.stdout + Process.stderr
        if Failure:
            assert Process.returncode != 0 and Failure in Output, (Arguments, Output)
        else:
            assert Process.returncode == 0, (Arguments, Output)
        return Output

    def Write(Path, Data):
        Path.write_text(json.dumps(Data, indent=2), encoding="utf-8")

    with tempfile.TemporaryDirectory(prefix="lui-package-ecosystem-") as Temporary:
        Root = Path(Temporary)
        Catalog, App = Root / "Catalog", Root / "App"
        (App / "Src").mkdir(parents=True)
        (App / "Manual").mkdir()
        Manual = App / "Manual/Support.luau"
        Manual.write_bytes(b'return "manual"\n')
        Write(App / "Manual.lock.json", {"SchemaVersion": 1, "Packages": [{
            "Alias": "Support", "Provenance": {"Kind": "LocalVendor", "Id": "fixture"},
            "Files": [{"Path": "Support.luau", "Sha256": hashlib.sha256(Manual.read_bytes()).hexdigest()}]}]})
        Manifest = App / "lui.json"
        Write(Manifest, {
            "SchemaVersion": 2, "Script": "Src/Main.luau", "Capabilities": [], "Extensions": [],
            "ExtensionDeclarations": [], "Assets": [], "Diagnostics": {"Mode": "Development"},
            "Modules": {"Root": "Src", "Files": ["Main.luau"], "Requires": ["Support"],
                        "Dependencies": [{"Alias": "Support", "Root": "Manual", "Files": ["Support.luau"], "Requires": []}],
                        "Lock": "Manual.lock.json"},
        })
        Original = Manifest.read_bytes()
        SdkRoot = Options.cli.resolve().parent.parent
        SdkManifest = SdkRoot / "SdkManifest.json"
        if SdkManifest.is_file():
            SdkBytes = SdkManifest.read_bytes()
            SdkVersion = json.loads(SdkBytes)["SdkVersion"]
            Write(App / "LuiToolchain.json", {
                "SchemaVersion": 1, "SdkVersion": SdkVersion, "SdkManifestSha256": hashlib.sha256(SdkBytes).hexdigest(),
            })
        (App / "Src/Main.luau").write_text(
            '''local A = require("@Alpha/Main")
local B = require("@Beta/Main")
assert(A() == 21 and B == 20 and require("@Support/Support") == "manual")
print("[LUI:PackageFixture] PASS")
''', encoding="utf-8")
        Requests = App / "LuiDependencies.json"
        def Require(**Names):
            Write(Requests, {"SchemaVersion": 1, "Dependencies": Names})

        def Package(Name, Version, Source, Dependencies=None, **Extra):
            Directory = Catalog / Name / Version
            Run(["package", "init", Directory, "--name", Name, "--version", Version, "--license", "MIT"])
            Descriptor = Directory / "LuiPackage.json"
            Document = json.loads(Descriptor.read_bytes())
            Document["Dependencies"] = Dependencies or {}
            Document.update(Extra)
            Write(Descriptor, Document)
            (Directory / "Main.luau").write_text(Source, encoding="utf-8")
            return Directory

        Package("Kit", "1.0.0", "return 10\n")
        Kit = Package("Kit", "2.0.0", "return 20\n")
        Package("Alpha", "1.9.0", 'return function() return require("@Kit/Main") + 1 end\n', {"Kit": "^2.0.0"})
        Package("Alpha", "1.10.0", 'return function() return require("@Kit/Main") + 100 end\n', {"Kit": "^1.0.0"})
        Package("Beta", "1.0.0", 'return require("@Kit/Main")\n', {"Kit": "2.0.0"})
        Package("Denied", "1.0.0", "return true\n", Capabilities=["Storage"])
        Package("DeniedWrapper", "1.0.0", 'return require("@Denied/Main")\n', {"Denied": "*"})
        Package("NativeWrapper", "1.0.0", "return true\n", Capabilities=[], TrustedNative=True)
        Package("Cycle", "1.0.0", "return true\n", {"Cycle": "*"})
        Output = Kit.parent / "2.0.0.luipkg"
        Run(["package", "pack", Kit, "--output", Output])
        Run(["package", "pack", Kit, "--output", Root / "exact.luipkg"])
        assert Output.read_bytes() == (Root / "exact.luipkg").read_bytes()
        Run(["package", "pack", Kit, "--output", Output], "PackageOutputExists")
        assert Output.read_bytes() == (Root / "exact.luipkg").read_bytes()
        assert json.loads(Run(["package", "verify", Kit])) == json.loads(Run(["package", "verify", Output]))
        Kit.rename(Root / "ArchivedKit")

        Require(Alpha="^1.0.0", Beta="1.0.0")
        if SdkManifest.is_file():
            Run(["package", "restore", Manifest, "--source", Catalog, "--sdk", SdkRoot, "--sdk-version", "0.0.0"],
                "SdkVersionMismatch")
        Collision = App / "lui.resolved.json"
        Collision.write_text('{"CallerData": true}', encoding="utf-8")
        Run(["package", "restore", Manifest, "--source", Catalog], "PackageOutputExists")
        assert not (App / "LuiPackages.lock.json").exists()
        assert json.loads(Collision.read_bytes()) == {"CallerData": True}
        Collision.unlink()
        Run(["package", "restore", Manifest, "--source", Catalog])
        Resolved = App / "lui.resolved.json"
        Lock = App / "LuiPackages.lock.json"
        Frozen = Lock.read_bytes(), Resolved.read_bytes()
        Data = json.loads(Frozen[0])
        assert [Item["Version"] for Item in Data["Packages"] if Item["Name"] == "Alpha"] == ["1.9.0"]
        assert Manifest.read_bytes() == Original
        Staged = Root / "Staged"
        Run(["stage", Resolved, "--output", Staged, "--project-tool", Tool])
        Run(["verify", Staged, "--project-tool", Tool])
        assert "[LUI:PackageFixture] PASS" in Run(["run", Resolved, "--mode", "headless",
            "--diagnostics", "development", "--host", Host])

        Catalog.rename(Root / "OfflineCatalog")
        Catalog = Root / "OfflineCatalog"
        Resolved.unlink()
        Run(["package", "restore", Manifest])
        assert (Lock.read_bytes(), Resolved.read_bytes()) == Frozen
        CachedFile = App / ".lui/Packages" / Data["Cache"] / "Kit/Main.luau"
        CachedBytes = CachedFile.read_bytes()
        CachedFile.write_text("return 1", encoding="utf-8")
        Run(["package", "restore", Manifest], "PackageIntegrityMismatch")
        Run(["stage", Resolved, "--output", Root / "Tampered", "--project-tool", Tool], "DependencyMismatch")
        CachedFile.write_bytes(CachedBytes)
        Require(Alpha="1.10.0", Beta="1.0.0")
        Run(["package", "restore", Manifest], "PackageLockChanged")
        Run(["package", "restore", Manifest, "--source", Catalog, "--update", "true"], "VersionConflict")
        Require(DeniedWrapper="*")
        Run(["package", "restore", Manifest, "--source", Catalog, "--update", "true"], "CapabilityRequired")
        Require(NativeWrapper="*")
        Run(["package", "restore", Manifest, "--source", Catalog, "--update", "true"], "TrustedNativeRequired")
        Require(Cycle="*")
        Run(["package", "restore", Manifest, "--source", Catalog, "--update", "true"], "DependencyCycle")
        assert (Lock.read_bytes(), Resolved.read_bytes()) == Frozen

        for Name, Entry, Mode, Failure in [
            ("Traversal", "../Outside.luau", 0o100644, "InvalidPackagePath"),
            ("Link", "Main.luau", 0o120777, "PackageLinkOrSpecialEntry"),
            ("Reserved", "AUX.luau", 0o100644, "InvalidPackagePath"),
        ]:
            Bad = Root / (Name + ".luipkg")
            with zipfile.ZipFile(Bad, "w") as Archive:
                Info = zipfile.ZipInfo(Entry)
                Info.create_system = 3
                Info.external_attr = Mode << 16
                Archive.writestr(Info, "fixture\n")
            Run(["package", "verify", Bad], Failure)

        if os.name != "nt":
            Special = Package("Special", "1.0.0", "return 0\n")
            (Special / "Main.luau").unlink()
            os.mkfifo(Special / "Main.luau")
            Run(["package", "verify", Special], "PackageLinkOrSpecialEntry")

        Standalone = Root / "Standalone"
        Run(["build", Resolved, "--mode", "headless", "--host", Host, "--project-tool", Tool, "--output", Standalone])
        Notices, = (Standalone / "Application").glob("LuiPackageNotices-*.txt")
        assert "Alpha 1.9.0" in Notices.read_text(encoding="utf-8")
        Launcher = Standalone / ("Start-LUI.exe" if os.name == "nt" else "Start-LUI.sh")
        shutil.move(App, Root / "Original")
        Process = subprocess.run([Launcher, "--diagnostics", "development"], cwd=Root, capture_output=True, text=True, timeout=25)
        assert Process.returncode == 0 and "[LUI:PackageFixture] PASS" in Process.stdout + Process.stderr, Process

        if Options.native_sdk:
            NativeApp = Root / "NativeApp"
            (NativeApp / "Src").mkdir(parents=True)
            NativeManifest = NativeApp / "lui.json"
            AppData = {
                "SchemaVersion": 2, "Script": "Src/Main.luau", "Capabilities": [], "Extensions": [],
                "ExtensionDeclarations": [], "Assets": [], "Diagnostics": {"Mode": "Development"},
                "Modules": {"Root": "Src", "Files": ["Main.luau"], "Requires": [], "Dependencies": []},
            }
            (NativeApp / "Src/Main.luau").write_text(
                'local Crypto = require("@CryptoWrapper/Main")\n'
                'local Digest = Crypto.Sha256("abc")\n'
                'local Hex = ""\n'
                'for Index = 1, #Digest do Hex ..= string.format("%02x", string.byte(Digest, Index)) end\n'
                'assert(Hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")\n'
                'print("[LUI:PackageFixture] NATIVE PASS")\n', encoding="utf-8")
            Write(NativeApp / "LuiDependencies.json", {"SchemaVersion": 1, "Dependencies": {"CryptoWrapper": "*"}})
            Package("CryptoWrapper", "1.0.0", 'return require("@CryptoPackage/Main")\n', {"CryptoPackage": "1.0.0"})
            NativePackage = Package("CryptoPackage", "1.0.0", "return nil\n",
                                    Capabilities=["HostServices", "NativeExtensions"],
                                    TrustedNative=True, NativeProviders=["Crypto.json"])
            NativeExample = Options.native_sdk / "Examples/digest"
            ForeignExample = Options.foreign_native_sdk / "Examples/digest"
            Sidecar = json.loads((NativeExample / "Crypto.json").read_bytes())
            Foreign = json.loads((ForeignExample / "Crypto.json").read_bytes())
            LocalBinding, = Sidecar["Extension"]["Bindings"]
            ForeignBinding, = Foreign["Extension"]["Bindings"]
            assert LocalBinding["Platform"] != ForeignBinding["Platform"]
            for Example, Binding in [(NativeExample, LocalBinding), (ForeignExample, ForeignBinding)]:
                Destination = NativePackage / Binding["Path"]
                Destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(Example / Binding["Path"], Destination)
            shutil.copyfile(NativeExample / "Vendor/Crypto/Crypto.luau", NativePackage / "Main.luau")
            Write(NativePackage / "Crypto.json", Foreign)
            NativeRestore = ["package", "restore", NativeManifest, "--source", Catalog, "--trusted-native", "true"]
            AppData["Capabilities"] = ["HostServices", "NativeExtensions"]
            AppData["Extensions"] = ["Crypto"]
            Write(NativeManifest, AppData)
            Run(NativeRestore, "UnsupportedPackagePlatform")
            Sidecar["Extension"]["Bindings"] = sorted([LocalBinding, ForeignBinding], key=lambda Item: Item["Platform"])
            Write(NativePackage / "Crypto.json", Sidecar)
            LocalBinary = NativePackage / LocalBinding["Path"]
            Binary = LocalBinary.read_bytes()
            LocalBinary.write_bytes(Binary + b"tampered")
            Run(["package", "verify", NativePackage], "PackageProviderIntegrityMismatch")
            LocalBinary.write_bytes(Binary)
            Altered = bytearray(Binary)
            Machine = int.from_bytes(Binary[60:64], "little") + 4 if os.name == "nt" else 18
            Altered[Machine] ^= 1
            LocalBinary.write_bytes(Altered)
            OriginalDigest = LocalBinding["Sha256"]
            LocalBinding["Sha256"] = hashlib.sha256(Altered).hexdigest()
            Write(NativePackage / "Crypto.json", Sidecar)
            Run(["package", "verify", NativePackage], "InvalidPackageNativeImage")
            LocalBinary.write_bytes(Binary)
            LocalBinding["Sha256"] = OriginalDigest
            LocalBinding["Path"] = "../Outside" + LocalBinary.suffix
            Write(NativePackage / "Crypto.json", Sidecar)
            Run(["package", "verify", NativePackage], "InvalidPackagePath")
            LocalBinding["Path"] = LocalBinary.relative_to(NativePackage).as_posix()
            Write(NativePackage / "Crypto.json", Sidecar)
            PackedNative = NativePackage.parent / "1.0.0.luipkg"
            NativeInfo = json.loads(Run(["package", "pack", NativePackage, "--output", PackedNative]))
            assert NativeInfo["NativeProviders"] == ["Crypto"]
            NativePackage.rename(Root / "NativePackageSource")
            AppData["Capabilities"] = ["NativeExtensions"]
            Write(NativeManifest, AppData)
            Run(NativeRestore, "CapabilityRequired")
            AppData["Capabilities"] = ["HostServices", "NativeExtensions"]
            AppData["Extensions"] = ["Unrelated"]
            Write(NativeManifest, AppData)
            Run(NativeRestore, "PackageProviderRequired")
            AppData["Extensions"] = ["Crypto"]
            Write(NativeManifest, AppData)
            Run(NativeRestore[:-2], "TrustedNativeRequired")
            Run(NativeRestore)
            RestoredNative = NativeApp / "lui.resolved.json"
            Active = json.loads(RestoredNative.read_bytes())
            assert Active["Capabilities"] == AppData["Capabilities"] and Active["Extensions"] == ["Crypto"]
            DeclarationPath, = Active["ExtensionDeclarations"]
            SelectedMetadata = NativeApp / DeclarationPath
            Rebased, = json.loads(SelectedMetadata.read_bytes())["Extension"]["Bindings"]
            assert Rebased["Platform"] == LocalBinding["Platform"] and Rebased["Sha256"] == LocalBinding["Sha256"]
            assert Rebased["Path"].endswith(LocalBinding["Path"]) and Rebased["Path"].startswith(".lui/Packages/")
            if Options.expects_native_hashes:
                Original = SelectedMetadata.read_bytes()
                SelectedMetadata.write_bytes(Original + b"\n")
                try:
                    Run(["stage", RestoredNative, "--output", Root / "AlteredDeclaration", "--project-tool", Tool],
                        "PackageMetadataMismatch")
                finally:
                    SelectedMetadata.write_bytes(Original)
            Run(["run", RestoredNative, "--mode", "headless", "--host", Host], "NativeProvidersUnsupported")
            assert "[LUI:PackageFixture] NATIVE PASS" in Run(["run", RestoredNative, "--mode", "headless",
                "--host", Host, "--providers", "Crypto", "--diagnostics", "development"])
            NativeSnapshot = RestoredNative.read_bytes()
            Catalog.rename(Root / "NativeOfflineCatalog")
            Run(["package", "restore", NativeManifest, "--trusted-native", "true"])
            assert RestoredNative.read_bytes() == NativeSnapshot
            CachedBinary = NativeApp / Rebased["Path"]
            CachedBinary.write_bytes(Binary + b"tampered")
            Run(["package", "restore", NativeManifest, "--trusted-native", "true"], "PackageProviderIntegrityMismatch")
            Run(["stage", RestoredNative, "--output", Root / "TamperedNative", "--project-tool", Tool], "BinaryMismatch")
            CachedBinary.write_bytes(Binary)
            NativeStandalone = Root / "NativeStandalone"
            Run(["build", RestoredNative, "--mode", "headless", "--host", Host, "--project-tool", Tool, "--output", NativeStandalone])
            assert (NativeStandalone / "Application" / Rebased["Path"]).is_file()
            assert not list((NativeStandalone / "Application").rglob(Path(ForeignBinding["Path"]).name))
            Run(["verify", NativeStandalone / "Application", "--project-tool", Tool])
            shutil.move(NativeApp, Root / "OriginalNative")
            NativeLauncher = NativeStandalone / Launcher.name
            Process = subprocess.run([NativeLauncher, "--diagnostics", "development", "--provider", "Crypto"],
                                     cwd=Root, capture_output=True, text=True, timeout=25)
            assert Process.returncode == 0 and "[LUI:PackageFixture] NATIVE PASS" in Process.stdout + Process.stderr, Process
            print("[LUI:PackageFixture] Platform-selected native package, explicit trust, and detached provider execution passed")
    print("[LUI:PackageFixture] Local and archive packages, restore, integrity, authority, and standalone passed")


if __name__ == "__main__":
    Main()
