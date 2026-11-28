"""Focused-editor command routing through the real public native owner ingress."""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import subprocess
import tempfile
from Run import Bind, Stage, Options, RunOptions

Main = r'''
--!strict
local Commands = require('@LuiWorkbench/Commands')
local Registry = Commands.New()
local Window = Instance.new('Window', {Name = 'Window'})
local Editor = Instance.new('TextBox', {Parent = Window, Name = 'Editor', Text = 'Keep this text'})
local Status = Instance.new('TextLabel', {Parent = Window, Name = 'Status', Text = '0'})
local Count = 0
Registry:Register({Id = 'Save', Label = 'Save', Shortcut = {Key = 'S', Control = true}, AllowWhileEditing = true,
    Execute = function() Count += 1; Status.Text = tostring(Count) end})
Registry:Register({Id = 'Default', Label = 'Default', Shortcut = {Key = 'D', Control = true},
    Execute = function() Count += 1000; Status.Text = tostring(Count) end})
Registry:Register({Id = 'Undo', Label = 'Application undo', Shortcut = {Key = 'Z', Control = true},
    Execute = function() Count += 1000; Status.Text = tostring(Count) end})
Window.Shortcut:Connect(function(Input) Registry:Dispatch(Input) end)
local Retire = Instance.new('TextButton', {Parent = Window, Name = 'Retire', Text = 'Retire'})
Retire.Activated:Connect(function() Registry:Dispose() end)
Window.Destroying:Connect(function() Registry:Dispose() end)
Window.Visible = true
'''


def MainTest():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--runtime', type=Path, required=True)
    Parser.add_argument('--checker', type=Path, required=True)
    Parser.add_argument('--output', type=Path, required=True)
    Arguments = Parser.parse_args()
    if os.name == 'nt': C.windll.kernel32.SetErrorMode(0x8003)
    Root = Path(__file__).resolve().parents[2]
    Library = C.CDLL(str(Arguments.runtime.resolve()))
    Bind(Library)
    for Name, Result, Inputs in [
        ('Lui_Pump', C.c_int, [C.c_void_p]),
        ('Lui_Activate', C.c_int, [C.c_void_p, C.c_int]),
        ('Lui_FocusChanged', C.c_int, [C.c_void_p, C.c_int, C.c_int]),
        ('Lui_TextComposition', C.c_int, [C.c_void_p, C.c_int, C.c_int, C.c_char_p]),
        ('Lui_WindowShortcut', C.c_int, [C.c_void_p, C.c_int, C.c_int, C.c_char_p, C.c_uint, C.c_int]),
    ]:
        Function = getattr(Library, Name)
        Function.restype, Function.argtypes = Result, Inputs
    Checks = []
    with tempfile.TemporaryDirectory(prefix='lui-command-input-') as Temporary:
        Stage(Root, Path(Temporary), Main)
        Manifest = Path(Temporary) / 'lui.json'
        Checked = subprocess.run([str(Arguments.checker.resolve()), str(Root / 'types/LUI.d.luau'),
            '--project', str(Manifest), '--diagnostics', 'development'], capture_output=True, text=True, timeout=30,
            **({'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}))
        assert Checked.returncode == 0, Checked.stdout + Checked.stderr
        Input = Options()
        Input.StructSize, Input.AbiVersion = C.sizeof(Input), 1
        Project, Error = C.c_void_p(), C.create_string_buffer(256)
        ManifestBytes = str(Manifest).encode()
        assert Library.Lui_ProjectPrepare(C.byref(Input), ManifestBytes, len(ManifestBytes), C.byref(Project), Error, len(Error)), Error.value
        Runtime = Library.Lui_Create()
        assert Runtime
        try:
            Run = RunOptions(C.sizeof(RunOptions), 1, 1, 0, 0)
            assert Library.Lui_RunProject(Runtime, Project, C.byref(Run)), Library.Lui_GetLastError(Runtime)
            def Tree(): return json.loads(Library.Lui_GetPreviewTreeJson(Runtime))
            def Find(Name): return next(Node for Node in Tree() if Node['name'] == Name)
            Window, Editor = Find('Window')['id'], Find('Editor')['id']
            assert Library.Lui_FocusChanged(Runtime, Editor, 1)
            def Shortcut(Key='S', Modifiers=1, Repeat=0, Admitted=1):
                assert Library.Lui_WindowShortcut(Runtime, Window, Editor, Key.encode(), Modifiers, Repeat) == Admitted
                assert Library.Lui_Pump(Runtime) >= 0
            def Count(Value):
                assert Find('Status')['text'] == str(Value)
                assert Find('Editor')['text'] == 'Keep this text'
            Shortcut(); Count(1); Checks.append('Control-S in focused editor')
            Shortcut('D'); Count(1); Checks.append('default editing guard')
            Shortcut('Z'); Count(1); Checks.append('native undo precedence even with an empty local stack')
            Shortcut(Repeat=1); Count(1); Checks.append('repeat suppression')
            Shortcut(Modifiers=0); Count(1); Checks.append('plain typing protected')
            Shortcut(Modifiers=5); Count(1); Checks.append('AltGr protected')
            Shortcut(Modifiers=3); Count(1); Checks.append('exact Shift matching')
            assert Library.Lui_TextComposition(Runtime, Editor, 0, b'')
            Shortcut(Admitted=0); Count(1); Checks.append('composition suppression')
            assert Library.Lui_TextComposition(Runtime, Editor, 2, b'')
            Shortcut(); Count(2); Checks.append('post-composition recovery')
            assert Library.Lui_Activate(Runtime, Find('Retire')['id'])
            Shortcut(); Count(2); Checks.append('retired registry ignores input')
        finally:
            Library.Lui_Destroy(Runtime)
            Library.Lui_ProjectRelease(Project)
    Report = {'Status': 'PASS', 'Platform': os.name, 'Scope': 'Headless owner ingress, no physical keyboard/native host claim', 'Checks': Checks}
    Arguments.output.write_text(json.dumps(Report, indent=2) + '\n', encoding='utf-8')
    print('[LUI:Commands] Owner input ' + str(len(Checks)) + '/10 PASS')


if __name__ == '__main__': MainTest()
