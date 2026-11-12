#!/usr/bin/env python3
"""Carry exact source and managed dependency notices into a Windows host."""

import argparse
import json
import os
from pathlib import Path
import re
import xml.etree.ElementTree as Xml


def Collect(Root, NativeBuild, ManagedRoot, Nuget=None):
    DependenciesFiles = sorted(ManagedRoot.rglob('*.deps.json')) if ManagedRoot.is_dir() else []
    if not DependenciesFiles:
        raise ValueError('Missing managed host dependency manifests')
    Files = {File.relative_to(Root / 'third_party/licenses').as_posix(): File.read_bytes()
             for File in (Root / 'third_party/licenses').rglob('*') if File.is_file()}
    Files['LUI.txt'] = (Root / 'LICENSE').read_bytes()
    Cache = (NativeBuild / 'CMakeCache.txt').read_text(encoding='utf-8')

    def DependencyRoot(Key, Default):
        Match = re.search(r'^' + re.escape(Key) + r':[^=\n]+=(.+)$', Cache, re.MULTILINE)
        return Path(Match[1].strip()) if Match and Match[1].strip() else NativeBuild / Default

    for Name, Paths in {
        'Luau.txt': [DependencyRoot('LUAU_SOURCE_DIR', '_deps/luau-src') / 'LICENSE.txt'],
        'Asio.txt': [DependencyRoot('FETCHCONTENT_SOURCE_DIR_ASIO', '_deps/asio-src') / 'asio/LICENSE_1_0.txt',
                     DependencyRoot('FETCHCONTENT_SOURCE_DIR_ASIO', '_deps/asio-src') / 'LICENSE_1_0.txt'],
    }.items():
        Source = next((File for File in Paths if File.is_file()), None)
        if Source is None:
            raise ValueError('Missing dependency notice: ' + Name)
        Files[Name] = Source.read_bytes()

    Nuget = Path(Nuget or os.environ.get('NUGET_PACKAGES') or Path.home() / '.nuget/packages')
    Managed = set()
    for Dependencies in DependenciesFiles:
        Data = json.loads(Dependencies.read_text(encoding='utf-8'))
        for Identity, Value in Data.get('libraries', {}).items():
            if Value.get('type') == 'runtimepack' and Identity.startswith('runtimepack.'):
                Managed.add(Identity.removeprefix('runtimepack.').lower())
            elif Value.get('type') == 'package' and 'path' in Value:
                Managed.add(Value['path'].lower())
    for Name in sorted(Managed):
        if not re.fullmatch(r'[a-z0-9_.-]+/[a-z0-9_.+-]+', Name):
            raise ValueError('Invalid managed dependency identity: ' + Name)
        Source = Nuget / Name
        if not Source.is_dir():
            raise ValueError('Missing managed dependency provenance: ' + Name)
        Count = 0
        for File in Source.rglob('*'):
            if File.is_file() and File.name.lower().startswith(('license', 'notice', 'third-party', 'thirdparty', 'copyright')):
                Files['Managed/' + Name + '/' + File.relative_to(Source).as_posix()] = File.read_bytes()
                Count += 1
        Nuspec = Source / (Name.split('/')[0] + '.nuspec')
        if not Nuspec.is_file():
            raise ValueError('Missing managed license metadata: ' + Name)
        Metadata = {}
        for Element in Xml.parse(Nuspec).getroot().iter():
            Tag = Element.tag.rsplit('}', 1)[-1]
            if Tag in ('id', 'version', 'authors', 'copyright', 'license', 'licenseUrl'):
                Metadata[Tag] = {'Value': Element.text or '', **Element.attrib}
        if (Metadata.get('id', {}).get('Value', '').lower() + '/' + Metadata.get('version', {}).get('Value', '').lower()) != Name:
            raise ValueError('Mismatched managed license metadata: ' + Name)
        if Count == 0 and not any(Metadata.get(Key, {}).get('Value') for Key in ('license', 'licenseUrl')):
            raise ValueError('Missing managed license material: ' + Name)
        Files['Managed/' + Name + '/LicenseMetadata.json'] = (json.dumps(Metadata, indent=2) + '\n').encode('utf-8')
    return Files, sorted(Managed)


def Stage(Files, Destinations):
    # Refuse collisions before changing any destination; retain existing vendor notices.
    for Destination in Destinations:
        for Name, Data in Files.items():
            Target = Destination / Name
            Current = Target.parent
            while Current != Destination.parent:
                if Current.exists() and not Current.is_dir():
                    raise ValueError('Conflicting notice directory: ' + str(Current))
                Current = Current.parent
            if Target.exists() and (not Target.is_file() or Target.read_bytes() != Data):
                raise ValueError('Conflicting dependency notice: ' + Name)
    for Destination in Destinations:
        for Name, Data in Files.items():
            Target = Destination / Name
            Target.parent.mkdir(parents=True, exist_ok=True)
            Target.write_bytes(Data)


if __name__ == '__main__':
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    Parser.add_argument('--native-build', type=Path, required=True)
    Parser.add_argument('--host', type=Path, required=True)
    Arguments = Parser.parse_args()
    Files, Managed = Collect(Arguments.root, Arguments.native_build, Arguments.host)
    Stage(Files, [Arguments.host / 'THIRD-PARTY-LICENSES'])
    print('[LUI:Package] Staged dependency notices for ' + str(len(Managed)) + ' managed dependencies')
