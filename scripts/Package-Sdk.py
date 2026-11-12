#!/usr/bin/env python3
"""Inventory a Windows preview SDK and package its offline VS Code extension."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
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
    if Version != Catalog['TargetSdk']:
        return
    Spec = importlib.util.spec_from_file_location('LibraryExamples', Root / 'scripts/Stage-LibraryExample.py')
    Examples = importlib.util.module_from_spec(Spec)
    Spec.loader.exec_module(Examples)
    for Name in ('commands', 'workbench', 'json-client', 'numeric-input'):
        Examples.Stage(Root, Name, Sdk / 'Examples' / Name)
    for Name in ('LuiWorkbench', 'Json', 'JsonClient', 'LuiUI'):
        Source = Root / 'libraries' / Name
        Catalog, ModuleFiles = Examples.GetFiles(Source)
        if Catalog['TargetSdk'] != Version:
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


def Package(Root, Sdk, Version, NativeBuild, CppRuntime):
    Spec = importlib.util.spec_from_file_location('WindowsCppRuntime', Root / 'scripts/Stage-WindowsCppRuntime.py')
    Runtime = importlib.util.module_from_spec(Spec)
    Spec.loader.exec_module(Runtime)
    Metadata = Runtime.Stage(Root, CppRuntime, [Sdk / Name for Name in ('Cli', 'Tools', 'Preview', 'Native')])
    (Sdk / 'CppRuntime.json').write_text(json.dumps(Metadata, indent=2) + '\n', encoding='utf-8')
    StageOptionalLibraries(Root, Sdk, Version)
    Editor = Sdk / 'Editor'
    Editor.mkdir()
    PackageExtension(Root, Editor / 'LUI.vsix')
    shutil.copyfile(Root / 'LICENSE', Sdk / 'LICENSE.txt')
    shutil.copyfile(Root / 'docs/SDK.md', Sdk / 'README.md')
    if Version == '0.1.0-preview.2':
        Notes = (Root / 'docs/SDK-PREVIEW-2.md').read_text(encoding='utf-8')
        (Sdk / 'ReleaseNotes.md').write_text(Notes.replace('](SDK.md)', '](README.md)'), encoding='utf-8')
    Spec = importlib.util.spec_from_file_location('WindowsNotices', Root / 'scripts/Stage-WindowsNotices.py')
    Notices = importlib.util.module_from_spec(Spec)
    Spec.loader.exec_module(Notices)
    Files, Managed = Notices.Collect(Root, NativeBuild, Sdk)
    Files['VisualCppRuntime.json'] = (json.dumps(Metadata, indent=2) + '\n').encode('utf-8')
    Notices.Stage(Files, [Sdk / 'ThirdPartyNotices', Sdk / 'Native/THIRD-PARTY-LICENSES'])
    shutil.copyfile(Root / 'cmake/Dependencies.json', Sdk / 'Dependencies.json')
    (Sdk / 'ManagedDependencies.json').write_text(json.dumps(sorted(Managed), indent=2) + '\n', encoding='utf-8')
    (Sdk / 'lui.cmd').write_text('@echo off\r\n"%~dp0Cli\\lui.exe" %*\r\n', encoding='utf-8')
    # Symbols and build-machine debugging data are not part of the developer SDK.
    for File in Sdk.rglob('*.pdb'):
        File.unlink()
    Files = {File.relative_to(Sdk).as_posix(): Hash(File) for File in sorted(Sdk.rglob('*')) if File.is_file()}
    Components = {'Cli': 'Cli/lui.exe', 'Checker': 'Tools/LuiTypeCheck.exe', 'Definitions': 'Tools/LUI.d.luau',
                  'Runtime': 'Native/LuiRuntime.dll', 'PreviewHost': 'Preview/lui-preview-host.exe',
                  'NativeHost': 'Native/Lui.WinUI.exe', 'ProjectTool': 'Tools/LuiProjectTool.exe'}
    if not set(Components.values()) <= set(Files):
        raise ValueError('Incomplete SDK component set')
    (Sdk / 'SdkManifest.json').write_text(json.dumps({'SchemaVersion': 1, 'SdkVersion': Version, 'Target': 'WindowsX64',
        'Components': Components, 'Files': Files}, indent=2) + '\n', encoding='utf-8')
    Archive = Path(str(Sdk) + '.zip')
    with ZipFile(Archive, 'x', ZIP_DEFLATED, strict_timestamps=False) as Output:
        for File in sorted(Sdk.rglob('*')):
            if File.is_file():
                Output.write(File, Sdk.name + '/' + File.relative_to(Sdk).as_posix())
    Path(str(Archive) + '.sha256').write_text(Hash(Archive) + '  ' + Archive.name + '\n', encoding='utf-8')


if __name__ == '__main__':
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--root', type=Path, required=True)
    Parser.add_argument('--sdk', type=Path, required=True)
    Parser.add_argument('--version', required=True)
    Parser.add_argument('--native-build', type=Path, required=True)
    Parser.add_argument('--cpp-runtime', type=Path, required=True)
    Arguments = Parser.parse_args()
    Package(Arguments.root, Arguments.sdk, Arguments.version, Arguments.native_build, Arguments.cpp_runtime)
