"""Exercise the five projects shipped in a clean release-review SDK."""

import argparse
from contextlib import closing
import ctypes
import gzip
import hashlib
import http.client
import json
import os
from pathlib import Path
import queue
import shutil
import socket
import subprocess
import sys
import threading
import time


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("--sdk", type=Path, required=True)
    Parser.add_argument("--other-sdk", type=Path)
    Parser.add_argument("--output", type=Path, required=True)
    Options = Parser.parse_args()
    Root, Sdk = Options.output.resolve(), Options.sdk.resolve()
    Root.mkdir(parents=True)
    Away = Root / "Outside"
    Away.mkdir()
    SdkData = json.loads((Sdk / "SdkManifest.json").read_bytes())
    Cli = Sdk / SdkData["Components"]["Cli"]
    Windows = SdkData["Target"] == "WindowsX64"
    Exe = "Start-LUI.exe" if Windows else "Start-LUI.sh"
    if Windows:
        ctypes.windll.kernel32.SetErrorMode(0x8003)
    Output = {}

    def Save():
        (Root / "Report.json").write_text(json.dumps(Output, indent=2) + "\n", encoding="utf-8")

    def Run(Name, Arguments, Input=None, Expected=0):
        Start = time.monotonic()
        Result = subprocess.run(Arguments, cwd=Away, input=Input, capture_output=True, timeout=60)
        Duration = (time.monotonic() - Start) * 1000
        (Root / (Name + ".stderr")).write_bytes(Result.stderr)
        if Name != "LogStandalone":
            (Root / (Name + ".stdout")).write_bytes(Result.stdout)
        assert Result.returncode == Expected, (Name, Result.returncode, Result.stdout[:240], Result.stderr[:400])
        return Result, Duration

    for Name, Headless in (("LogBundle", True), ("TicketService", True), ("NativeDigest", True),
                           ("InventoryDesk", False), ("SourceWorkbench", False)):
        App = Root / Name
        Prepare = Sdk / "Examples/DeveloperJourneys/Prepare.py"
        Other = ["--other-sdk", Options.other_sdk.resolve()] if Options.other_sdk and Name == "NativeDigest" else []
        Run(Name + "-prepare", [sys.executable, Prepare, "--sdk", Sdk, "--app", Name, "--output", App, *Other])
        Manifest = App / ("lui.resolved.json" if Name == "NativeDigest" else "lui.json")
        Bundle = Root / (Name + "Bundle")
        Report = Output[Name] = {}
        _, Report["BuildMs"] = Run(Name + "-build", [Cli, "build", Manifest, "--sdk", Sdk, "--mode",
                                  "headless" if Headless else "project", "--output", Bundle])
        Report["Bytes"] = sum(File.stat().st_size for File in Bundle.rglob("*") if File.is_file())
        Staged = Bundle / "Application"
        Status = json.loads((Staged / "lui.json").read_bytes())
        Report["Providers"] = Status.get("Extensions", [])
        if Windows and not Headless:
            assert '"%~dp0Application\\lui.json" %*' in (Bundle / "Start-LUI.cmd").read_text()
        if Headless:
            shutil.move(App, Root / (Name + "Source"))
        if Name == "LogBundle":
            Result, Report["StandaloneMs"] = Run("LogStandalone",
                [Bundle / Exe, "--stdio", "--provider", "Compression", "--provider", "Regex", "--", "ERROR|WARN"],
                b"INFO awake\nERROR timeout\nWARN retries\n")
            assert gzip.decompress(Result.stdout) == b"ERROR timeout\nWARN retries\n"
            Result, _ = Run("LogBadRegex", [Bundle / Exe, "--stdio", "--provider", "Compression", "--provider",
                                           "Regex", "--", "["], b"", Expected=2)
            assert b"regular expression" in Result.stderr
        elif Name == "NativeDigest":
            Bytes = b"abc\0def" * 8000
            Result, Report["StandaloneMs"] = Run("DigestStandalone", [Bundle / Exe, "--stdio", "--provider", "Crypto"], Bytes)
            assert Result.stdout.strip() == hashlib.sha256(Bytes).hexdigest().encode()
            Denied = subprocess.run([Bundle / Exe, "--stdio"], cwd=Away, input=b"abc", capture_output=True, timeout=10)
            assert Denied.returncode != 0 and hashlib.sha256(b"abc").hexdigest().encode() not in Denied.stdout
        elif Name == "TicketService":
            Report["Journey"] = Ticket(Bundle / Exe, Away, Root)
        Save()

    for Name in ("BareHeadless", "BareDesktop"):
        App = Root / Name
        Run(Name + "-new", [Cli, "new", App, "--sdk", Sdk])
        if Name == "BareHeadless":
            (App / "Src/Main.luau").write_text('print("[Bare:Startup] Ready")\n', encoding="utf-8")
        Bundle = Root / (Name + "Bundle")
        Run(Name + "-build", [Cli, "build", App / "lui.json", "--sdk", Sdk, "--mode",
                             "headless" if Name == "BareHeadless" else "project", "--output", Bundle])
        Files = [File for File in Bundle.rglob("*") if File.is_file()]
        Report = Output[Name] = {"Bytes": sum(File.stat().st_size for File in Files)}
        assert not any(File.name.lower().startswith(("luicrypto", "libluicrypto", "luisqlite", "libluisqlite")) for File in Files)
        if Name == "BareHeadless":
            Result, Report["StandaloneMs"] = Run("BareStandalone", [Bundle / Exe, "--diagnostics", "development"])
            assert b"[Bare:Startup] Ready" in Result.stdout
    Save()
    print("[LUI:DeveloperJourneys] Five projects checked and built, and three headless standalone journeys passed")


def Ticket(Launcher, Away, Root):
    Data = Root / "TicketStorage"
    Data.mkdir(mode=0o700)
    with socket.socket() as Probe:
        Probe.bind(("127.0.0.1", 0))
        Port = Probe.getsockname()[1]

    def Request(Method, Body=None):
        with closing(http.client.HTTPConnection("127.0.0.1", Port, timeout=3)) as Client:
            Client.request(Method, "/tickets", Body,
                           {"Content-Type": "application/json"} if Body is not None else {})
            Reply = Client.getresponse()
            return Reply.status, Reply.read()

    def Start(Callback):
        Began = time.monotonic()
        with subprocess.Popen([Launcher, "--stdio", "--grant", "network.server", "--grant", "Storage",
                               "--storage-root", Data, "--", str(Port)], cwd=Away,
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as Child:
            try:
                Read = queue.Queue()
                Thread = threading.Thread(target=lambda: Read.put(Child.stdout.readline()), daemon=True)
                Thread.start()
                Line = Read.get(timeout=10)
                if Line != f"Ready {Port}\n".encode():
                    _, Error = Child.communicate(timeout=3)
                    raise AssertionError((Line, Error, Child.returncode))
                Ready = (time.monotonic() - Began) * 1000
                Callback()
                _, Error = Child.communicate(b"\n", timeout=10)
                assert Child.returncode == 0, Error
                return Ready
            finally:
                if Child.poll() is None:
                    Child.kill()
                    Child.wait(timeout=5)

    def Write():
        assert Request("POST", "broken")[0] == 400
        assert Request("POST", '{"Title":"Prototype"}')[0] == 202
        Deadline = time.monotonic() + 4
        while time.monotonic() < Deadline:
            if b"Prototype" in Request("GET")[1]:
                return
            time.sleep(0.03)
        raise RuntimeError("[LUI:DeveloperJourneys] Ticket did not persist")

    def Read():
        Status, Body = Request("GET")
        assert Status == 200 and json.loads(Body) == ["Prototype"]

    return {"StartupMs": Start(Write), "RestartMs": Start(Read), "InvalidInput": 400, "PersistedTitle": "Prototype"}


if __name__ == "__main__":
    Main()
