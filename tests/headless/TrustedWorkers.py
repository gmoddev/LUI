"""Exercise explicit actor consent, separate FFI sessions and blocked-call retirement."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    for Name in ("host", "provider", "fixture", "source", "tool"):
        Parser.add_argument("--" + Name, type=Path, required=True)
    Parser.add_argument("--clang", type=Path, help="also run a generated binding inside a trusted actor")
    Options = Parser.parse_args()
    if os.name == "nt":
        import ctypes
        ctypes.windll.kernel32.SetErrorMode(0x8003)

    with tempfile.TemporaryDirectory(prefix="lui-trusted-workers-") as Temporary:
        Root = Path(Temporary)
        (Root / "Src").mkdir()
        Libraries = Options.source.resolve() / "libraries"
        Dependencies, Pins = [], []
        for Alias in ("Ffi", "Workers", "Messages", "Json", "Encoding"):
            Library = Libraries / Alias
            Catalog = json.loads((Library / "Library.json").read_bytes())
            Target = Root / "Vendor" / Alias
            Target.mkdir(parents=True)
            Files = []
            for Item in Catalog["Modules"]:
                Name = Item["Name"] + ".luau"
                Data = (Library / Name).read_bytes()
                (Target / Name).write_bytes(Data)
                Files.append({"Path": Name, "Sha256": hashlib.sha256(Data).hexdigest()})
            Dependencies.append({"Alias": Alias, "Root": f"Vendor/{Alias}", "Requires": Catalog.get("Requires", []),
                                 "Files": [Item["Path"] for Item in Files]})
            Pins.append({"Alias": Alias, "Provenance": {"Kind": "LocalVendor", "Id": "trusted-worker-test"}, "Files": Files})
        (Root / "modules.lock.json").write_text(json.dumps({"SchemaVersion": 1, "Packages": Pins}), encoding="utf-8")
        (Root / "Native").mkdir()
        Provider = Root / "Native" / Options.provider.name
        shutil.copyfile(Options.provider, Provider)
        (Root / "Ffi.json").write_text(json.dumps({
            "SchemaVersion": 2, "AbiVersion": 1, "Provider": "Native",
            "Extension": {"Name": "Ffi", "Version": 1, "RequiredCapabilities": ["HostServices", "NativeExtensions"],
                          "Provenance": {"Kind": "LocalVendor", "Id": "trusted-worker-test"},
                          "Bindings": [{"Platform": "WindowsX64" if os.name == "nt" else "LinuxX64",
                                        "Path": "Native/" + Provider.name, "Sha256": Hash(Provider)}]},
            "Services": [json.loads((Libraries / "Ffi/Service.json").read_bytes())],
        }), encoding="utf-8")
        Manifest = Root / "lui.json"
        ModuleFiles = ["Main.luau", "Compute.luau", "Ordinary.luau", "Forbidden.luau"]
        WorkerSource = Compute
        if Options.clang:
            Header = Root / "Bindings.h"
            Header.write_text("extern unsigned int LuiFfiEchoU32(unsigned int);\n", encoding="utf-8")
            Target = "x86_64-pc-windows-msvc" if os.name == "nt" else "x86_64-unknown-linux-gnu"
            subprocess.run([sys.executable, Options.source.resolve() / "scripts/Generate-FfiBindings.py",
                            Header, "--target", Target, "--clang", Options.clang.resolve(),
                            "--symbol", "LuiFfiEchoU32", "--output", Root / "Src/Generated.luau"],
                           check=True, timeout=15)
            ModuleFiles.append("Generated.luau")
            WorkerSource = Compute.replace("-- GENERATED", """
        local Generated, CloseBindings = require("./Generated")(Image)
        assert(Generated.LuiFfiEchoU32:Call({42}) == 42)
        CloseBindings()
        CloseBindings()
""")
        Manifest.write_text(json.dumps({
            "SchemaVersion": 2, "Script": "Src/Main.luau", "Capabilities": ["HostServices", "NativeExtensions"],
            "Extensions": ["Ffi"], "ExtensionDeclarations": ["Ffi.json"], "Assets": [],
            "Diagnostics": {"Mode": "Development"},
            "Modules": {"Root": "Src", "Files": ModuleFiles, "Lock": "modules.lock.json",
                        "Requires": ["Workers", "Ffi"], "Dependencies": Dependencies},
        }), encoding="utf-8")
        Fixture = Options.fixture.resolve()
        Prefix = ("local Path = " + json.dumps(str(Fixture)) + "\nlocal Digest = " + json.dumps(Hash(Fixture)) + "\n")
        (Root / "Src/Compute.luau").write_text(Prefix + WorkerSource, encoding="utf-8")
        (Root / "Src/Ordinary.luau").write_text(
            'return require("@Workers/Workers").Handler(function() assert(app == nil); return true end)\n', encoding="utf-8")
        (Root / "Src/Forbidden.luau").write_text(
            'return function() return app:GetService("Ffi"):Dispatch("LFB1X" .. string.rep("\\0", 4)) end\n',
            encoding="utf-8")
        (Root / "Src/Main.luau").write_text(Prefix + Work, encoding="utf-8")
        Prepared = subprocess.run([Options.tool.resolve(), "--stage", Manifest, Root / "Staged"],
                                  capture_output=True, text=True, timeout=15)
        assert Prepared.returncode == 0, Prepared.stderr
        Base = [Options.host.resolve(), "--project", Manifest, "--diagnostics", "development", "--poll-ms", "1"]
        NoOwnerTrust = subprocess.run(Base + ["--trusted-ffi-workers"], capture_output=True, text=True, timeout=12)
        assert NoOwnerTrust.returncode == 1 and "FfiWorkerTrustRequired" in NoOwnerTrust.stderr, NoOwnerTrust
        Denied = subprocess.run(Base + ["--trusted-ffi"], capture_output=True, text=True, timeout=12)
        assert Denied.returncode == 0 and "Trusted actor denied" in Denied.stdout and "NativeResources=0" in Denied.stdout, Denied
        Allowed = subprocess.run(Base + ["--trusted-ffi", "--trusted-ffi-workers"], capture_output=True, text=True, timeout=12)
        assert (Allowed.returncode == 0 and "Trusted actor passed" in Allowed.stdout
                and "Worker pool passed" in Allowed.stdout and "NativeResources=0" in Allowed.stdout), Allowed
        print(Allowed.stdout.strip())
        (Root / "Src/Main.luau").write_text(PoolWork, encoding="utf-8")
        for Flags, Marker in ((["--trusted-ffi"], "Trusted pool denied"),
                              (["--trusted-ffi", "--trusted-ffi-workers"], "Trusted pool passed")):
            Pool = subprocess.run(Base + Flags, capture_output=True, text=True, timeout=12)
            assert Pool.returncode == 0 and Marker in Pool.stdout and "NativeResources=0" in Pool.stdout, Pool
            assert ("Trusted binary opcode refused" in Pool.stdout) == (len(Flags) == 2), Pool
            print(Pool.stdout.strip())
        print("[LUI:TrustedWorkers] Owner consent, actor consent, exact integers and worker-affine cleanup passed")


def Hash(File):
    return hashlib.sha256(File.read_bytes()).hexdigest()


PoolWork = """
local Workers = require("@Workers/Workers")
local Pool = Workers.PoolTrusted("./Compute", 2)
local Completed, Successful = 0, 0
for _ = 1, 4 do
    assert(Pool:Send(0, function(Value, Failure)
        if Value == nil then assert(Failure == "Closed")
        else
            assert(Value == "ffffffffffffffff" and Failure == nil)
            Successful += 1
        end
        Completed += 1
        if Completed == 4 then
            Pool:Close()
            assert(Successful == 0 or Successful == 4)
            print(if Successful == 4 then "Trusted pool passed" else "Trusted pool denied")
        end
    end))
end
Workers.StartTrusted("./Forbidden", {}, function(Actor, Failure)
    if not Actor then assert(Failure == "CapabilityDenied"); return end
    Actor:Send(true, function(Value, Error)
        assert(Value == nil and Error == "ProviderFailure")
        Actor:Close()
        print("Trusted binary opcode refused")
    end)
end)
"""


Compute = """
local Ffi = require("@Ffi/Ffi")
local Workers = require("@Workers/Workers")
local Image, Wait, Echo, Pair, Adjust
return Workers.Handler(function(Milliseconds)
    if Image == nil then
        Image = Ffi.Open(Path, Digest)
        -- GENERATED
        Wait = Image:Bind("LuiFfiWait", {"u32"}, "u32")
        Echo = Image:Bind("LuiFfiEchoU64", {"u64"}, "u64")
        Pair = Ffi.Struct({{Name="Left", Type="i32"}, {Name="Right", Type="f64"}})
        Adjust = Image:Bind("LuiFfiAdjustPair", {Pair, "i32"}, Pair)
        assert(app:GetService("Ffi").DispatchScoped == nil)
        local Memory = Ffi.Allocate(7)
        Memory:Write(0, "a" .. string.char(0) .. "b")
        assert(Memory:Read(0, 3) == "a" .. string.char(0) .. "b" and Memory:Close())
    end
    assert(Wait:Call({Milliseconds}) == Milliseconds)
    local PairValue = Adjust:Call({{Left=2, Right=4.5}, 3})
    assert(PairValue.Left == 2 and PairValue.Right == 7.5)
    return Echo:Call({"ffffffffffffffff"})
end)
"""


Work = """
local Ffi = require("@Ffi/Ffi")
local Workers = require("@Workers/Workers")
local Pool = Workers.Pool("./Ordinary", 1)
local PoolResults = 0
for Index = 1, 16 do
    local Job, Admission = Pool:Send(true, function(Value, Failure)
        if Index == 16 then assert(Value == nil and Failure == "Canceled")
        else assert(Value == true and Failure == nil) end
        PoolResults += 1
        if PoolResults == 16 then
            Pool:Close()
            assert(select(2, Pool:Send(true, function() error("closed") end)) == "Closed")
            print("Worker pool passed")
        end
    end)
    assert(Job and not Admission)
    if Index == 16 then
        assert(select(2, Pool:Send(true, function() error("full") end)) == "WouldBlock")
        assert(Job:Cancel() and not Job:Cancel())
    end
end
Workers.Start("./Ordinary", {}, function(Worker, Failure)
    assert(Worker and not Failure)
    Worker:Send(true, function(Value, Error)
        assert(Value == true and Error == nil)
        Worker:Close()
    end)
end)
local ActorStart = os.clock()
Workers.StartTrusted("./Compute", {}, function(Actor, Failure)
    if not Actor then
        assert(Failure == "CapabilityDenied")
        print("Trusted actor denied")
        return
    end
    local ColdMs = (os.clock() - ActorStart) * 1000
    local ImageStart = os.clock()
    local Image = Ffi.Open(Path, Digest)
    local ImageMs = (os.clock() - ImageStart) * 1000
    local Pending = Image:Bind("LuiFfiWaiting", {}, "u32")
    local Variadic = Image:BindVariadic("LuiFfiVariadicMix", 1,
        {"i32", "i32", "f64", "i32", "f64", "u64", "utf8"}, "f64")
    assert(Variadic:Call({10, -7, 1.5, 65000, -2.25, "ffffffffffffffff", "A"}) == 65322.25)
    Variadic:Close()
    local Echo = Image:Bind("LuiFfiEchoU64", {"u64"}, "u64")
    local Warm = os.clock()
    for _ = 1, 16 do assert(Echo:Call({"ffffffffffffffff"}) == "ffffffffffffffff") end
    print(string.format("[LUI:TrustedWorkers] ActorColdMs=%.3f ImageColdMs=%.3f OwnerScalarUs=%.3f",
        ColdMs, ImageMs, (os.clock() - Warm) * 1e6 / 16))
    Echo:Close()
    local Responsive = false
    local function CheckOwner()
        if Pending:Call({}) == 0 then task.delay(.002, CheckOwner); return end
        Responsive = true
    end
    Actor:Send(140, function(Value, Error)
        assert(Value == "ffffffffffffffff" and not Error and Responsive)
        Actor:Send(200, function(_, Why)
            assert(Why == "Canceled" or Why == "Closed")
            assert(Pending:Call({}) <= 1)
            Pending:Close()
            Image:Close()
            print("Trusted actor passed")
        end)
        local function CloseDuringCall()
            if Pending:Call({}) == 0 then task.delay(.002, CloseDuringCall); return end
            Actor:Close()
        end
        task.delay(.002, CloseDuringCall)
    end)
    task.delay(.002, CheckOwner)
end)
"""


if __name__ == "__main__":
    Main()
