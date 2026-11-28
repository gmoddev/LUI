"""Assemble an experimental FFI SDK from a verified compact SDK and newly built components."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

Version = '0.1.0-preview.4-ffi.1'
Baselines = {'WindowsX64': 'b2a8dda7319c41360a7e6e11aa6b01999a1abcf6a0c4907b5f7ba92f011b1d94',
             'LinuxX64': '25d4bc45d86372ea6a58ee92ee7fbfaac17276a54a7bb405a9ebbbdcc4c2087f'}
Root = Path(__file__).resolve().parents[1]


def Hash(File):
    with File.open('rb') as Stream:
        return hashlib.file_digest(Stream, 'sha256').hexdigest()


def Write(File, Data):
    File.write_text(json.dumps(Data, indent=2) + '\n', encoding='utf-8', newline='\n')


def Assemble(Base, Binaries, Output, Target):
    Windows = Target == 'WindowsX64'
    if Hash(Base) != Baselines[Target]:
        raise ValueError('Expected the verified preview.3 archive')
    Output.mkdir(exist_ok=True, parents=True)
    Name = 'LUI-' + Version + '-' + Target
    Suffix = '.zip' if Windows else '.tar.gz'
    if any((Output / (Name + S)).exists() for S in ('', Suffix, Suffix + '.sha256')):
        raise ValueError('Select a fresh destination')
    with tempfile.TemporaryDirectory(dir=Output) as Temporary:
        Stage = Path(Temporary)
        Modes = {}
        if Windows:
            with ZipFile(Base) as Archive:
                Archive.extractall(Stage)
        else:
            with tarfile.open(Base) as Archive:
                for Member in Archive:
                    if Member.isfile():
                        Modes['/'.join(Path(Member.name).parts[1:])] = Member.mode
                Archive.extractall(Stage, filter='data')
        Sdk = Stage / Name
        (Stage / ('LUI-0.1.0-preview.3-' + Target)).rename(Sdk)
        Inventory = json.loads((Sdk / 'SdkManifest.json').read_bytes())
        if Inventory['Target'] != Target or Inventory['SdkVersion'] != '0.1.0-preview.3':
            raise ValueError('Incorrect baseline')
        Changed = ['Cli/lui.dll', 'Cli/Lui.WinUI.dll', 'Cli/LuiRuntime.dll', 'Cli/LuiHeadlessHost.exe'] if Windows else [
            'Cli/lui.dll', 'Native/lib/libLuiRuntime.so', 'Native/bin/LuiHeadlessHost']
        for Relative in Changed:
            shutil.copyfile(Binaries / 'Hosts' / Relative, Sdk / Relative)
        Image = 'LuiFfi.dll' if Windows else 'libLuiFfi.so'
        Broker = 'LuiFfiBroker' + ('.exe' if Windows else '')
        Fixture = 'LuiFfiFixture.dll' if Windows else 'libLuiFfiFixture.so'
        for Library in ('Ffi', 'FfiBroker'):
            shutil.copytree(Root / 'libraries' / Library, Sdk / 'Libraries' / Library)
        shutil.copyfile(Root / 'libraries/Json/Json.luau', Sdk / 'Libraries/Json/Json.luau')
        Package = Sdk / 'Libraries/Ffi'
        (Package / 'Native').mkdir()
        shutil.copyfile(Binaries / Image, Package / 'Native' / Image)
        shutil.copyfile(Root / 'third_party/licenses/Libffi.txt', Package / 'Libffi-LICENSE.txt')
        shutil.copyfile(Binaries / Broker, Sdk / 'Tools' / Broker)
        shutil.copytree(Binaries / 'THIRD-PARTY-LICENSES', Sdk / 'Tools/FfiBroker-LICENSES')
        shutil.copyfile(Root / 'scripts/Generate-FfiBindings.py', Sdk / 'Tools/Generate-FfiBindings.py')
        Modes['Tools/' + Broker] = 0o755
        Write(Package / 'Ffi.json', {'SchemaVersion': 2, 'AbiVersion': 1, 'Provider': 'Native',
            'Extension': {'Name': 'Ffi', 'Version': 1, 'RequiredCapabilities': ['HostServices', 'NativeExtensions'],
                          'Provenance': {'Kind': 'LocalVendor', 'Id': 'LUI-experimental-Ffi-1'},
                          'Bindings': [{'Platform': Target, 'Path': 'Native/' + Image, 'Sha256': Hash(Binaries / Image)}]},
            'Services': [json.loads((Package / 'Service.json').read_bytes())]})
        PackageTool = importlib.util.spec_from_file_location('Examples', Root / 'scripts/Stage-LibraryExample.py')
        Examples = importlib.util.module_from_spec(PackageTool)
        PackageTool.loader.exec_module(Examples)
        for Example in Examples.FfiExamples:
            Examples.Stage(Root, Example, Sdk / 'Examples' / Example, Sdk, Binaries / 'Qualification' / Fixture)
        Inventory['SdkVersion'] = Version
        Members = sorted((P for P in Sdk.rglob('*') if P.is_file()), key=lambda P: P.relative_to(Sdk).as_posix())
        Inventory['Files'] = {P.relative_to(Sdk).as_posix(): Hash(P) for P in Members if P != Sdk / 'SdkManifest.json'}
        Write(Sdk / 'SdkManifest.json', Inventory)
        PackageFile = Stage / (Name + Suffix)
        if Windows:
            with ZipFile(PackageFile, 'x', ZIP_DEFLATED) as Archive:
                for P in Members:
                    Entry = ZipInfo(Name + '/' + P.relative_to(Sdk).as_posix())
                    Entry.create_system = 3
                    Entry.external_attr = 0o100644 << 16
                    Entry.compress_type = ZIP_DEFLATED
                    with P.open('rb') as Source, Archive.open(Entry, 'w') as Destination:
                        shutil.copyfileobj(Source, Destination)
        else:
            def SetMode(Info):
                Relative = '/'.join(Path(Info.name).parts[1:])
                Info.mode = Modes.get(Relative, 0o755 if Info.isdir() else 0o644)
                Info.mtime = 0
                Info.uid = Info.gid = 0
                Info.uname = Info.gname = ''
                return Info
            with PackageFile.open('xb') as Raw, gzip.GzipFile(filename='', mode='wb', fileobj=Raw, mtime=0) as Gzip:
                with tarfile.open(fileobj=Gzip, mode='w') as Archive:
                    Archive.add(Sdk, arcname=Name, filter=SetMode)
        Digest = Hash(PackageFile)
        PackageFile.rename(Output / PackageFile.name)
        Sdk.rename(Output / Name)
        (Output / (PackageFile.name + '.sha256')).write_text(Digest + '  ' + PackageFile.name + '\n', encoding='ascii')
        print('[LUI:FfiSdk] ' + Digest + '  ' + PackageFile.name)


if __name__ == '__main__':
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--base', required=True, type=Path)
    Parser.add_argument('--binaries', required=True, type=Path)
    Parser.add_argument('--output', required=True, type=Path)
    Parser.add_argument('--target', required=True, choices=Baselines)
    Args = Parser.parse_args()
    Assemble(Args.base, Args.binaries, Args.output, Args.target)
