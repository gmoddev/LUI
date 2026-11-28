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


RuntimeExamples = {
    'dock-workspace': ('LuiWorkbench', 'Json'),
    'dock-editor': ('LuiWorkbench',),
    'asset-report': ('Config', 'Encoding', 'Json', 'Path', 'Utc'),
    'timestamp-viewer': ('Utc',),
    'parallel-viewer': ('Workers', 'Messages', 'Json'),
    'message-viewer': ('Messages','Json'),
    'file-inspector': ('Encoding', 'LuiWorkbench'),
    'app-tests': ('Spec', 'Encoding', 'Binary'),
}
ExampleNames = ('commands', 'workbench', 'json-client', 'numeric-input', 'command-shortcuts', 'selected-files', *RuntimeExamples)
HostExamples = {'tool-session': ('Stdio','Process','Json'), 'tool-pipeline': ('Stdio','Process','Json'), 'spec-cli': ('Args','Spec','Random','Json','Stdio'), 'source-inspect': ('Language','Process','Json','Stdio'), 'parallel-data': ('Workers','Messages','Json','Stdio'), 'folder-copy': ('LuiWorkbench','Stdio'), 'tool-check': ('Stdio','Process','Json'), 'context-report': ('Stdio','Context'), 'pipe-copy': ('Stdio',), 'atomic-copy': ('Stdio','LuiWorkbench'), 'file-summary': ('LuiWorkbench', 'Stdio'), 'json-lines': ('Stdio','Json')}
ExampleNames += tuple(HostExamples)
ProviderExamples = {'codec': ('Compression',), 'digest': ('Crypto',), 'records': ('SQLite',), 'live-records': ('SQLite',), 'regex-filter': ('Regex',), 'toml-config': ('Toml',)}
ExampleNames += tuple(ProviderExamples)


FfiExamples = ('ffi-platform', 'ffi-sqlite', 'ffi-callback', 'ffi-workbench', 'ffi-isolated')
ExampleNames += FfiExamples


def Stage(Root, Name, Output, Sdk=None, Fixture=None):
    if Name not in ExampleNames:
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
    if Name in RuntimeExamples:
        Sources = [(Alias, Root / 'libraries' / Alias) for Alias in RuntimeExamples[Name]]
    if Name in HostExamples:
        Sources = [(Alias, Root / 'libraries' / Alias) for Alias in HostExamples[Name]]
    if Name in FfiExamples:
        if Sdk is None: raise ValueError('Select the experimental FFI SDK')
        Names = ['Ffi', 'Json', 'Encoding'] if Name != 'ffi-isolated' else ['FfiBroker', 'Process', 'Json']
        if Name != 'ffi-workbench': Names.append('Stdio')
        Sources = [(Alias, Sdk / 'Libraries' / Alias) for Alias in Names]
    if Name in ProviderExamples:
        if Sdk is None: raise ValueError('Select the SDK containing the matching provider images')
        Sources = [(Alias, Root / 'libraries' / Alias) for Alias in ('Stdio', *ProviderExamples[Name])]
        if Name == 'toml-config':
            Sources.extend((Alias,Root/'libraries'/Alias) for Alias in ('Json','Config'))
        if Name == 'digest': Sources.append(('Encoding', Root / 'libraries/Encoding'))
        if Name in ('records','live-records'): Sources.append(('Binary', Root / 'libraries/Binary'))
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
            if Alias == 'LuiWorkbench':
                Desktop = {'DockModel.luau', 'DockHost.luau', 'WindowMetrics.luau', 'Presentation.luau'}
                Used = {'dock-workspace': Desktop, 'dock-editor': {'DockModel.luau', 'DockHost.luau'}}.get(Name,set())
                ModuleFiles = [File for File in ModuleFiles if File not in Desktop or File in Used]
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
        Version = Catalog['TargetSdk'] if (Package / 'Library.json').is_file() else 'private-workbench-support'
        Packages.append({'Alias': Alias, 'Provenance': {'Kind': 'LocalVendor', 'Id': Alias + '-' + Version}, 'Files': Pins})
    Manifest = {'SchemaVersion': 2, 'Script': 'Src/Main.luau', 'Capabilities': ['Storage'] if Name == 'workbench' else [], 'Extensions': [],
        'Assets': [], 'ExtensionDeclarations': [], 'Diagnostics': {'Mode': 'Development'},
        'Sandbox': {'MaxMemoryBytes': 67108864, 'MaxInterrupts': 1000000},
        'Modules': {'Root': 'Src', 'Files': AppFiles, 'Requires': [D['Alias'] for D in Dependencies],
            'Lock': 'Modules.lock.json', 'Dependencies': Dependencies}}
    if Name == 'workbench':
        Manifest['Storage'] = {'ApplicationId': 'lui.example.workbench'}
    if Name == 'json-client':
        Manifest['Capabilities'] = ['network.client', 'network.server']
    if Name in ('selected-files', 'file-inspector'):
        Manifest['Capabilities'] = ['Dialogs']
    if Name in HostExamples or Name in ProviderExamples or Name in FfiExamples:
        Manifest['Capabilities'] = ['HostServices']
        if Name in ('file-summary','atomic-copy','folder-copy'): Manifest['Capabilities'].append('Dialogs')
        for Alias, Package in Sources:
            if (Package / 'Host.json').is_file():
                Relative = Alias + '.json'
                (Output / Relative).write_bytes((Package / 'Host.json').read_bytes())
                Manifest['ExtensionDeclarations'].append(Relative)
        Providers = ProviderExamples.get(Name, ())
        if Name in FfiExamples and Name != 'ffi-isolated': Providers = ('Ffi',)
        if Providers:
            # Native-project manifests use the runtime's bounded default Luau profile.
            del Manifest['Sandbox']
            Manifest['Capabilities'].append('NativeExtensions')
            if Name == 'records':
                Manifest['Capabilities'].append('Storage')
                Manifest['Storage'] = {'ApplicationId': 'lui.example.records'}
            for Provider in Providers:
                Package = Sdk / 'Libraries' / Provider
                Declaration = Provider + '.json'
                Captured = (Package / Declaration).read_bytes()
                Data = json.loads(Captured)
                Bindings = Data['Extension']['Bindings']
                if len(Bindings) != 1: raise ValueError('Expected one target binding')
                Binding = Bindings[0]
                Expected = {'WindowsX64': 'Native/Lui' + Provider + '.dll', 'LinuxX64': 'Native/libLui' + Provider + '.so'}
                PathName = Binding['Path']
                if Binding['Platform'] not in Expected or PathName != Expected[Binding['Platform']]:
                    raise ValueError('Invalid standard provider binding')
                Image = (Package / PathName).read_bytes()
                if hashlib.sha256(Image).hexdigest() != Binding['Sha256']:
                    raise ValueError('Provider image differs from its declaration')
                (Output / 'Native').mkdir(exist_ok=True)
                (Output / PathName).write_bytes(Image)
                (Output / Declaration).write_bytes(Captured)
                if Provider == 'Ffi':
                    Notice = 'Libffi-LICENSE.txt'
                    (Output / Notice).write_bytes((Package / Notice).read_bytes())
                    Manifest['Assets'].append(Notice)
                if Provider == 'Compression':
                    for Notice in ('Zlib-LICENSE.txt', 'Zstd-LICENSE.txt'):
                        (Output / Notice).write_bytes((Package / Notice).read_bytes())
                        Manifest['Assets'].append(Notice)
                if Provider in ('Regex','Toml'):
                    Notice=Provider+'-LICENSE.txt'
                    (Output / Notice).write_bytes((Package / Notice).read_bytes())
                    Manifest['Assets'].append(Notice)
                Manifest['ExtensionDeclarations'].append(Declaration)
                Manifest['Extensions'].append(Provider)
    if Name in ('ffi-workbench', 'ffi-callback'):
        if Fixture is None or Fixture.name not in ('LuiFfiFixture.dll', 'libLuiFfiFixture.so'):
            raise ValueError('Select the target C fixture library')
        Content = Fixture.read_bytes()
        Relative = Fixture.name
        Target = Output / Relative
        Target.write_bytes(Content)
        Digest = hashlib.sha256(Content).hexdigest()
        (Output / 'Src/Binding.luau').write_text(
            '--!strict\nlocal Ffi = require("@Ffi/Ffi")\nlocal Binding = {}\n'
            + 'function Binding.Open(): Ffi.Library\n    return Ffi.Packaged("'
            + Fixture.name + '", "' + Digest + '")\nend\nreturn table.freeze(Binding)\n', encoding='utf-8')
        Manifest['Modules']['Files'].append('Binding.luau')
        Manifest['Assets'].append(Relative)
    for Name, Data in [('lui.json', Manifest), ('Modules.lock.json', {'SchemaVersion': 1, 'Packages': Packages})]:
        (Output / Name).write_text(json.dumps(Data, indent=2) + '\n', encoding='utf-8', newline='\n')
    return Output


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--example', choices=ExampleNames, required=True)
    Parser.add_argument('--output', type=Path, required=True)
    Parser.add_argument('--sdk', type=Path)
    Parser.add_argument('--fixture', type=Path)
    Args = Parser.parse_args()
    Stage(Path(__file__).resolve().parents[1], Args.example, Args.output.resolve(), Args.sdk, Args.fixture)
    print('[LUI:Examples] Standalone project staged')
