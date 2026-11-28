"""Qualify selected text providers using applications copied from the installed SDK."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def Main():
    Parser=argparse.ArgumentParser()
    Parser.add_argument('--sdk',required=True,type=Path)
    Sdk=Parser.parse_args().sdk.resolve()
    Cli=Sdk/json.loads((Sdk/'SdkManifest.json').read_bytes())['Components']['Cli']
    with tempfile.TemporaryDirectory(prefix='lui-text-') as Temporary:
        Root=Path(Temporary)
        for AppName,Name,Arguments,Input,Expected in (
            ('regex-filter','Regex',['WARN|ERROR'],b'INFO one\nWARN two\nERROR three\n',b'WARN two\nERROR three\n'),
            ('toml-config','Toml',[],b'# demo\nName = "Ada"\nCopies = 2\n',b'Hello Ada\nHello Ada\n')):
            App=Root/AppName
            shutil.copytree(Sdk/'Examples'/AppName,App)
            Manifest=App/'lui.json'
            Stage=Root/(AppName+'Stage')
            for Command in (['check',Manifest],['stage',Manifest,'--output',Stage],['verify',Stage]):
                subprocess.run([Cli,*Command,'--sdk',Sdk],stdout=subprocess.DEVNULL,check=True,timeout=20)
            def Run(Project,Args=(),Data=b''):
                Done=subprocess.run([Cli,'run',Project/'lui.json','--sdk',Sdk,'--mode','headless','--stdio','true','--providers',Name,'--',*Args],
                    input=Data,capture_output=True,timeout=10)
                assert Done.returncode==0,(Name,Done.returncode,Done.stderr[:180],Done.stdout[:100])
                return Done.stdout
            assert Run(Stage,Arguments,Input)==Expected
            assert (Stage/(Name+'-LICENSE.txt')).is_file()
            if Name=='Regex':
                Source="""--!strict
local R=require("@Regex/Regex")
local S=require("@Stdio/Stdio")
S.SetExitStatus(3)
local P=R.Compile("([a-z]+)([0-9]+)?")
local Hit=assert(P:Find("..ab123"))
assert(Hit.First==3 and Hit.Last==7 and Hit.Captures[2].First==5 and Hit.Captures[2].Last==7)
assert(P:Find("#")==nil and P:Find("abc").Captures[2]==nil)
assert(table.concat(P:Split("a2:b"),"|")=="|:|" and P:Replace("a2:b","$1")=="$1:$1")
assert(not pcall(function() P:Find(string.rep("a",32769)) end))
P:Close(); P:Close()
assert(not pcall(function() P:Find("x") end))
local Z=R.Compile("(?:|a)")
local Hits=Z:Matches("a")
assert(#Hits==3 and Hits[1].Last==0 and Hits[2].Last==1 and Hits[3].First==2)
Z:Close()
local Expand=R.Compile("x|a+")
assert(#Expand:Replace("xxx"..string.rep("a",30000),string.rep("r",12000))==48000)
assert(not pcall(function() Expand:Replace("xxx",string.rep("r",30000)) end))
assert(not pcall(function() Expand:Matches(string.rep("x",257)) end)); Expand:Close()
assert(not pcall(R.Compile,"["))
local Hard=R.Compile("^(a+)+$")
assert(not pcall(function() Hard:Find(string.rep("a",1000).."!") end)); Hard:Close()
local Pool={}
for _=1,32 do table.insert(Pool,R.Compile(".")) end
assert(not pcall(R.Compile,"."))
for _,Item in Pool do Item:Close() end
S.SetExitStatus(0)
"""
            else:
                Source=r"""--!strict
local T=require("@Toml/Toml")
local S=require("@Stdio/Stdio")
S.SetExitStatus(3)
local D=T.Decode('name = "a:b"\n[table]\nvalues = [true, false]\nquoted = "a\\\"b"\n')
assert(D.table.values[1] and not D.table.values[2] and D.name=="a:b")
local Copy=T.Decode(T.Encode(D))
assert(Copy.table.quoted==D.table.quoted and Copy.name==D.name)
assert(T.Settings("Enabled=true",{Enabled=false}).Enabled==true)
assert(not pcall(T.Settings,"Extra=3",{Enabled=false}))
for _,Bad in {"x=1\nx=2","x=inf","d=2025-04-01","i=9007199254740992",string.rep("a.",40).."x=1"} do
    assert(not pcall(T.Decode,Bad))
end
local Words=string.rep("a.",90)
assert(T.Decode('"'..Words..'"=1')[Words]==1)
for _,N in {1e16,-1e16,9007199254740991,-9007199254740991} do assert(T.Decode(T.Encode({x=N})).x==N) end
assert(1/T.Decode(T.Encode({x=-0.0})).x==-math.huge)
assert(not pcall(T.Encode,{bad=0/0}))
local WithControls=string.char(0,9,127)
assert(T.Decode(T.Encode({x=WithControls})).x==WithControls)
S.SetExitStatus(0)
"""
            (App/'Src/Main.luau').write_text(Source,encoding='utf-8')
            assert Run(App)==b''
            print('[LUI:Formats] '+Name+' standalone, semantic and limit checks passed',flush=True)


if __name__=='__main__':
    Main()
