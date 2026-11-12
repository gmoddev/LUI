#!/usr/bin/env python3
"""Public UI workflows against the real project loader and native scoped storage."""
import argparse
import ctypes as C
import importlib.util
import json
from pathlib import Path
import tempfile
import time
import threading
from Run import Bind, Options, RunOptions


class Grants(C.Structure):
    _fields_ = [('StructSize', C.c_uint32), ('AbiVersion', C.c_uint32), ('GrantedCapabilities', C.c_uint64)]


class Stats(C.Structure):
    _fields_ = [('StructSize', C.c_uint32), ('AbiVersion', C.c_uint32)] + [
        (Name, C.c_uint64) for Name in ('Bytes', 'ReservedBytes', 'Requests', 'Handles', 'Sessions', 'RetiringSessions', 'Leases', 'Events')]


def Configure(Library):
    Bind(Library)
    for Name, Output, Inputs in (
        ('Lui_CreateNativeDomainV2', C.c_void_p, []),
        ('Lui_ReleaseNativeDomainV2', None, [C.c_void_p]),
        ('Lui_PumpNativeDomainV2', C.c_uint32, [C.c_void_p]),
        ('Lui_GetNativeStatsV2', C.c_uint32, [C.c_void_p, C.POINTER(Stats)]),
        ('Lui_SetNativeDomainV2', C.c_uint32, [C.c_void_p, C.c_void_p]),
        ('Lui_DeclareCapabilities', C.c_int, [C.c_void_p, C.POINTER(Grants)]),
        ('Lui_InstallStorageV1', C.c_uint32, [C.c_void_p, C.c_char_p]),
        ('Lui_InstallProjectStorageV1', C.c_uint32, [C.c_void_p, C.c_void_p, C.c_char_p, C.c_char_p]),
        ('Lui_Pump', C.c_int, [C.c_void_p]),
        ('Lui_RunScript', C.c_int, [C.c_void_p, C.c_char_p, C.c_char_p]),
        ('Lui_GetPreviewTreeJson', C.c_char_p, [C.c_void_p]),
        ('Lui_Activate', C.c_int, [C.c_void_p, C.c_int]),
        ('Lui_TextChanged', C.c_int, [C.c_void_p, C.c_int, C.c_char_p]),
        ('Lui_WindowResized', C.c_int, [C.c_void_p, C.c_int, C.c_double, C.c_double]),
        ('Lui_WindowCloseRequested', C.c_int, [C.c_void_p, C.c_int]),
    ):
        Function = getattr(Library, Name); Function.restype, Function.argtypes = Output, Inputs


class Session:
    def __init__(Self, Library, Manifest, Storage, InitialPump=True):
        Self.Library, Self.Runtime, Self.Domain, Self.Project = Library, None, None, C.c_void_p()
        try:
            Self.Domain = Library.Lui_CreateNativeDomainV2()
            Self.Runtime = Library.Lui_Create()
            assert Self.Domain and Self.Runtime
            assert Library.Lui_SetNativeDomainV2(Self.Runtime, Self.Domain) == 0
            Grant = Grants(C.sizeof(Grants), 1, 128)
            assert Library.Lui_DeclareCapabilities(Self.Runtime, C.byref(Grant)) == 1
            Input = Options(); Input.StructSize = C.sizeof(Input); Input.AbiVersion = 1
            Bytes = str(Manifest.resolve()).encode()
            Error = C.create_string_buffer(256)
            assert Library.Lui_ProjectPrepare(C.byref(Input), Bytes, len(Bytes), C.byref(Self.Project), Error, len(Error)), Error.value
            assert Library.Lui_InstallProjectStorageV1(Self.Runtime, Self.Project,
                str(Storage.resolve()).encode(), str(Manifest.resolve().parent).encode()) == 0
            Run = RunOptions(C.sizeof(RunOptions), 1, 1, 0, 0)
            assert Library.Lui_RunProject(Self.Runtime, Self.Project, C.byref(Run)), Self.Error()
            if InitialPump:
                Self.Pump()
        except BaseException:
            Self.Close()
            raise

    def Error(Self):
        return (Self.Library.Lui_GetLastError(Self.Runtime) or b'').decode()

    def Pump(Self):
        Deadline = time.monotonic() + 5
        Idle = 0
        while time.monotonic() < Deadline:
            Self.Library.Lui_Pump(Self.Runtime)
            assert not Self.Error(), Self.Error()
            Value = Stats(); Value.StructSize = C.sizeof(Value); Value.AbiVersion = 2
            assert Self.Library.Lui_GetNativeStatsV2(Self.Domain, C.byref(Value)) == 0
            Idle = Idle + 1 if not Value.Requests else 0
            if Idle >= 3:
                return
            time.sleep(0.002)
        raise RuntimeError('Storage completion deadline')

    def Run(Self, Source):
        assert Self.Library.Lui_RunScript(Self.Runtime, Source.encode(), b'WorkbenchCheck'), Self.Error()
        Self.Pump()

    def Tree(Self):
        return json.loads(Self.Library.Lui_GetPreviewTreeJson(Self.Runtime).decode())

    def Get(Self, Name):
        return next(Item for Item in Self.Tree() if Item['name'] == Name)

    def Activate(Self, Name):
        assert Self.Library.Lui_Activate(Self.Runtime, Self.Get(Name)['id']) == 1, Name
        Self.Pump()

    def Edit(Self, Name, Text):
        assert Self.Library.Lui_TextChanged(Self.Runtime, Self.Get(Name)['id'], Text.encode()) == 1, Name
        Self.Pump()

    def Close(Self):
        if Self.Runtime:
            Self.Library.Lui_Destroy(Self.Runtime); Self.Runtime = None
        if Self.Project:
            Self.Library.Lui_ProjectRelease(Self.Project); Self.Project = C.c_void_p()
        if Self.Domain:
            Deadline = time.monotonic() + 5
            while time.monotonic() < Deadline:
                Self.Library.Lui_PumpNativeDomainV2(Self.Domain)
                Value = Stats(); Value.StructSize = C.sizeof(Value); Value.AbiVersion = 2
                Self.Library.Lui_GetNativeStatsV2(Self.Domain, C.byref(Value))
                if not Value.Sessions:
                    assert not any(getattr(Value, Name) for Name, _ in Stats._fields_[2:]), 'Native ownership leaked'
                    Self.Library.Lui_ReleaseNativeDomainV2(Self.Domain); Self.Domain = None
                    return
                time.sleep(0.002)
            raise RuntimeError('Native retirement deadline')


Lookup = """
local W=app:GetService('WindowService'):GetWindows()[1]
assert(W)
local function Get(Name)
    for _, Item in W:GetDescendants() do if Item.Name==Name then return Item end end
    error('Missing control: '..Name)
end
"""


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--runtime', type=Path, required=True)
    Parser.add_argument('--output', type=Path)
    Parser.add_argument('--stress-cycles', type=int, default=0)
    Args = Parser.parse_args()
    if not 0 <= Args.stress_cycles <= 1000:
        Parser.error('stress cycles must be between 0 and 1000')
    Root = Path(__file__).resolve().parents[2]
    Spec = importlib.util.spec_from_file_location('Stage', Root / 'scripts/Stage-LibraryExample.py')
    Stage = importlib.util.module_from_spec(Spec); Spec.loader.exec_module(Stage)
    Library = C.CDLL(str(Args.runtime.resolve())); Configure(Library)
    Started = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix='lui-workbench-') as Temporary:
        Project = Stage.Stage(Root, 'workbench', Path(Temporary) / 'Project')
        Storage = Path(Temporary) / 'Storage'; Storage.mkdir()
        # Failure paths must not install a provider or create application data.
        Input = Options(); Input.StructSize = C.sizeof(Input); Input.AbiVersion = 1
        Prepared, Error = C.c_void_p(), C.create_string_buffer(256)
        ManifestBytes = str((Project / 'lui.json').resolve()).encode()
        assert Library.Lui_ProjectPrepare(C.byref(Input), ManifestBytes, len(ManifestBytes), C.byref(Prepared), Error, len(Error)), Error.value
        Runtime = Library.Lui_Create()
        try:
            Arguments = (Runtime, Prepared, str(Storage.resolve()).encode(), str(Project.resolve()).encode())
            assert Library.Lui_InstallProjectStorageV1(*Arguments) == 2, 'Storage installed without a grant'
            OtherThread = []
            Worker = threading.Thread(target=lambda: OtherThread.append(Library.Lui_InstallProjectStorageV1(*Arguments)))
            Worker.start(); Worker.join(timeout=5)
            assert OtherThread == [1], 'Foreign owner thread accepted'
            Grant = Grants(C.sizeof(Grants), 1, 128)
            assert Library.Lui_DeclareCapabilities(Runtime, C.byref(Grant))
            assert Library.Lui_InstallProjectStorageV1(Runtime, Prepared, str(Storage.resolve()).encode(), b'relative') == 1
            assert not list(Storage.iterdir()), 'Failed authorization mutated the storage root'
            assert Library.Lui_RunScript(Runtime, b'local Value = 1', b'OwnerFence')
            assert Library.Lui_InstallProjectStorageV1(*Arguments) == 1, 'Provider installation after application startup accepted'
            assert not list(Storage.iterdir()), 'Startup refusal mutated the storage root'
        finally:
            Library.Lui_Destroy(Runtime); Library.Lui_ProjectRelease(Prepared)
        SessionValue = Session(Library, Project / 'lui.json', Storage)
        try:
            assert 'document 3' in SessionValue.Get('LeftEditor')['text']
            assert 'document 2' in SessionValue.Get('RightEditor')['text']
            SessionValue.Edit('LeftEditor', '-- persistent synthetic edit')
            assert SessionValue.Get('Tab-Note-3')['text'].startswith('* ')
            SessionValue.Activate('Document.Close')
            assert 'Dirty' in SessionValue.Get('Status')['text']
            SessionValue.Activate('Document.Save')
            assert not SessionValue.Get('Tab-Note-3')['text'].startswith('* ')
            SessionValue.Activate('Tab-Note-2')
            SessionValue.Edit('RightEditor', '-- independent second note')
            SessionValue.Activate('Document.Save')
            SessionValue.Activate('Tab-Note-3')
            SessionValue.Activate('Workspace.Move')
            assert SessionValue.Get('RightEditor')['text'] == '-- persistent synthetic edit'
            SessionValue.Activate('Workspace.Save')
            SessionValue.Activate('Workspace.Move')
            SessionValue.Activate('Workspace.Restore')
            assert SessionValue.Get('RightEditor')['text'] == '-- persistent synthetic edit'
            SessionValue.Activate('View.Commands')
            assert SessionValue.Get('Palette')['visible']
            SessionValue.Edit('CommandSearch', 'theme')
            SessionValue.Activate('Command-View.Theme')
            assert not SessionValue.Get('Palette')['visible']
            WindowId = SessionValue.Get('Workbench')['id']
            assert Library.Lui_WindowResized(SessionValue.Runtime, WindowId, 900, 700) == 1
            SessionValue.Pump()
            assert not SessionValue.Get('RightEditor')['visible']
            assert SessionValue.Get('LeftEditor')['text'] == '-- persistent synthetic edit'
            Tree = SessionValue.Tree()
            assert sum(Item['name'].startswith('Open-') for Item in Tree) <= 24
            Resources = json.loads(Library.Lui_GetPerformanceJson(SessionValue.Runtime).decode())
        finally:
            SessionValue.Close()
        Reload = Session(Library, Project / 'lui.json', Storage)
        try:
            assert Reload.Get('RightEditor')['text'] == '-- persistent synthetic edit'
            Reload.Activate('Tab-Note-2')
            assert Reload.Get('RightEditor')['text'] == '-- independent second note'
        finally:
            Reload.Close()
        Seed = Session(Library, Project / 'lui.json', Storage)
        try:
            Seed.Activate('Tab-Note-3'); Seed.Activate('Workspace.Move'); Seed.Activate('Workspace.Save')
        finally:
            Seed.Close()
        Raced = Session(Library, Project / 'lui.json', Storage, InitialPump=False)
        try:
            Library.Lui_Pump(Raced.Runtime)
            Raced.Activate('Workspace.Move')
            assert Raced.Get('RightEditor')['text'] == '-- persistent synthetic edit', 'Late initial layout replaced user intent'
        finally:
            Raced.Close()
        Stress = None
        ExpectedNote = '-- persistent synthetic edit'
        if Args.stress_cycles:
            Loaded = Session(Library, Project / 'lui.json', Storage)
            try:
                Samples = []
                Loaded.Activate('Tab-Note-3')
                EditorName = next(Name for Name in ('LeftEditor', 'RightEditor') if Loaded.Get(Name)['text'] == ExpectedNote)
                for Index in range(Args.stress_cycles):
                    Loaded.Activate('Tab-Note-3')
                    ExpectedNote = '-- stress save ' + str(Index)
                    Loaded.Edit(EditorName, ExpectedNote)
                    Loaded.Activate('Document.Save')
                    Loaded.Activate('Workspace.Move'); Loaded.Activate('Workspace.Move')
                    Loaded.Activate('View.Theme')
                    assert Library.Lui_WindowResized(Loaded.Runtime, Loaded.Get('Workbench')['id'], 1180 + Index % 4 * 20, 780)
                    Loaded.Pump()
                    assert sum(Item['name'].startswith('Open-') for Item in Loaded.Tree()) <= 24
                    if Index % 20 == 0:
                        Samples.append(json.loads(Library.Lui_GetPerformanceJson(Loaded.Runtime).decode()))
                Stress = {'Cycles': Args.stress_cycles, 'Samples': Samples,
                    'Final': json.loads(Library.Lui_GetPerformanceJson(Loaded.Runtime).decode())}
                Final = Stress['Final']
                assert Final['LiveInstances'] <= 256 and Final['Connections'] <= 192, 'Existing desktop resource ceiling exceeded'
                assert max(Sample['LuaMemoryBytes'] for Sample in [*Samples, Final]) <= 67108864, 'Workbench VM grant exceeded'
                if Args.stress_cycles >= 200:
                    assert Final['Layout']['Samples'] >= 200, 'Insufficient layout distribution samples'
                    assert Final['Layout']['P95Ms'] <= 50 and Final['Layout']['P99Ms'] <= 100, 'Existing shared layout budget exceeded'
                Stress['BudgetSource'] = 'docs/PERFORMANCE.md; shared resource/layout ceilings only, no native render/input claim'
            finally:
                Loaded.Close()
        IsolatedProject = Stage.Stage(Root, 'workbench', Path(Temporary) / 'UnrelatedProject')
        Isolated = Session(Library, IsolatedProject / 'lui.json', Storage)
        try:
            assert 'document 3' in Isolated.Get('LeftEditor')['text'], 'Same ApplicationId escaped its project scope'
        finally:
            Isolated.Close()
        Moved = Path(Temporary) / 'RenamedProject'
        Project.rename(Moved)
        Renamed = Session(Library, Moved / 'lui.json', Storage)
        try:
            Renamed.Activate('Tab-Note-3')
            assert ExpectedNote in (Renamed.Get('LeftEditor')['text'], Renamed.Get('RightEditor')['text']), 'Directory rename lost scope identity'
            assert Library.Lui_InstallProjectStorageV1(Renamed.Runtime, Renamed.Project,
                str(Storage.resolve()).encode(), str(Moved.resolve()).encode()) == 1, 'Installation after execution accepted'
        finally:
            Renamed.Close()
        Manifest = json.loads((Moved / 'lui.json').read_text(encoding='utf-8'))
        Manifest['Storage']['ApplicationId'] = 'lui.example.other'
        (Moved / 'lui.json').write_text(json.dumps(Manifest), encoding='utf-8')
        OtherIdentity = Session(Library, Moved / 'lui.json', Storage)
        try:
            assert 'document 3' in OtherIdentity.Get('LeftEditor')['text'], 'Changed ApplicationId reused old data'
        finally:
            OtherIdentity.Close()
        PendingRead = Session(Library, Moved / 'lui.json', Storage, InitialPump=False)
        try:
            Library.Lui_Pump(PendingRead.Runtime)
            Outstanding = Stats(); Outstanding.StructSize = C.sizeof(Outstanding); Outstanding.AbiVersion = 2
            assert Library.Lui_GetNativeStatsV2(PendingRead.Domain, C.byref(Outstanding)) == 0
            assert Outstanding.Requests > 0, 'Pending read shutdown fixture had no outstanding native request'
        finally:
            PendingRead.Close()
        PendingWrite = Session(Library, Moved / 'lui.json', Storage)
        try:
            PendingWrite.Edit('LeftEditor', '-- pending shutdown write')
            assert Library.Lui_Activate(PendingWrite.Runtime, PendingWrite.Get('Document.Save')['id'])
            Outstanding = Stats(); Outstanding.StructSize = C.sizeof(Outstanding); Outstanding.AbiVersion = 2
            assert Library.Lui_GetNativeStatsV2(PendingWrite.Domain, C.byref(Outstanding)) == 0
            assert Outstanding.Requests > 0, 'Pending write shutdown fixture had no outstanding native request'
        finally:
            PendingWrite.Close()
        Report = {'Status': 'PASS', 'Milliseconds': round((time.perf_counter()-Started)*1000, 3),
            'Stress': Stress,
            'Resources': Resources, 'NativeRetirement': 'PASS', 'Workflows': [
                'async initial load', 'two documents edited and saved independently', 'dirty close refusal',
                'redock and layout restore', 'palette search and theme command', 'narrow pane fallback',
                'bounded navigation', 'runtime restart and durable note/layout reload', 'user redock wins pending restoration',
                'normal owner provisioning', 'same application identity in another project is isolated']}
        if Args.output:
            Args.output.write_text(json.dumps(Report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(Report))


if __name__ == '__main__':
    Main()
