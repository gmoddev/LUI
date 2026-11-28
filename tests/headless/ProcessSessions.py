"""Exercise interactive Process sessions and concurrent pipelines through a staged SDK project."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


Fixture = r'''import os
import sys
import time

Input = sys.stdin.buffer
Output = sys.stdout.buffer
Error = sys.stderr.buffer
Mode = sys.argv[1]
if Mode == 'dialog':
    Error.write(b'ready\n')
    assert Input.read(6) == b'alpha\0'
    Output.write(b'first\0')
    assert Input.read() == b'B\xff!'
    Output.write(b'second\xff')
    Error.write(b'closed\n')
    sys.exit(7)
if Mode == 'blocked':
    Output.write(b'ready\n')
    assert Input.read(1) == b'x'
    Output.write(b'read-one\n')
    time.sleep(.3)
    Output.write(b'stop\n')
    time.sleep(12)
    sys.exit(81)
if Mode == 'grace':
    Output.write(b'ready\n')
    assert Input.read() == b''
    Output.write(b'graceful-eof\n')
    sys.exit(17)
if Mode == 'closed-input':
    os.close(0)
    for _ in range(12):
        Output.write(b'closed\n')
        time.sleep(.08)
    sys.exit(84)
if Mode == 'limit-up':
    Error.write(b'e' * 4000)
    Output.write(b'x')
    sys.exit(0)
if Mode == 'limit-down':
    assert Input.read() == b'x'
    Output.write(b'o' * 4000)
    sys.exit(0)
if Mode == 'upstream':
    Output.write(b'pipeline header\n')
    Error.write(b'upstream stderr\n')
    Deadline = time.monotonic() + 5
    while not os.path.isfile('consumed-header'):
        if time.monotonic() >= Deadline:
            sys.exit(82)
        time.sleep(.01)
    Output.write(bytes(range(256)) * 384)
elif Mode == 'downstream':
    assert Input.readline() == b'pipeline header\n'
    with open('consumed-header', 'xb'):
        pass
    Error.write(b'downstream stderr\n')
    time.sleep(.15)
    Data = Input.read()
    assert Data == bytes(range(256)) * 384
    Output.write(b'final\0' + Data[-256:] + b'\xff')
    sys.exit(23)
else:
    sys.exit(83)
'''


Application = r'''--!strict
local Process = require("@Process/Process")
local Stdio = require("@Stdio/Stdio")
local Mode = Stdio.Arguments()[1]
Stdio.SetExitStatus(3)
local function Arguments(ChildMode: string): {string}
    return {"-I", "-S", "-u", "session_fixture.py", ChildMode}
end
local function Passed()
    Stdio.Write("passed " .. Mode .. "\n", function(Status)
        assert(Status == "Ok")
        Stdio.SetExitStatus(0)
    end)
end
local Output, Error = "", ""
local function Record(Stream: string, Bytes: string)
    if Stream == "stdout" then Output ..= Bytes else
        assert(Stream == "stderr")
        Error ..= Bytes
    end
end
local function FillInput(Handle: Process.Session): number
    local Block = string.rep("x", 1024)
    local Accepted = 0
    while Accepted < 2048 and Handle:Write(Block) do Accepted += 1 end
    assert(Accepted < 2048)
    return Accepted
end
if Mode == "pipeline-limit" then
    Process.Pipeline({{Arguments=Arguments("limit-up")}, {Arguments=Arguments("limit-down")}},
        {TimeoutMilliseconds=5000, MaxOutputBytes=6000, OnOutput=Record}, function(Result)
        assert(Result.Status == "OutputLimit" and Result.ExitCode == nil)
        assert(Result.Stdout == Output and Result.Stderr == Error)
        assert(#Output + #Error > 0 and #Output + #Error <= 6000)
        Passed()
    end)
elseif Mode == "pipeline" then
    Process.Pipeline({{Arguments=Arguments("upstream")}, {Arguments=Arguments("downstream")}},
        {TimeoutMilliseconds=10000, MaxOutputBytes=131072, OnOutput=Record}, function(Result)
        assert(Result.Status == "Exited" and Result.ExitCode == 23)
        local Block = ""
        for Byte = 0, 255 do Block ..= string.char(Byte) end
        assert(Output == "final" .. string.char(0) .. Block .. string.char(255))
        assert(Error == "upstream stderr\ndownstream stderr\n" or Error == "downstream stderr\nupstream stderr\n")
        assert(Result.Stdout == Output and Result.Stderr == Error)
        Passed()
    end)
else
    local Current: Process.Session? = nil
    local Phase = 0
    local Old: Process.Session? = nil
    local function Run(ChildMode: string): ()
        Output, Error, Phase = "", "", 0
        Current = Process.Open({Arguments=Arguments(ChildMode), TimeoutMilliseconds=8000,
            OnOutput=function(Stream: string, Bytes: string)
                Record(Stream, Bytes)
                local Handle = assert(Current)
                if ChildMode == "dialog" and Phase == 0 and Error == "ready\n" then
                    Phase = 1
                    local Previous = Old
                    if Previous then assert(not Previous:Write("stale") and not Previous:Kill()) end
                    assert(Handle:Write("alpha") and Handle:Write(string.char(0)))
                elseif ChildMode == "dialog" and Phase == 1 and Output == "first" .. string.char(0) then
                    Phase = 2
                    assert(Handle:Write("B" .. string.char(255) .. "!"))
                    assert(Handle:CloseInput() and not Handle:Write("too late"))
                elseif ChildMode == "blocked" and Stream == "stdout" then
                    if Phase == 0 and Output == "ready\n" then
                        assert(FillInput(Handle) > 0)
                        Phase = 1
                    elseif Phase == 1 and string.find(Output, "read-one\n", 1, true) then
                        FillInput(Handle)
                        Phase = 2
                    end
                    if Phase == 2 and string.find(Output, "stop\n", 1, true) then
                        FillInput(Handle)
                        Phase = 3
                        assert(Handle:Kill())
                    end
                elseif ChildMode == "grace" and Phase == 0 and Output == "ready\n" then
                    Phase = 1
                    assert(Handle:Terminate())
                elseif ChildMode == "closed-input" and Phase == 0 and Stream == "stdout" then
                    if not Handle:Write("probe") and Handle:Kill() then Phase = 1 end
                end
            end}, function(Result)
                assert(Result.Stdout == Output and Result.Stderr == Error)
                if ChildMode == "blocked" then
                    assert(Phase == 3 and (Result.Status == "Canceled" or Result.Status == "Killed"))
                    Old = assert(Current)
                    Run("dialog")
                    return
                elseif ChildMode == "dialog" then
                    assert(Phase == 2 and Result.Status == "Exited" and Result.ExitCode == 7)
                    assert(Output == "first" .. string.char(0) .. "second" .. string.char(255))
                    assert(Error == "ready\nclosed\n")
                elseif ChildMode == "grace" then
                    assert(Phase == 1 and Output == "ready\ngraceful-eof\n")
                    assert((Result.Status == "Exited" and Result.ExitCode == 17) or Result.Status == "Terminated")
                else
                    assert(Phase == 1 and (Result.Status == "Canceled" or Result.Status == "Killed"))
                end
                Passed()
            end)
    end
    Run(Mode)
end
'''


def CheckApplications(Cli, Sdk, Root):
    Git = Path(shutil.which('git') or 'git').resolve(strict=True)
    Repository = Root / 'sample-repository'
    Repository.mkdir()

    def GitRun(*Arguments, Input=None):
        return subprocess.run([Git, *Arguments], cwd=Repository, input=Input, capture_output=True,
                              timeout=15, check=True).stdout

    GitRun('init', '--quiet')
    (Repository / 'two words.txt').write_bytes(b'fixture\n')
    GitRun('add', 'two words.txt')
    Queries = GitRun('write-tree') + GitRun('rev-parse', ':two words.txt') + b'missing-object\n'
    Expected = GitRun('cat-file', '--batch-check', Input=Queries)
    Digest = GitRun('hash-object', '--stdin', Input=GitRun('ls-files', '--cached', '-z'))
    for Name, Input, Output in [('tool-session', Queries, Expected), ('tool-pipeline', b'', Digest)]:
        Destination = Root / Name
        Manifest = Sdk / 'Examples' / Name / 'lui.json'
        subprocess.run([Cli, 'build', Manifest, '--mode', 'headless', '--sdk', Sdk, '--output', Destination],
                       check=True, timeout=40, stdout=subprocess.DEVNULL)
        Start = Destination / ('Start-LUI.exe' if os.name == 'nt' else 'Start-LUI.sh')
        Result = subprocess.run([Start, '--stdio', '--tool', Git, '--tool-cwd', Repository], cwd=Repository,
                                input=Input, capture_output=True, timeout=25)
        assert (Result.returncode, Result.stdout, Result.stderr) == (0, Output, b''), (
            Name, Result.returncode, Result.stdout[:200], Result.stderr[:200])
        print(f'[LUI:Process] Standalone {Name} matched Git on {sys.platform}', flush=True)


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--sdk', required=True, type=Path)
    Sdk = Parser.parse_args().sdk.resolve()
    Components = json.loads((Sdk / 'SdkManifest.json').read_bytes())['Components']
    Cli = Sdk / Components['Cli']
    Tool = Path(sys.executable).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='lui-session-') as Temporary:
        Root = Path(Temporary)
        App = Root / 'App'
        Stage = Root / 'Stage'
        Working = Root / 'work'
        Working.mkdir()
        (Working / 'session_fixture.py').write_text(Fixture, encoding='utf-8')
        shutil.copytree(Sdk / 'Examples' / 'tool-check', App)
        (App / 'Src' / 'Main.luau').write_text(Application, encoding='utf-8')
        Manifest = App / 'lui.json'
        for Command in (['check', Manifest], ['stage', Manifest, '--output', Stage], ['verify', Stage]):
            subprocess.run([Cli, *Command, '--sdk', Sdk], check=True, timeout=30)
        Command = [Cli, 'run', Stage / 'lui.json', '--sdk', Sdk, '--mode', 'headless', '--stdio', 'true',
                   '--tool', Tool, '--tool-cwd', Working]
        for Mode in ('dialog', 'blocked', 'grace', 'closed-input', 'pipeline', 'pipeline-limit'):
            Result = subprocess.run([*Command, '--', Mode], capture_output=True, timeout=20)
            assert (Result.returncode, Result.stdout, Result.stderr) == (0, f'passed {Mode}\n'.encode(), b''), (
                Mode, Result.returncode, Result.stdout[:200], Result.stderr[:200])
            print(f'[LUI:Process] {Mode} passed on {sys.platform}', flush=True)
        CheckApplications(Cli, Sdk, Root)


if __name__ == '__main__':
    Main()
