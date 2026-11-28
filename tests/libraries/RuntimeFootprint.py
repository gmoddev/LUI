#!/usr/bin/env python3
"""Measure incremental optional-library payloads and warm project startup."""
import argparse
import ctypes
import hashlib
import importlib.util
import io
import json
import platform
import statistics
import subprocess
import tempfile
import time
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

from Run import Bind, Execute, Stage


Profiles = {
    'Baseline': (),
    'Binary': ('Binary',),
    'Path': ('Path',),
    'Encoding': ('Encoding',),
    'Utc': ('Utc',),
    'Config': ('Config', 'Json'),
    'Spec': ('Spec',),
}
Root = Path(__file__).resolve().parents[2]


def Payload(Files):
    Archive = io.BytesIO()
    with ZipFile(Archive, 'w', ZIP_DEFLATED) as Output:
        for Name, Source in Files:
            Output.writestr(Name, Source.read_bytes())
    return {'Bytes': sum(Source.stat().st_size for _, Source in Files), 'ZipBytes': len(Archive.getvalue())}


def Measure(Runtime, Samples):
    Library = ctypes.CDLL(str(Runtime.resolve()))
    Bind(Library)
    Report = {}
    with tempfile.TemporaryDirectory(prefix='lui-footprint-') as Temp:
        Folder = Path(Temp)
        for Name, Packages in Profiles.items():
            Project = Folder / Name
            Project.mkdir()
            Main = '--!strict\n'
            if Packages:
                Main += 'local Module = require("@' + Name + '/' + Name + '")\nassert(Module.Version)\n'
            Stage(Root, Project, Main, Include=Packages)
        # Interleave profiles so drift in filesystem cache, frequency or load
        # does not always affect a particular package. The first round warms up.
        Observations = {Name: [] for Name in Profiles}
        for Round in range(Samples + 1):
            Names = list(Profiles)
            Shift = Round % len(Names)
            for Name in Names[Shift:] + Names[:Shift]:
                Result = Execute(Library, Folder / Name / 'lui.json')
                if Round:
                    Observations[Name].append(Result)
        for Name, Values in Observations.items():
            Project = Folder / Name
            Files = [(File.relative_to(Project).as_posix(), File) for File in sorted(Project.rglob('*')) if File.is_file()]
            Entries = Profiles[Name]
            SourceFiles = [(File.relative_to(Root).as_posix(), File) for Alias in Entries
                           for File in sorted((Root / 'libraries' / Alias).glob('*.luau'))]
            Report[Name] = {
                'Project': Payload(Files), 'ModuleSources': Payload(SourceFiles),
                'MedianPrepareCreateRunMs': statistics.median(Value['Milliseconds'] for Value in Values),
                'MedianVmBytesAfterRun': statistics.median(Value['Resources']['LuaMemoryBytes'] for Value in Values),
            }
    return Report


def SdkFootprint():
    Definition = importlib.util.spec_from_file_location('SdkPackage', Root / 'scripts/Package-Sdk.py')
    Package = importlib.util.module_from_spec(Definition)
    Definition.loader.exec_module(Package)
    Added = set(Profiles) - {'Baseline'} | {'asset-report', 'app-tests', 'timestamp-viewer'}
    with tempfile.TemporaryDirectory(prefix='lui-sdk-footprint-') as Temp:
        Folder = Path(Temp)
        Package.StageOptionalLibraries(Root, Folder, '0.1.0-preview.3')
        Files = [(File.relative_to(Folder).as_posix(), File) for File in sorted(Folder.rglob('*')) if File.is_file()]
        Previous = Payload([(Name, File) for Name, File in Files if Name.split('/')[1] not in Added])
        Current = Payload(Files)
        return {Key: Current[Key] - Previous[Key] for Key in Current}


def HostStartup(Host, Samples):
    Names = tuple(Name for Name in Profiles if Name != 'Baseline')
    All = set().union(*Profiles.values())
    Imports = ''.join(f'assert(require("@{Name}/{Name}").Version)\n' for Name in Names)
    Workloads = [('Baseline', (), ''), ('Declared', All, ''), ('Imported', All, Imports)]
    Times = {Name: [] for Name, _, _ in Workloads}
    with tempfile.TemporaryDirectory(prefix='lui-host-startup-') as Temp:
        for Name, Packages, Main in Workloads:
            Project = Path(Temp) / Name
            Project.mkdir()
            Stage(Root, Project, '--!strict\n' + Main, Include=Packages)
        for Round in range(Samples + 1):
            Offset = Round % len(Workloads)
            for Name, _, _ in Workloads[Offset:] + Workloads[:Offset]:
                Start = time.perf_counter()
                Result = subprocess.run([str(Host), '--project', str(Path(Temp) / Name / 'lui.json')],
                                        capture_output=True, timeout=30)
                Elapsed = (time.perf_counter() - Start) * 1000
                if Result.returncode:
                    raise RuntimeError('[LUI:Qualification] Headless startup failed')
                if Round:
                    Times[Name].append(Elapsed)
    return {Name: round(statistics.median(Values), 3) for Name, Values in Times.items()}


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--runtime', type=Path, required=True)
    Parser.add_argument('--headless', type=Path)
    Parser.add_argument('--samples', default=15, type=int, choices=range(3, 102))
    Parser.add_argument('--output', type=Path, required=True)
    Args = Parser.parse_args()
    Report = {'Platform': platform.platform(), 'RuntimeSha256': hashlib.sha256(Args.runtime.read_bytes()).hexdigest(),
              'Samples': Args.samples, 'Profiles': Measure(Args.runtime, Args.samples), 'SdkContentDelta': SdkFootprint()}
    if Args.headless:
        Report['MedianHeadlessProcessMs'] = HostStartup(Args.headless.resolve(), Args.samples)
    Args.output.write_text(json.dumps(Report, indent=2) + '\n', encoding='utf-8')
    print('[LUI:Qualification] Measured ' + str(len(Profiles)) + ' project profiles')


if __name__ == '__main__':
    Main()
