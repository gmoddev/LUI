#!/usr/bin/env python3
"""Inventory a Windows preview SDK and package its offline VS Code extension."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as Xml
from zipfile import ZipFile, ZIP_DEFLATED


def Hash(File):
    with File.open('rb') as Stream:
        return hashlib.file_digest(Stream, 'sha256').hexdigest()


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
        Files = ('Extension.js', 'Inspector.html', 'InspectorProvider.js', 'NativePreview.js', 'Onboarding.js',
                 'PreviewClient.js', 'PreviewModel.js', 'ProjectProtocol.js', 'SdkClient.js', 'TypeCheckClient.js',
                 'TypeDiagnostics.js', 'package.json', 'media/icon.svg', 'walkthrough/SDK.md',
                 'walkthrough/PROJECT.md', 'walkthrough/RUN.md')
        for Name in Files:
            Archive.write(Root / 'vscode' / Name, 'extension/' + Name)
        Archive.write(Root / 'docs/SDK.md', 'extension/README.md')
        Archive.write(Root / 'LICENSE', 'extension/LICENSE.txt')


def Package(Root, Sdk, Version, NativeBuild):
    Editor = Sdk / 'Editor'
    Editor.mkdir()
    PackageExtension(Root, Editor / 'LUI.vsix')
    shutil.copyfile(Root / 'LICENSE', Sdk / 'LICENSE.txt')
    shutil.copyfile(Root / 'docs/SDK.md', Sdk / 'README.md')
    Notices = Sdk / 'ThirdPartyNotices'
    shutil.copytree(Root / 'third_party/licenses', Notices)
    Cache = (NativeBuild / 'CMakeCache.txt').read_text(encoding='utf-8')
    def DependencyRoot(Key, Default):
        Match = re.search(r'^' + re.escape(Key) + r':[^=\n]+=(.+)$', Cache, re.MULTILINE)
        return Path(Match[1].strip()) if Match and Match[1].strip() else NativeBuild / Default
    for Name, Paths in {
        'Luau.txt': [DependencyRoot('LUAU_SOURCE_DIR', '_deps/luau-src') / 'LICENSE.txt'],
        'Asio.txt': [DependencyRoot('FETCHCONTENT_SOURCE_DIR_ASIO', '_deps/asio-src') / 'asio/LICENSE_1_0.txt', DependencyRoot('FETCHCONTENT_SOURCE_DIR_ASIO', '_deps/asio-src') / 'LICENSE_1_0.txt'],
    }.items():
        Source = next((File for File in Paths if File.is_file()), None)
        if Source is None:
            raise ValueError('Missing dependency notice: ' + Name)
        shutil.copyfile(Source, Notices / Name)
    # Preserve license/notice material from the exact managed dependency versions.
    Nuget = Path.home() / '.nuget/packages'
    Managed = set()
    for Dependencies in Sdk.rglob('*.deps.json'):
        Data = json.loads(Dependencies.read_text(encoding='utf-8'))
        for Value in Data.get('libraries', {}).values():
            if Value.get('type') == 'package' and 'path' in Value:
                Managed.add(Value['path'])
    for Name in sorted(Managed):
        Source = Nuget / Name
        if not Source.is_dir():
            raise ValueError('Missing managed dependency provenance: ' + Name)
        for File in Source.rglob('*'):
            if File.is_file() and File.name.lower().startswith(('license', 'notice', 'third-party', 'thirdparty', 'copyright')):
                Destination = Notices / 'Managed' / Name / File.relative_to(Source)
                Destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(File, Destination)
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
    Arguments = Parser.parse_args()
    Package(Arguments.root, Arguments.sdk, Arguments.version, Arguments.native_build)
