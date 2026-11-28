"""Exercise the public selected writer through an installed candidate SDK."""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile


def Main():
    Parser=argparse.ArgumentParser()
    Parser.add_argument('--sdk',type=Path,required=True)
    Sdk=Parser.parse_args().sdk.resolve()
    Cli=Sdk/json.loads((Sdk/'SdkManifest.json').read_bytes())['Components']['Cli']
    with tempfile.TemporaryDirectory(prefix='lui-draft-') as Temporary:
        Root=Path(Temporary)
        App=Root/'App'
        shutil.copytree(Sdk/'Examples/atomic-copy',App)
        Manifest=App/'lui.json'
        Destination=Root/'output.bin'
        subprocess.run([Cli,'check',Manifest,'--sdk',Sdk],check=True)
        subprocess.run([Cli,'stage',Manifest,'--sdk',Sdk,'--output',Root/'Stage'],check=True,stdout=subprocess.DEVNULL)
        subprocess.run([Cli,'verify',Root/'Stage','--sdk',Sdk],check=True,stdout=subprocess.DEVNULL)
        Cmd=[Cli,'run',Root/'Stage/lui.json','--sdk',Sdk,'--mode','headless','--stdio','true']
        def Run(Data=b'',Code=0,**Options):
            Child=subprocess.run([*Cmd,'--write-file',Destination],input=Data,capture_output=True,timeout=30,**Options)
            assert Child.returncode==Code and Child.stdout==b'' and Child.stderr==b'',(Child.returncode,Child.stderr[:160])
        Data=bytes(range(256))*32768
        Destination.write_bytes(b'old')
        with subprocess.Popen([*Cmd,'--write-file',Destination],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE) as Child:
            Child.stdin.write(Data); Child.stdin.flush()
            assert Destination.read_bytes()==b'old'
            Child.stdin.close()
            assert Child.wait(timeout=30)==0 and not Child.stdout.read() and not Child.stderr.read()
        assert Destination.read_bytes()==Data
        Run()
        assert Destination.read_bytes()==b''
        Destination.unlink()
        Run(b'new')
        assert Destination.read_bytes()==b'new'
        Denied=subprocess.run(Cmd,input=b'no grant',capture_output=True,timeout=10)
        assert Denied.returncode!=0 and Destination.read_bytes()==b'new'
        with subprocess.Popen([*Cmd,'--write-file',Destination],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE) as Child:
            Child.stdin.write(Data[:1048576]); Child.stdin.flush()
            if os.name=='nt': Child.kill()
            else: Child.send_signal(signal.SIGTERM)
            Child.wait(timeout=10)
            Child.stdout.read()
        assert Destination.read_bytes()==b'new' and not list(Root.glob('.lui-selected-*'))
        if os.name!='nt':
            import resource
            def LimitFile():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
                resource.setrlimit(resource.RLIMIT_FSIZE,(98304,98304))
            Native=Sdk/json.loads((Sdk/'SdkManifest.json').read_bytes())['Components']['HeadlessHost']
            Limited=subprocess.run([Native,'--project',Root/'Stage/lui.json','--write-file',Destination,'--stdio'],
                input=Data[:262144],capture_output=True,timeout=15,preexec_fn=LimitFile)
            assert (Limited.returncode,Limited.stdout,Limited.stderr)==(1,b'',b''),Limited
            assert Destination.read_bytes()==b'new' and not list(Root.glob('.lui-selected-*'))
        (App/'Src/Main.luau').write_text("""--!strict
local Files=require("@LuiWorkbench/Files")
local S=require("@Stdio/Stdio")
local P=Files.New(app:GetService("FileService"))
S.SetExitStatus(8)
P:SaveFile("unused.bin",function(F)
    local File=assert(F)
    File:Read(function(Bytes,Denied)
        assert(not Bytes and Denied=="CapabilityDenied")
    File:BeginWriter(function(First)
        local A=assert(First)
        File:BeginWriter(function(Second)
            local B=assert(Second)
            assert(not pcall(function() A:Seek(4294967297) end))
            A:Seek(9*1024*1024)
            A:Write(buffer.fromstring("end"),function(Why)
                assert(not Why and A:Position()==9*1024*1024+3)
                A:Seek(0)
                A:Write(buffer.fromstring("begin"),function(Failed)
                    assert(not Failed)
                    A:Commit(function(Bad)
                        assert(not Bad)
                        B:Commit(function(Stale)
                            assert(Stale and not pcall(function() B:Position() end))
                            File:Close(); P:Dispose(); S.SetExitStatus(0)
                        end)
                    end)
                end)
            end)
        end)
    end)
    end)
end)
""",encoding='utf8')
        Cmd[2]=Manifest
        Run()
        with Destination.open('rb') as File:
            assert Destination.stat().st_size==9*1024*1024+3 and File.read(8)==b'begin\0\0\0'
            File.seek(-3,2); assert File.read()==b'end'
        assert not list(Root.glob('.lui-selected-*'))
    print('[LUI:Files] Atomic visibility, streamed replacement, create/truncate, seek, competing commits, missing authority and interruption passed')


if __name__=='__main__':
    Main()
