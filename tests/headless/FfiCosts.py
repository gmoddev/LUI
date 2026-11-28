"""Measure the actual Luau facade in a packaged application, with a fresh process for each sample."""
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tempfile

Header = '''
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
int sqlite3_open(const char *, sqlite3 **);
int sqlite3_close(sqlite3 *);
int sqlite3_prepare_v2(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
int sqlite3_bind_int64(sqlite3_stmt *, int, long long);
int sqlite3_step(sqlite3_stmt *);
long long sqlite3_column_int64(sqlite3_stmt *, int);
int sqlite3_reset(sqlite3_stmt *);
int sqlite3_finalize(sqlite3_stmt *);
'''
SqlSymbols = ('sqlite3_open', 'sqlite3_close', 'sqlite3_prepare_v2', 'sqlite3_bind_int64',
              'sqlite3_step', 'sqlite3_column_int64', 'sqlite3_reset', 'sqlite3_finalize')

Source = '''--!strict
local Ffi = require("@Ffi/Ffi")
local Json = require("@Json/Json")
local Stdio = require("@Stdio/Stdio")
local Fixture = require("./Binding")
local Clock = app:GetService("PerformanceService")
local Costs: {[string]: number} = {}
local First = Clock:GetTime()
local Image = Fixture.Open()
Costs.FirstImageUs = (Clock:GetTime() - First) * 1000
local Echo = Image:Bind("LuiFfiEchoU32", {"u32"}, "u32")
local Pair = Ffi.Struct({{Name="Left",Type="i32"},{Name="Right",Type="f64"}})
local Adjust = Image:Bind("LuiFfiAdjustPair", {Pair,"i32"}, Pair)
local Sum = Image:Bind("LuiFfiSumPair", {"pointer"}, "f64")
local Hash = Image:Bind("LuiFfiHash", {"pointer","u32"}, "u32")
local Exact = Image:Bind("LuiFfiEchoU64", {"u64"}, "u64")
local Call = Image:Bind("LuiFfiCall", {"pointer","i32"}, "i32")
local Notify = Image:Bind("LuiFfiNotifyOnThread", {"pointer","i32"}, "void")
local Float = Image:Bind("LuiFfiEchoF64", {"f64"}, "f64")
local Zero = Float:Call({-0.0})
assert(Zero == 0)
Costs.NegativeZeroPreserved = if 1 / Zero < 0 then 1 else 0
Float:Close()
local Seen = 0
local Notice = Ffi.Callback({"i32"}, "void", function(Values) Seen += Values[1] end)
local Memory = Ffi.Allocate(4096)
local PairMemory = Ffi.Allocate(16, 8)
PairMemory:Set(0, Pair, {Left=7, Right=2.5})
local Proxy = setmetatable({}, {__iter=function() return next, {Left=7, Right=2.5} end})
local Word = Ffi.Word64(0xfedcba98, 0x76543210)
local Process = Ffi.System(if Ffi.Platform().OS == "Windows" then "math" else "process")
local DivType = Ffi.Struct({{Name="Quotient", Type="i32"}, {Name="Remainder", Type="i32"}})
local Divide = Process:Bind("div", {"i32", "i32"}, DivType)
local Payload = string.rep("AB", 128)
local Block = string.rep(Payload, 16)
local BlockHash = Hash:Call({Memory, #Block})
local Compute = (Ffi :: any).SyncCallback
local Request = {Op="Call", Function=1, Args=Json.Array({42})}
local Limits: Json.Limits = {MaxBytes=131072, MaxDepth=24}
local Encoded = '{"Ok":true,"Result":{"Errno":0,"LastError":0,"Value":42}}'
local function Measure(Name: string, N: number, Operation: () -> ())
    local Start = Clock:GetTime()
    for _ = 1, N do Operation() end
    Costs[Name] = (Clock:GetTime() - Start) * 1000 / N
end
local function AdjustPair()
    local Result = Adjust:Call({{Left=7, Right=2.5}, 3})
    assert(Result.Left == 7 and Result.Right == 5.5)
end
local Stages = {
    function() Measure("CachedImageAndCloseUs", 8, function() Fixture.Open():Close() end) end,
    function() Measure("ScalarUs", 16, function() assert(Echo:Call({42}) == 42) end) end,
    function() Measure("ScalarWithErrorUs", 16, function()
        local Reply = Echo:CallWithError({42})
        assert(Reply.Value == 42 and Reply.Errno == 0 and Reply.LastError == 0)
    end) end,
    function() Measure("WarmScalarUs", 128, function() assert(Echo:Call({42}) == 42) end) end,
    function() Measure("WarmScalarWithErrorUs", 128, function()
        local Reply = Echo:CallWithError({42})
        assert(Reply.Value == 42 and Reply.Errno == 0 and Reply.LastError == 0)
    end) end,
    function() Measure("BindAndCloseUs", 8, function() Image:Bind("LuiFfiEchoU32", {"u32"}, "u32"):Close() end) end,
    function() Measure("ServiceLookupUs", 64, function() assert(app:GetService("Ffi")) end) end,
    function() Measure("JsonEncodeScalarUs", 24, function() assert(#Json.Encode(Request, Limits) > 0) end) end,
    function() Measure("JsonDecodeScalarUs", 24, function() assert(Json.Decode(Encoded).Result.Value == 42) end) end,
    function() Measure("StructUs", 8, AdjustPair) end,
    function() Measure("WarmStructUs", 64, AdjustPair) end,
    function() Measure("WriteRead256Us", 3, function() Memory:Write(0, Payload); assert(Memory:Read(0, 256) == Payload) end) end,
    function() Measure("WarmWriteRead256Us", 256, function() Memory:Write(0, Payload); assert(Memory:Read(0, 256) == Payload) end) end,
    function()
        Memory:Write(0, Block)
        local Expected = Hash:Call({Memory, #Block})
        assert(Expected ~= BlockHash)
        Measure("BufferHash4096Us", 32, function()
            Memory:Write(0, Block)
            assert(Hash:Call({Memory, #Block}) == Expected and Memory:Read(0, #Block) == Block)
        end)
    end,
    function() Measure("PointerCallUs", 64, function() assert(Sum:Call({PairMemory}) == 9.5) end) end,
    function() Measure("ExactWordUs", 64, function() assert(Exact:Call({Word}) == Word) end) end,
    function() Measure("StructuredFallbackUs", 32, function() assert(Adjust:Call({Proxy, 3}).Right == 5.5) end) end,
    function() Measure("CrtDivideUs", 64, function()
        local Result = Divide:Call({-117, 10})
        assert(Result.Quotient == -11 and Result.Remainder == -7)
    end) end,
    -- SQLITE
    function()
        if not Compute then return end
        local Callback = (Compute :: any)({"i32"}, "i32", function(Values: {any}) return Values[1] * 2 end, -1)
        Measure("ComputedCallbackUs", 64, function() assert(Call:Call({Callback, 9}) == 18) end)
        Callback:Close()
    end,
    function() Measure("AllocateCloseUs", 6, function() Ffi.Allocate(64):Close() end) end,
    function() Measure("ForeignThreadAndPollUs", 4, function()
        Notify:Call({Notice,1})
        local Count, Lost, Errors = Ffi.Poll()
        assert(Count == 1 and Lost == 0 and #Errors == 0)
    end) end,
}
local Index = 0
local function Next()
    Index += 1
    local Stage = Stages[Index]
    if Stage then
        local Ok, Problem = pcall(function() Stage() end)
        if Ok then task.defer(Next)
        else
            Stdio.SetExitStatus(2)
            Stdio.Write(string.format("stage %d: %s", Index, tostring(Problem)), function() end)
        end
        return
    end
    assert(Seen == 4)
    Notice:Close(); Notify:Close(); Echo:Close(); Adjust:Close(); Memory:Close(); Pair:Close(); Image:Close()
    Sum:Close(); Hash:Close(); Exact:Close(); Call:Close(); PairMemory:Close()
    Divide:Close(); DivType:Close(); Process:Close()
    local Final = Ffi.Stats()
    assert(Final.Live == 0 and Final.AllocatedBytes == 0)
    Stdio.Write(Json.Encode(Costs), function() end)
end
task.defer(Next)
'''

Sqlite = '''
    function()
        local Image = Ffi.System("sqlite")
        local Sql, CloseBindings = require("./GeneratedSQLite")(Image)
        local Slot = Ffi.Allocate(Ffi.Platform().PointerBytes)
        assert(Sql.sqlite3_open:Call({":memory:", Slot}) == 0)
        local Database = Slot:Get(0, "pointer")
        assert(Sql.sqlite3_prepare_v2:Call({Database, "SELECT ?1", -1, Slot, Ffi.Null}) == 0)
        local Statement = Slot:Get(0, "pointer")
        Measure("SQLitePrepared64Us", 96, function()
            assert(Sql.sqlite3_bind_int64:Call({Statement, 1, Word}) == 0)
            assert(Sql.sqlite3_step:Call({Statement}) == 100)
            assert(Sql.sqlite3_column_int64:Call({Statement, 0}) == Word)
            assert(Sql.sqlite3_reset:Call({Statement}) == 0)
        end)
        assert(Sql.sqlite3_finalize:Call({Statement}) == 0 and Statement:Close())
        assert(Sql.sqlite3_close:Call({Database}) == 0 and Database:Close())
        assert(Slot:Close())
        CloseBindings()
        CloseBindings()
        assert(Image:Close())
    end,
'''


def Prepare(Sdk, Root, Fixture, Clang=None, Generator=None):
    Manifest = Sdk / 'SdkManifest.json'
    Inventory = json.loads(Manifest.read_bytes())
    Cli = Sdk / Inventory['Components']['Cli']
    App, Package = Root / 'App', Root / 'Packaged'
    shutil.copytree(Sdk / 'Examples/ffi-callback', App)
    # Hold the tested native workload constant while each SDK supplies its own
    # facade, provider and host. Only the disposable project receives the image.
    Image = App / Fixture.name
    OldDigest = hashlib.sha256(Image.read_bytes()).hexdigest()
    Payload = Fixture.read_bytes()
    Digest = hashlib.sha256(Payload).hexdigest()
    Binding = App / 'Src/Binding.luau'
    Text = Binding.read_text(encoding='utf-8')
    if Text.count(OldDigest) != 1:
        raise ValueError('Expected one captured fixture digest in the example binding')
    Image.write_bytes(Payload)
    Binding.write_text(Text.replace(OldDigest, Digest), encoding='utf-8')
    Flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
    MainSource = Source
    if Clang:
        NativeHeader = Root / 'SQLite.h'
        NativeHeader.write_text(Header, encoding='utf-8')
        Target = 'x86_64-pc-windows-msvc' if Inventory['Target'] == 'WindowsX64' else 'x86_64-unknown-linux-gnu'
        Command = [sys.executable, Generator, NativeHeader, '--clang', Clang, '--target', Target,
                   '--utf8', 'sqlite3_open=1', '--utf8', 'sqlite3_prepare_v2=2',
                   '--output', App / 'Src/GeneratedSQLite.luau']
        for Symbol in SqlSymbols:
            Command += ['--symbol', Symbol]
        subprocess.run(Command, check=True, timeout=45, creationflags=Flags)
        ProjectManifest = App / 'lui.json'
        Project = json.loads(ProjectManifest.read_bytes())
        Project['Modules']['Files'].append('GeneratedSQLite.luau')
        ProjectManifest.write_text(json.dumps(Project), encoding='utf-8')
        MainSource = Source.replace('-- SQLITE', Sqlite)
    (App / 'Src/Main.luau').write_text(MainSource, encoding='utf-8')
    subprocess.run([Cli, 'check', App / 'lui.json', '--mode', 'project', '--sdk', Sdk], check=True, timeout=45, creationflags=Flags)
    subprocess.run([Cli, 'build', App / 'lui.json', '--mode', 'headless', '--sdk', Sdk, '--output', Package], check=True, timeout=45, creationflags=Flags)
    Launcher = Package / ('Start-LUI.exe' if os.name == 'nt' else 'Start-LUI.sh')
    return Launcher, {'Target': Inventory['Target'], 'Sdk': Sdk.name, 'FixtureSha256': Digest,
                      'ManifestSha256': hashlib.sha256(Manifest.read_bytes()).hexdigest(), 'Samples': []}


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--sdk', type=Path, required=True)
    Parser.add_argument('--baseline', type=Path, help='alternate fresh processes with an intact SDK from the same target')
    Parser.add_argument('--runs', type=int, default=7, choices=range(3, 32))
    Parser.add_argument('--clang', type=Path, help='also exercise generated SQLite bindings')
    Parser.add_argument('--output', type=Path, required=True)
    Args = Parser.parse_args()
    with contextlib.ExitStack() as Cleanup:
        SdkPaths = [Args.sdk.resolve()]
        if Args.baseline:
            SdkPaths.append(Args.baseline.resolve())
        Generator = SdkPaths[0] / 'Tools/Generate-FfiBindings.py'
        Fixture = SdkPaths[0] / 'Examples/ffi-callback' / ('LuiFfiFixture.dll' if os.name == 'nt' else 'libLuiFfiFixture.so')
        Profiles = [Prepare(Sdk, Path(Cleanup.enter_context(tempfile.TemporaryDirectory(prefix='lui-ffi-costs-'))),
                            Fixture, Args.clang.resolve() if Args.clang else None, Generator)
                    for Sdk in SdkPaths]
        if len({Profile['Target'] for _, Profile in Profiles}) != 1:
            Parser.error('benchmarks require the same SDK target')
        for Index in range(Args.runs):
            for Launcher, Profile in (reversed(Profiles) if Index % 2 else Profiles):
                Flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
                Result = subprocess.run([Launcher, '--stdio', '--trusted-ffi'], timeout=20, capture_output=True, creationflags=Flags)
                if Result.returncode:
                    Detail = (Result.stderr + Result.stdout)[:1000].decode(errors='replace')
                    raise RuntimeError(f'{Profile["Sdk"]} launcher exited {Result.returncode}\n{Detail}')
                Profile['Samples'].append(json.loads(Result.stdout))
    for _, Profile in Profiles:
        Samples = Profile['Samples']
        Profile['Median'] = {K: statistics.median(S[K] for S in Samples) for K in Samples[0]}
    Report = Profiles[0][1]
    if Args.baseline:
        Report['Baseline'] = Profiles[1][1]
    Args.output.write_text(json.dumps(Report, indent=2) + '\n', encoding='utf-8')
    print('[LUI:Ffi] Measured selected Luau calls in ' + str(Args.runs) + ' fresh processes')


if __name__ == '__main__':
    Main()
