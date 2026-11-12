#!/usr/bin/env python3
"""Exercise numeric controls through the real project loader and owner input ABI."""
import argparse
import ctypes as C
import hashlib
import json
import os
import subprocess
from pathlib import Path
import tempfile
from Run import Bind, Options, RunOptions, Stage, Execute

Main = r'''
--!strict
local NumericInput = require('@LuiUI/NumericInput')
local Window = Instance.new('Window', {Visible=true, Name='NumericTest'}) :: Window
local Count = 0
local Caption = Instance.new('TextLabel', {Parent=Window, Name='Caption', Text='0'}) :: TextLabel
local Calls = Instance.new('TextLabel', {Parent=Window, Name='Calls', Text='0'}) :: TextLabel
local Status = Instance.new('TextLabel', {Parent=Window, Name='Status', Text='Ready'}) :: TextLabel
local Number = NumericInput.New(Window, {Value=0, Minimum=-1, Maximum=1, Step=0.5,
    OnChanged=function(Next) Count+=1; Caption.Text=tostring(Next); Calls.Text=tostring(Count) end})
Number.View.Name='Number'; Number.Input.Name='Value'; Number.Decrease.Name='Decrease'; Number.Increase.Name='Increase'
local Commit = Instance.new('TextButton', {Parent=Window, Name='Commit', Text='Commit'}) :: TextButton
Commit.Activated:Connect(function() Status.Text=if Number:Commit() then 'Applied' else 'Rejected' end)
local Inspect = Instance.new('TextButton', {Parent=Window, Name='Inspect', Text='Inspect'}) :: TextButton
Inspect.Activated:Connect(function() Status.Text=if Number.Input.IsComposing then 'Composing' else 'Idle' end)
local Close = Instance.new('TextButton', {Parent=Window, Name='Dispose', Text='Dispose'}) :: TextButton
Close.Activated:Connect(function() Number:Dispose(); Number:Dispose(); Status.Text='Disposed' end)
'''

FaultMain = r'''
--!strict
local NumericInput = require('@LuiUI/NumericInput')
local Window = Instance.new('Window') :: Window
local Ok, Failure = pcall(function() NumericInput.New(Window) end)
assert(not Ok and Failure == '[LUI:NumericInput] ConstructionFailed')
assert(#Window:GetChildren() == 0, 'failed constructor retained admitted controls')
local Recovered = NumericInput.New(Window)
Recovered:SetValue(42); assert(Recovered:GetValue() == 42)
Recovered:Dispose(); assert(#Window:GetChildren() == 0)
Window:Destroy()
'''


def MainTest():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--runtime', type=Path, required=True)
    Parser.add_argument('--output', type=Path)
    Parser.add_argument('--checker', type=Path)
    Args = Parser.parse_args()
    Root = Path(__file__).resolve().parents[2]
    Library = C.CDLL(str(Args.runtime.resolve()))
    Bind(Library)
    for Name, Result, Inputs in (
        ('Lui_Pump', C.c_int, [C.c_void_p]),
        ('Lui_Activate', C.c_int, [C.c_void_p, C.c_int]),
        ('Lui_TextChanged', C.c_int, [C.c_void_p, C.c_int, C.c_char_p]),
        ('Lui_TextComposition', C.c_int, [C.c_void_p, C.c_int, C.c_int, C.c_char_p]),
        ('Lui_FocusChanged', C.c_int, [C.c_void_p, C.c_int, C.c_int]),
        ('Lui_KeyInput', C.c_int, [C.c_void_p, C.c_int, C.c_int, C.c_char_p]),
    ):
        Function = getattr(Library, Name)
        Function.restype, Function.argtypes = Result, Inputs
    Checks = []
    with tempfile.TemporaryDirectory(prefix='lui-numeric-') as Temporary:
        Stage(Root, Path(Temporary), Main)
        if Args.checker:
            Checked = subprocess.run([str(Args.checker.resolve()), str(Root / 'types/LUI.d.luau'),
                '--project', str(Path(Temporary) / 'lui.json'), '--diagnostics', 'development'], capture_output=True, text=True, timeout=30,
                **({'creationflags':subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}))
            assert Checked.returncode == 0, Checked.stdout + Checked.stderr
        Manifest = str(Path(Temporary) / 'lui.json').encode('utf-8')
        Input = Options(); Input.StructSize=C.sizeof(Input); Input.AbiVersion=1
        Project, Error = C.c_void_p(), C.create_string_buffer(256)
        assert Library.Lui_ProjectPrepare(C.byref(Input), Manifest, len(Manifest), C.byref(Project), Error, len(Error)), Error.value
        Runtime = None
        try:
            Runtime = Library.Lui_Create(); assert Runtime
            Run = RunOptions(C.sizeof(RunOptions), 1, 1, 0, 0)
            assert Library.Lui_RunProject(Runtime, Project, C.byref(Run)), Library.Lui_GetLastError(Runtime)
            def Tree():
                Library.Lui_Pump(Runtime)
                return json.loads(Library.Lui_GetPreviewTreeJson(Runtime).decode())
            def Get(Name):
                return next(Item for Item in Tree() if Item['name'] == Name)
            def Activate(Name):
                assert Library.Lui_Activate(Runtime, Get(Name)['id']), Name
                Library.Lui_Pump(Runtime)
            def Edit(Text):
                assert Library.Lui_TextChanged(Runtime, Get('Value')['id'], Text.encode('utf-8'))
                Library.Lui_Pump(Runtime)
            Activate('Increase'); assert Get('Caption')['text']=='0.5' and Get('Calls')['text']=='1'
            Activate('Increase'); assert Get('Caption')['text']=='1' and not Get('Increase')['enabled']
            assert not Library.Lui_Activate(Runtime, Get('Increase')['id'])
            Id = Get('Value')['id']
            assert Library.Lui_FocusChanged(Runtime, Id, 1)
            Edit('0.25')
            assert Library.Lui_KeyInput(Runtime, Id, 0, b'Enter')
            assert Library.Lui_KeyInput(Runtime, Id, 2, b'Enter')
            assert Get('Caption')['text']=='0.25' and Get('Calls')['text']=='3'
            assert Library.Lui_FocusChanged(Runtime, Id, 0)
            assert Get('Calls')['text']=='3', 'equal-value blur committed twice'
            assert Library.Lui_FocusChanged(Runtime, Id, 1)
            Edit('0.5')
            assert Library.Lui_FocusChanged(Runtime, Id, 0)
            assert Get('Caption')['text']=='0.5' and Get('Calls')['text']=='4'
            Checks.append('actual Enter and FocusLost handlers commit once')
            Edit('0.75'); Activate('Commit'); assert Get('Caption')['text']=='0.75'
            Before = Get('Calls')['text']
            Edit('invalid'); Activate('Commit')
            assert Get('Status')['text']=='Rejected' and Get('Value')['text']=='0.75' and Get('Calls')['text']==Before
            Checks.append('owner ABI step bounds, text commit and invalid rollback')
            Id = Get('Value')['id']
            assert Library.Lui_FocusChanged(Runtime, Id, 1)
            assert Library.Lui_TextComposition(Runtime, Id, 0, b'')
            assert Library.Lui_TextComposition(Runtime, Id, 1, '候補'.encode('utf-8'))
            Activate('Inspect'); assert Get('Status')['text']=='Composing'
            Activate('Increase'); Activate('Decrease'); Activate('Commit')
            assert Get('Caption')['text']=='0.75' and Get('Calls')['text']==Before and Get('Status')['text']=='Rejected'
            Activate('Inspect'); assert Get('Status')['text']=='Composing'
            assert Library.Lui_TextComposition(Runtime, Id, 2, b'')
            Activate('Inspect'); assert Get('Status')['text']=='Idle'
            Activate('Increase'); assert Get('Caption')['text']=='1'
            Edit('-9999'); Activate('Commit'); assert Get('Caption')['text']=='-1' and not Get('Decrease')['enabled']
            Checks.append('composition admission preserves preedit across component commit and step actions')
            Retired = Get('Increase')['id']
            Activate('Dispose'); assert Get('Status')['text']=='Disposed'
            assert not any(Item['name'] in {'Number','Value','Increase','Decrease'} for Item in Tree())
            assert not Library.Lui_Activate(Runtime, Retired)
            Checks.append('explicit retirement removes subtree and rejects stale owner input')
        finally:
            if Runtime: Library.Lui_Destroy(Runtime)
            Library.Lui_ProjectRelease(Project)
    # Separate negative fixture: production sources above remain unmodified.
    # A test-only local Instance facade fails the third creation after two real
    # admissions. It is not a public constructor option or another VM model.
    with tempfile.TemporaryDirectory(prefix='lui-numeric-fault-') as Temporary:
        Stage(Root, Path(Temporary), FaultMain)
        File = Path(Temporary) / 'Vendor/LuiUI/NumericInput.luau'
        Original = File.read_bytes()
        Prelude = "local NativeInstance = Instance\nlocal Creations = 0\nlocal Instance = {new = function(...) Creations += 1; if Creations == 3 then error('PRIVATE_FACTORY_CANARY', 0) end; return NativeInstance.new(...) end}\n"
        Header, Separator, Body = Original.decode('utf-8').partition('\n')
        assert Separator and Header.rstrip('\r') == '--!strict'
        Instrumented = Header + Separator + Prelude + Body
        assert Instrumented != Original.decode('utf-8')
        File.write_text(Instrumented, encoding='utf-8', newline='\n')
        LockFile = Path(Temporary) / 'Modules.lock.json'
        Lock = json.loads(LockFile.read_text())
        for Package in Lock['Packages']:
            if Package['Alias'] == 'LuiUI':
                for Entry in Package['Files']:
                    if Entry['Path'] == 'NumericInput.luau': Entry['Sha256'] = hashlib.sha256(File.read_bytes()).hexdigest()
        LockFile.write_text(json.dumps(Lock), encoding='utf-8')
        Execute(Library, Path(Temporary) / 'lui.json')
        Fault = {'ProductionSourceSha256':hashlib.sha256(Original).hexdigest(),
            'InstrumentedSourceSha256':hashlib.sha256(File.read_bytes()).hexdigest(),
            'Types':'Not claimed for test-only instrumentation; original source is checked separately'}
        Checks.append('private creation-failure fixture retires admitted controls and hides its canary')
    Report = {'Status':'PASS', 'RuntimeSha256':hashlib.sha256(Args.runtime.read_bytes()).hexdigest(),
        'ConstructionFaultFixture':Fault,
        'Types':'PASS' if Args.checker else 'NOT TESTED',
        'Profile':'Headless owner ABI; no physical keyboard, display, IME or accessibility claim', 'Checks':Checks}
    if Args.output: Args.output.write_text(json.dumps(Report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(Report), flush=True)


if __name__ == '__main__':
    MainTest()
