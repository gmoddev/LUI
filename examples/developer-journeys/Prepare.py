"""Create a developer journey from installed SDK components and the example's Luau sources."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


Recipes = {
    "InventoryDesk": (("SQLite",), ("Storage",)),
    "LogBundle": (("Compression", "Regex", "Stdio"), ()),
    "TicketService": (("Json", "Stdio"), ("Storage", "network.server")),
    "SourceWorkbench": (("Workers",), ()),
    "NativeDigest": (("Stdio",), ()),
}


def Read(Path):
    return json.loads(Path.read_bytes())


def Write(Path, Document):
    Path.write_text(json.dumps(Document, indent=2, sort_keys=True) + "\n", encoding="utf-8")


class Installation:
    def __init__(Self, Root):
        Self.Root = Root.resolve()
        Self.Inventory = Read(Self.Root / "SdkManifest.json")
        Self.Cli = Self.Root / Self.Inventory["Components"]["Cli"]

    def Capture(Self, Relative):
        Name = Relative.as_posix()
        Bytes = (Self.Root / Relative).read_bytes()
        Digest = hashlib.sha256(Bytes).hexdigest()
        if Self.Inventory["Files"].get(Name) != Digest:
            raise ValueError(f"[LUI:DeveloperJourney] SDK file differs from its inventory: {Name}")
        return Bytes

    def Run(Self, *Args):
        Selection = ["--sdk", Self.Root] if Args[0] != "package" or Args[1] == "restore" else []
        subprocess.run([Self.Cli, *Args, *Selection], check=True, timeout=60)


def Prepare(Sdk, Name, Root, Other):
    Normal, Capabilities = Recipes[Name]
    Storage = ["--storage-id", "com.lui.example." + Name.lower()] if "Storage" in Capabilities else []
    Sdk.Run("new", Root, *Storage)
    App = Root / "lui.json"
    Manifest = Read(App)
    Manifest["Capabilities"] = list(Capabilities)
    Source = Path(__file__).resolve().parent / Name
    for Module in Source.glob("*.luau"):
        shutil.copyfile(Module, Root / "Src" / Module.name)
    Manifest["Modules"]["Files"] = sorted(File.name for File in (Root / "Src").glob("*.luau"))
    Manifest["Modules"]["Requires"] = list(Normal)
    Packages = {}

    def Install(Library):
        if Library in Packages:
            return
        Prefix = Path("Libraries") / Library
        Catalog = json.loads(Sdk.Capture(Prefix / "Library.json"))
        Files = [Item["Name"] + ".luau" for Item in Catalog["Modules"]]
        Requires = Catalog.get("Requires", [])
        Vendor = Root / "Vendor" / Library
        Vendor.mkdir(parents=True)
        Pins = []
        for File in Files:
            Content = Sdk.Capture(Prefix / File)
            (Vendor / File).write_bytes(Content)
            Pins.append({"Path": File, "Sha256": hashlib.sha256(Content).hexdigest()})
        Packages[Library] = {"Alias": Library, "Provenance": {"Kind": "LocalVendor", "Id": Sdk.Inventory["SdkVersion"]},
                             "Files": Pins}
        Manifest["Modules"]["Dependencies"].append({"Alias": Library, "Root": f"Vendor/{Library}", "Files": Files, "Requires": Requires})
        if Library == "Stdio":
            (Root / "Stdio.json").write_bytes(Sdk.Capture(Prefix / "Host.json"))
            Manifest["ExtensionDeclarations"].append("Stdio.json")
            Manifest["Capabilities"].append("HostServices")
        if "NativeProvider" in Catalog:
            Decl = f"{Library}.json"
            Bytes = Sdk.Capture(Prefix / Decl)
            (Root / Decl).write_bytes(Bytes)
            Provider = json.loads(Bytes)["Extension"]
            Manifest["ExtensionDeclarations"].append(Decl)
            Manifest["Extensions"].append(Provider["Name"])
            Manifest["Capabilities"].extend(Provider["RequiredCapabilities"])
            for Binding in Provider["Bindings"]:
                PathName = Path(Binding["Path"])
                Target = Root / PathName
                Target.parent.mkdir(parents=True, exist_ok=True)
                Target.write_bytes(Sdk.Capture(Prefix / PathName))
            for Relative in sorted(Sdk.Inventory["Files"]):
                Notice = Path(Relative)
                if Notice.parent == Prefix and ("LICENSE" in Notice.name or Notice.name.upper() == "NOTICE"):
                    (Root / Notice.name).write_bytes(Sdk.Capture(Notice))
                    Manifest["Assets"].append(Notice.name)
        for Dependency in Requires:
            Install(Dependency)

    for Library in Normal:
        Install(Library)
    Manifest["Modules"]["Lock"] = "Modules.lock.json"
    Write(Root / "Modules.lock.json", {"SchemaVersion": 1, "Packages": list(Packages.values())})
    if Name == "NativeDigest":
        Manifest["Capabilities"].extend(("NativeExtensions", "HostServices"))
        Manifest["Extensions"].append("Crypto")
    for Key in ("Capabilities", "Extensions", "ExtensionDeclarations", "Assets"):
        Manifest[Key] = sorted(set(Manifest[Key]))
    Write(App, Manifest)
    if Name != "NativeDigest":
        return App

    CatalogRoot = Root.parent / (Root.name + "Catalog")
    Package = CatalogRoot / "DigestBindings/1.0.0"
    Sdk.Run("package", "init", Package, "--name", "DigestBindings", "--version", "1.0.0")
    Prefix = Path("Libraries/Crypto")
    (Package / "Crypto.luau").write_bytes(Sdk.Capture(Prefix / "Crypto.luau"))
    (Package / "Main.luau").write_text(
        '--!strict\nlocal Crypto = require("./Crypto")\nlocal Digest = {}\n'
        'function Digest.Hex(Bytes: string): string\n'
        '    return (string.gsub(Crypto.Sha256(Bytes), ".", function(Byte) return string.format("%02x", string.byte(Byte)) end))\n'
        'end\nreturn table.freeze(Digest)\n', encoding="utf-8")
    Provider = json.loads(Sdk.Capture(Prefix / "Crypto.json"))
    Bindings = []
    for Platform in [Sdk] + ([Other] if Other else []):
        Decl = json.loads(Platform.Capture(Prefix / "Crypto.json"))
        for Binding in Decl["Extension"]["Bindings"]:
            NativePath = Path(Binding["Path"])
            Bytes = Platform.Capture(Prefix / NativePath)
            Target = Path("Native") / Binding["Platform"] / NativePath.name
            (Package / Target).parent.mkdir(parents=True, exist_ok=True)
            (Package / Target).write_bytes(Bytes)
            Bindings.append({**Binding, "Path": Target.as_posix()})
    Provider["Extension"]["Bindings"] = sorted(Bindings, key=lambda Binding: Binding["Platform"])
    Write(Package / "Crypto.json", Provider)
    SourceNotices = [Path(File) for File in Sdk.Inventory["Files"]
                     if Path(File).name in ("OpenSSL.txt", "LICENSE.txt", "LICENSE") and
                     (Path(File).name == "OpenSSL.txt" or len(Path(File).parts) == 1)]
    if not SourceNotices:
        raise ValueError("[LUI:DeveloperJourney] Crypto redistribution notice is missing")
    (Package / "LICENSE.txt").write_bytes(b"\n\n".join(Sdk.Capture(File) for File in sorted(SourceNotices)))
    Descriptor = Read(Package / "LuiPackage.json")
    Descriptor.update({"License": "See LICENSE.txt", "LicenseFile": "LICENSE.txt", "Modules": ["Main.luau", "Crypto.luau"],
                       "Capabilities": Provider["Extension"]["RequiredCapabilities"], "TrustedNative": True,
                       "NativeProviders": ["Crypto.json"]})
    Write(Package / "LuiPackage.json", Descriptor)
    Sdk.Run("package", "pack", Package, "--output", Package.parent / "1.0.0.luipkg")
    Package.rename(CatalogRoot / "PackageSource")
    Sdk.Run("package", "add", "DigestBindings", "--manifest", App, "--version", "1.0.0")
    Sdk.Run("package", "restore", App, "--source", CatalogRoot, "--trusted-native", "true")
    return Root / "lui.resolved.json"


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("--sdk", type=Path, required=True)
    Parser.add_argument("--other-sdk", type=Path, help="Optional extracted SDK for the other x64 platform")
    Parser.add_argument("--output", type=Path, required=True)
    Parser.add_argument("--app", choices=Recipes, required=True)
    Options = Parser.parse_args()
    Sdk = Installation(Options.sdk)
    Other = Installation(Options.other_sdk) if Options.other_sdk else None
    if Other and Other.Inventory["Target"] == Sdk.Inventory["Target"]:
        Parser.error("--other-sdk must select the other platform")
    App = Prepare(Sdk, Options.app, Options.output.resolve(), Other)
    Sdk.Run("check", App)


if __name__ == "__main__":
    Main()
