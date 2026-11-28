"""Run the synthetic workload on an isolated interactive Windows test desktop."""
import argparse
from contextlib import nullcontext
import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time


class MemoryCounters(ctypes.Structure):
    _fields_ = [
        ("Size", ctypes.c_uint32), ("PageFaultCount", ctypes.c_uint32),
        *((Name, ctypes.c_size_t) for Name in (
            "PeakWorkingSet", "WorkingSet", "QuotaPeakPagedPool", "QuotaPagedPool",
            "QuotaPeakNonPagedPool", "QuotaNonPagedPool", "Pagefile", "PeakPagefile",
        )),
    ]


def Percentiles(Values):
    return {f"P{P}Ms": Values[math.ceil(len(Values) * P / 100) - 1] for P in (50, 95, 99)} if Values else {}


def FrameTimestamps(Observations):
    Admissions, Publications, Rendering = {}, {}, {}
    for Line in Observations:
        Admission = re.search(r"AdmissionTimes first=(\d+) ms=([\d.,]+)$", Line)
        if Admission:
            for Index, Value in enumerate(Admission[2].split(","), int(Admission[1])):
                Admissions[Index] = float(Value)
        Stage = re.search(r"(PublicationTimes|RenderingTimes) ms=([\d.,:]+)$", Line)
        if Stage:
            Pending = Publications if Stage[1] == "PublicationTimes" else Rendering
            for Pair in Stage[2].split(","):
                Sequence, Time = Pair.split(":")
                Pending[int(Sequence)] = float(Time)
    return Admissions, Publications, Rendering


def FrameAges(Observations, Desktop=None):
    Admissions, Publications, Rendering = FrameTimestamps(Observations)
    if not Admissions or not (Publications or Rendering):
        return None

    def Ages(Samples):
        Values = sorted(Time - Admissions[Sequence] for Sequence, Time in Samples.items() if Sequence in Admissions)
        Missing = len(Samples) - len(Values)
        if not Values:
            return {"Matched": 0, "Unmatched": Missing}
        if Values[0] < 0:
            return {"Matched": len(Values), "Unmatched": Missing, "ClockMismatch": True}
        return {"Matched": len(Values), "Unmatched": Missing, **Percentiles(Values)}

    Result = {"SubmitToPublication": Ages(Publications), "SubmitToRenderingCallback": Ages(Rendering)}
    if Desktop is not None:
        Result["SubmitToDesktopObservation"] = Ages(Desktop)
    return Result


def ReadDesktop(Pathname):
    Identity, Header, *Lines = Pathname.read_text(encoding="utf-8").splitlines()
    Frequency = int(Identity.rsplit("qpc=", 1)[1])
    if Frequency <= 0 or Header != "sequence,qpc,accumulated":
        raise ValueError("Desktop observation format mismatch")
    Samples, Missed, PreviousStamp = {}, 0, 0
    for Line in Lines:
        Sequence, Stamp, Accumulated = map(int, Line.split(","))
        if Stamp <= PreviousStamp or (Samples and Sequence <= next(reversed(Samples))):
            raise ValueError("Desktop observations must advance by sequence and time")
        Samples[Sequence] = Stamp * 1000 / Frequency
        PreviousStamp = Stamp
        Missed += max(0, Accumulated - 1)
    Stamps = list(Samples.values())
    Gaps = sorted(Second - First for First, Second in zip(Stamps, Stamps[1:]))
    Rate = (len(Stamps) - 1) * 1000 / (Stamps[-1] - Stamps[0]) if Gaps else 0
    return Samples, {"Output": Identity, "Samples": len(Samples), "Hz": Rate, "Gaps": Percentiles(Gaps),
                     "AccumulatedUpdates": Missed}


def ReadProcessCpuMs(Handle):
    Created, Exited, Kernel, User = (wintypes.FILETIME() for _ in range(4))
    if not ctypes.windll.kernel32.GetProcessTimes(Handle, ctypes.byref(Created), ctypes.byref(Exited),
                                                  ctypes.byref(Kernel), ctypes.byref(User)):
        return None
    return sum((Value.dwHighDateTime << 32) | Value.dwLowDateTime for Value in (Kernel, User)) / 10000


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("Host", type=Path)
    Parser.add_argument("Workloads", type=Path)
    Parser.add_argument("Output", type=Path)
    Parser.add_argument("--cases", nargs="+")
    Parser.add_argument("--project", action="store_true")
    Parser.add_argument("--observe-desktop", type=Path)
    Parser.add_argument("--adapter", default="NVIDIA")
    CaptureMode = Parser.add_mutually_exclusive_group()
    CaptureMode.add_argument("--capture-window", action="store_const", dest="capture_mode", const="window")
    CaptureMode.add_argument("--capture-monitor", action="store_const", dest="capture_mode", const="monitor")
    Parser.add_argument("--sample-process-cpu", action="store_true",
                        help="record cumulative CPU time with QPC bounds for steady-state comparisons")
    Options = Parser.parse_args()
    ctypes.windll.kernel32.SetErrorMode(0x8003)
    Options.Output.mkdir(parents=True, exist_ok=False)
    Environment = {**os.environ, "DOTNET_DISABLE_GUI_ERRORS": "1",
                   "LUI_IMAGE_WORKLOAD_LOG_DIR": str(Options.Output / "Logs")}
    Passed, Ran = True, False
    for Project in sorted(Options.Workloads.iterdir()):
        if not Project.is_dir() or (Options.cases and Project.name not in Options.cases):
            continue
        Ran = True
        Result = {"Case": Project.name}
        Desktop = None
        Started = time.monotonic()
        with (Options.Output / f"{Project.name}.stderr").open("wb") as Diagnostics:
            Mode, Manifest = ("--project", "lui.json") if Options.project else ("--manifest", "Legacy.json")
            Flags = ["--diagnostics-log"] if Options.project else []
            with subprocess.Popen([str(Options.Host), Mode, str(Project / Manifest), *Flags],
                                  env=Environment, stdout=Diagnostics, stderr=subprocess.STDOUT) as Process:
                Handle = ctypes.c_void_p(int(Process._handle))
                CpuSamples = []
                DesktopFile = Options.Output / f"{Project.name}.desktop.csv"
                ObserverLog = Options.Output / f"{Project.name}.desktop.stderr"
                with (DesktopFile.open("wb") if Options.observe_desktop else nullcontext()) as Capture, (
                      ObserverLog.open("wb") if Options.observe_desktop else nullcontext()) as Errors:
                    Observer = (subprocess.Popen([str(Options.observe_desktop), str(Process.pid), Options.adapter,
                                                   *([Options.capture_mode] if Options.capture_mode else [])],
                                                  stdout=Capture, stderr=Errors, env=Environment)
                                if Options.observe_desktop else None)
                    Deadline = time.monotonic() + (85 if Observer else 65)
                    Peak = 0
                    while Process.poll() is None and time.monotonic() < Deadline:
                        if Observer and Environment.get("LUI_FRAME_PASSIVE_ORIGIN") and Observer.poll() not in (None, 0):
                            break
                        Counters = MemoryCounters()
                        Counters.Size = ctypes.sizeof(Counters)
                        if ctypes.windll.psapi.GetProcessMemoryInfo(Handle, ctypes.byref(Counters), Counters.Size):
                            Peak = max(Peak, Counters.PeakWorkingSet)
                        if Options.sample_process_cpu:
                            Before = time.perf_counter_ns()
                            Cpu = ReadProcessCpuMs(Handle)
                            After = time.perf_counter_ns()
                            if Cpu is not None:
                                CpuSamples.append((Before, After, Cpu))
                        time.sleep(0.025)
                    Result["Exit"] = Process.returncode
                    if Result["Exit"] is None:
                        Process.kill()
                        Process.wait()
                        Result["Exit"] = 124
                    if Observer:
                        try:
                            Result["ObserverExit"] = Observer.wait(timeout=4)
                        except subprocess.TimeoutExpired:
                            Observer.kill()
                            Observer.wait()
                            Result["ObserverExit"] = 124
                if Result.get("ObserverExit") == 0:
                    try:
                        Desktop, Result["Desktop"] = ReadDesktop(DesktopFile)
                    except (ValueError, IndexError) as Error:
                        Result["DesktopError"] = str(Error)
                Result["PeakWorkingSetBytes"] = Peak
                Result["Pid"] = Process.pid
                if (Cpu := ReadProcessCpuMs(Handle)) is not None:
                    Result["CpuMs"] = Cpu
                if Options.sample_process_cpu:
                    Result["ProcessCpuSamplesQpcNsAndCpuMs"] = CpuSamples
        Result["WallMs"] = (time.monotonic() - Started) * 1000
        Logs = list(Path(Environment["LUI_IMAGE_WORKLOAD_LOG_DIR"]).glob(f"*-{Result['Pid']}.log"))
        if Logs:
            Lines = Logs[0].read_text(encoding="utf-8", errors="replace").splitlines()
            Result["Observations"] = [Line for Line in Lines if "[LUI:ImageBench]" in Line or "[LUI:ImageResources]" in Line]
        Observations = Result.get("Observations", [])
        Age = FrameAges(Observations, Desktop)
        if Age is not None:
            Result["FrameAge"] = Age
        Result["Passed"] = Result["Exit"] == 0 and any(
            "[LUI:ImageBench] Done " in Line or "[LUI:ImageBench] Skipped Atlas " in Line for Line in Observations
        ) and not any("[LUI:ImageBench] Workers passed=false" in Line for Line in Observations)
        if Options.observe_desktop:
            Result["Passed"] = Result["Passed"] and Desktop is not None and len(Desktop) > 1
        Passed = Passed and Result["Passed"]
        with (Options.Output / "Results.jsonl").open("a", encoding="utf-8") as Report:
            Report.write(json.dumps(Result) + "\n")
    return 0 if Passed and Ran else 1


if __name__ == "__main__":
    raise SystemExit(Main())
