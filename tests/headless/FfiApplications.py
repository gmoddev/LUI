"""Qualify the SDK's public FFI applications and record native launch and distribution costs."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile

from NativeCosts import Run as MeasureNative


Facade = '''--!strict
local Ffi = require("@Ffi/Ffi")
local Stdio = require("@Stdio/Stdio")
local Image = require("./Binding").Open()
local Text = Image:Bind("LuiFfiText", {}, "utf8")
local Pair = Ffi.Struct({{Name="Left", Type="i32"}, {Name="Right", Type="f64"}})
local Layout = Ffi.Describe(Pair)
assert(Layout.Size == 16 and Layout.Alignment == 8 and Layout.Offsets[1] == 0 and Layout.Offsets[2] == 8)
local Make = Image:Bind("LuiFfiMakePair", {"i32", "f64"}, Pair)
local Value = Make:Call({-3, 4.5})
assert(Value.Left == -3 and Value.Right == 4.5 and Text:Call({}) == "café")
assert(not pcall(function() Make:Call({2.5, 4.5}) end))
local Adjust = Image:Bind("LuiFfiAdjustPair", {Pair, "i32"}, Pair)
local Moved = Adjust:CallWithError({Value, 5})
assert(Moved.Value.Left == -3 and Moved.Value.Right == 9.5 and Moved.Errno == 0)
assert(not pcall(function() Adjust:Call({{Left=7, Extra=2}, 1}) end))
assert(not pcall(function() Adjust:Call({{Left=7}, 1}) end))
local Proxy = setmetatable({}, {__iter=function() return next, {Left=-3, Right=4.5} end})
assert(Adjust:Call({Proxy, 1}).Right == 5.5)
assert(Adjust:Close())
local Entries = Ffi.Array("pointer", 3)
local Pointers = Ffi.Struct({{Name="Entries", Type=Entries}})
local CountNulls = Image:Bind("LuiFfiCountNulls", {Pointers}, "u32")
local Items: {any} = {Ffi.Null, Ffi.Null, Ffi.Null}
assert(CountNulls:Call({{Entries=Items}}) == 3)
Items[2] = nil
assert(not pcall(function() CountNulls:Call({{Entries=Items}}) end))
assert(CountNulls:Close() and Pointers:Close() and Entries:Close())
local Bytes = Ffi.Allocate(16, 8)
Bytes:Set(0, Pair, Value)
assert(Bytes:Get(0, Pair).Left == -3 and Bytes:Get(0, Pair).Right == 4.5)
local View = Bytes:Slice(0, 8)
View:Write(1, "a" .. string.char(0) .. "b")
assert(Bytes:Read(1, 3) == "a" .. string.char(0) .. "b")
assert(not pcall(function() View:Read(7, 2) end))
assert(View:Close() and Bytes:Close() and not Bytes:Close())
assert(not pcall(function() Bytes:Read(0, 1) end))
local Packed = Ffi.Struct({{Name="Tag", Type="u8"}, {Name="Count", Type="u32"}}, 1)
local PackedInfo = Ffi.Describe(Packed)
assert(PackedInfo.Size == 5 and PackedInfo.Alignment == 1 and PackedInfo.Offsets[2] == 1)
local PackedBytes = Ffi.Allocate(5)
PackedBytes:Set(0, Packed, {Tag=3, Count=12345})
assert(PackedBytes:Get(0, Packed).Count == 12345)
assert(Packed:Close() and PackedBytes:Close())
local Echo = Image:Bind("LuiFfiEchoI32", {"i32"}, "i32")
local Reply = Echo:CallWithError({-2147483648})
assert(Echo:Call({2147483647}) == 2147483647 and Reply.Value == -2147483648 and Reply.Errno == 0 and Reply.LastError == 0)
assert(not pcall(function() Echo:Call({2147483648}) end))
local EchoWord = Image:Bind("LuiFfiEchoU64", {"u64"}, "u64")
assert(EchoWord:Call({"ABCDEF0189ABCDEF"}) == "abcdef0189abcdef")
assert(not pcall(function() EchoWord:Call({"+000000000000001"}) end))
assert(not pcall(function() EchoWord:Call({"1"}) end))
assert(EchoWord:Close())
local Call = Image:Bind("LuiFfiCall", {"pointer", "i32"}, "i32")
local Foreign = Image:Bind("LuiFfiCallOnThread", {"pointer", "i32"}, "i32")
local Remember = Image:Bind("LuiFfiRememberComputed", {"pointer"}, "void")
local Recall = Image:Bind("LuiFfiCallRemembered", {"i32"}, "i32")
local Invocations = 0
local Computed = Ffi.SyncCallback({"i32"}, "i32", function(Values)
    Invocations += 1
    if Values[1] == 1 then error("private callback failure") end
    if Values[1] == 2 then return "invalid result" end
    if Values[1] == 3 then return Echo:Call({3}) end
    if Values[1] == 4 then return coroutine.yield() end
    return Values[1] * 2
end, -17)
assert(Call:Call({Computed, 10}) == 20)
assert(Foreign:Call({Computed, 10}) == -17 and Invocations == 1)
Remember:Call({Computed})
assert(Recall:Call({12}) == 24)
for Index = 1, 3 do assert(Call:Call({Computed, Index}) == -17) end
task.defer(function()
    assert(Call:Call({Computed, 4}) == -17)
    assert(Call:Call({Computed, 15}) == 30 and Invocations == 7)
    assert(Computed:Close() and Recall:Call({10}) == -17 and Invocations == 7)
    Remember:Call({Ffi.Null})
    assert(Echo:Close() and Call:Close() and Foreign:Close() and Remember:Close() and Recall:Close())
    assert(Make:Close() and Pair:Close() and Text:Close() and Image:Close())
    assert(Ffi.Stats().Live == 0 and Ffi.Stats().AllocatedBytes == 0)
    Stdio.Write("facade passed", function() end)
end)
'''

BrokerFailure = '''--!strict
local Broker = require("@FfiBroker/FfiBroker")
local Stdio = require("@Stdio/Stdio")
local Words = Stdio.Arguments()
local CancelTest = Words[1] == "timeout"
local ExitTest = Words[1] == "exit"
local Selection: Broker.Selection = if CancelTest then {Kind="Selected", Name=Words[2], Sha256=Words[3]}
    elseif ExitTest then {Kind="System", Name="process"}
    else {Kind="System", Name="MissingQualificationLibrary"}
Stdio.SetExitStatus(2)
Broker.Call({Library=Selection, Symbol=if ExitTest then Words[4] else "LuiFfiWait",
    Params={if ExitTest and Words[4] == "_exit" then "i32" else "u32"},
    Result=if ExitTest then "void" else "u32", Args={if ExitTest then 23 else 250}},
    if CancelTest then 75 else 5000, function(Reply, Failure)
        assert(not Reply and Failure)
        if CancelTest then assert(Failure == "TimedOut") end
        if ExitTest then assert(Failure == "InvalidBrokerReply") end
        Broker.CallBatch({Library={Kind="System", Name="math"}, Symbol="sqrt", Params={"f64"}, Result="f64", Rows={{81}, {16}}},
            5000, function(Next, Why)
                assert(Next and Next[1].Value == 9 and Next[2].Value == 4 and not Why)
                Stdio.Write("recovered", function(Status) if Status == "Ok" then Stdio.SetExitStatus(0) end end)
            end)
    end)
'''


def Read(PathName):
    return json.loads(PathName.read_bytes())


def Digest(PathName):
    with PathName.open('rb') as Stream:
        return hashlib.file_digest(Stream, 'sha256').hexdigest()


def Distribution(Folder, ZipPath):
    Files = sorted(File for File in Folder.rglob('*') if File.is_file())
    with ZipFile(ZipPath, 'w', ZIP_DEFLATED, compresslevel=6, strict_timestamps=False) as Archive:
        for File in Files:
            Archive.write(File, File.relative_to(Folder).as_posix())
    return {'InstalledBytes':sum(File.stat().st_size for File in Files), 'ZipBytes':ZipPath.stat().st_size,
            'Files':len(Files)}


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--sdk', type=Path, required=True)
    Parser.add_argument('--baseline', type=Path, help='Intact previous SDK for the identical ordinary workload')
    Parser.add_argument('--archive', type=Path, help='Original distribution archive for compressed SDK size')
    Parser.add_argument('--output', type=Path)
    Parser.add_argument('--desktop-output', type=Path, help='Fresh destination for desktop interaction qualification')
    Parser.add_argument('--runs', type=int, default=7)
    Args = Parser.parse_args()
    if not 3 <= Args.runs <= 31: Parser.error('--runs must be from 3 through 31')
    Sdk = Args.sdk.resolve(strict=True)
    Metadata = Read(Sdk/'SdkManifest.json')
    Components = Metadata['Components']
    Cli, Host = Sdk/Components['Cli'], Sdk/Components['HeadlessHost']
    Windows = os.name == 'nt'
    Broker = Sdk/'Tools'/('LuiFfiBroker.exe' if Windows else 'LuiFfiBroker')
    Provider = 'LuiFfi.dll' if Windows else 'libLuiFfi.so'
    FixtureName = 'LuiFfiFixture.dll' if Windows else 'libLuiFfiFixture.so'
    Environment = {Key:Value for Key, Value in os.environ.items() if Key not in ('LUI_SDK','LD_LIBRARY_PATH')}
    Flags = subprocess.CREATE_NO_WINDOW if Windows else 0
    Result = {'Target':Metadata['Target'], 'SdkManifestSha256':Digest(Sdk/'SdkManifest.json'),
              'Applications':{}, 'Launch':{}, 'Samples':Args.runs, 'ArchiveMethod':'ZIP deflate level 6'}
    Result['SdkInstalledBytes'] = sum(File.stat().st_size for File in Sdk.rglob('*') if File.is_file())
    if Args.archive:
        Result['SdkArchiveBytes'] = Args.archive.stat().st_size
        Result['SdkArchiveSha256'] = Digest(Args.archive)
    Result['OptionalBinaries'] = {}
    for File in (Sdk/'Libraries/Ffi/Native'/Provider, Broker):
        Data = File.read_bytes()
        Result['OptionalBinaries'][File.name] = {'InstalledBytes':len(Data),
            'GzipBytes':len(gzip.compress(Data, 9, mtime=0)), 'Sha256':Digest(File)}
    with tempfile.TemporaryDirectory(prefix='lui ffi apps ') as Temp:
        Root = Path(Temp)
        def Run(Command):
            Done = subprocess.run(Command, stdin=subprocess.DEVNULL, capture_output=True, env=Environment,
                                  cwd=Root, creationflags=Flags, timeout=50)
            assert Done.returncode == 0, (Command[:3], Done.returncode, Done.stdout[:450], Done.stderr[:450])
            return Done.stdout

        def Project(Name, CopyName=None):
            App = Root/(CopyName or Name)
            shutil.copytree(Sdk/'Examples'/Name, App)
            return App

        def Build(App, Output, Mode='headless'):
            Manifest = App/'lui.json'
            Run([Cli,'check',Manifest,'--sdk',Sdk])
            Run([Cli,'build',Manifest,'--sdk',Sdk,'--mode',Mode,'--output',Output])
            return Output/('Start-LUI.exe' if Windows else 'Start-LUI.sh')

        for Name in ('ffi-platform','ffi-sqlite','ffi-callback','ffi-isolated','ffi-workbench'):
            App = Project(Name)
            Desktop = Name == 'ffi-workbench'
            Output = Args.desktop_output.resolve() if Desktop and Args.desktop_output else Root/(Name+' output')
            Launcher = Build(App, Output, 'project' if Desktop else 'headless')
            Included = Output/'Application/Native'/Provider
            if Name != 'ffi-isolated':
                assert Included.is_file() and Digest(Included) == Digest(Sdk/'Libraries/Ffi/Native'/Provider)
                assert (Output/'Application/Libffi-LICENSE.txt').is_file()
            else:
                assert not Included.exists()
            if Desktop:
                assert (Output/'Application'/FixtureName).is_file()
                Result['Applications'][Name] = {'Built':True,'InstalledBytes':sum(
                    File.stat().st_size for File in Output.rglob('*') if File.is_file())}
                continue
            Options = ['--tool',Broker,'--tool-cwd',Root] if Name == 'ffi-isolated' else ['--trusted-ffi']
            Bytes = Run([Launcher,'--stdio',*Options])
            if Name == 'ffi-platform':
                Pattern = rb'Windows pid=[1-9][0-9]* long=4\n' if Windows else rb'Linux pid=[1-9][0-9]* long=8\n'
                assert re.fullmatch(Pattern, Bytes), Bytes
            else:
                Expected = {'ffi-sqlite':b'inserted=2\n','ffi-callback':b'notifications=3 sum=42 handler-errors=1 live=0\n',
                            'ffi-isolated':b'isolated=42\n'}
                assert Bytes == Expected[Name], (Name,Bytes)
            Result['Applications'][Name] = Distribution(Output, Root/(Name+'.zip'))

        App = Project('ffi-callback','facade')
        (App/'Src/Main.luau').write_text(Facade, encoding='utf-8')
        assert Run([Build(App,Root/'facade output'),'--stdio','--trusted-ffi']) == b'facade passed'
        Isolated = Project('ffi-isolated','broker failures')
        (Isolated/'Src/Main.luau').write_text(BrokerFailure,encoding='utf-8')
        Launcher = Build(Isolated, Root/'broker output')
        Fixture = Root/'ffi-callback output'/'Application'/FixtureName
        for Mode in ('timeout','invalid','exit'):
            Symbol = 'ExitProcess' if Windows else '_exit'
            assert Run([Launcher,'--stdio','--tool',Broker,'--tool-cwd',Root,'--',Mode,Fixture,Digest(Fixture),Symbol]) == b'recovered'

        Ordinary = Root/'Control'
        Run([Cli,'new',Ordinary,'--sdk',Sdk])
        Noop = 'assert(app:GetService("PerformanceService"):GetTime() >= 0)'
        (Ordinary/'Src/Main.luau').write_text(Noop,encoding='utf-8')
        ControlOutput = Root/'control output'
        Build(Ordinary,ControlOutput)
        assert not (ControlOutput/'Application/Native').exists()
        assert not any('luiffi' in Item.name.lower() for Item in ControlOutput.rglob('*'))
        Result['Applications']['ordinary'] = Distribution(ControlOutput, Root/'ordinary.zip')
        Control = [Host,'--project',Ordinary/'lui.json']
        IdleSelected = Project('ffi-platform','idle with Ffi')
        (IdleSelected/'Src/Main.luau').write_text(Noop,encoding='utf-8')
        IdleHost = Root/'idle without Ffi'
        shutil.copytree(IdleSelected,IdleHost)
        Manifest = Read(IdleHost/'lui.json')
        Manifest['Capabilities'].remove('NativeExtensions')
        Manifest['Extensions'].remove('Ffi')
        Manifest['ExtensionDeclarations'].remove('Ffi.json')
        (IdleHost/'lui.json').write_text(json.dumps(Manifest),encoding='utf-8')
        Selected = [Host,'--project',Root/'ffi-platform/lui.json','--stdio','--trusted-ffi']
        IsolatedMath = [Host,'--project',Root/'ffi-isolated/lui.json','--stdio','--tool',Broker,'--tool-cwd',Root]
        Commands = {'Ordinary':Control,'SameGraphStdioIdle':[Host,'--project',IdleHost/'lui.json','--stdio'],
                    'SameGraphFfiIdle':[Host,'--project',IdleSelected/'lui.json','--stdio','--trusted-ffi'],
                    'FfiPlatform':Selected,'IsolatedMath':IsolatedMath}
        if Args.baseline:
            Baseline = Args.baseline.resolve(strict=True)
            Prior = Baseline/Read(Baseline/'SdkManifest.json')['Components']['HeadlessHost']
            Commands['PriorOrdinary'] = [Prior,*Control[1:]]
            Result['BaselineManifestSha256'] = Digest(Baseline/'SdkManifest.json')
        Samples = {Name:[] for Name in Commands}
        for Command in Commands.values(): MeasureNative(Command)
        Order = list(Commands)
        for Index in range(Args.runs):
            for Name in (reversed(Order) if Index % 2 else Order):
                Samples[Name].append(MeasureNative(Commands[Name]))
        for Name, Values in Samples.items():
            Result['Launch'][Name] = {'MedianMilliseconds':round(statistics.median(X[0] for X in Values),3),
                                      'MedianPeakBytes':int(statistics.median(X[1] for X in Values)),
                                      'Milliseconds':[round(Elapsed,3) for Elapsed, _Peak in Values],
                                      'PeakBytes':[Peak for _Elapsed, Peak in Values]}
        Paired = list(zip(Samples['SameGraphStdioIdle'], Samples['SameGraphFfiIdle']))
        Result['PairedFfiAdmissionDelta'] = {
            'MedianMilliseconds':round(statistics.median(With[0]-Without[0] for Without, With in Paired),3),
            'MedianPeakBytes':int(statistics.median(With[1]-Without[1] for Without, With in Paired))}
        Run([sys.executable,Path(__file__).with_name('FfiAuthority.py'),'--sdk',Sdk])
        Result['Authority'] = 'PASS'
    Text = json.dumps(Result,indent=2)
    if Args.output: Args.output.write_text(Text+'\n',encoding='utf-8')
    print(Text)


if __name__ == '__main__':
    Main()
