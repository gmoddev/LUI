#!/usr/bin/env python3
"""Capture a standalone library example with explicit vendored module pins."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def GetFiles(Package):
    Catalog = json.loads((Package / 'Library.json').read_text(encoding='utf-8'))
    Modules = [Module['Name'] + '.luau' for Module in Catalog['Modules']]
    Support = Catalog.get('SupportFiles', [])
    if not isinstance(Support, list):
        raise ValueError('Invalid library support files')
    Files = Modules + Support
    if not Files or any(not isinstance(File, str) or not re.fullmatch(r'[A-Z][A-Za-z]+\.luau', File) for File in Files) or len(Files) != len({File.lower() for File in Files}):
        raise ValueError('Invalid library file catalog')
    if set(Files) != {File.name for File in Package.glob('*.luau')}:
        raise ValueError('Library source/catalog mismatch')
    return Catalog, sorted(Files)


def Stage(Root, Name, Output):
    if Name not in ('commands', 'workbench', 'json-client', 'numeric-input'):
        raise ValueError('Unknown library example')
    Source = Root / 'examples' / Name / 'Src'
    if not Source.is_dir() or Output.exists():
        raise ValueError('Example missing or output already exists')
    Output.mkdir(parents=True)
    Guide = Root / 'examples' / Name / 'README.md'
    if Guide.is_file():
        (Output / 'README.md').write_bytes(Guide.read_bytes())
    AppFiles = []
    for File in sorted(Source.glob('*.luau')):
        Target = Output / 'Src' / File.name
        Target.parent.mkdir(exist_ok=True)
        Target.write_bytes(File.read_bytes())
        AppFiles.append(File.name)
    Dependencies, Packages = [], []
    Sources = [('LuiWorkbench', Root / 'libraries/LuiWorkbench')]
    if Name == 'json-client':
        Sources = [('Json', Root / 'libraries/Json'), ('JsonClient', Root / 'libraries/JsonClient')]
    if Name == 'numeric-input':
        Sources = [('LuiUI', Root / 'libraries/LuiUI')]
    if Name == 'workbench':
        Sources.append(('LuiUI', Root / 'examples/conversation/Vendor/LuiUI'))
    for Alias, Package in Sources:
        Files, Pins = [], []
        Requires = []
        if (Package / 'Library.json').is_file():
            Catalog, ModuleFiles = GetFiles(Package)
            Requires = Catalog.get('Requires', [])
        else:
            ModuleFiles = ['Scope.luau', 'State.luau', 'VirtualList.luau']
        for File in [Package / Name for Name in sorted(ModuleFiles)]:
            Bytes = File.read_bytes()
            Target = Output / 'Vendor' / Alias / File.name
            Target.parent.mkdir(parents=True, exist_ok=True)
            Target.write_bytes(Bytes)
            Files.append(File.name)
            Pins.append({'Path': File.name, 'Sha256': hashlib.sha256(Bytes).hexdigest()})
        Dependencies.append({'Alias': Alias, 'Root': 'Vendor/' + Alias, 'Files': Files, 'Requires': Requires})
        Packages.append({'Alias': Alias, 'Provenance': {'Kind': 'LocalVendor', 'Id': Alias + '-0.1.0-preview.2'}, 'Files': Pins})
    Manifest = {'SchemaVersion': 2, 'Script': 'Src/Main.luau', 'Capabilities': ['Storage'] if Name == 'workbench' else [], 'Extensions': [],
        'Assets': [], 'ExtensionDeclarations': [], 'Diagnostics': {'Mode': 'Development'},
        'Sandbox': {'MaxMemoryBytes': 67108864, 'MaxInterrupts': 1000000},
        'Modules': {'Root': 'Src', 'Files': AppFiles, 'Requires': [D['Alias'] for D in Dependencies],
            'Lock': 'Modules.lock.json', 'Dependencies': Dependencies}}
    if Name == 'workbench':
        Manifest['Storage'] = {'ApplicationId': 'lui.example.workbench'}
    if Name == 'json-client':
        Manifest['Capabilities'] = ['network.client', 'network.server']
    for Name, Data in [('lui.json', Manifest), ('Modules.lock.json', {'SchemaVersion': 1, 'Packages': Packages})]:
        (Output / Name).write_text(json.dumps(Data, indent=2) + '\n', encoding='utf-8', newline='\n')
    return Output


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--example', choices=['commands', 'workbench', 'json-client', 'numeric-input'], required=True)
    Parser.add_argument('--output', type=Path, required=True)
    Args = Parser.parse_args()
    Stage(Path(__file__).resolve().parents[1], Args.example, Args.output.resolve())
    print('[LUI:Examples] Standalone project staged')
