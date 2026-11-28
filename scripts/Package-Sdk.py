#!/usr/bin/env python3
"""Inventory a desktop preview SDK and package its offline VS Code extension."""

import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path
import shutil
import stat
import tarfile
import xml.etree.ElementTree as Xml
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED


def Hash(File):
    with File.open('rb') as Stream:
        return hashlib.file_digest(Stream, 'sha256').hexdigest()


def StageOptionalLibraries(Root, Sdk, Version, Providers=None, Target=None, Pins=None):
    # A historical release rebuild must never acquire later development APIs.
    if Version == '0.1.0-preview.1':
        return
    Catalog = json.loads((Root / 'libraries/LuiWorkbench/Library.json').read_text(encoding='utf-8'))
    if Version != Catalog['TargetSdk']:
        raise ValueError('SDK version differs from source libraries; rebuild a historical SDK from its tagged source')
    Spec = importlib.util.spec_from_file_location('LibraryExamples', Root / 'scripts/Stage-LibraryExample.py')
    Examples = importlib.util.module_from_spec(Spec)
    Spec.loader.exec_module(Examples)
    Names = ['commands', 'workbench', 'json-client', 'numeric-input']
    if Version == '0.1.0-preview.3': Names.extend(('command-shortcuts', 'selected-files', *Examples.RuntimeExamples, *Examples.HostExamples))
    for Name in Names:
        Examples.Stage(Root, Name, Sdk / 'Examples' / Name)
    if Version == '0.1.0-preview.3':
        StageNativeExamples(Root, Sdk)
    for Name in ('LuiWorkbench', 'Json', 'JsonClient', 'LuiUI', 'Binary', 'Config', 'Encoding', 'Path', 'Spec', 'Utc', 'Stdio','Process','Context','Args','Messages','Workers','Language','Random'):
        Source = Root / 'libraries' / Name
        Catalog, ModuleFiles = Examples.GetFiles(Source)
        if Catalog['TargetSdk'] != Version:
            raise ValueError('Development library SDK mismatch')
        Library = Sdk / 'Libraries' / Name
        Library.mkdir(parents=True)
        Files = ['Library.json', 'README.md'] + ModuleFiles
        if (Source / 'Host.json').is_file(): Files.append('Host.json')
        for File in Files:
            shutil.copyfile(Source / File, Library / File)
    if Version == '0.1.0-preview.3' and Providers:
        StageStandardProviders(Root, Sdk, Providers, Target, Examples, Pins)


def GetStandardPins(Build):
    Cache = Build / 'CMakeCache.txt'
    if not Cache.is_file(): return {}
    Settings = Cache.read_text(encoding='utf-8')
    Options = dict(re.findall(r'^LUI_STANDARD_(\w+):(?:BOOL|UNINITIALIZED)=(.*)$', Settings, re.M))
    Selected = set()
    for Name in ('Crypto','Compression','SQLite','Regex','Toml'):
        Value = Options.get(Name.upper(), '').upper()
        if Value not in ('','0','OFF','NO','FALSE','N','IGNORE','NOTFOUND') and not Value.endswith('-NOTFOUND'):
            Selected.add(Name)
    Header = Build / 'StandardPins/Release/LuiStandardPins.h'
    if not Selected: return {}
    Pins = dict(re.findall(r'^#define LUI_PIN_(\w+) "([0-9a-f]{64})"$', Header.read_text(encoding='ascii'), re.M))
    if set(Pins) != Selected: raise ValueError('Headless provider pins differ from the configured selection')
    return Pins


def StageStandardProviders(Root, Sdk, Directory, Target, Examples, Pins):
    if Target not in ('WindowsX64', 'LinuxX64'): raise ValueError('Unknown standard provider target')
    for Name in ('Crypto', 'Compression', 'SQLite', 'Regex', 'Toml'):
        File = 'Lui' + Name + '.dll' if Target == 'WindowsX64' else 'libLui' + Name + '.so'
        Image = Directory / File
        if Pins is not None:
            if Name not in Pins: continue
            if not Image.is_file(): raise ValueError('Missing selected provider: ' + Name)
        elif not Image.is_file(): continue
        Source, Library = Root / 'libraries' / Name, Sdk / 'Libraries' / Name
        Relative = 'Native/' + File
        (Library / 'Native').mkdir(parents=True)
        shutil.copyfile(Image, Library / Relative)
        Digest = Hash(Library / Relative)
        if Pins is not None and Digest != Pins[Name]:
            raise ValueError('Mismatched selected provider: ' + Name)
        for Member in ('Library.json', 'README.md', Name + '.luau'):
            shutil.copyfile(Source / Member, Library / Member)
        if Name == 'Compression':
            shutil.copyfile(Source / 'Zlib-LICENSE.txt', Library / 'Zlib-LICENSE.txt')
            shutil.copyfile(Source / 'Zstd-LICENSE.txt', Library / 'Zstd-LICENSE.txt')
        if Name in ('Regex','Toml'):
            shutil.copyfile(Source / (Name+'-LICENSE.txt'),Library / (Name+'-LICENSE.txt'))
        Abi = 1 if Name in ('Crypto','Regex','Toml') else 2
        Declaration = {'SchemaVersion': Abi + 1, 'AbiVersion': Abi, 'Provider': 'Native',
            'Extension': {'Name': Name, 'Version': 1, 'RequiredCapabilities': ['HostServices', 'NativeExtensions'],
                'Provenance': {'Kind': 'LocalVendor', 'Id': 'LUI-standard-' + Name + '-1'},
                'Bindings': [{'Platform': Target, 'Path': Relative, 'Sha256': Digest}]},
            'Services': [json.loads((Source / 'Service.json').read_bytes())]}
        (Library / (Name + '.json')).write_text(json.dumps(Declaration, indent=2) + '\n', encoding='utf-8')
    for Example, Names in Examples.ProviderExamples.items():
        if all((Sdk / 'Libraries' / Name / (Name + '.json')).is_file() for Name in Names):
            Examples.Stage(Root, Example, Sdk / 'Examples' / Example, Sdk)


def StageNativeExamples(Root, Sdk):
    for Name in ('headless-websocket', 'websocket-echo'):
        shutil.copytree(Root / 'examples' / Name, Sdk / 'Examples' / Name,
                        ignore=shutil.ignore_patterns('__pycache__', '*.pyc', '*.pyo', '.lui', 'bin', 'obj'))


def PackageExtension(Root, Destination):
    Package = json.loads((Root / 'vscode/package.json').read_text(encoding='utf-8'))
    Namespace = 'http://schemas.microsoft.com/developer/vsx-schema/2011'
    Xml.register_namespace('', Namespace)
    def Element(Parent, Name, Attributes=None, Text=None):
        Child = Xml.SubElement(Parent, '{' + Namespace + '}' + Name, Attributes or {})
        Child.text = Text
        return Child
    Manifest = Xml.Element('{' + Namespace + '}PackageManifest', {'Version': '2.0.0'})
    Metadata = Element(Manifest, 'Metadata')
    Element(Metadata, 'Identity', {'Language': 'en-US', 'Id': Package['name'], 'Version': Package['version'], 'Publisher': Package['publisher']})
    Element(Metadata, 'DisplayName', Text=Package['displayName'])
    Element(Metadata, 'Description', Text=Package['description'])
    Properties = Element(Metadata, 'Properties')
    Element(Properties, 'Property', {'Id': 'Microsoft.VisualStudio.Code.Engine', 'Value': Package['engines']['vscode']})
    Element(Properties, 'Property', {'Id': 'Microsoft.VisualStudio.Code.ExtensionKind', 'Value': 'workspace'})
    Targets = Element(Manifest, 'Installation')
    Element(Targets, 'InstallationTarget', {'Id': 'Microsoft.VisualStudio.Code'})
    Element(Manifest, 'Dependencies')
    Assets = Element(Manifest, 'Assets')
    Element(Assets, 'Asset', {'Type': 'Microsoft.VisualStudio.Code.Manifest', 'Path': 'extension/package.json', 'Addressable': 'true'})
    with ZipFile(Destination, 'x', ZIP_DEFLATED, strict_timestamps=False) as Archive:
        def Write(Name, Bytes):
            Info = ZipInfo(Name)
            Info.compress_type = ZIP_DEFLATED
            Info.create_system = 3
            Info.external_attr = 0o100644 << 16
            Archive.writestr(Info, Bytes)
        Write('extension.vsixmanifest', Xml.tostring(Manifest, encoding='utf-8', xml_declaration=True))
        Write('[Content_Types].xml', '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="json" ContentType="application/json"/><Default Extension="js" ContentType="application/javascript"/><Default Extension="md" ContentType="text/markdown"/><Default Extension="html" ContentType="text/html"/><Default Extension="svg" ContentType="image/svg+xml"/><Default Extension="vsixmanifest" ContentType="text/xml"/><Default Extension="txt" ContentType="text/plain"/></Types>')
        # An exact payload avoids capturing local notes, settings or future unreviewed files.
        Files = ('Extension.js', 'Inspector.html', 'InspectorProvider.js', 'NativePreview.js', 'NativeDiagnostics.js', 'Onboarding.js', 'ProjectFiles.js',
                 'PreviewClient.js', 'PreviewModel.js', 'ProjectProtocol.js', 'ProjectSetup.js', 'SdkClient.js', 'TypeCheckClient.js',
                 'TypeDiagnostics.js', 'package.json', 'media/icon.svg', 'walkthrough/SDK.md',
                 'walkthrough/PROJECT.md', 'walkthrough/RUN.md')
        for Name in Files:
            Write('extension/' + Name, (Root / 'vscode' / Name).read_bytes())
        Write('extension/README.md', (Root / 'docs/SDK.md').read_bytes())
        Write('extension/LICENSE.txt', (Root / 'LICENSE').read_bytes())


def CompactHosts(Sdk, Target):
    Destination = Sdk / 'Cli'
    Sources = [Sdk / 'Preview']
    if Target == 'WindowsX64': Sources.append(Sdk / 'Native')
    elif Target != 'LinuxX64': raise ValueError('Unknown compact target')
    Entries = {}
    Pending = []
    for Directory in [Destination,*Sources]:
        if Directory.is_symlink() or getattr(Directory.lstat(),'st_file_attributes',0) & stat.FILE_ATTRIBUTE_REPARSE_POINT or not Directory.is_dir():
            raise ValueError('Unlinked host directory required')
        for File in sorted(Directory.rglob('*')):
            Info = File.lstat()
            if stat.S_ISLNK(Info.st_mode) or getattr(Info,'st_file_attributes',0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                raise ValueError('Linked host entry')
            Name = File.relative_to(Directory).as_posix()
            Key = Name.casefold() if Target=='WindowsX64' else Name
            if stat.S_ISDIR(Info.st_mode): Record = (Name,'directory',None)
            elif stat.S_ISREG(Info.st_mode): Record = (Name,Hash(File),Info.st_mode & 0o111 if Target=='LinuxX64' else None)
            else: raise ValueError('Invalid host entry')
            if Key in Entries and Record != Entries[Key]: raise ValueError('Conflicting shared host entry: '+Name)
            if Directory != Destination and Key not in Entries: Pending.append((File,Destination/Name))
            Entries[Key] = Record
    # The SDK builder supplies fresh private publish directories. Copy all entries before retiring a source.
    for File,Output in Pending:
        if File.is_dir(): Output.mkdir(parents=True,exist_ok=True)
        else: shutil.copy2(File,Output)
    for Directory in Sources: shutil.rmtree(Directory)


def DesktopInputs(Sdk):
    Native=Sdk/'Native'
    return sorted(F.relative_to(Native).as_posix() for F in Native.rglob('*')
        if F.is_file() and F.suffix.lower()!='.pdb' and F.relative_to(Native).as_posix() not in ('bin/LuiHeadlessHost','LuiHeadlessHost.exe')
        and not F.relative_to(Native).as_posix().startswith('THIRD-PARTY-LICENSES/'))


def WriteHostFiles(Sdk,Target,Desktop):
    Windows=Target=='WindowsX64'
    Host=Sdk/('Cli' if Windows else 'Native')
    Notices=sorted(F.relative_to(Host).as_posix() for F in (Host/'THIRD-PARTY-LICENSES').rglob('*') if F.is_file())
    NativeNotices=[N for N in Notices if not N.startswith('THIRD-PARTY-LICENSES/Managed/')]
    Headless=['LuiHeadlessHost.exe','LuiRuntime.dll','msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll'] if Windows else ['bin/LuiHeadlessHost','lib/libLuiRuntime.so']
    Profiles={'Desktop':sorted(set(Desktop+Notices)),'Headless':sorted(set(Headless+NativeNotices))}
    if not NativeNotices or any(not (Host/N).is_file() for Values in Profiles.values() for N in Values):
        raise ValueError('Incomplete distributable host profile')
    (Host/'HostFiles.json').write_text(json.dumps(Profiles,indent=2)+'\n',encoding='utf-8')


def CompactComponents(Components, Target):
    Result = dict(Components)
    for Key in ('PreviewHost','NativeHost','HeadlessHost','Runtime'):
        if Key=='PreviewHost' or Target=='WindowsX64':
            Result[Key]='Cli/'+Path(Result[Key]).name
    return Result


def Package(Root, Sdk, Version, NativeBuild, CppRuntime=None, Target='WindowsX64'):
    if Target not in ('WindowsX64','LinuxX64'):
        raise ValueError('Unsupported SDK target')
    if Target=='LinuxX64' and Version!='0.1.0-preview.3':
        raise ValueError('Linux SDK candidate cannot use a historical version label')
    Metadata=None
    if Target=='WindowsX64':
        if CppRuntime is None:raise ValueError('Windows SDK requires pinned C++ runtime')
        Spec = importlib.util.spec_from_file_location('WindowsCppRuntime', Root / 'scripts/Stage-WindowsCppRuntime.py')
        Runtime = importlib.util.module_from_spec(Spec)
        Spec.loader.exec_module(Runtime)
        Metadata = Runtime.Stage(Root, CppRuntime, [Sdk / Name for Name in ('Cli', 'Tools', 'Preview', 'Native')])
        (Sdk / 'CppRuntime.json').write_text(json.dumps(Metadata, indent=2) + '\n', encoding='utf-8')
    if Version=='0.1.0-preview.3':
        Desktop=DesktopInputs(Sdk)
        CompactHosts(Sdk,Target)
    StageOptionalLibraries(Root, Sdk, Version, NativeBuild / 'StandardProviders/Release', Target, GetStandardPins(NativeBuild))
    Editor = Sdk / 'Editor'
    Editor.mkdir()
    PackageExtension(Root, Editor / 'LUI.vsix')
    shutil.copyfile(Root / 'LICENSE', Sdk / 'LICENSE.txt')
    Guide='docs/SDK-LINUX-CANDIDATE-3.md' if Target=='LinuxX64' else 'docs/SDK-CANDIDATE-3.md' if Version=='0.1.0-preview.3' else 'docs/SDK.md'
    shutil.copyfile(Root / Guide, Sdk / 'README.md')
    if Version == '0.1.0-preview.3':
        (Sdk / 'ReleaseNotes.md').write_text('Unpublished preview.3 qualification candidate. See README.md for scope and remaining gates.\n', encoding='utf-8')
    if Version == '0.1.0-preview.2':
        Notes = (Root / 'docs/SDK-PREVIEW-2.md').read_text(encoding='utf-8')
        (Sdk / 'ReleaseNotes.md').write_text(Notes.replace('](SDK.md)', '](README.md)'), encoding='utf-8')
    Spec = importlib.util.spec_from_file_location('WindowsNotices', Root / 'scripts/Stage-WindowsNotices.py')
    Notices = importlib.util.module_from_spec(Spec)
    Spec.loader.exec_module(Notices)
    Files, Managed = Notices.Collect(Root, NativeBuild, Sdk)
    if Metadata is not None:Files['VisualCppRuntime.json'] = (json.dumps(Metadata, indent=2) + '\n').encode('utf-8')
    AppHost = 'Cli' if Version=='0.1.0-preview.3' and Target=='WindowsX64' else 'Native'
    Notices.Stage(Files, [Sdk / 'ThirdPartyNotices', Sdk / AppHost / 'THIRD-PARTY-LICENSES'])
    if Version=='0.1.0-preview.3': WriteHostFiles(Sdk,Target,Desktop)
    shutil.copyfile(Root / 'cmake/Dependencies.json', Sdk / 'Dependencies.json')
    (Sdk / 'ManagedDependencies.json').write_text(json.dumps(sorted(Managed), indent=2) + '\n', encoding='utf-8')
    if Target=='WindowsX64':
        (Sdk / 'lui.cmd').write_text('@echo off\r\n"%~dp0Cli\\lui.exe" %*\r\n', encoding='utf-8')
    else:
        Launcher=Sdk/'lui'
        Launcher.write_text('#!/bin/sh\nset -eu\nRoot=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexec "$Root/Cli/lui" "$@"\n',encoding='utf-8')
        Launcher.chmod(0o755)
        (Sdk/'LinuxRequirements.json').write_text(json.dumps({'SchemaVersion':1,'Target':'LinuxX64',
            'QualificationProfile':'Ubuntu24.04 x64, GTK4.14, X11; candidate qualification recorded separately',
            'SystemDependencies':['GTK >=4.10','OpenSSL3','glibc/libstdc++ compatible with the qualification profile',
                                  '.NET self-contained native prerequisites (ICU, zlib, libgcc)'],
            'BundledDotNet':True,'AppImage':False},indent=2)+'\n',encoding='utf-8')
    # Symbols and build-machine debugging data are not part of the developer SDK.
    for File in Sdk.rglob('*.pdb'):
        File.unlink()
    Files = {File.relative_to(Sdk).as_posix(): Hash(File) for File in sorted(Sdk.rglob('*')) if File.is_file()}
    Components = {'Cli': 'Cli/lui.exe', 'Checker': 'Tools/LuiTypeCheck.exe', 'Definitions': 'Tools/LUI.d.luau',
                  'Runtime': 'Native/LuiRuntime.dll', 'PreviewHost': 'Preview/lui-preview-host.exe',
                  'NativeHost': 'Native/Lui.WinUI.exe', 'ProjectTool': 'Tools/LuiProjectTool.exe'}
    if Version == '0.1.0-preview.3': Components['HeadlessHost'] = 'Native/LuiHeadlessHost.exe'
    if Target=='LinuxX64':
        Components={'Cli':'Cli/lui','Checker':'Tools/LuiTypeCheck','Definitions':'Tools/LUI.d.luau',
                    'Runtime':'Native/lib/libLuiRuntime.so','PreviewHost':'Preview/lui-preview-host',
                    'NativeHost':'Native/bin/lui-gtk','ProjectTool':'Tools/LuiProjectTool'}
        if Version=='0.1.0-preview.3':Components['HeadlessHost']='Native/bin/LuiHeadlessHost'
    if Version=='0.1.0-preview.3': Components=CompactComponents(Components,Target)
    if not set(Components.values()) <= set(Files):
        raise ValueError('Incomplete SDK component set')
    (Sdk / 'SdkManifest.json').write_text(json.dumps({'SchemaVersion': 1, 'SdkVersion': Version, 'Target': Target,
        'Components': Components, 'Files': Files}, indent=2) + '\n', encoding='utf-8')
    Archive = Path(str(Sdk) + ('.zip' if Target=='WindowsX64' else '.tar.gz'))
    if Target=='WindowsX64':
        with ZipFile(Archive, 'x', ZIP_DEFLATED, strict_timestamps=False) as Output:
            for File in sorted(Sdk.rglob('*')):
                if File.is_file():Output.write(File, Sdk.name + '/' + File.relative_to(Sdk).as_posix())
    else:
        with tarfile.open(Archive,'x:gz') as Output:Output.add(Sdk,arcname=Sdk.name)
    Path(str(Archive) + '.sha256').write_text(Hash(Archive) + '  ' + Archive.name + '\n', encoding='utf-8')


if __name__ == '__main__':
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--root', type=Path, required=True)
    Parser.add_argument('--sdk', type=Path, required=True)
    Parser.add_argument('--version', required=True)
    Parser.add_argument('--native-build', type=Path, required=True)
    Parser.add_argument('--cpp-runtime', type=Path)
    Parser.add_argument('--target',choices=('WindowsX64','LinuxX64'),default='WindowsX64')
    Arguments = Parser.parse_args()
    Package(Arguments.root, Arguments.sdk, Arguments.version, Arguments.native_build, Arguments.cpp_runtime, Arguments.target)
