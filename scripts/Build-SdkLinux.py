"""Build a private self-contained Linux x64 SDK using an existing native cache.

Run under an admitted build-worker lease. This does not install dependencies or
qualify other distributions, desktops, physical input or downloaded-file trust.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import platform
import shutil
import subprocess

def Main():
    Parser=argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--native-build',type=Path,required=True)
    Parser.add_argument('--output',type=Path,required=True)
    Parser.add_argument('--version',choices=('0.1.0-preview.3',),required=True)
    Args=Parser.parse_args()
    if platform.system()!='Linux' or platform.machine() not in ('x86_64','amd64'):raise ValueError('Linux x64 build required')
    Root=Path(__file__).resolve().parents[1];Build=Args.native_build.resolve(strict=True);Output=Args.output.absolute()
    Managed=Output.parent/(Output.name+'.ManagedBuild')
    if Output.exists() or Managed.exists():raise ValueError('Choose fresh SDK and managed output directories')
    def Run(Command):subprocess.run([str(Item) for Item in Command],cwd=Root,check=True,stdin=subprocess.DEVNULL,timeout=600)
    Run(['python3',Root/'scripts/Check-Toolchains.py'])
    for Name in ('LuiTypeCheck','LuiProjectTool','LuiHeadlessHost','libLuiRuntime.so'):
        if not (Build/Name).is_file():raise ValueError('Missing native component: '+Name)
    Output.mkdir(parents=True);Managed.mkdir()
    for Project,Part in [('tools/cli/Lui.Cli.csproj','Cli'),('tools/preview-host/Lui.PreviewHost.csproj','Preview')]:
        Run(['dotnet','publish',Root/Project,'--configuration','Release','--runtime','linux-x64','--self-contained','true',
             '--artifacts-path',Managed,'-m:2','-p:PublishTrimmed=false','-p:PublishSingleFile=false',
             '-p:PublishReadyToRun=false','-p:DebugType=None','-p:DebugSymbols=false','--output',Output/Part])
    Run(['cmake','--install',Build,'--prefix',Output/'Native','--component','GtkHost'])
    Run(['cmake','--install',Build,'--prefix',Output/'Native','--component','SdkHeadless'])
    (Output/'Tools').mkdir()
    for Name in ('LuiTypeCheck','LuiProjectTool'):shutil.copy2(Build/Name,Output/'Tools'/Name)
    shutil.copyfile(Root/'types/LUI.d.luau',Output/'Tools/LUI.d.luau')
    Spec=importlib.util.spec_from_file_location('PackageSdk',Root/'scripts/Package-Sdk.py')
    Package=importlib.util.module_from_spec(Spec);Spec.loader.exec_module(Package)
    Package.Package(Root,Output,Args.version,Build,Target='LinuxX64')
    Run([Output/'Cli/lui','doctor','--format','json'])
    print('[LUI:SDK] Private Linux candidate prepared; distribution/desktop/recipient qualification is separate.')

if __name__=='__main__':Main()
