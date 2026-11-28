"""Run independent applications produced by an extracted compact SDK."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def Main():
    Parser=argparse.ArgumentParser()
    Parser.add_argument('--sdk',type=Path,required=True)
    Parser.add_argument('--fixture',type=Path,required=True)
    A=Parser.parse_args()
    Sdk=A.sdk.resolve()
    Manifest=json.loads((Sdk/'SdkManifest.json').read_bytes())
    Cli=Sdk/Manifest['Components']['Cli']
    Windows=os.name=='nt'
    assert not (Sdk/'Preview').exists() and (not Windows or not (Sdk/'Native').exists())
    assert 'Cli/' in Manifest['Components']['PreviewHost']
    if Windows:
        assert not (Sdk/'Cli/onnxruntime.dll').exists()
    subprocess.run([Cli,'doctor','--sdk',Sdk],capture_output=True,check=True,timeout=30)
    with tempfile.TemporaryDirectory(prefix='lui-distribution-') as Temp:
        Root=Path(Temp)
        Work=Root/'work'
        Work.mkdir()
        Db=Work/'notebook';Db.mkdir(mode=0o700)
        Fixture=A.fixture.resolve()
        Tests=[
            ('atomic-copy',['--write-file',Work/'copy'],b'a'*1048576,b'',0),
            ('regex-filter',['--provider','Regex','--','ID-[0-9]+'],b'no\nID-24\n',b'ID-24\n',0),
            ('toml-config',['--provider','Toml'],b'Name="Jo"\nCopies=2',b'Hello Jo\nHello Jo\n',0),
            ('live-records',['--provider','SQLite','--database-root',Db,'--','add','persisted'],b'',b'1\tpersisted\n',0),
            ('tool-check',['--tool',Fixture,'--tool-cwd',Work,'--','input-rights'],b'',b'',0),
        ]
        for Name,Options,Input,Expected,Code in Tests:
            Destination=Root/(Name+'-app')
            subprocess.run([Cli,'build',Sdk/'Examples'/Name/'lui.json','--mode','headless','--sdk',Sdk,'--output',Destination],
                check=True,stdout=subprocess.DEVNULL,timeout=30)
            Names={F.name for F in Destination.rglob('*') if F.is_file()}
            assert not Names.intersection({'coreclr.dll','libcoreclr.so','Lui.WinUI.exe','lui-gtk','lui.dll','LuiTypeCheck.exe','LuiProjectTool','HostFiles.json'})
            assert (Destination/'THIRD-PARTY-LICENSES/Luau.txt').is_file()
            Native=Destination/('Start-LUI.exe' if Windows else 'Start-LUI.sh')
            R=subprocess.run([Native,'--stdio',*Options],input=Input,capture_output=True,cwd=Work,timeout=20,
                env={K:V for K,V in os.environ.items() if K not in ('LUI_SDK','LD_LIBRARY_PATH')})
            assert (R.returncode,R.stdout,R.stderr)==(Code,Expected,b''),(Name,R.returncode,R.stdout[:96],R.stderr[:128])
            if Name=='atomic-copy':assert (Work/'copy').read_bytes()==Input
            if Name in ('regex-filter','toml-config','live-records'):
                assert len(list((Destination/'Application/Native').iterdir()))==1
            else:assert not (Destination/'Application/Native').exists()
        Desktop=Root/'desktop'
        subprocess.run([Cli,'build',Sdk/'Examples/message-viewer/lui.json','--mode','project','--sdk',Sdk,'--output',Desktop],
            stdout=subprocess.DEVNULL,check=True,timeout=35)
        assert not {F.name for F in Desktop.rglob('*') if F.is_file()}.intersection({'lui.exe','lui.dll','lui-preview-host.exe','lui-preview-host.dll','LuiHeadlessHost.exe','LuiHeadlessHost','HostFiles.json'})
        assert (Desktop/'Application/lui.json').is_file() and (Desktop/('Lui.WinUI.exe' if Windows else 'bin/lui-gtk')).is_file()
    print('[LUI:Distribution] Doctor, five standalone headless apps, selected-only providers, licenses and desktop-only packaging passed')


if __name__=='__main__':
    Main()
