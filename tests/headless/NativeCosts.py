"""Measure native process startup and peak RSS/working set on one SDK target."""
import argparse
import ctypes
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile
import time
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


def Run(Command):
    if os.name != 'nt':
        # Measure the native child's own RSS without the Python parent's fork-time watermark.
        with tempfile.TemporaryDirectory() as Temp:
            Metric = Path(Temp) / 'peak'
            Start = time.perf_counter()
            subprocess.run(['/usr/bin/time','-f','%M','-o',str(Metric),*Command], stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            Elapsed = (time.perf_counter()-Start)*1000
            return Elapsed, int(Metric.read_text().strip())*1024
    Start = time.perf_counter()
    with subprocess.Popen(Command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) as Process:
        if os.name == 'nt':
            from ctypes import wintypes
            class Counters(ctypes.Structure):
                _fields_ = [('Size', wintypes.DWORD), ('Faults', wintypes.DWORD)] + [(Name, ctypes.c_size_t) for Name in
                    ('PeakWorkingSet', 'WorkingSet', 'PeakPaged', 'Paged', 'PeakNonPaged', 'NonPaged', 'PageFile', 'PeakPageFile')]
            Memory = Counters()
            Memory.Size = ctypes.sizeof(Memory)
            Peak = 0
            while True:
                if ctypes.windll.psapi.GetProcessMemoryInfo(ctypes.c_void_p(int(Process._handle)),ctypes.byref(Memory),Memory.Size):
                    Peak = max(Peak, Memory.PeakWorkingSet)
                if Process.poll() is not None: break
                time.sleep(0.001)
        assert Process.returncode == 0 and Peak > 0
    return (time.perf_counter()-Start)*1000, Peak


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--sdk', type=Path, required=True)
    Parser.add_argument('--baseline', type=Path)
    Parser.add_argument('--fixture', type=Path, help='Selected executable for the optional process-service workload')
    Parser.add_argument('--runs', type=int, default=7)
    Parser.add_argument('--output', type=Path)
    Args = Parser.parse_args()
    Sdk = Args.sdk.resolve()
    def Host(S): return S / json.loads((S/'SdkManifest.json').read_bytes())['Components']['HeadlessHost']
    def Size(F):
        Data=F.read_bytes()
        return {'Bytes':len(Data),'GzipBytes':len(gzip.compress(Data,9,mtime=0)),'Sha256':hashlib.sha256(Data).hexdigest()}
    def Distribution(Directory):
        Archive = io.BytesIO()
        Installed = 0
        with ZipFile(Archive,'w') as Package:
            for File in sorted(Directory.rglob('*')):
                if File.is_file():
                    Data = File.read_bytes()
                    Installed += len(Data)
                    Package.writestr(ZipInfo(File.relative_to(Directory).as_posix()),Data,ZIP_DEFLATED,9)
        return {'InstalledBytes':Installed,'ZipBytes':len(Archive.getvalue())}
    Records={}
    Costs={'Host':Size(Host(Sdk))}
    with tempfile.TemporaryDirectory(prefix='lui-runtime-costs-') as Temporary:
        for Name in ('Baseline','Control','Crypto','Compression','SQLite','Regex','Toml','Context','Process','WorkersPrepared','Workers','LanguagePrepared','Language','RandomPrepared','Random'):
            if Name=='Baseline' and not Args.baseline: continue
            if Name=='Process' and not Args.fixture: continue
            Folder=Path(Temporary)/Name
            Folder.mkdir()
            Manifest={'SchemaVersion':2,'Script':'Src/Main.luau','Capabilities':[],'Extensions':[],
                'Assets':[],'ExtensionDeclarations':[],'Diagnostics':{'Mode':'Development'},
                'Modules':{'Root':'Src','Files':['Main.luau'],'Requires':[],'Dependencies':[]}}
            (Folder/'Src').mkdir()
            Command=[Host(Args.baseline.resolve() if Name=='Baseline' else Sdk),'--project',Folder/'lui.json']
            Text='assert(app:GetService("PerformanceService"):GetTime() >= 0)'
            Alias=Name.removesuffix('Prepared')
            Selected={'Workers':'parallel-data','Language':'source-inspect','Random':'spec-cli'}
            if Alias in Selected:
                Example=Sdk/'Examples'/Selected[Alias]
                if not Example.is_dir(): continue
                shutil.copytree(Example,Folder,dirs_exist_ok=True)
                Manifest=json.loads((Folder/'lui.json').read_bytes())
                Command += ['--stdio']
                if Alias=='Language':
                    Checker=json.loads((Sdk/'SdkManifest.json').read_bytes())['Components']['Checker']
                    Command += ['--tool',Sdk/Checker,'--tool-cwd',Folder]
                if Name==Alias:
                    Text=f'assert(require("@{Alias}/{Alias}"))'
                    Costs[Alias]={'Library':Distribution(Sdk/'Libraries'/Alias),'Example':Distribution(Example)}
            elif Name in ('Context','Process'):
                Package=Sdk/'Libraries'/Name
                if not Package.is_dir(): continue
                Decl=Name+'.json'
                shutil.copyfile(Package/'Host.json',Folder/Decl)
                Manifest.update(Capabilities=['HostServices'],ExtensionDeclarations=[Decl])
                if Name=='Context':
                    Command += ['--context']
                    Text='assert(app:GetService("Context"):Elapsed() >= 0)'
                else:
                    Command += ['--tool',Args.fixture.resolve(),'--tool-cwd',Folder]
                    Text='assert(app:GetService("Process"))'
            elif Name not in ('Control','Baseline'):
                Package=Sdk/'Libraries'/Name
                if not Package.is_dir(): continue
                Decl=Name+'.json'
                Binding=json.loads((Package/Decl).read_bytes())['Extension']['Bindings'][0]
                Image=Path(Binding['Path'])
                (Folder/Image.parent).mkdir()
                shutil.copyfile(Package/Image,Folder/Image)
                shutil.copyfile(Package/Decl,Folder/Decl)
                Costs[Name]=Size(Package/Image)
                Costs[Name]['Library']=Distribution(Package)
                Example={'Crypto':'digest','Compression':'codec','SQLite':'records','Regex':'regex-filter','Toml':'toml-config'}[Name]
                Costs[Name]['Example']=Distribution(Sdk/'Examples'/Example)
                Manifest.update(Capabilities=['HostServices','NativeExtensions'],Extensions=[Name],ExtensionDeclarations=[Decl])
                Text='assert(app:GetService("'+Name+'"))'
                Command += ['--provider',Name]
            (Folder/'Src/Main.luau').write_text(Text)
            (Folder/'lui.json').write_text(json.dumps(Manifest))
            Run(Command)
            Samples=[Run(Command) for _ in range(Args.runs)]
            Records[Name]={'MedianMs':round(statistics.median(X[0] for X in Samples),3),
                'MedianPeakBytes':int(statistics.median(X[1] for X in Samples)),'Runs':len(Samples)}
    if Args.baseline: Costs['BaselineHost']=Size(Host(Args.baseline.resolve()))
    Result=json.dumps({'Status':'PASS','Measurements':Records,'Components':Costs},indent=2)
    if Args.output: Args.output.write_text(Result+'\n',encoding='utf-8')
    print(Result)


if __name__=='__main__':
    Main()
