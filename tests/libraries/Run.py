#!/usr/bin/env python3
"""Exercise exact vendored modules through LUI's real schema-v2 project loader."""
import argparse
import ctypes as C
import hashlib
import json
import platform
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


class Options(C.Structure):
    _fields_ = [(Name, Kind) for Name, Kind in (
        ('StructSize', C.c_uint32), ('AbiVersion', C.c_uint32), ('Reserved', C.c_uint64),
        ('CancellationContext', C.c_void_p), ('Canceled', C.c_void_p))] + [
        (Name, C.c_uint64) for Name in ('ManifestBytes', 'LockBytes', 'Owners', 'Files',
            'FileBytes', 'SourceBytes', 'ArtifactBytes', 'BytecodeBytes', 'RetainedBytes')]


class RunOptions(C.Structure):
    _fields_ = [(Name, C.c_uint32) for Name in ('StructSize', 'AbiVersion', 'Flags', 'MaxImportDepth')] + [('Reserved', C.c_uint64)]


def Bind(Library):
    for Name, Output, Inputs in (
        ('Lui_Create', C.c_void_p, []), ('Lui_Destroy', None, [C.c_void_p]),
        ('Lui_GetLastError', C.c_char_p, [C.c_void_p]),
        ('Lui_GetPerformanceJson', C.c_char_p, [C.c_void_p]),
        ('Lui_GetPreviewTreeJson', C.c_char_p, [C.c_void_p]),
        ('Lui_ProjectPrepare', C.c_int, [C.POINTER(Options), C.c_char_p, C.c_uint64,
            C.POINTER(C.c_void_p), C.c_char_p, C.c_uint64]),
        ('Lui_ProjectRelease', None, [C.c_void_p]),
        ('Lui_RunProject', C.c_int, [C.c_void_p, C.c_void_p, C.POINTER(RunOptions)]),
    ):
        Function = getattr(Library, Name)
        Function.restype, Function.argtypes = Output, Inputs


def Stage(Root, Destination, Main, MaxInterrupts=1000000):
    (Destination / 'Src').mkdir()
    (Destination / 'Src/Main.luau').write_text(Main, encoding='utf-8', newline='\n')
    Dependencies, Packages = [], []
    for Package in sorted((Root / 'libraries').iterdir()):
        if not Package.is_dir():
            continue
        Files, Pins = [], []
        for Source in sorted(Package.glob('*.luau')):
            Target = Destination / 'Vendor' / Package.name / Source.name
            Target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(Source, Target)
            Files.append(Source.name)
            Pins.append({'Path': Source.name, 'Sha256': hashlib.sha256(Source.read_bytes()).hexdigest()})
        if Files:
            Catalog = json.loads((Package / 'Library.json').read_text(encoding='utf-8'))
            Dependencies.append({'Alias': Package.name, 'Root': 'Vendor/' + Package.name, 'Files': Files, 'Requires': Catalog.get('Requires', [])})
            Packages.append({'Alias': Package.name, 'Provenance': {'Kind': 'LocalVendor', 'Id': Package.name + '-0.1.0-preview.2'}, 'Files': Pins})
    Manifest = {'SchemaVersion': 2, 'Script': 'Src/Main.luau', 'Capabilities': [], 'Extensions': [],
        'Assets': [], 'ExtensionDeclarations': [], 'Diagnostics': {'Mode': 'Development'},
        'Sandbox': {'MaxMemoryBytes': 67108864, 'MaxInterrupts': MaxInterrupts},
        'Modules': {'Root': 'Src', 'Files': ['Main.luau'], 'Requires': [D['Alias'] for D in Dependencies],
            'Lock': 'Modules.lock.json', 'Dependencies': Dependencies}}
    (Destination / 'lui.json').write_text(json.dumps(Manifest), encoding='utf-8')
    (Destination / 'Modules.lock.json').write_text(json.dumps({'SchemaVersion': 1, 'Packages': Packages}), encoding='utf-8')


def Execute(Library, Manifest):
    Input = Options(); Input.StructSize = C.sizeof(Input); Input.AbiVersion = 1
    PathBytes = str(Manifest.resolve()).encode('utf-8')
    Project, Error = C.c_void_p(), C.create_string_buffer(256)
    Started = time.perf_counter()
    if not Library.Lui_ProjectPrepare(C.byref(Input), PathBytes, len(PathBytes), C.byref(Project), Error, len(Error)):
        raise RuntimeError(Error.value.decode())
    Runtime = None
    try:
        Runtime = Library.Lui_Create()
        if not Runtime:
            raise RuntimeError('Runtime creation failed')
        Run = RunOptions(C.sizeof(RunOptions), 1, 1, 0, 0)
        if not Library.Lui_RunProject(Runtime, Project, C.byref(Run)):
            Failure = (Library.Lui_GetLastError(Runtime) or b'Run failed').decode()
            if Failure == '[LUI:Modules] LimitExceeded':
                assert json.loads(Library.Lui_GetPreviewTreeJson(Runtime).decode()) == []
            raise RuntimeError(Failure)
        return {'Milliseconds': round((time.perf_counter() - Started) * 1000, 3),
            'Resources': json.loads(Library.Lui_GetPerformanceJson(Runtime).decode())}
    finally:
        if Runtime:
            Library.Lui_Destroy(Runtime)
        Library.Lui_ProjectRelease(Project)


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--runtime', type=Path, required=True)
    Parser.add_argument('--suite', default='Commands')
    Parser.add_argument('--output', type=Path)
    Parser.add_argument('--checker', type=Path)
    Parser.add_argument('--max-interrupts', type=int)
    Parser.add_argument('--expect-limit', action='store_true')
    Args = Parser.parse_args()
    Root = Path(__file__).resolve().parents[2]
    Library = C.CDLL(str(Args.runtime.resolve())); Bind(Library)
    Suites = sorted((Root / 'tests/libraries').glob('*.luau')) if Args.suite == 'all' else [Root / 'tests/libraries' / (Args.suite + '.luau')]
    Results = []
    for Suite in Suites:
        with tempfile.TemporaryDirectory(prefix='lui-library-') as Temporary:
            # The full codec corpus exercises the 1 MiB boundary repeatedly.
            # Existing library workloads retain their original admission budget.
            Budget = Args.max_interrupts if Args.max_interrupts is not None else (100000000 if Suite.stem == 'Json' else 1000000)
            Stage(Root, Path(Temporary), Suite.read_text(encoding='utf-8'), Budget)
            if Args.checker:
                Checked = subprocess.run([str(Args.checker.resolve()), str(Root / 'types/LUI.d.luau'),
                    '--project', str(Path(Temporary) / 'lui.json'), '--diagnostics', 'development'],
                    capture_output=True, text=True, timeout=30)
                if Checked.returncode:
                    raise RuntimeError(Checked.stdout + Checked.stderr)
            try:
                Result = Execute(Library, Path(Temporary) / 'lui.json')
            except RuntimeError as Error:
                if not Args.expect_limit or str(Error) != '[LUI:Modules] LimitExceeded':
                    raise
                Result = {'ExpectedFailure': str(Error)}
            else:
                if Args.expect_limit:
                    raise AssertionError('Expected execution quota refusal')
            Result['MaxInterrupts'] = Budget
            Result['Types'] = 'PASS' if Args.checker else 'NOT TESTED'
            Result.update(Suite=Suite.stem, Status='PASS', Sha256=hashlib.sha256(Suite.read_bytes()).hexdigest())
            Results.append(Result)
    Report = {'Platform': platform.platform(), 'RuntimeSha256': hashlib.sha256(Args.runtime.read_bytes()).hexdigest(),
        'Modules': {str(File.relative_to(Root)).replace('\\', '/'): hashlib.sha256(File.read_bytes()).hexdigest()
            for File in sorted((Root / 'libraries').rglob('*.luau'))}, 'Results': Results}
    Text = json.dumps(Report, indent=2)
    if Args.output:
        Args.output.write_text(Text + '\n', encoding='utf-8')
    print(Text)


if __name__ == '__main__':
    Main()
