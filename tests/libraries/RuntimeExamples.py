#!/usr/bin/env python3
"""Exercise staged development examples with an existing platform SDK."""
import argparse
import ctypes
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

from Run import Bind, Execute


Root = Path(__file__).resolve().parents[2]


def Command(Cli, Arguments, Expected=0):
    Process = subprocess.run([str(Cli), *map(str, Arguments)], capture_output=True, text=True, timeout=45)
    if Process.returncode != Expected:
        raise RuntimeError(f'[LUI:Examples] Exit {Process.returncode}: {Process.stdout} {Process.stderr}')
    return Process.stdout + Process.stderr


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--sdk', type=Path, required=True)
    Args = Parser.parse_args()
    Sdk = Args.sdk.resolve()
    Inventory = json.loads((Sdk / 'SdkManifest.json').read_text(encoding='utf-8'))
    Cli = Sdk / Inventory['Components']['Cli']
    Library = ctypes.CDLL(str(Sdk / Inventory['Components']['Runtime']))
    Bind(Library)
    Definition = importlib.util.spec_from_file_location('Examples', Root / 'scripts/Stage-LibraryExample.py')
    Examples = importlib.util.module_from_spec(Definition)
    Definition.loader.exec_module(Examples)
    with tempfile.TemporaryDirectory(prefix='lui-runtime-examples-') as Temp:
        for Name in Examples.RuntimeExamples:
            Project = Examples.Stage(Root, Name, Path(Temp) / Name)
            Manifest = Project / 'lui.json'
            Command(Cli, ['check', Manifest, '--sdk', Sdk])
            Command(Cli, ['stage', Manifest, '--sdk', Sdk, '--output', Project.parent / (Name + '-stage')])
            Command(Cli, ['verify', Project.parent / (Name + '-stage'), '--sdk', Sdk])
            if Name in ('file-inspector', 'parallel-viewer'):
                # Native interaction is exercised by the desktop accessibility tests.
                continue
            if Name == 'message-viewer':
                with (Project/'Src/Main.luau').open('a',encoding='utf-8') as Entry:
                    Entry.write("""\nassert(string.find(Output.Text, 'bytes', 1, true))
Input.Text='invalid'
assert(Output.Text=='Enter a supported JSON document.')
Input.Text='{"x":1}'
assert(Output.Text=='12 bytes · {"x":1}')
Window:Destroy()
""")
                Execute(Library,Manifest)
                continue
            if Name == 'timestamp-viewer':
                # Exercise the genuine entry and reflected text-change signal in the
                # native runtime without opening a desktop window on the operator.
                with (Project / 'Src/Main.luau').open('a', encoding='utf-8') as Entry:
                    Entry.write('\nassert(Output.Text == "2024-02-29T12:45:09Z")\n'
                                'Input.Text = "invalid"\n'
                                'assert(Output.Text == "Enter a timestamp with Z or a signed UTC offset.")\n'
                                'Input.Text = "1970-01-01T05:30:00+05:30"\n'
                                'assert(Output.Text == "1970-01-01T00:00:00Z")\n'
                                'Window:Destroy()\n')
                Execute(Library, Manifest)
                continue
            Arguments = ['run', Manifest, '--sdk', Sdk, '--mode', 'headless', '--diagnostics', 'development']
            Output = Command(Cli, Arguments)
            assert 'Exit=0 NativeResources=0' in Output
            if Name == 'asset-report':
                Line = next(Line for Line in Output.splitlines() if '[LUI:AssetReport] ' in Line)
                Rows = json.loads(Line.split('[LUI:AssetReport] ', 1)[1])
                assert len(Rows) == 2 and Rows[0] == {'Name': 'sun rise.png', 'Extension': '.png',
                    'Link': '/download/sun%20rise.png', 'Modified': '2024-02-29T12:45:09Z', 'Tag': 'AP8='}
                assert Rows[1]['Modified'] == '2024-02-29T23:00:00Z'
            elif Name == 'app-tests':
                assert '[LUI:AppTests] Passed 2 cases' in Output
                Entry = Project / 'Src/Main.luau'
                Entry.write_text(Entry.read_text(encoding='utf-8').replace('"AP8="', '"wrong"'), encoding='utf-8')
                Command(Cli, Arguments, Expected=1)
    print('[LUI:Examples] Check, stage and verify passed for the runtime examples; '
          'headless outputs, failing exit and native runtime text changes passed')


if __name__ == '__main__':
    Main()
