"""Measure cold worker bootstrap and warm worker crossover in a captured project."""
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import tempfile
import time

from Prepare import VendorWorkers


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("Host", type=Path)
    Parser.add_argument("--samples", type=int, default=5)
    Parser.add_argument("--kernel", choices=("integer", "numeric"), default="integer")
    Options = Parser.parse_args()
    if not 1 <= Options.samples <= 20:
        Parser.error("--samples must be between 1 and 20")
    Host = Options.Host.resolve()
    Samples = {}
    with tempfile.TemporaryDirectory(prefix="lui-worker-crossover-") as Temporary:
        Root = Path(Temporary)
        (Root / "Src").mkdir()
        Modules = {"Root": "Src", "Files": ["Main.luau", "Compute.luau", "Math.luau"], "Requires": [], "Dependencies": []}
        VendorWorkers(Root, Modules)
        Manifest = Root / "lui.json"
        Manifest.write_text(json.dumps({
            "SchemaVersion": 2, "Script": "Src/Main.luau", "Capabilities": [], "Extensions": [],
            "ExtensionDeclarations": [], "Assets": [], "Diagnostics": {"Mode": "Development"},
            "Modules": Modules, "Sandbox": {"MaxMemoryBytes": 33554432, "MaxInterrupts": 100000000},
        }), encoding="utf-8")
        Integer = """return function(Count)
    local Sum = 0
    for Index = 1, Count do Sum = (Sum + Index * 17) % 65521 end
    return Sum
end
"""
        Numeric = """return function(Count)
    local Sum = 0
    for Index = 1, Count do
        local X, Y = (Index % 317) / 317, (Index % 421) / 421
        local Wave = math.sin(X * 8) * math.cos(Y * 10) + math.sin((X + Y) * 19) / 3
        local Weight = math.exp(-X * X - Y * Y) * math.sqrt(1 + X + Y)
        Sum += (Wave + math.cos(Weight * 11)) * Weight
    end
    return math.floor(Sum)
end
"""
        (Root / "Src/Math.luau").write_text(Integer if Options.kernel == "integer" else Numeric, encoding="utf-8")
        (Root / "Src/Compute.luau").write_text(
            'return require("@Workers/Workers").Handler(require("./Math"))\n', encoding="utf-8")
        for Size in (100, 4000, 16000, 64000, 250000):
            for Round in range(Options.samples + 1):
                Actors = (0, 1, 2, 4)
                for Count in Actors[Round % 4:] + Actors[:Round % 4]:
                    MainSource = Root / "Src/Main.luau"
                    MainSource.write_text(f"local Actors, Size = {Count}, {Size}\n" + Workload, encoding="utf-8")
                    Started = time.perf_counter()
                    Process = subprocess.run([Host, "--project", Manifest, "--diagnostics", "development",
                        "--poll-ms", "1"], capture_output=True, text=True, timeout=15)
                    WallMs = (time.perf_counter() - Started) * 1000
                    Result = re.search(r"\[LUI:WorkerCost\] ([\d.]+) ([\d.]+)", Process.stdout + Process.stderr)
                    if Process.returncode != 0 or not Result:
                        raise RuntimeError((Size, Count, Process.stdout[-800:], Process.stderr[-800:]))
                    if Round:
                        Samples.setdefault(f"{Size}/{Count}", []).append({
                            "StartupMs": float(Result[1]), "WorkMs": float(Result[2]), "ProcessMs": WallMs,
                        })
    print(json.dumps({
        "Kernel": Options.kernel,
        "LogicalCpus": os.cpu_count(),
        "AllowedCpuIndices": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
        "Jobs": 12, "Samples": Samples,
        "Medians": {Key: {Name: statistics.median(Run[Name] for Run in Runs) for Name in Runs[0]}
                    for Key, Runs in Samples.items()},
    }))


Workload = """
local WithWorkers = Actors > 0
local Compute = require("./Math")
local Workers = require("@Workers/Workers")
local Expected = Compute(Size)
local Started = os.clock()
local WarmStarted, Startup = Started, 0
local Handles = {}
local Completed, StartedJobs = 0, 0
local function Finish()
    print(string.format("[LUI:WorkerCost] %.6f %.6f", Startup, (os.clock() - WarmStarted) * 1000))
    for _, Worker in Handles do Worker:Close() end
end
if not WithWorkers then
    for _ = 1, 12 do assert(Compute(Size) == Expected) end
    Finish()
    return
end
local function Queue(Actor)
    if StartedJobs == 12 then return end
    StartedJobs += 1
    Actor:Send(Size, function(Value, Failure)
        assert(not Failure and Value == Expected, tostring(Failure))
        Completed += 1
        if Completed == 12 then Finish() else Queue(Actor) end
    end)
end
for _ = 1, Actors do
    Workers.Start("./Compute", {MaxInterrupts = 10000000}, function(Actor, Failure)
        assert(Actor and not Failure, tostring(Failure))
        table.insert(Handles, Actor)
        if #Handles == Actors then
            WarmStarted = os.clock()
            Startup = (WarmStarted - Started) * 1000
            for _, Worker in Handles do Queue(Worker) end
        end
    end)
end
"""

if __name__ == "__main__":
    Main()
