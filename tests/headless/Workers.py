"""Qualify isolated workers and measure fixed-work scaling with an installed SDK."""
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import subprocess
import tempfile
import threading
import time


Setup = """--!strict
local Workers: any = require("@Workers/Workers")
local Clock = app:GetService("PerformanceService")
local function Done()
    print("[LUI:WorkersTest] PASS")
end
"""

Work = """--!strict
local function Compute(Seed: number, Iterations: number): number
    local Sum = Seed
    for Index = 1, Iterations do Sum = (Sum * 31 + Index) % 65521 end
    return Sum
end
return Compute
"""

Handler = """--!strict
local Workers: any = require("@Workers/Workers")
local Compute = require("./WorkerMath")
local Sequence = 0
local Export: any = nil
Export = Workers.Handler(function(Value: any): any
    Sequence += 1
    if typeof(Value) == "buffer" then return Value end
    if Value.Mode == "authority" then
        local G: any = _G
        return {Owner=G.app~=nil, Instance=G.Instance~=nil, Scheduler=G.task~=nil,
            FileIO=G.io~=nil, Environment=G.os~=nil and G.os.getenv~=nil,
            Shell=G.os~=nil and G.os.execute~=nil, DynamicCode=G.loadstring~=nil,
            Coroutine=G.coroutine~=nil, Catch=G.pcall~=nil or G.xpcall~=nil,
            NativePattern=G.string.match~=nil or G.string.split~=nil}
    elseif Value.Mode == "identity" then
        local Current = "./TestWorker"
        return require(Current) == Export
    elseif Value.Mode == "sequence" then return {Sequence=Sequence, Tag=Value.Tag}
    elseif Value.Mode == "cpu" then return Compute(Value.Seed,Value.Iterations)
    elseif Value.Mode == "memory" then return buffer.create(Value.Size)
    elseif Value.Mode == "spin" then
        while true do Sequence = (Sequence + 1) % 65521 end
    elseif Value.Mode == "error" then error("WORKER_PRIVATE_FAILURE_CANARY")
    elseif Value.Mode == "unsupported" then return function() end
    end
    return Value
end)
return Export
"""

RoundTrip = """
local Entered = false
Workers.Start("./TestWorker",{},function(First: any, Failure: any)
    assert(Entered and First and not Failure)
    Workers.Start("./TestWorker",{},function(Second: any, Why: any)
        assert(Second and not Why)
        local Received = 0
        local function Hit()
            Received += 1
            if Received == 8 then First:Close(); Second:Close(); Done() end
        end
        local Delivered = 0
        for I = 1,3 do
            local Source = {Mode="sequence", Tag=I}
            First:Send(Source,function(Reply: any, Err: any)
                assert(not Err)
                Delivered += 1
                assert(Reply.Sequence==I and Reply.Tag==I and Delivered==I)
                Hit()
            end)
            Source.Tag = -1
        end
        Second:Send({Mode="sequence",Tag=7},function(Reply: any, Err: any)
            assert(not Err and Reply.Sequence==1 and Reply.Tag==7); Hit()
        end)
        Second:Send({Mode="authority"},function(Rights: any, Err: any)
            assert(not Err and type(Rights)=="table")
            local Count=0
            for _,Available in Rights do Count+=1; assert(Available==false) end
            assert(Count==10); Hit()
        end)
        Second:Send({Mode="identity"},function(Same: any, Err: any)
            assert(not Err and Same==true); Hit()
        end)
        local Bytes=buffer.create(65531)
        buffer.writeu8(Bytes,0,255); buffer.writeu8(Bytes,65530,177)
        Second:Send(Bytes,function(Reply: any, Err: any)
            assert(not Err and buffer.len(Reply)==65531 and buffer.readu8(Reply,0)==255 and buffer.readu8(Reply,65530)==177)
            Hit()
        end)
        buffer.writeu8(Bytes,0,0)
        Second:Send(buffer.create(0), function(Empty: any, Err: any)
            assert(not Err and typeof(Empty)=="buffer" and buffer.len(Empty)==0)
            Hit()
        end)
    end)
end)
Entered = true
"""

def Rejections():
    return """
local Paths = {"./MissingPrivateWorker", "./Main", "../OutsideWorker", "@Json/Json", "./BadExport"}
local Index = 0
local function Next()
    Index += 1
    if Index > #Paths then Done(); return end
    local Accepted = pcall(function()
        Workers.Start(Paths[Index],{},function(Actor: any, Failure: any)
            assert(not Actor and type(Failure)=="string")
            task.defer(Next)
        end)
    end)
    if not Accepted then task.defer(Next) end
end
Next()
"""

InvalidValues = """
Workers.Start("./TestWorker",{},function(Actor: any, Failure: any)
    assert(Actor and not Failure)
    local Invalid: {any} = {buffer.create(65532), function() end, app:GetService("PlatformService")}
    local Index=0
    local function Next()
        Index+=1
        if Index>#Invalid then
            Actor:Send({Mode="sequence",Tag=8},function(Reply: any, Err: any)
                assert(not Err and Reply.Sequence==1 and Reply.Tag==8)
                Actor:Close(); Done()
            end)
            return
        end
        local Ok=pcall(function()
            Actor:Send(Invalid[Index],function(Reply: any, Err: any)
                assert(not Reply and type(Err)=="string")
                task.defer(Next)
            end)
        end)
        if not Ok then task.defer(Next) end
    end
    Next()
end)
"""

Actors = """
local Held: {any}={}
local function StartNext()
    if #Held<4 then
        Workers.Start("./TestWorker",{},function(Actor: any, Failure: any)
            assert(Actor and not Failure)
            table.insert(Held,Actor)
            StartNext()
        end)
        return
    end
    local function Refused()
        for _,Actor in Held do Actor:Close(); Actor:Close() end
        local Deadline=Clock:GetTime()+2500
        local function Again()
            Workers.Start("./TestWorker",{},function(Replacement: any, Why: any)
                if Replacement then assert(not Why); Replacement:Close(); Done(); return end
                assert(type(Why)=="string" and Clock:GetTime()<Deadline)
                task.delay(.02,Again)
            end)
        end
        task.delay(.02,Again)
    end
    local Ok=pcall(function()
        Workers.Start("./TestWorker",{},function(Actor: any, Failure: any)
            assert(not Actor and type(Failure)=="string"); Refused()
        end)
    end)
    if not Ok then Refused() end
end
StartNext()
"""

Cancel = """
Workers.Start("./TestWorker",{},function(Actor: any, Failure: any)
    assert(Actor and not Failure)
    local Callbacks=0
    local Request=Actor:Send({Mode="spin"},function(Reply: any, Why: any)
        Callbacks+=1
        assert(not Reply and Why=="Canceled" and Callbacks==1)
    end)
    assert(Request:Cancel()); assert(not Request:Cancel())
    Actor:Close()
    task.delay(.12,function() assert(Callbacks<=1); Done() end)
end)
"""

def Backpressure(ByteBudget):
    Count = 5 if ByteBudget else 9
    Payload = "buffer.create(65531)" if ByteBudget else '{Mode="sequence",Tag=I}'
    return """
Workers.Start("./TestWorker",{MaxInterrupts=100000000},function(Actor: any, Failure: any)
    assert(Actor and not Failure)
    local Refused=false
    local function Reject()
        assert(not Refused); Refused=true; Actor:Close()
        task.delay(.05,function() Done() end)
    end
    Actor:Send({Mode="spin"},function(Value: any, Why: any)
        assert(Value==nil and (Why=="Canceled" or Why=="Closed"))
    end)
    for I=2,COUNT do
        local Ok,Error=pcall(function()
            Actor:Send(PAYLOAD,function(Reply: any, Err: any)
                assert(Reply==nil)
                if I==COUNT then
                    assert(Err=="WouldBlock" or Err=="LimitExceeded")
                    Reject()
                else assert(Err=="Canceled" or Err=="Closed") end
            end)
        end)
        if not Ok then
            assert(I==COUNT and type(Error)=="string")
            Reject()
        end
    end
end)
""".replace("COUNT", str(Count)).replace("PAYLOAD", Payload)


CancelStartup = """
for _=1,4 do
    local Request=Workers.Start("./SlowStart",{MaxInterrupts=100000000},function(Actor: any, Why: any)
        assert(not Actor and Why=="Canceled")
    end)
    assert(Request:Cancel())
end
task.delay(.12,function()
    local Held: {any}={}
    local function Next()
        Workers.Start("./TestWorker",{},function(Actor: any, Why: any)
            assert(Actor and not Why); table.insert(Held,Actor)
            if #Held<4 then Next(); return end
            for _,Item in Held do Item:Close() end
            Done()
        end)
    end
    Next()
end)
"""

Shutdown = """
Workers.Start("./TestWorker",{MaxInterrupts=100000000,TimeoutMilliseconds=30000},function(Actor: any, Why: any)
    assert(Actor and not Why)
    Actor:Send({Mode="spin"},function() error("late worker result") end)
    print("[LUI:WorkersTest] Running")
end)
"""

InitMemory = """
Workers.Start("./MemoryInit",{MemoryBytes=1048576},function(Actor: any, Why: any)
    assert(not Actor and Why=="LimitExceeded")
    Done()
end)
"""

def Refusal(Mode, Options, Extra=""):
    return """
Workers.Start("./TestWorker",OPTIONS,function(Actor: any, Failure: any)
    assert(Actor and not Failure)
    Actor:Send({Mode="MODE"EXTRA},function(Reply: any, Why: any)
        assert(Reply==nil and type(Why)=="string" and #Why<=64)
        assert(not string.find(Why,"CANARY",1,true))
        Actor:Close()
        print("[LUI:WorkersTest] Refusal="..Why)
        Done()
    end)
end)
""".replace("OPTIONS", Options).replace("MODE", Mode).replace("EXTRA", Extra)


def GetChecksum(Iterations, Jobs):
    Total = 0
    for Job in range(1, Jobs + 1):
        Value = Job
        for Index in range(1, Iterations + 1):
            Value = (Value * 31 + Index) % 65521
        Total += Value
    return Total


def Affinity():
    if os.name != "nt":
        return sorted(os.sched_getaffinity(0))
    Current, Available = ctypes.c_size_t(), ctypes.c_size_t()
    assert ctypes.windll.kernel32.GetProcessAffinityMask(ctypes.c_void_p(-1), ctypes.byref(Current), ctypes.byref(Available))
    return [Index for Index in range(Current.value.bit_length()) if Current.value & (1 << Index)]


def Measure(Command, Directory):
    if os.name != "nt":
        # Using a native launcher gives the host's RSS, excluding Python's
        # pre-exec memory watermark. GNU time reports CPU to centiseconds.
        Log = Directory / "child-resources.txt"
        Child = subprocess.run(["/usr/bin/time", "-f", "%U %S %M", "-o", Log, *Command], capture_output=True, timeout=12)
        if Child.returncode:
            raise RuntimeError((Child.returncode, Child.stdout[-300:], Child.stderr[-300:]))
        User, Kernel, Peak = Log.read_text().split()
        return Child, {"CpuMs": (float(User) + float(Kernel)) * 1000, "PeakResidentBytes": int(Peak) * 1024}

    from ctypes import wintypes

    class ProcessMemory(ctypes.Structure):
        _fields_ = [("Size", wintypes.DWORD), ("Faults", wintypes.DWORD)] + [
            (Key, ctypes.c_size_t) for Key in ("PeakResident", "Resident", "PeakPaged", "Paged",
                                               "PeakNonpaged", "Nonpaged", "Pagefile", "PeakPagefile")]

    with tempfile.TemporaryFile() as Out, tempfile.TemporaryFile() as Err, subprocess.Popen(Command, stdout=Out, stderr=Err) as Child:
        Handle = ctypes.c_void_p(int(Child._handle))
        Peak, Deadline = 0, time.monotonic() + 12
        Counters = ProcessMemory()
        Counters.Size = ctypes.sizeof(Counters)
        while True:
            if ctypes.windll.psapi.GetProcessMemoryInfo(Handle, ctypes.byref(Counters), Counters.Size):
                Peak = max(Peak, Counters.PeakResident)
            if Child.poll() is not None:
                break
            if time.monotonic() >= Deadline:
                Child.kill()
                raise subprocess.TimeoutExpired(Command, 12)
            time.sleep(.001)
        Created, Exited, Kernel, User = (ctypes.c_uint64() for _ in range(4))
        assert ctypes.windll.kernel32.GetProcessTimes(Handle, ctypes.byref(Created), ctypes.byref(Exited),
                                                     ctypes.byref(Kernel), ctypes.byref(User)) and Peak > 0
        Out.seek(0)
        Err.seek(0)
        Finished = subprocess.CompletedProcess(Command, Child.returncode, Out.read(), Err.read())
        return Finished, {"CpuMs": (Kernel.value + User.value) / 10000, "PeakResidentBytes": Peak}


Latency = """
Workers.Start("./TestWorker",{},function(Actor: any,Failure: any)
    assert(Actor and not Failure)
    local Sizes={64,4096,65531}
    local function Sample(SizeIndex: number)
        if SizeIndex>#Sizes then Actor:Close(); Done(); return end
        local Size=Sizes[SizeIndex]
        local Input=buffer.create(Size)
        buffer.writeu8(Input,0,253);buffer.writeu8(Input,Size-1,19)
        local Results: {string}={}
        local Index=0
        local function Send()
            Index+=1
            local Before=Clock:GetTime()
            Actor:Send(Input,function(Reply: any,Why: any)
                local Ms=Clock:GetTime()-Before
                assert(not Why and buffer.len(Reply)==Size and buffer.readu8(Reply,0)==253 and buffer.readu8(Reply,Size-1)==19)
                if Index>3 then table.insert(Results,string.format("%.5f",Ms)) end
                if Index<34 then Send(); return end
                print(string.format("[LUI:WorkersTest] Latency %d %s",Size,table.concat(Results,",")))
                Sample(SizeIndex+1)
            end)
        end
        Send()
    end
    Sample(1)
end)
"""


def CheckSnapshot(Sdk, Host, App, Base):
    # Hold the parent at a stdio handshake after project preparation. Replace a
    # declared worker source on disk before Start, then require the original result.
    Project = Base / "snapshot"
    shutil.copytree(App, Project)
    Manifest = Project / "lui.json"
    Data = json.loads(Manifest.read_bytes())
    Starter = json.loads((Sdk / "Examples/parallel-data/lui.json").read_bytes())
    Data["Capabilities"] = Starter["Capabilities"]
    Data["ExtensionDeclarations"] = Starter["ExtensionDeclarations"]
    for Field in ("Requires", "Dependencies"):
        Data["Modules"][Field] = Starter["Modules"][Field]
    Lock = Starter["Modules"]["Lock"]
    shutil.copyfile(Sdk / "Examples/parallel-data" / Lock, Project / Lock)
    Manifest.write_text(json.dumps(Data), encoding="utf-8")
    (Project / Data["Script"]).write_text(Setup + """
local Stdio=require("@Stdio/Stdio")
Stdio.SetExitStatus(8)
Stdio.Write("READY\\n",function(Status)
    assert(Status=="Ok")
    Stdio.ReadLine(16,function(Received)
        assert(Received=="Ok")
        Workers.Start("./TestWorker",{},function(Actor: any,Failure: any)
            assert(Actor and not Failure)
            Actor:Send({Mode="sequence",Tag=67},function(Reply: any,Err: any)
                assert(not Err and Reply.Tag==67 and Reply.Sequence==1)
                Actor:Close()
                Stdio.Write("CAPTURED\\n",function(Written)
                    if Written=="Ok" then Stdio.SetExitStatus(0) end
                end)
            end)
        end)
    end)
end)
""", encoding="utf-8")
    with subprocess.Popen([Host, "--project", Manifest, "--stdio", "--timeout-ms", "6000"],
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as Process:
        Received = []
        def ReadReady():
            Received.append(Process.stdout.readline())
        Reader = threading.Thread(target=ReadReady, daemon=True)
        Reader.start()
        try:
            Reader.join(4)
            assert Received == [b"READY\n"], ("snapshot handshake", Received)
            (Project / Data["Modules"]["Root"] / "TestWorker.luau").write_text("return 37\n", encoding="utf-8")
            Output, Error = Process.communicate(b"start\n", timeout=5)
            assert (Process.returncode, Output, Error) == (0, b"CAPTURED\n", b""), (Process.returncode, Output, Error)
        finally:
            if Process.poll() is None:
                Process.kill()
            Reader.join(1)


def Benchmark(Count, Iterations, Jobs, Checksum):
    return """
local Compute=require("./WorkerMath")
local Count,Iterations,Jobs=ACTORS,ITERATIONS,JOBS
local Started=Clock:GetTime()
local Ready=Started
local Sum,Completed=0,0
local Held: {any}={}
local function Finish()
    assert(Completed==Jobs and Sum==CHECKSUM)
    local End=Clock:GetTime()
    for _,Actor in Held do Actor:Close() end
    print(string.format("[LUI:WorkersTest] Timing %.4f %.4f",Ready-Started,End-Ready))
    Done()
end
if Count==0 then
    for I=1,Jobs do Sum+=Compute(I,Iterations); Completed+=1 end
    Finish()
else
    local function Run()
        Ready=Clock:GetTime()
        local Next=0
        local function Send(Actor: any)
            Next+=1
            local Id=Next
            if Id>Jobs then return end
            Actor:Send({Mode="cpu",Seed=Id,Iterations=Iterations},function(Reply: any,Failure: any)
                assert(type(Reply)=="number" and not Failure)
                Sum+=Reply; Completed+=1
                if Completed==Jobs then Finish() else Send(Actor) end
            end)
        end
        for _,Actor in Held do Send(Actor) end
    end
    for _=1,Count do
        Workers.Start("./TestWorker",{},function(Actor: any,Failure: any)
            assert(Actor and not Failure)
            table.insert(Held,Actor)
            if #Held==Count then Run() end
        end)
    end
end
""".replace("ACTORS", str(Count)).replace("ITERATIONS", str(Iterations)).replace("JOBS", str(Jobs)).replace("CHECKSUM", str(Checksum))


def PoolBenchmark(Count, Iterations, Jobs, Checksum, Pooled):
    return """
local Count,Iterations,Jobs=ACTORS,ITERATIONS,JOBS
local Started=Clock:GetTime()
local Finished=false
local function Run(Dispatch: (number,any,(any,string?)->())->(), Close: ()->())
    local Warming,Next,Completed,Sum=Count,0,0,0
    local Ready=0
    local MaxOwnerGap=0
    local Ticks=0
    local Times: {number}={}
    local function Send(Slot: number)
        Next+=1
        local Id=Next
        if Id>Jobs then return end
        local Admitted=Clock:GetTime()
        Dispatch(Slot,{Mode="cpu",Seed=Id,Iterations=Iterations},function(Value,Failure)
            assert(type(Value)=="number" and not Failure)
            table.insert(Times,Clock:GetTime()-Admitted)
            Sum+=Value
            Completed+=1
            if Completed<Jobs then Send(Slot);return end
            local Work=Clock:GetTime()-Ready
            assert(Sum==CHECKSUM)
            Finished=true
            Close()
            table.sort(Times)
            print(string.format("[LUI:WorkersTest] Pool %.4f %.4f %.4f %.4f %d",
                Ready-Started,Work,Times[math.ceil(#Times*.95)],MaxOwnerGap,Ticks))
            Done()
        end)
    end
    local function Warmed(Value: any,Failure: string?)
        assert(type(Value)=="number" and not Failure)
        Warming-=1
        if Warming>0 then return end
        Ready=Clock:GetTime()
        local Previous=Ready
        local function Tick()
            if Finished then return end
            local Now=Clock:GetTime()
            Ticks+=1
            MaxOwnerGap=math.max(MaxOwnerGap,Now-Previous)
            Previous=Now
            task.delay(0.001,Tick)
        end
        task.delay(0.001,Tick)
        for Slot=1,Count do Send(Slot) end
    end
    for Slot=1,Count do Dispatch(Slot,{Mode="cpu",Seed=1,Iterations=Iterations},Warmed) end
end
if POOLED then
    local Pool=Workers.Pool("./TestWorker",Count)
    Run(function(_,Value,Reply) assert(Pool:Send(Value,Reply)) end,function() Pool:Close() end)
else
    local Actors: {any}={}
    for _=1,Count do
        Workers.Start("./TestWorker",{},function(Actor: any,Failure: any)
            assert(Actor and not Failure)
            table.insert(Actors,Actor)
            if #Actors<Count then return end
            Run(function(Slot,Value,Reply) Actors[Slot]:Send(Value,Reply) end,
                function() for _,Active in Actors do Active:Close() end end)
        end)
    end
end
""".replace("ACTORS", str(Count)).replace("ITERATIONS", str(Iterations)).replace("JOBS", str(Jobs)).replace(
        "CHECKSUM", str(Checksum)).replace("POOLED", str(Pooled).lower())


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("--sdk", required=True, type=Path)
    Parser.add_argument("--samples", "--runs", dest="samples", type=int, default=5, choices=range(1, 21))
    Parser.add_argument("--iterations", type=int, default=16000, choices=range(1, 250001), metavar="1..250000")
    Parser.add_argument("--compare-pool", action="store_true")
    Parser.add_argument("--output", type=Path)
    Options = Parser.parse_args()
    Sdk = Options.sdk.resolve()
    Metadata = json.loads((Sdk / "SdkManifest.json").read_bytes())
    Cli = Sdk / Metadata["Components"]["Cli"]
    Host = Sdk / Metadata["Components"]["HeadlessHost"]
    Results = {}
    with tempfile.TemporaryDirectory(prefix="lui-workers-") as Temporary:
        Base = Path(Temporary)
        App = Base / "app"
        shutil.copytree(Sdk / "Examples/parallel-data", App)
        Manifest = App / "lui.json"

        def CliCheck(Arguments):
            Completed = subprocess.run([Cli, *Arguments, "--sdk", Sdk], capture_output=True, timeout=45)
            assert Completed.returncode == 0, (Arguments[0], Completed.stdout[-700:], Completed.stderr[-700:])

        def Execute(Name, Project, Marker=True, Code=0, Extra=(), Measured=False):
            Started = time.perf_counter()
            Command = [Host, "--project", Project, "--diagnostics", "development", "--poll-ms", "1", *Extra]
            if Measured:
                Completed, Metrics = Measure(Command, Base)
            else:
                Completed = subprocess.run(Command, capture_output=True, timeout=12)
                Metrics = {}
            Metrics["ProcessMs"] = (time.perf_counter() - Started) * 1000
            Output = (Completed.stdout + Completed.stderr).decode("utf-8", "replace")
            assert "WORKER_PRIVATE_FAILURE_CANARY" not in Output, Name
            assert Completed.returncode == Code and "NativeResources=0" in Output, (Name, Completed.returncode, Output[-700:])
            if Marker:
                assert Output.count("[LUI:WorkersTest] PASS") == 1, (Name, Output[-700:])
            return Output, Metrics

        CliCheck(["check", Manifest])
        CliCheck(["stage", Manifest, "--output", Base / "starter"])
        CliCheck(["verify", Base / "starter"])
        Starter = subprocess.run([Cli, "run", Base / "starter/lui.json", "--sdk", Sdk, "--mode", "headless", "--stdio", "true"],
                                 capture_output=True, timeout=15)
        Package = Base / "standalone"
        CliCheck(["build", Manifest, "--mode", "headless", "--output", Package])
        Launcher = Package / ("Start-LUI.exe" if os.name == "nt" else "Start-LUI.sh")
        Environment = {Key:Value for Key,Value in os.environ.items() if Key not in ("LUI_SDK", "LD_LIBRARY_PATH")}
        Standalone = subprocess.run([Launcher, "--stdio"], cwd=Base, env=Environment, capture_output=True, timeout=15)
        assert (Standalone.returncode, Standalone.stdout, Standalone.stderr) == (Starter.returncode, Starter.stdout, Starter.stderr)
        Expected = sum(I % 65521 for I in range(1, 200001)) % 1000000007
        assert (Starter.returncode, Starter.stdout, Starter.stderr) == (0, f"{Expected}\n".encode(), b""), (
            Starter.returncode, Starter.stdout[-200:], Starter.stderr[-300:])
        Document = json.loads(Manifest.read_bytes())
        # Run the remaining fixtures without IO grants, with development output
        # and the native host's post-destruction resource count visible.
        Document["Capabilities"] = []
        Document["ExtensionDeclarations"] = []
        Document["Modules"]["Requires"].remove("Stdio")
        Document["Modules"]["Dependencies"] = [D for D in Document["Modules"]["Dependencies"] if D["Alias"] != "Stdio"]
        Lock = App / Document["Modules"]["Lock"]
        Pins = json.loads(Lock.read_bytes())
        Pins["Packages"] = [P for P in Pins["Packages"] if P["Alias"] != "Stdio"]
        Lock.write_text(json.dumps(Pins), encoding="utf-8")
        Src = App / Document["Modules"]["Root"]
        MainSource = App / Document["Script"]
        SlowStart = 'local Compute=require("./WorkerMath")\nassert(Compute(1,500000)>=0)\nreturn require("./TestWorker")\n'
        InitAllocation = "local Bytes=buffer.create(134217728)\nreturn function() return buffer.tostring(Bytes) end\n"
        for Name, Body in (("TestWorker.luau", Handler), ("WorkerMath.luau", Work), ("BadExport.luau", "return 123\n"),
                           ("SlowStart.luau", SlowStart), ("MemoryInit.luau", InitAllocation)):
            (Src / Name).write_text(Body, encoding="utf-8")
            if Name not in Document["Modules"]["Files"]:
                Document["Modules"]["Files"].append(Name)
        Document["Sandbox"] = {"MaxMemoryBytes": 33554432, "MaxInterrupts": 100000000}
        Manifest.write_text(json.dumps(Document), encoding="utf-8")
        Ungranted = Base / "undeclared"
        (Ungranted / "Src").mkdir(parents=True)
        (Ungranted / "Src/Main.luau").write_text(
            'assert(not pcall(function() app:GetService("WorkerService") end))\nprint("[LUI:WorkersTest] PASS")\n',
            encoding="utf-8")
        (Ungranted / "lui.json").write_text(json.dumps({"SchemaVersion": 2, "Script": "Src/Main.luau",
            "Capabilities": [], "Extensions": [], "ExtensionDeclarations": [], "Assets": [],
            "Diagnostics": {"Mode": "Development"},
            "Modules": {"Root": "Src", "Files": ["Main.luau"], "Requires": [], "Dependencies": []}}), encoding="utf-8")
        Execute("undeclared", Ungranted / "lui.json")
        Results["undeclared"] = "PASS"
        for Name, Body in (
            ("roundtrip", RoundTrip), ("paths", Rejections()), ("invalid-values", InvalidValues), ("actors", Actors),
            ("packet-backpressure", Backpressure(False)), ("byte-backpressure", Backpressure(True)),
            ("cancel", Cancel), ("cancel-start", CancelStartup), ("init-memory", InitMemory),
            ("memory", Refusal("memory", "{}", ",Size=134217728")),
            ("interrupts", Refusal("spin", "{}")), ("handler-error", Refusal("error", "{}")),
            ("deadline", Refusal("spin", "{MaxInterrupts=100000000,TimeoutMilliseconds=200}")),
            ("bad-response", Refusal("unsupported", "{}")),
            ("idle-owner-exit", 'Workers.Start("./TestWorker",{},function(Actor: any, Why: any) assert(Actor and not Why); Done() end)'),
        ):
            MainSource.write_text(Setup + Body, encoding="utf-8")
            Project = Manifest
            if Name == "roundtrip":
                CliCheck(["check", Manifest])
                CliCheck(["stage", Manifest, "--output", Base / "tested"])
                CliCheck(["verify", Base / "tested"])
                Project = Base / "tested/lui.json"
            Output, _ = Execute(Name, Project)
            Match = re.search(r"\[LUI:WorkersTest\] Refusal=(\w+)", Output)
            Results[Name] = Match.group(1) if Match else "PASS"

        for Name, Code in (("memory", "LimitExceeded"), ("interrupts", "LimitExceeded"), ("deadline", "DeadlineExceeded"),
                           ("handler-error", "ProviderFailure"), ("bad-response", "ProviderFailure")):
            assert Results[Name] == Code, (Name, Results[Name], Code)
        MainSource.write_text(Setup + Shutdown, encoding="utf-8")
        Output, Elapsed = Execute("shutdown", Manifest, Marker=False, Code=124, Extra=("--timeout-ms", "100"))
        assert "[LUI:WorkersTest] Running" in Output and Elapsed["ProcessMs"] < 4000, ("shutdown", Elapsed, Output[-700:])
        Results["shutdown"] = "PASS"
        CheckSnapshot(Sdk, Host, App, Base)
        Results["captured-source"] = "PASS"
        Jobs = 12
        Checksum = GetChecksum(Options.iterations, Jobs)
        Counts = [0, 1, 2, 4]
        Samples = {str(N): [] for N in Counts}
        Trips = {str(Size): [] for Size in (64, 4096, 65531)}
        for Round in range(Options.samples + 1):
            Shift = Round % len(Counts)
            for N in Counts[Shift:] + Counts[:Shift]:
                MainSource.write_text(Setup + Benchmark(N, Options.iterations, Jobs, Checksum), encoding="utf-8")
                Output, Metrics = Execute(f"scaling-{N}", Manifest, Measured=True)
                Timing = re.search(r"\[LUI:WorkersTest\] Timing ([\d.]+) ([\d.]+)", Output)
                assert Timing is not None, Output[-700:]
                if Round:
                    Samples[str(N)].append({"StartupMs": float(Timing[1]), "WorkMs": float(Timing[2]), **Metrics})
            if Round:
                MainSource.write_text(Setup + Latency, encoding="utf-8")
                Output, _ = Execute("latency", Manifest)
                for Size in Trips:
                    Match = re.search(r"\[LUI:WorkersTest\] Latency " + Size + r" ([\d.,]+)", Output)
                    assert Match is not None, Output[-800:]
                    Values = list(map(float, Match[1].split(",")))
                    assert len(Values) == 31
                    Trips[Size].extend(Values)
        PoolResults = {}
        if Options.compare_pool:
            PoolJobs = 24
            for Iterations in sorted({16, Options.iterations}):
                Expected = GetChecksum(Iterations, PoolJobs)
                Profiles = [(Count, Pooled) for Count in (1, 2, 4) for Pooled in (False, True)]
                PoolSamples = {f"{Count}:{'pool' if Pooled else 'direct'}": [] for Count, Pooled in Profiles}
                for Round in range(Options.samples + 1):
                    Shift = Round % len(Profiles)
                    for Count, Pooled in Profiles[Shift:] + Profiles[:Shift]:
                        MainSource.write_text(Setup + PoolBenchmark(Count, Iterations, PoolJobs, Expected, Pooled), encoding="utf-8")
                        Output, Metrics = Execute("pool-scaling", Manifest, Measured=True)
                        Timing = re.search(r"\[LUI:WorkersTest\] Pool ([\d.]+) ([\d.]+) ([\d.]+) ([\d.]+) (\d+)", Output)
                        assert Timing is not None, Output[-700:]
                        if Round:
                            Keys = ("WarmupMs", "WorkMs", "ResultP95Ms", "ScheduledOwnerGapMaxMs", "ScheduledOwnerTicks")
                            Sample = dict(zip(Keys, map(float, Timing.groups())))
                            PoolSamples[f"{Count}:{'pool' if Pooled else 'direct'}"].append({**Sample, **Metrics})
                PoolResults[str(Iterations)] = {"Jobs": PoolJobs, "Samples": PoolSamples,
                    "Medians": {Key: {Field: statistics.median(Item[Field] for Item in Items)
                                      for Field in Items[0]} for Key, Items in PoolSamples.items()}}
    Report = {"Platform": platform.platform(), "LogicalCpus": os.cpu_count(), "AllowedCpuIndices": Affinity(),
              "SdkManifestSha256": hashlib.sha256((Sdk / "SdkManifest.json").read_bytes()).hexdigest(),
              "Tests": Results, "Jobs": Jobs, "IterationsPerJob": Options.iterations, "Checksum": Checksum,
              "Samples": Samples, "Medians": {N: {Key: statistics.median(Item[Key] for Item in Values)
                  for Key in Values[0]} for N, Values in Samples.items()},
              "LatencyMs": {Size: {"Samples": Values, "Median": statistics.median(Values),
                  "P95": sorted(Values)[math.ceil(len(Values) * .95) - 1]} for Size, Values in Trips.items()}}
    if Options.compare_pool:
        Report["PoolComparison"] = PoolResults
    Text = json.dumps(Report, indent=2)
    if Options.output:
        Options.output.write_text(Text + "\n", encoding="utf-8")
    print(Text)


if __name__ == "__main__":
    Main()
