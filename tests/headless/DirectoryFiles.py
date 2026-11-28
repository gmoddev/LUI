"""Verify direct-child edits, copy and read-only authority through a staged app."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def Main():
    Parser=argparse.ArgumentParser()
    Parser.add_argument('--sdk',required=True,type=Path)
    Sdk=Parser.parse_args().sdk.resolve()
    Cli=Sdk/json.loads((Sdk/'SdkManifest.json').read_bytes())['Components']['Cli']
    with tempfile.TemporaryDirectory(prefix='lui-folder-') as Temporary:
        Base=Path(Temporary)
        App=Base/'app';shutil.copytree(Sdk/'Examples/folder-copy',App)
        Folder=Base/'selected';Folder.mkdir()
        Outside=Base/'outside';Outside.write_bytes(b'original')
        Source=Folder/'input.bin'; Source.write_bytes(bytes(range(256))*24576)
        Stage=Base/'stage'
        for Task in (['check',App/'lui.json'],['stage',App/'lui.json','--output',Stage],['verify',Stage]):
            subprocess.run([Cli,*Task,'--sdk',Sdk],check=True,stdout=subprocess.DEVNULL,timeout=20)
        def Run(Project,Grant,Args=(),Code=0):
            R=subprocess.run([Cli,'run',Project/'lui.json','--sdk',Sdk,'--mode','headless','--stdio','true',Grant,str(Folder)+os.sep+'.'+os.sep,'--',*Args],capture_output=True,timeout=25)
            assert R.returncode==Code,(R.returncode,R.stdout[:128],R.stderr[:180])
        Run(Stage,'--read-directory',['input.bin','copied.bin'],2)
        assert not (Folder/'copied.bin').exists()
        Run(Stage,'--write-directory',['input.bin','copied.bin'])
        assert (Folder/'copied.bin').read_bytes()==Source.read_bytes()
        (Folder/'occupied').write_bytes(b'stay')
        (Folder/'nonempty').mkdir();(Folder/'nonempty'/'keep').write_bytes(b'child')
        os.link(Outside,Folder/'hard')
        HasLink=True
        try: (Folder/'link').symlink_to(Outside)
        except OSError: HasLink=False
        Script=App/'Src/Main.luau'
        Script.write_text("""--!strict
local F=require("@LuiWorkbench/Files")
local S=require("@Stdio/Stdio")
local P=F.New(app:GetService("FileService"))
S.SetExitStatus(9)
local Args=S.Arguments()
P:OpenFolder(function(R)
    local Folder=assert(R)
    local Work: {((string?)->())->()}={}
    local function Add(Method: string, Name: string, Expected: boolean, To: string?)
        table.insert(Work,function(Next)
            local function Check(Failure: string?)
                assert((Failure == nil) == Expected)
                Next(nil)
            end
            if Method=="Rename" then Folder:Rename(Name,assert(To),Check)
            else (Folder :: any)[Method](Folder,Name,Check) end
        end)
    end
    local Writable=Args[1]=="write"
    Add("CreateDirectory","new-folder",Writable)
    if Writable then Add("CreateDirectory","new-folder",false); Add("Rename","new-folder",true,"moved-folder"); Add("RemoveDirectory","moved-folder",true) end
    Add("RemoveDirectory","nonempty",false)
    Add("Rename","copied.bin",false,"occupied")
    Add("Rename","copied.bin",Writable,"renamed.bin")
    Add("RemoveFile","renamed.bin",Writable)
    Add("RemoveFile","hard",false)
    if Args[2]=="link" then Add("Rename","link",false,"wrong"); Add("RemoveFile","link",false) end
    Add("CreateDirectory","../escape",false); Add("CreateDirectory",".LUI-SELECTED-test",false)
    local I=0
    local function Next(_Failure: string?)
        I+=1
        if I<=#Work then Work[I](Next);return end
        Folder:List(function(Entries)
            local Found=false
            for _,Entry in assert(Entries) do
                if Entry.Name=="input.bin" then assert(not Entry.IsDirectory and Entry.Size==6291456); Found=true end
            end
            assert(Found)
            P:Dispose(); S.SetExitStatus(0)
        end)
    end
    Next(nil)
end)
""",encoding='utf8')
        Run(App,'--read-directory',['read','link' if HasLink else 'none'])
        Run(App,'--write-directory',['write','link' if HasLink else 'none'])
        assert (Folder/'occupied').read_bytes()==b'stay' and Outside.read_bytes()==b'original'
        assert not any((Folder/N).exists() for N in ('renamed.bin','copied.bin','new-folder','wrong'))
        assert (Folder/'nonempty'/'keep').read_bytes()==b'child' and not (Base/'escape').exists()
        assert not list(Folder.glob('.lui-selected-*'))
        print('[LUI:Files] 6 MiB copy, direct-child rename and removal, empty-directory rules, metadata, read-only denial and link protections passed')


if __name__=='__main__':
    Main()
