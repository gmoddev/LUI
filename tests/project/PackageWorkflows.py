"""Use the extracted CLI to exercise editing, locks, features and immutable imports."""

import argparse
import concurrent.futures
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("--sdk", type=Path, required=True)
    Parser.add_argument("--expects-native-hashes", action="store_true")
    Options = Parser.parse_args()
    Sdk = Options.sdk.resolve()
    Info = json.loads((Sdk / "SdkManifest.json").read_bytes())
    Cli = Sdk / Info["Components"]["Cli"]
    Windows = Info["Target"] == "WindowsX64"
    Target, Foreign = ("WindowsX64", "LinuxX64") if Windows else ("LinuxX64", "WindowsX64")
    if Windows:
        ctypes.windll.kernel32.SetErrorMode(0x8003)

    def Run(*Args, Failure=None):
        Result = subprocess.run([Cli, *Args], text=True, capture_output=True, timeout=35)
        if Failure:
            assert Result.returncode != 0 and Failure in Result.stdout + Result.stderr, (Args, Result)
        else:
            assert Result.returncode == 0, (Args, Result)
        return Result.stdout

    def Write(Path, Value):
        Path.write_text(json.dumps(Value, indent=2), encoding="utf-8")

    with tempfile.TemporaryDirectory(prefix="lui-package-workflows-") as Directory:
        Root = Path(Directory)
        App, Catalog = Root / "App", Root / "Catalog"
        Run("new", App, "--sdk", Sdk)
        Manifest = App / "lui.json"
        Entry = App / "Src/Main.luau"

        def Package(Name, Version, Body, **Extra):
            Folder = Catalog / Name / Version
            Run("package", "init", Folder, "--name", Name, "--version", Version)
            Path = Folder / "LuiPackage.json"
            Data = json.loads(Path.read_bytes())
            Data.update(Extra)
            Write(Path, Data)
            (Folder / "Main.luau").write_text(Body, encoding="utf-8")
            return Folder

        Provider = Package("Format", "1.0.0", 'return "supported"', Platforms=[Target],
                           OptionalDependencies={"Accent": "1.0.0"}, FeatureModules={"Accent": ["Rich.luau"]})
        (Provider / "Rich.luau").write_text('return require("@Accent/Main") .. " supported"', encoding="utf-8")
        Package("Format", "1.1.0", 'return "foreign"', Platforms=[Foreign])
        (Provider.parent / ("1.0.0.luipkg." + "a" * 32 + ".pending")).write_bytes(b"interrupted writer")
        Package("Accent", "1.0.0", 'return "accent"')
        Package("Theme", "2.0.0", 'return "theme"')
        Package("LocalTarget", "1.0.0", "return true")
        Project = ["--manifest", Manifest]
        Run("package", "add", "Format", "--version", "^1.0.0", *Project)
        Run("package", "add", "Theme", "--optional", "true", *Project)
        Run("package", "add", "LocalTarget", "--platform", Target, *Project)
        Run("package", "add", "ForeignTarget", "--platform", Foreign, *Project)
        Run("package", "add", "Clobber", "--requests", App / "LuiToolchain.json", *Project,
            Failure="InvalidDependencyRequestsPath")
        Run("package", "restore", Manifest, "--source", Catalog, "--locked", "true", Failure="PackageLockMissing")
        Entry.write_text('assert(require("@Format/Main") == "supported")\nprint("[LUI:PackageWorkflow] default")', encoding="utf-8")
        Restore = ["package", "restore", Manifest, "--source", Catalog]
        Run(*Restore)
        Resolved = App / "lui.resolved.json"
        Default = json.loads(Run("package", "inspect", Manifest))
        assert list(Default["Direct"]) == ["Format", "LocalTarget"]
        assert [(Row["Name"], Row["Version"]) for Row in Default["Packages"]] == [("Format", "1.0.0"), ("LocalTarget", "1.0.0")]
        assert "[LUI:PackageWorkflow] default" in Run("run", Resolved, "--mode", "headless", "--diagnostics", "development")

        FeatureLock = ["--lock", "Feature.lock.json", "--output", "lui.feature.json", "--include", "Accent,Theme"]
        Run(*Restore, *FeatureLock)
        Feature = json.loads(Run("package", "inspect", Manifest, "--lock", "Feature.lock.json", "--include", "Theme,Accent"))
        assert {Row["Name"] for Row in Feature["Packages"]} == {"Format", "Accent", "Theme", "LocalTarget"}
        assert next(Item for Item in Feature["Packages"] if Item["Name"] == "Format")["Dependencies"] == {"Accent": "1.0.0"}
        Entry.write_text('assert(require("@Format/Rich") == "accent supported" and require("@Theme/Main") == "theme")',
                         encoding="utf-8")
        Run("check", App / "lui.feature.json")
        Run("run", App / "lui.feature.json", "--mode", "headless")
        Run("package", "restore", Manifest, *FeatureLock, "--locked", "true")
        Run(*Restore, "--lock", "Unknown.lock.json", "--output", "lui.unknown.json", "--include", "Missing",
            Failure="UnknownOptionalDependency")

        if Options.expects_native_hashes:
            Data = json.loads(Resolved.read_bytes())
            NativeLock = App / Data["Modules"]["Lock"]
            Original = NativeLock.read_bytes()
            NativeLock.write_bytes(Original + b"\n")
            try:
                Run("stage", Resolved, "--output", Root / "WrongLock", Failure="PackageMetadataMismatch")
            finally:
                NativeLock.write_bytes(Original)

        Git = shutil.which("git")
        if Git:
            Repo = Root / "Publisher"
            Repo.mkdir()
            def GitRun(*Args):
                return subprocess.check_output([Git, "-C", Repo, *Args], stderr=subprocess.STDOUT, text=True, timeout=12).strip()
            GitRun("init", "-q")
            (Repo / ".gitattributes").write_bytes(b"* -text\n")
            Snapshot = Repo / "Package"
            shutil.copytree(Catalog / "Theme/2.0.0", Snapshot)
            GitRun("add", ".")
            Hooks = Root / "EmptyHooks"
            Hooks.mkdir()
            GitRun("-c", "user.name=LUI Fixture", "-c", "user.email=fixture@example.invalid",
                   "-c", "commit.gpgsign=false", "-c", f"core.hooksPath={Hooks}", "commit", "-qm", "Package")
            Commit = GitRun("rev-parse", "HEAD")
            (Snapshot / "Main.luau").write_text('error("working tree data")', encoding="utf-8")
            Imported = Root / "Imported"
            GitImport = ["package", "import-git", Repo, "--git", Git, "--subdir", "Package", "--revision", Commit, "--source", Imported]
            Run(*GitImport)
            PackagePath = Imported / "Theme/2.0.0.luipkg"
            ImportedDigest = json.loads(Run("package", "verify", PackagePath))["Digest"]
            assert ImportedDigest == json.loads(Run("package", "verify", Catalog / "Theme/2.0.0"))["Digest"]
            Before = hashlib.sha256(PackagePath.read_bytes()).hexdigest()
            Run(*GitImport)
            assert hashlib.sha256(PackagePath.read_bytes()).hexdigest() == Before
            Run("package", "import-git", Repo, "--git", Git, "--revision", "HEAD", "--source", Imported, Failure="InvalidPackageGitRevision")

        for Name, Flags in (("Format", []), ("Theme", ["--optional", "true"]),
                            ("LocalTarget", ["--platform", Target]), ("ForeignTarget", ["--platform", Foreign])):
            Run("package", "remove", Name, *Project, *Flags)
        Run("package", "restore", Manifest, "--locked", "true", Failure="PackageLockChanged")
        Run("package", "update", Manifest, "--replace-output", "true")
        assert json.loads(Run("package", "inspect", Manifest))["Packages"] == []
        Entry.write_text("return true\n", encoding="utf-8")
        Run("check", Resolved)
        with concurrent.futures.ThreadPoolExecutor(2) as Threads:
            def FrozenRestore():
                return subprocess.run([Cli, "package", "restore", Manifest, "--locked", "true"],
                                      text=True, capture_output=True, timeout=30)
            for Result in Threads.map(lambda _: FrozenRestore(), range(2)):
                assert Result.returncode == 0 or "PackageRestoreBusy" in Result.stderr, Result
        Run("package", "restore", Manifest, "--locked", "true")
    print("[LUI:PackageWorkflow] Add, remove, update, inspect, features, platform backtracking and frozen restore passed")


if __name__ == "__main__":
    Main()
