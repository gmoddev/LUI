"""Exercise generated recipe projects through the real loader without a display.

This covers shared semantics and local HTTP, not native image presentation,
physical input, desktop accessibility or a fresh-machine SDK qualification.
"""
import argparse
import ctypes as C
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import time

Specification = importlib.util.spec_from_file_location('LibraryRunner', Path(__file__).resolve().parents[1] / 'libraries/Run.py')
Runner = importlib.util.module_from_spec(Specification)
Specification.loader.exec_module(Runner)


class Grants(C.Structure):
    _fields_ = [('StructSize', C.c_uint32), ('AbiVersion', C.c_uint32), ('Granted', C.c_uint64)]


class NetworkPolicy(C.Structure):
    _fields_ = [(Name, C.c_uint32) for Name in ('StructSize', 'AbiVersion', 'Flags', 'ClientMin', 'ClientMax', 'ServerMin', 'ServerMax')]


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--runtime', type=Path, required=True)
    Parser.add_argument('--projects', type=Path, required=True)
    Parser.add_argument('--output', type=Path, required=True)
    Arguments = Parser.parse_args()
    RuntimePath = Arguments.runtime.resolve()
    Search = None
    if os.name == 'nt':
        C.windll.kernel32.SetErrorMode(0x8003)
        Search = os.add_dll_directory(str(RuntimePath.parent))
    Library = C.CDLL(str(RuntimePath))
    Runner.Bind(Library)
    for Name, Output, Inputs in (
        ('Lui_DeclareCapabilities', C.c_int, [C.c_void_p, C.POINTER(Grants)]),
        ('Lui_SetNetworkPolicy', C.c_int, [C.c_void_p, C.POINTER(NetworkPolicy)]),
        ('Lui_RegisterAsset', C.c_int, [C.c_void_p, C.c_char_p]),
        ('Lui_GetPreviewTreeJson', C.c_char_p, [C.c_void_p]),
        ('Lui_Pump', C.c_int, [C.c_void_p]),
        ('Lui_Activate', C.c_int, [C.c_void_p, C.c_int]),
        ('Lui_PointerInputV2', C.c_int, [C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_double, C.c_double, C.c_int]),
        ('Lui_KeyInput', C.c_int, [C.c_void_p, C.c_int, C.c_int, C.c_char_p]),
    ):
        Function = getattr(Library, Name)
        Function.restype, Function.argtypes = Output, Inputs
    Results = []
    for Name in ('Recipe-Images', 'Recipe-Time', 'Recipe-RightClick', 'Recipe-Http'):
        ManifestPath = Arguments.projects.resolve() / Name / 'lui.json'
        Manifest = json.loads(ManifestPath.read_text(encoding='utf-8'))
        App = Library.Lui_Create()
        Project = C.c_void_p()
        assert App
        try:
            Bits = sum({'network.client': 16, 'network.server': 32}[Token] for Token in Manifest['Capabilities'])
            Grant = Grants(C.sizeof(Grants), 1, Bits)
            assert Library.Lui_DeclareCapabilities(App, C.byref(Grant)) == 1
            if Name == 'Recipe-Http':
                Policy = NetworkPolicy(C.sizeof(NetworkPolicy), 1, 3, 0, 0, 0, 0)
                assert Library.Lui_SetNetworkPolicy(App, C.byref(Policy)) == 1
            for Asset in Manifest['Assets']:
                assert (ManifestPath.parent / Asset).is_file()
                assert Library.Lui_RegisterAsset(App, Asset.encode()) == 1
            Options = Runner.Options()
            Options.StructSize, Options.AbiVersion = C.sizeof(Options), 1
            Input = str(ManifestPath).encode()
            Error = C.create_string_buffer(256)
            assert Library.Lui_ProjectPrepare(C.byref(Options), Input, len(Input), C.byref(Project), Error, len(Error)), Error.value
            Run = Runner.RunOptions(C.sizeof(Runner.RunOptions), 1, 1, 0, 0)
            assert Library.Lui_RunProject(App, Project, C.byref(Run)), Library.Lui_GetLastError(App)

            def Tree():
                return json.loads(Library.Lui_GetPreviewTreeJson(App))

            def Find(Class, Text=None):
                return next(Node for Node in Tree() if Node['className'] == Class and (Text is None or Node.get('text') == Text))

            Library.Lui_Pump(App)
            if Name == 'Recipe-Images':
                Find('ImageLabel')
                Result = 'Declared asset admission and ImageLabel creation passed; native decoding/presentation is a separate check.'
            elif Name == 'Recipe-Time':
                assert Library.Lui_Activate(App, Find('TextButton', 'Update')['id'])
                assert any(Node.get('text', '').endswith('ms since opening') for Node in Tree())
                assert any(Node.get('text', '').endswith('UTC') for Node in Tree())
                Result = 'Clock formatting and elapsed-time update passed.'
            elif Name == 'Recipe-RightClick':
                Surface = next(Node for Node in Tree() if Node['className'] == 'Frame' and Node['parentId'] == Find('Window')['id'])
                MenuId = next(Node['id'] for Node in Tree() if Node['className'] == 'Frame' and Node['parentId'] == Surface['id'])

                def Visible():
                    return next(Node['visible'] for Node in Tree() if Node['id'] == MenuId)

                assert not Visible()
                for Phase in (0, 2):
                    assert Library.Lui_PointerInputV2(App, Surface['id'], Phase, 0, 1, 200, 100, 2)
                assert Visible()
                Choice = Find('TextButton', 'Choose this action')['id']
                assert Library.Lui_KeyInput(App, Choice, 0, b'Escape')
                assert not Visible()
                assert Library.Lui_Activate(App, Find('TextButton', 'Open menu')['id'])
                assert Visible()
                assert Library.Lui_Activate(App, Choice)
                assert not Visible()
                Find('TextLabel', 'Action selected')
                assert Library.Lui_PointerInputV2(App, Surface['id'], 0, 0, 2, 200, 100, 2)
                assert Library.Lui_PointerInputV2(App, Surface['id'], 3, 0, 2, 200, 100, 0)
                assert not Visible()
                Result = 'Right release, keyboard opening/Escape, action selection and canceled press passed.'
            else:
                assert Library.Lui_Activate(App, Find('TextButton', 'Send request')['id'])
                Deadline = time.monotonic() + 6
                while time.monotonic() < Deadline:
                    Library.Lui_Pump(App)
                    if any(Node.get('text') == 'Hello from your local server' for Node in Tree()):
                        break
                    time.sleep(0.01)
                else:
                    raise AssertionError('Local HTTP response was not delivered')
                Result = 'Loopback-only client/server request and scheduler-delivered UI update passed.'
            Results.append({'Project': Name, 'Outcome': 'PASS', 'Result': Result})
        finally:
            if Project:
                Library.Lui_ProjectRelease(Project)
            Library.Lui_Destroy(App)
    Report = {'RuntimeSha256': hashlib.sha256(RuntimePath.read_bytes()).hexdigest(), 'Scope': 'Headless shared semantics only', 'Projects': Results}
    Arguments.output.parent.mkdir(parents=True, exist_ok=True)
    with Arguments.output.open('x', encoding='utf-8') as File:
        File.write(json.dumps(Report, indent=2) + '\n')
    print('[LUI:DocsTest] Four real project-loader recipe journeys passed')
    if Search:
        Search.close()


if __name__ == '__main__':
    Main()
