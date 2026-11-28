"""Exercise source inspection using the checker and application from an installed SDK."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


Source = (
    '-- require("comment")\r\n'
    'local Text = "😀"; local Module = require("@example/Module")\r\n'
    'do local require = function(Name) return Name end; require("shadowed") end\r\n'
    'local Also = require("./Second")\r\n'
    'error("source inspection executed the application")\r\n'
    'while true do end\r\n'
)
Broken = 'local Text = "😀"; local Broken = )'

Application = '''--!strict
local Language = require("@Language/Language")
local Stdio = require("@Stdio/Stdio")
local Mode = Stdio.Arguments()[1]
Stdio.SetExitStatus(9)
local Calls = 0
local Returned = false
local function Finish()
    Stdio.Write("completed", function(Status)
        assert(Status == "Ok")
        Stdio.SetExitStatus(0)
    end)
end
if Mode == "cancel" then
    local Accepted = false
    local Request = Language.Analyze("return 42", function(Report, Failure)
        Calls += 1
        assert(Returned and Calls == 1)
        if Accepted then assert(Report == nil and Failure ~= nil)
        else assert(Report and Report.Valid and Failure == nil) end
        Finish()
    end)
    Accepted = Request:Cancel()
    assert(not Request:Cancel())
elseif Mode == "failure" then
    Language.Analyze("return true", function(Report, Failure)
        Calls += 1
        assert(Returned and Calls == 1 and Report == nil and Failure ~= nil)
        Finish()
    end)
elseif Mode == "oversize" then
    local Started = pcall(function()
        Language.Analyze(string.rep(" ", 4097), function(Report, Failure)
            Calls += 1
            assert(Calls == 1 and Report == nil and Failure ~= nil)
            Finish()
        end)
    end)
    if not Started then assert(Calls == 0); Finish() end
else
    local First = Language.Analyze("error('must not execute')", function(Report, Failure)
        Calls += 1
        assert(Returned and Calls == 1 and Failure == nil and Report)
        assert(Report.Valid and Report.BytecodeBytes > 0 and #Report.Diagnostics == 0)
        Language.Analyze("local Bad = )", function(Next, NextFailure)
            Calls += 1
            assert(Calls == 2 and NextFailure == nil and Next and not Next.Valid)
            assert(Next.BytecodeBytes == 0 and #Next.Diagnostics > 0)
            Finish()
        end)
    end)
    assert(type(First.Cancel) == "function" and Calls == 0)
end
Returned = true
'''


def Run(Arguments, **Options):
    Flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
    return subprocess.run(Arguments, capture_output=True, timeout=30, creationflags=Flags, **Options)


def Report(Checker, Data):
    Done = Run([Checker, '--inspect-source'], input=Data)
    assert Done.returncode in (0, 1), (Done.returncode, Done.stderr[:256])
    assert not Done.stderr, Done.stderr[:256]
    Result = json.loads(Done.stdout)
    assert isinstance(Result['Valid'], bool) and isinstance(Result['Truncated'], bool)
    assert isinstance(Result['Diagnostics'], list) and isinstance(Result['Imports'], list)
    return Result


def RangeText(Lines, Record):
    assert 1 <= Record['StartLine'] == Record['EndLine'] <= len(Lines), Record
    Row = Lines[Record['StartLine'] - 1].encode('utf-16-le')
    Start = 2 * (Record['StartColumn'] - 1)
    End = 2 * (Record['EndColumn'] - 1)
    assert 0 <= Start <= End <= len(Row), Record
    return Row[Start:End].decode('utf-16-le')


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--sdk', type=Path, required=True)
    Sdk = Parser.parse_args().sdk.resolve(strict=True)
    Components = json.loads((Sdk / 'SdkManifest.json').read_bytes())['Components']
    Checker, Cli = Sdk / Components['Checker'], Sdk / Components['Cli']

    Valid = Report(Checker, Source.encode())
    assert Valid['Valid'] and not Valid['Truncated'] and not Valid['Diagnostics']
    assert Valid['BytecodeBytes'] > 0 and 'Bytecode' not in Valid
    Imports = Valid['Imports']
    assert [Item['Path'] for Item in Imports] == ['@example/Module', './Second'], Imports
    for Item in Imports:
        Literal = '"' + Item['Path'] + '"'
        assert RangeText(Source.splitlines(), Item) in (Literal, 'require(' + Literal + ')'), Item
    Invalid = Report(Checker, Broken.encode())
    assert not Invalid['Valid'] and Invalid['BytecodeBytes'] == 0 and Invalid['Diagnostics'], Invalid
    Column = len(Broken[:Broken.index(')')].encode('utf-16-le')) // 2 + 1
    assert any(Issue['Message'] and Issue['StartLine'] == 1 and Issue['StartColumn'] == Column
               for Issue in Invalid['Diagnostics']), Invalid
    assert Report(Checker, b' ' * 4096)['Valid']
    print('[LUI:Language] Non-executing compilation, literal imports and UTF-16 positions passed', flush=True)

    with tempfile.TemporaryDirectory(prefix='lui language ') as Temporary:
        Root = Path(Temporary)
        Stock = Sdk / 'Examples/source-inspect'
        Project = Root / 'project'
        shutil.copytree(Stock, Project)
        PortableTools = Root / 'selected tools'
        shutil.copytree(Checker.parent, PortableTools)
        SelectedChecker = PortableTools / Checker.name
        Work = Root / 'working directory'
        Work.mkdir()
        def SdkCommand(*Args):
            Done = Run([Cli, *Args, '--sdk', Sdk], cwd=Work)
            assert Done.returncode == 0, (Args[0], Done.stderr[:384], Done.stdout[:384])
        def Standalone(Directory, Tool, *Arguments, Data=b''):
            Launcher = Directory / ('Start-LUI.exe' if os.name == 'nt' else 'Start-LUI.sh')
            Options = ['--tool', Tool, '--tool-cwd', Work] if Tool is not None else []
            Env = {Key: Value for Key, Value in os.environ.items() if Key not in ('LUI_SDK', 'LD_LIBRARY_PATH')}
            return Run([Launcher, '--stdio', *Options, '--', *Arguments], input=Data, cwd=Work, env=Env)
        Manifest = Project / 'lui.json'
        SdkCommand('check', Manifest)
        SdkCommand('stage', Manifest, '--output', Root / 'staged')
        SdkCommand('verify', Root / 'staged')
        SdkCommand('build', Manifest, '--mode', 'headless', '--output', Root / 'standalone')
        Demo = Standalone(Root / 'standalone', SelectedChecker, Data=Source.encode())
        assert Demo.returncode == 0 and Demo.stdout, (Demo.returncode, Demo.stderr[:256])
        assert Standalone(Root / 'standalone', None, Data=b'return true').returncode != 0

        (Project / 'Src/Main.luau').write_text(Application, encoding='utf-8')
        SdkCommand('check', Manifest)
        SdkCommand('build', Manifest, '--mode', 'headless', '--output', Root / 'callbacks')
        for Mode, Tool in [('reports', SelectedChecker), ('failure', Sdk / Components['ProjectTool']),
                           ('cancel', SelectedChecker), ('oversize', SelectedChecker)]:
            Done = Standalone(Root / 'callbacks', Tool, Mode)
            assert (Done.returncode, Done.stdout, Done.stderr) == (0, b'completed', b''), (
                Mode, Done.returncode, Done.stdout[:128], Done.stderr[:256])
    print('[LUI:Language] Packaged application, grant, callback reentry, cancellation, failure and input bound passed')


if __name__ == '__main__':
    Main()
