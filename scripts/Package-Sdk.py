#!/usr/bin/env python3
"""Inventory a Windows preview SDK and package its offline VS Code extension."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tarfile
import xml.etree.ElementTree as Xml
from zipfile import ZipFile, ZIP_DEFLATED


def Hash(File):
    with File.open('rb') as Stream:
        return hashlib.file_digest(Stream, 'sha256').hexdigest()


def StageOptionalLibraries(Root, Sdk, Version):
    # A historical release rebuild must never acquire later development APIs.
    if Version == '0.1.0-preview.1':
        return
    Catalog = json.loads((Root / 'libraries/LuiWorkbench/Library.json').read_text(encoding='utf-8'))
    Contract = '0.1.0-preview.2' if Version == '0.1.0-preview.2b' else Version
    if Contract != Catalog['TargetSdk']:
        raise ValueError('SDK version differs from source libraries')
    Spec = importlib.util.spec_from_file_location('LibraryExamples', Root / 'scripts/Stage-LibraryExample.py')
    Examples = importlib.util.module_from_spec(Spec)
    Spec.loader.exec_module(Examples)
    for Name in ('commands', 'workbench', 'json-client', 'numeric-input'):
        Examples.Stage(Root, Name, Sdk / 'Examples' / Name)
    for Name in ('LuiWorkbench', 'Json', 'JsonClient', 'LuiUI'):
        Source = Root / 'libraries' / Name
        Catalog, ModuleFiles = Examples.GetFiles(Source)
        if Catalog['TargetSdk'] != Contract:
            raise ValueError('Development library SDK mismatch')
        Library = Sdk / 'Libraries' / Name
        Library.mkdir(parents=True)
        Files = ['Library.json', 'README.md'] + ModuleFiles
        for File in Files:
            shutil.copyfile(Source / File, Library / File)


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
        Archive.writestr('extension.vsixmanifest', Xml.tostring(Manifest, encoding='utf-8', xml_declaration=True))
        Archive.writestr('[Content_Types].xml', '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="json" ContentType="application/json"/><Default Extension="js" ContentType="application/javascript"/><Default Extension="md" ContentType="text/markdown"/><Default Extension="html" ContentType="text/html"/><Default Extension="svg" ContentType="image/svg+xml"/><Default Extension="vsixmanifest" ContentType="text/xml"/><Default Extension="txt" ContentType="text/plain"/></Types>')
        # An exact payload avoids capturing local notes, settings or future unreviewed files.
        Files = ('Extension.js', 'Inspector.html', 'InspectorProvider.js', 'NativePreview.js', 'NativeDiagnostics.js', 'Onboarding.js', 'ProjectFiles.js',
                 'PreviewClient.js', 'PreviewModel.js', 'ProjectProtocol.js', 'ProjectSetup.js', 'SdkClient.js', 'TypeCheckClient.js',
                 'TypeDiagnostics.js', 'package.json', 'media/icon.svg', 'walkthrough/SDK.md',
                 'walkthrough/PROJECT.md', 'walkthrough/RUN.md')
        for Name in Files:
            Archive.write(Root / 'vscode' / Name, 'extension/' + Name)
        Archive.write(Root / 'docs/SDK.md', 'extension/README.md')
        Archive.write(Root / 'LICENSE', 'extension/LICENSE.txt')


def CompactHosts(Sdk, Target):
    # Apphosts resolve their own assembly and runtime beside the executable.
    # Keep the existing Cli discovery path; metadata locates the other hosts.
    # Validate every collision before moving any file. Different bytes fail closed.
    Destination = Sdk / 'Cli'
    Sources = [Sdk / 'Preview'] + ([Sdk / 'Native'] if Target == 'WindowsX64' else [])
    Inventory = {}
    for Directory in [Destination, *Sources]:
        if Directory.is_symlink() or not Directory.is_dir():
            raise ValueError('Unlinked host directories required')
        for File in Directory.rglob('*'):
            if File.is_symlink(): raise ValueError('Linked host input')
            if not File.is_file(): continue
            Name = File.relative_to(Directory).as_posix()
            Digest = Hash(File)
            if Name in Inventory and Inventory[Name] != Digest:
                raise ValueError('Conflicting shared host file: ' + Name)
            Inventory[Name] = Digest
    for Directory in Sources:
        for File in sorted(Directory.rglob('*')):
            if not File.is_file(): continue
            Output = Destination / File.relative_to(Directory)
            if not Output.exists():
                Output.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(File, Output)
            File.unlink()
        for Folder in sorted(Directory.rglob('*'), key=lambda Item: len(Item.parts), reverse=True):
            if Folder.is_dir(): Folder.rmdir()
        Directory.rmdir()


def Package(Root, Sdk, Version, NativeBuild, CppRuntime=None, Target='WindowsX64'):
    if Target not in ('WindowsX64', 'LinuxX64'): raise ValueError('Unsupported SDK target')
    if Target == 'LinuxX64' and Version != '0.1.0-preview.2b': raise ValueError('Linux requires preview.2b')
    Metadata = None
    if Target == 'WindowsX64':
        Spec = importlib.util.spec_from_file_location('WindowsCppRuntime', Root / 'scripts/Stage-WindowsCppRuntime.py')
        Runtime = importlib.util.module_from_spec(Spec)
        Spec.loader.exec_module(Runtime)
        Metadata = Runtime.Stage(Root, CppRuntime, [Sdk / Name for Name in ('Cli', 'Tools', 'Preview', 'Native')])
        (Sdk / 'CppRuntime.json').write_text(json.dumps(Metadata, indent=2) + '\n', encoding='utf-8')
    if Version == '0.1.0-preview.2b': CompactHosts(Sdk, Target)
    StageOptionalLibraries(Root, Sdk, Version)
    Editor = Sdk / 'Editor'
    Editor.mkdir()
    PackageExtension(Root, Editor / 'LUI.vsix')
    shutil.copyfile(Root / 'LICENSE', Sdk / 'LICENSE.txt')
    shutil.copyfile(Root / ('docs/SDK-PREVIEW-2B.md' if Version == '0.1.0-preview.2b' else 'docs/SDK.md'), Sdk / 'README.md')
    if Version == '0.1.0-preview.2b':
        (Sdk / 'ReleaseNotes.md').write_text('Preview.2b reduces SDK packaging size without adding preview.3 APIs. See README.md for setup and platform limits.\n', encoding='utf-8')
    if Version == '0.1.0-preview.2':
        Notes = (Root / 'docs/SDK-PREVIEW-2.md').read_text(encoding='utf-8')
        (Sdk / 'ReleaseNotes.md').write_text(Notes.replace('](SDK.md)', '](README.md)'), encoding='utf-8')
    Spec = importlib.util.spec_from_file_location('WindowsNotices', Root / 'scripts/Stage-WindowsNotices.py')
    Notices = importlib.util.module_from_spec(Spec)
    Spec.loader.exec_module(Notices)
    Files, Managed = Notices.Collect(Root, NativeBuild, Sdk)
    if Metadata is not None: Files['VisualCppRuntime.json'] = (json.dumps(Metadata, indent=2) + '\n').encode('utf-8')
    Host = Sdk / ('Cli' if Target == 'WindowsX64' and Version == '0.1.0-preview.2b' else 'Native')
    Notices.Stage(Files, [Sdk / 'ThirdPartyNotices', Host / 'THIRD-PARTY-LICENSES'])
    shutil.copyfile(Root / 'cmake/Dependencies.json', Sdk / 'Dependencies.json')
    (Sdk / 'ManagedDependencies.json').write_text(json.dumps(sorted(Managed), indent=2) + '\n', encoding='utf-8')
    if Target == 'WindowsX64':
        (Sdk / 'lui.cmd').write_text('@echo off\r\n"%~dp0Cli\\lui.exe" %*\r\n', encoding='utf-8')
    else:
        Launcher = Sdk / 'lui'
        Launcher.write_text('#!/bin/sh\nset -eu\nRoot=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexec "$Root/Cli/lui" "$@"\n', encoding='utf-8')
        Launcher.chmod(0o755)
        (Sdk / 'LinuxRequirements.json').write_text(json.dumps({'SchemaVersion': 1, 'Target': Target,
            'Profile': 'Experimental Ubuntu 24.04 x64, GTK 4.14, X11',
            'SystemDependencies': ['GTK >=4.10', 'OpenSSL3', 'compatible glibc/libstdc++', 'ICU', 'zlib', 'libgcc'],
            'BundledDotNet': True, 'AppImage': False}, indent=2) + '\n', encoding='utf-8')
    # Symbols and build-machine debugging data are not part of the developer SDK.
    for File in Sdk.rglob('*.pdb'):
        File.unlink()
    Files = {File.relative_to(Sdk).as_posix(): Hash(File) for File in sorted(Sdk.rglob('*')) if File.is_file()}
    Components = {'Cli': 'Cli/lui.exe', 'Checker': 'Tools/LuiTypeCheck.exe', 'Definitions': 'Tools/LUI.d.luau',
                  'Runtime': 'Native/LuiRuntime.dll', 'PreviewHost': 'Preview/lui-preview-host.exe',
                  'NativeHost': 'Native/Lui.WinUI.exe', 'ProjectTool': 'Tools/LuiProjectTool.exe'}
    if Version == '0.1.0-preview.2b':
        Components.update(Runtime='Cli/LuiRuntime.dll', PreviewHost='Cli/lui-preview-host.exe', NativeHost='Cli/Lui.WinUI.exe')
    if Target == 'LinuxX64':
        Components = {'Cli': 'Cli/lui', 'Checker': 'Tools/LuiTypeCheck', 'Definitions': 'Tools/LUI.d.luau',
            'Runtime': 'Native/lib/libLuiRuntime.so', 'PreviewHost': 'Cli/lui-preview-host',
            'NativeHost': 'Native/bin/lui-gtk', 'ProjectTool': 'Tools/LuiProjectTool'}
    if not set(Components.values()) <= set(Files):
        raise ValueError('Incomplete SDK component set')
    (Sdk / 'SdkManifest.json').write_text(json.dumps({'SchemaVersion': 1, 'SdkVersion': Version, 'Target': Target,
        'Components': Components, 'Files': Files}, indent=2) + '\n', encoding='utf-8')
    Archive = Path(str(Sdk) + ('.zip' if Target == 'WindowsX64' else '.tar.gz'))
    if Target == 'WindowsX64':
        with ZipFile(Archive, 'x', ZIP_DEFLATED, strict_timestamps=False) as Output:
            for File in sorted(Sdk.rglob('*')):
                if File.is_file(): Output.write(File, Sdk.name + '/' + File.relative_to(Sdk).as_posix())
    else:
        with tarfile.open(Archive, 'x:gz') as Output: Output.add(Sdk, arcname=Sdk.name)
    Path(str(Archive) + '.sha256').write_text(Hash(Archive) + '  ' + Archive.name + '\n', encoding='utf-8')


if __name__ == '__main__':
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--root', type=Path, required=True)
    Parser.add_argument('--sdk', type=Path, required=True)
    Parser.add_argument('--version', required=True)
    Parser.add_argument('--native-build', type=Path, required=True)
    Parser.add_argument('--cpp-runtime', type=Path)
    Parser.add_argument('--target', choices=('WindowsX64', 'LinuxX64'), default='WindowsX64')
    Arguments = Parser.parse_args()
    Package(Arguments.root, Arguments.sdk, Arguments.version, Arguments.native_build, Arguments.cpp_runtime, Arguments.target)
