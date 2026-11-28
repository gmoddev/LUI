"""Small SDK-recipient journeys through the public C owner ingress, without a display.

Application code uses only public Luau APIs. This owner harness supplies grants,
temporary storage, activation and text input. Selected-file candidates also use a
trusted test picker with real disk IO; no OS dialog or physical input is claimed.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import time


class Options(C.Structure):
    _fields_ = [("StructSize", C.c_uint32), ("AbiVersion", C.c_uint32), ("Reserved", C.c_uint64),
                ("CancellationContext", C.c_void_p), ("Canceled", C.c_void_p)] + [
        (Name, C.c_uint64) for Name in ("ManifestBytes", "LockBytes", "Owners", "Files", "FileBytes",
                                      "SourceBytes", "ArtifactBytes", "BytecodeBytes", "RetainedBytes")]


class RunOptions(C.Structure):
    _fields_ = [(Name, C.c_uint32) for Name in ("StructSize", "AbiVersion", "Flags", "MaxImportDepth")] + [("Reserved", C.c_uint64)]


class Grants(C.Structure):
    _fields_ = [("StructSize", C.c_uint32), ("AbiVersion", C.c_uint32), ("Granted", C.c_uint64)]


class NetworkPolicy(C.Structure):
    _fields_ = [(Name, C.c_uint32) for Name in ("StructSize", "AbiVersion", "Flags", "ClientMin", "ClientMax", "ServerMin", "ServerMax")]


class NativeStats(C.Structure):
    _fields_ = [("StructSize", C.c_uint32), ("AbiVersion", C.c_uint32)] + [
        (Name, C.c_uint64) for Name in ("Bytes", "ReservedBytes", "Requests", "Handles", "Sessions", "RetiringSessions", "Leases", "Events")]

StartPicker=C.CFUNCTYPE(C.c_uint32,C.c_void_p,C.c_uint64,C.c_uint32,C.c_char_p)
CancelPicker=C.CFUNCTYPE(None,C.c_void_p,C.c_uint64)
class PickerCallbacks(C.Structure):
    _fields_=[('StructSize',C.c_uint32),('AbiVersion',C.c_uint32),('Context',C.c_void_p),('Start',StartPicker),('Cancel',CancelPicker)]


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("--sdk", type=Path, required=True)
    Parser.add_argument("--projects", type=Path, required=True)
    Parser.add_argument("--output", type=Path, required=True)
    Arguments = Parser.parse_args()
    Output = Arguments.output.resolve()
    Output.mkdir(parents=True, exist_ok=False)
    SdkMetadata=json.loads((Arguments.sdk.resolve()/'SdkManifest.json').read_text(encoding='utf-8'))
    RuntimePath=Arguments.sdk.resolve()/SdkMetadata['Components']['Runtime']
    Native = RuntimePath.parent
    Candidate=SdkMetadata['SdkVersion']=='0.1.0-preview.3'
    Search = None
    if os.name == "nt":
        C.windll.kernel32.SetErrorMode(0x8003)
        Search = os.add_dll_directory(str(Native))
    Library = C.CDLL(str(RuntimePath))
    for Name, Result, Inputs in [
        ("Lui_Create", C.c_void_p, []), ("Lui_Destroy", None, [C.c_void_p]),
        ("Lui_SetBackendName", None, [C.c_void_p, C.c_char_p]),
        ("Lui_GetLastError", C.c_char_p, [C.c_void_p]),
        ("Lui_GetPreviewTreeJson", C.c_char_p, [C.c_void_p]),
        ("Lui_DeclareCapabilities", C.c_int, [C.c_void_p, C.POINTER(Grants)]),
        ("Lui_SetNetworkPolicy", C.c_int, [C.c_void_p, C.POINTER(NetworkPolicy)]),
        ("Lui_InstallStorageV1", C.c_uint32, [C.c_void_p, C.c_char_p]),
        ("Lui_ProjectPrepare", C.c_int, [C.POINTER(Options), C.c_char_p, C.c_uint64, C.POINTER(C.c_void_p), C.c_char_p, C.c_uint64]),
        ("Lui_ProjectRelease", None, [C.c_void_p]),
        ("Lui_RunProject", C.c_int, [C.c_void_p, C.c_void_p, C.POINTER(RunOptions)]),
        ("Lui_Pump", C.c_int, [C.c_void_p]),
        ("Lui_Activate", C.c_int, [C.c_void_p, C.c_int]),
        ("Lui_TextChanged", C.c_int, [C.c_void_p, C.c_int, C.c_char_p]),
        ("Lui_WindowCloseRequested", C.c_int, [C.c_void_p, C.c_int]),
        ("Lui_FocusChanged", C.c_int, [C.c_void_p, C.c_int, C.c_int]),
        ("Lui_WindowShortcut", C.c_int, [C.c_void_p, C.c_int, C.c_int, C.c_char_p, C.c_uint, C.c_int]),
        ("Lui_GetOwnerDomainV1", C.c_void_p, [C.c_void_p]),
        ("Lui_PumpNativeDomainV2", C.c_uint32, [C.c_void_p]),
        ("Lui_GetNativeStatsV2", C.c_uint32, [C.c_void_p, C.POINTER(NativeStats)]),
        ("Lui_ReleaseNativeDomainV2", None, [C.c_void_p]),
    ]:
        Function = getattr(Library, Name)
        Function.restype, Function.argtypes = Result, Inputs
    SelectedFiles=Candidate and hasattr(Library,'Lui_InstallFilesV1')
    if SelectedFiles:
        Library.Lui_InstallFilesV1.restype=C.c_uint32;Library.Lui_InstallFilesV1.argtypes=[C.c_void_p,C.POINTER(PickerCallbacks)]
        Library.Lui_CompleteFilePickerV1.restype=C.c_uint32;Library.Lui_CompleteFilePickerV1.argtypes=[C.c_void_p,C.c_uint64,C.c_char_p,C.c_uint32]
    Report = {"Status": "Running", "SdkVersion":SdkMetadata['SdkVersion'],"Scope": "Packaged SDK real loader/owner input/loopback and selected-file disk IO with a trusted test picker; no native display/clipboard/OS dialog/HTTPS qualification", "Cases": []}

    def Run(Name, Reopen=False):
        Folder = Arguments.projects.resolve() / Name
        App, Project = Library.Lui_Create(), C.c_void_p()
        assert App
        Library.Lui_SetBackendName(App,b'owner-input-harness')
        Domain = None
        PickerState={'Mode':'Auto','Pending':None,'Canceled':[],'Failures':[]}
        PickerPins=[]
        Start = time.monotonic()
        try:
            Manifest = json.loads((Folder / "lui.json").read_text())
            Bits = sum({"Clipboard": 4, "Dialogs": 8, "network.client": 16, "network.server": 32,
                        "network.raw": 64, "Storage": 128}[Token] for Token in Manifest["Capabilities"])
            Grant = Grants(C.sizeof(Grants), 1, Bits)
            assert Library.Lui_DeclareCapabilities(App, C.byref(Grant))
            if Name=='file-desk' and SelectedFiles:
                Documents=Output/'SelectedDocuments';Documents.mkdir(exist_ok=True)
                (Documents/'input.txt').write_text('selected SDK document',encoding='utf-8')
                def Pick(_Context,Request,Kind,_Name):
                    try:
                        PickerState['Pending']=Request
                        if PickerState['Mode']=='Pending':return 0
                        if PickerState['Mode']=='Dismiss':Library.Lui_CompleteFilePickerV1(App,Request,None,0);return 0
                        Chosen=Documents/'input.txt' if Kind==1 else Documents/'output.txt' if Kind==2 else Documents
                        return Library.Lui_CompleteFilePickerV1(App,Request,str(Chosen).encode('utf-8'),0)
                    except Exception as Failure:
                        PickerState['Failures'].append(type(Failure).__name__);return 9
                Started=StartPicker(Pick);Canceled=CancelPicker(lambda _Context,Request:PickerState['Canceled'].append(Request))
                Callbacks=PickerCallbacks(C.sizeof(PickerCallbacks),1,None,Started,Canceled);PickerPins=[Started,Canceled,Callbacks]
                assert Library.Lui_InstallFilesV1(App,C.byref(Callbacks))==0
            if Bits & 112:
                Policy = NetworkPolicy(C.sizeof(NetworkPolicy), 1, 3, 0, 0, 0, 0)
                assert Library.Lui_SetNetworkPolicy(App, C.byref(Policy))
            if Bits & 128:
                Data = Output / (Name + "-Storage")
                Data.mkdir(exist_ok=True)
                assert Library.Lui_InstallStorageV1(App, str(Data).encode()) == 0
            Prepare = Options()
            Prepare.StructSize, Prepare.AbiVersion = C.sizeof(Options), 1
            Error = C.create_string_buffer(256)
            PathBytes = str(Folder / "lui.json").encode()
            assert Library.Lui_ProjectPrepare(C.byref(Prepare), PathBytes, len(PathBytes), C.byref(Project), Error, len(Error)), Error.value
            Running = RunOptions(C.sizeof(RunOptions), 1, 1, 0, 0)
            assert Library.Lui_RunProject(App, Project, C.byref(Running)), Library.Lui_GetLastError(App)
            Domain = Library.Lui_GetOwnerDomainV1(App)
            assert Domain

            def Tree(): return json.loads(Library.Lui_GetPreviewTreeJson(App))
            def Find(Key): return next(Node for Node in Tree() if Node["name"] == Key)
            def Has(Text): return any(Text in Node.get("text", "") for Node in Tree())
            def Pump(Predicate, Seconds=3):
                End = time.monotonic() + Seconds
                while time.monotonic() < End:
                    assert Library.Lui_Pump(App) >= 0
                    if Predicate(): return
                    time.sleep(.005)
                raise AssertionError("Condition timed out: " + json.dumps(Tree())[:1400])
            def Click(Key):
                assert Library.Lui_Activate(App, Find(Key)["id"])
                assert Library.Lui_Pump(App) >= 0
            def Edit(Key, Text):
                assert Library.Lui_TextChanged(App, Find(Key)["id"], Text.encode())
                assert Library.Lui_Pump(App) >= 0

            if Name == "file-desk":
                if Reopen:
                    Click("Restore"); Pump(lambda: Has("Note restored."))
                    assert Find("Note")["text"] == "SDK recipient note"
                else:
                    if SelectedFiles:
                        Click('Open');Pump(lambda:Has('Opened for reading.'))
                        assert Find('Note')['text']=='selected SDK document'
                        Edit('Note','saved SDK copy');Click('SaveFile');Pump(lambda:Has('File saved.'))
                        assert (Documents/'output.txt').read_bytes()==b'saved SDK copy'
                        PickerState['Mode']='Pending';Click('Open');Edit('Note','keep newer file edits')
                        assert Library.Lui_CompleteFilePickerV1(App,PickerState['Pending'],str(Documents/'input.txt').encode('utf-8'),0)==0
                        Pump(lambda:Has('Your newer edits were kept.'));assert Find('Note')['text']=='keep newer file edits'
                        Click('Discard');PickerState['Mode']='Auto'
                        Click('Folder');Pump(lambda:Has('entries. Type a file name'))
                        Edit('ChildName','input.txt');Click('OpenChild');Pump(lambda:Has('Opened for reading.'))
                        assert Find('Note')['text']=='selected SDK document'
                        Click('SaveFile');Pump(lambda:Has('File saved.'))
                        assert (Documents/'output.txt').read_bytes()==b'selected SDK document'
                        Edit('ChildName','../outside.txt');Click('OpenChild');Pump(lambda:Has('Named file was not opened:'))
                        assert Find('Note')['text']=='selected SDK document'
                        PickerState['Mode']='Dismiss';Click('Open');Pump(lambda:Has('Selection cancelled.'))
                        PickerState['Mode']='Pending';Click('Open');Click('Cancel');Pump(lambda:bool(PickerState['Canceled']))
                        assert Library.Lui_CompleteFilePickerV1(App,PickerState['Pending'],str(Documents/'input.txt').encode('utf-8'),0)==8
                        PickerState['Mode']='Auto';assert not PickerState['Failures']
                    else:
                        Click("Open"); assert Has("no file picker")
                    Click("Copy"); assert Has("no clipboard")
                    Edit("Note", "SDK recipient note")
                    Click("Save"); Pump(lambda: Has("Note saved in this app."))
                    Edit("Note", "keep these unsaved edits")
                    Click("Restore"); assert Has("Save your edits")
                    Click("Discard"); Click("Restore"); Pump(lambda: Has("Note restored."))
                    assert Find("Note")["text"] == "SDK recipient note"
            elif Name == "network-desk":
                Click("Echo"); Pump(lambda: Has("Server received and echoed:"))
                assert Has("Real requests handled: 1")
                Click("Broken"); Pump(lambda: Has("Invalid JSON was rejected"))
                Click("Slow"); Click("Cancel"); Pump(lambda: Has("failed or was cancelled"))
                Click("Echo"); Pump(lambda: Has("Real requests handled: 2"))
                if Candidate:
                    Click('WebSocket');Pump(lambda:Has('Real messages handled: 1'))
            else:
                Pump(lambda: Has("Ready."))
                if Reopen:
                    assert Find("SharedEditor")["text"] == "shared recipient note"
                    assert len([Node for Node in Tree() if Node["className"] == "Window"]) == (4 if Candidate else 3)
                else:
                    Click("View.New")
                    Editors = [Node for Node in Tree() if Node["name"] == "SharedEditor"]
                    assert len(Editors) == 2
                    assert Library.Lui_TextChanged(App, Editors[0]["id"], b"shared recipient note")
                    Pump(lambda: all(Node["text"] == "shared recipient note" for Node in Tree() if Node["name"] == "SharedEditor"))
                    if Candidate:
                        Editor=Editors[0]
                        Nodes={Node['id']:Node for Node in Tree()}
                        Parent=Nodes[Editor['parentId']]
                        while Parent['className']!='Window':Parent=Nodes[Parent['parentId']]
                        assert Library.Lui_FocusChanged(App,Editor['id'],1)
                        assert Library.Lui_WindowShortcut(App,Parent['id'],Editor['id'],b'S',1,0)
                        Pump(lambda:Has('Note saved.'))
                        assert Library.Lui_WindowShortcut(App,Parent['id'],Editor['id'],b'N',1,0)
                        Pump(lambda:len([Node for Node in Tree() if Node['name']=='SharedEditor'])==3)
                    else:
                        Click("Note.Save"); Pump(lambda: Has("Note saved."))
                    Click("Workspace.Save"); Pump(lambda: Has("Workspace saved."))
            Report["Cases"].append({"Name": Name, "Reopen": Reopen, "Status": "PASS",
                "Milliseconds": round(1000 * (time.monotonic() - Start), 2),
                "SourceSha256": hashlib.sha256((Folder / "Src/Main.luau").read_bytes()).hexdigest()})
            print("[LUI:Applications] " + Name + (" reopen" if Reopen else "") + " PASS", flush=True)
        finally:
            Library.Lui_Destroy(App)
            if Domain:
                End = time.monotonic() + 3
                Stats = NativeStats()
                Stats.StructSize, Stats.AbiVersion = C.sizeof(NativeStats), 2
                while True:
                    assert Library.Lui_PumpNativeDomainV2(Domain) == 0
                    assert Library.Lui_GetNativeStatsV2(Domain, C.byref(Stats)) == 0
                    if Stats.Sessions == 0: break
                    assert time.monotonic() < End, 'Native sessions did not retire'
                    time.sleep(.005)
                assert not any(getattr(Stats, Name) for Name in ('Bytes', 'ReservedBytes', 'Requests', 'Handles', 'Leases', 'Events'))
                Library.Lui_ReleaseNativeDomainV2(Domain)
            if Project: Library.Lui_ProjectRelease(Project)

    try:
        for Name in ("file-desk", "network-desk", "window-desk"):
            Run(Name)
            if Name != "network-desk": Run(Name, True)
        Report["Status"] = "PASS"
    except Exception as Failure:
        Report.update(Status="FAIL", Reason=str(Failure)[:2000], FailureType=type(Failure).__name__)
        raise
    finally:
        (Output / "Report.json").write_text(json.dumps(Report, indent=2) + "\n")
        if Search: Search.close()


if __name__ == "__main__":
    Main()
