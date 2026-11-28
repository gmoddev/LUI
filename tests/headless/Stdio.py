"""Run against the assembled preview.3 SDK with the headless streams profile."""
import argparse
import json
import os
import signal
from pathlib import Path
import shutil
import subprocess
import tempfile


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--sdk', type=Path, required=True)
    Args = Parser.parse_args()
    Sdk = Args.sdk.resolve()
    Inventory = json.loads((Sdk / 'SdkManifest.json').read_text())
    Cli = Sdk / Inventory['Components']['Cli']
    Results = []
    with tempfile.TemporaryDirectory(prefix='lui-stdio-') as Temp:
        Project = Path(Temp) / 'Pipe'
        shutil.copytree(Sdk / 'Examples/pipe-copy', Project)
        Manifest = Project / 'lui.json'
        Entry = Project / 'Src/Main.luau'
        Source = lambda Text: Entry.write_text('--!strict\nlocal S = require("@Stdio/Stdio")\n' + Text, encoding='utf-8')
        Run = [Cli, 'run', Manifest, '--sdk', Sdk, '--mode', 'headless', '--stdio', 'true']

        def Check(Name, Data=b'', Output=b'', Errors=b'', Code=0, Extra=()):
            Process = subprocess.run([*Run, *Extra], input=Data, capture_output=True, timeout=16)
            assert (Process.returncode, Process.stdout, Process.stderr) == (Code, Output, Errors), (
                Name, Process.returncode, len(Process.stdout), Process.stderr[:200])
            Results.append(Name)

        for Command in ([Cli, 'check', Manifest], [Cli, 'stage', Manifest, '--output', Path(Temp) / 'stage'],
                        [Cli, 'verify', Path(Temp) / 'stage']):
            subprocess.run([*Command, '--sdk', Sdk], check=True, stdout=subprocess.DEVNULL)
        Bytes = bytes(range(256)) * 4096
        Check('binary-megabyte', Bytes, Bytes)
        Check('empty-input')
        Source('assert(not S.IsTerminal("stdin") and not S.IsTerminal("stdout") and not S.IsTerminal("stderr"))\n'
               'local A = S.Arguments(); assert(#A == 4 and A[1] == "" and A[2] == "--help" and A[3] == \'a"b\' and A[4] == "雪\\\\")\n'
               'S.Write("out\\0\\255", function(Status) assert(Status == "Ok") end)\n'
               'S.WriteError("[LUI:Headless] Failed\\0\\255", function(Status) assert(Status == "Ok") end)\n'
               'print("PRIVATE"); S.SetExitStatus(7)\n')
        Check('channels-arguments-exit', Output=b'out\x00\xff', Errors=b'[LUI:Headless] Failed\x00\xff', Code=7,
              Extra=('--diagnostics', 'development', '--', '', '--help', 'a"b', '雪\\'))
        Source('S.ReadLine(8, function(A, B) assert(A == "Ok" and B == "a\\r\\n")\n'
               ' S.ReadLine(2, function(C, D) assert(C == "Limit" and D == "lo")\n'
               '  S.Read(1, function(E, F) assert(E == "Ok" and F == "n")\n'
               '   S.ReadLine(8, function(G, H) assert(G == "Ok" and H == "g")\n'
               '    S.Read(5, function(I, J) assert(I == "Eof" and J == ""); S.SetExitStatus(9) end)\n'
               '   end) end) end) end)')
        Check('line-bound-eof-read-ahead', Data=b'a\r\nlong', Code=9)
        Source('local Id = S.Read(1, function(Status) assert(Status == "Canceled"); S.SetExitStatus(13) end)\n'
               'assert(not pcall(S.Read, 1, function() end)); assert(S.Cancel(Id))')
        Check('cancel-retires-once', Code=13)
        Source('S.Read(5, function() error("unexpected") end)')
        with subprocess.Popen([*Run, '--timeout-ms', '180'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE) as Blocked:
            Blocked.wait(timeout=9)
            assert Blocked.returncode == 124 and Blocked.stdout.read() == b''
            assert Blocked.stderr.read() == b''
        Results.append('blocked-read-timeout')
        Source('local function More() S.Write(string.rep("x", 65536), function(Status) if Status == "Ok" then More() end end) end\nMore()')
        with subprocess.Popen([*Run, '--timeout-ms', '180'], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE) as Blocked:
            Blocked.wait(timeout=9)
            assert Blocked.returncode == 124 and not Blocked.stderr.read()
        Results.append('blocked-write-timeout')
        Source('local function More() S.WriteError(string.rep("z", 65536), function(Status) if Status == "Ok" then More() end end) end\nMore()')
        with subprocess.Popen([*Run, '--timeout-ms', '180'], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE) as Blocked:
            Blocked.wait(timeout=9)
            assert Blocked.returncode == 124 and Blocked.stdout.read() == b''
        Results.append('blocked-stderr-timeout')
        Source('S.Write("ready", function(Status) assert(Status == "Ok"); S.Read(1, function() end) end)')
        with subprocess.Popen([*Run, '--timeout-ms', '8000'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE) as Waiting:
            import concurrent.futures
            with concurrent.futures.ThreadPoolExecutor(max_workers=1) as Reader:
                assert Reader.submit(Waiting.stdout.read, 5).result(timeout=6) == b'ready'
                if os.name == 'nt': Waiting.kill()
                else: Waiting.send_signal(signal.SIGTERM)
                Code = Waiting.wait(timeout=4)
                assert (Code != 0 if os.name == 'nt' else Code == 130)
                assert Reader.submit(Waiting.stdout.read).result(timeout=3) == b''
        Results.append('supervisor-termination-releases-pipes')
        Spec = json.loads(Manifest.read_text())
        Spec['Capabilities'] = []
        Spec['ExtensionDeclarations'] = []
        Manifest.write_text(json.dumps(Spec))
        Refused = subprocess.run(Run, input=b'SECRET', capture_output=True, timeout=12)
        assert Refused.returncode and not Refused.stdout and b'SECRET' not in Refused.stderr
        Results.append('grant-without-declaration')
        Records = Sdk / 'Examples/json-lines/lui.json'
        Stage = Path(Temp) / 'JsonLinesStage'
        for Command in (['check',Records],['stage',Records,'--output',Stage],['verify',Stage]):
            subprocess.run([Cli,*Command,'--sdk',Sdk],check=True,stdout=subprocess.DEVNULL,timeout=20)
        Cmd = [Cli,'run',Stage/'lui.json','--sdk',Sdk,'--mode','headless','--stdio','true']
        for Data,Output,Code in ((b'{ "b":2,"a":[] }\r\nnull',b'{"a":[],"b":2}\nnull\n',0),
                                 (b'1\nPRIVATE\n',b'1\n',2),(b'"'+b'x'*65536+b'"',b'',2),
                                 (b' '*65534+b'1\r\n',b'',2)):
            Child = subprocess.run(Cmd,input=Data,capture_output=True,timeout=16)
            assert Child.returncode == Code and Child.stdout == Output and b'PRIVATE' not in Child.stderr
        with subprocess.Popen(Cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE) as Child:
            Child.stderr.close()
            Child.stdin.write(b'invalid\n')
            Child.stdin.close()
            assert Child.wait(timeout=12)==1 and Child.stdout.read()==b''
        Results.append('packaged JSON Lines conversion, unterminated final record, invalid input and size cap')
    print(json.dumps({'Status': 'PASS', 'Checks': Results}))


if __name__ == '__main__':
    Main()
