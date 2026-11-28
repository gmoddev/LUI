"""Compare startup of the same minimal standalone project in two extracted SDKs."""

import argparse
import ctypes
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("--baseline", type=Path, required=True)
    Parser.add_argument("--candidate", type=Path, required=True)
    Parser.add_argument("--output", type=Path, required=True)
    Parser.add_argument("--gtk-idle", action="store_true", help="Also measure a simple window on an isolated GTK X11 display")
    Options = Parser.parse_args()
    Root = Options.output.resolve()
    Root.mkdir(parents=True)
    Away = Root / "Outside"
    Away.mkdir()
    Bundles = {}
    Desktops = {}
    Data = {}
    for Name, Location in (("Baseline", Options.baseline), ("Candidate", Options.candidate)):
        Sdk = Location.resolve()
        Info = json.loads((Sdk / "SdkManifest.json").read_bytes())
        Cli = Sdk / Info["Components"]["Cli"]
        Win = Info["Target"] == "WindowsX64"
        if Win:
            ctypes.windll.kernel32.SetErrorMode(0x8003)
        App, Bundle = Root / Name, Root / (Name + "Bundle")
        subprocess.run([Cli, "new", App, "--sdk", Sdk], cwd=Away, check=True, capture_output=True, timeout=20)
        (App / "Src/Main.luau").write_bytes(b"return true\n")
        subprocess.run([Cli, "build", App / "lui.json", "--mode", "headless", "--sdk", Sdk, "--output", Bundle],
                       cwd=Away, check=True, capture_output=True, timeout=30)
        Bundles[Name] = Bundle / ("Start-LUI.exe" if Win else "Start-LUI.sh")
        Data[Name] = {"Version": Info["SdkVersion"], "Target": Info["Target"], "SamplesMs": [],
                      "Bytes": sum(Item.stat().st_size for Item in Bundle.rglob("*") if Item.is_file())}
        if Options.gtk_idle:
            assert not Win and os.environ.get("DISPLAY")
            Desktop = Root / (Name + "Desktop") / "BareDesk"
            subprocess.run([Cli, "new", Desktop, "--sdk", Sdk], cwd=Away, check=True, capture_output=True, timeout=20)
            Installed = Desktop.parent / "Bundle"
            subprocess.run([Cli, "build", Desktop / "lui.json", "--mode", "project", "--sdk", Sdk, "--output", Installed],
                           cwd=Away, check=True, capture_output=True, timeout=30)
            Desktops[Name] = Installed / "Start-LUI.sh"

    for Round in range(16):
        Order = ("Baseline", "Candidate") if Round % 2 == 0 else ("Candidate", "Baseline")
        for Name in Order:
            Start = time.perf_counter()
            subprocess.run([Bundles[Name]], cwd=Away, check=True, capture_output=True, timeout=10)
            if Round:
                Data[Name]["SamplesMs"].append((time.perf_counter() - Start) * 1000)
    for Profile in Data.values():
        Profile["MedianMs"] = statistics.median(Profile["SamplesMs"])
    if Desktops:
        Samples = {"Baseline": [], "Candidate": []}
        for Round in range(3):
            for Name in (("Baseline", "Candidate") if Round != 1 else ("Candidate", "Baseline")):
                Samples[Name].append(Idle(Desktops[Name], Away))
        for Name, Values in Samples.items():
            Data[Name]["GtkIdle"] = {"Samples": Values, "Median": {
                Key: statistics.median(Value[Key] for Value in Values) for Key in Values[0]
            }}
    (Root / "Report.json").write_text(json.dumps(Data, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({Name: {"MedianMs": Row["MedianMs"], "Bytes": Row["Bytes"]} for Name, Row in Data.items()}))


def Idle(Launcher, Away):
    Start = time.perf_counter()
    with subprocess.Popen([Launcher], cwd=Away, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) as Process:
        try:
            while time.perf_counter() - Start < 8:
                Found = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "^BareDesk$"],
                                       capture_output=True, text=True, timeout=2)
                if Found.returncode == 0:
                    break
                assert Process.poll() is None
                time.sleep(0.025)
            else:
                raise TimeoutError("BareDesk did not open")
            FirstWindow = (time.perf_counter() - Start) * 1000
            time.sleep(0.75)
            Status = Path(f"/proc/{Process.pid}/status")
            Stat = Path(f"/proc/{Process.pid}/stat")
            def Ticks():
                Parts = Stat.read_text().rsplit(") ", 1)[1].split()
                return int(Parts[11]) + int(Parts[12])
            Count = Ticks()
            time.sleep(1.5)
            Cpu = (Ticks() - Count) * 1000 / os.sysconf("SC_CLK_TCK")
            Memory = {Line.split(":")[0]: int(Line.split()[1]) * 1024 for Line in Status.read_text().splitlines()
                      if Line.startswith(("VmRSS:", "VmHWM:"))}
            Window = Found.stdout.splitlines()[0]
            subprocess.run(["xdotool", "windowactivate", "--sync", Window, "key", "alt+F4"], capture_output=True,
                           check=True, timeout=4)
            assert Process.wait(timeout=5) == 0
            return {"FirstWindowMs": FirstWindow, "IdleCpuMs": Cpu,
                    "IdleRssBytes": Memory["VmRSS"], "PeakBytes": Memory["VmHWM"]}
        finally:
            if Process.poll() is None:
                Process.terminate()
                try:
                    Process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    Process.kill()
                    Process.wait(timeout=3)


if __name__ == "__main__":
    Main()
