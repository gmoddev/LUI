"""Qualify a deliberately selected native fixture, streaming and child retirement."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def Main():
    Parser=argparse.ArgumentParser()
    Parser.add_argument('--sdk',type=Path,required=True)
    Parser.add_argument('--fixture',type=Path,required=True)
    Options=Parser.parse_args()
    Sdk=Options.sdk.resolve()
    Fixture=Options.fixture.resolve()
    Components=json.loads((Sdk/'SdkManifest.json').read_bytes())['Components']
    Cli=Sdk/Components['Cli']
    with tempfile.TemporaryDirectory(prefix='lui-process-') as Temporary:
        Root=Path(Temporary)
        Cwd=Root/'work'
        Cwd.mkdir()
        Env={**os.environ,'LUI_PRIVATE_TEST':'test-private','LUI_PROJECT_NAME':'granted-name'}
        def Project(Name):
            App=Root/Name
            shutil.copytree(Sdk/'Examples'/Name,App)
            for Command in (['check',App/'lui.json'],['stage',App/'lui.json','--output',Root/(Name+'Stage')],['verify',Root/(Name+'Stage')]):
                subprocess.run([Cli,*Command,'--sdk',Sdk],check=True,stdout=subprocess.DEVNULL,timeout=25)
            return App
        App=Project('tool-check')
        Context=Project('context-report')
        def Command(Folder):
            return [Cli,'run',Folder/'lui.json','--sdk',Sdk,'--mode','headless','--stdio','true']
        Grant=['--tool',Fixture,'--tool-cwd',Cwd]
        def Run(Args,Code=0):
            Child=subprocess.run(Args,env=Env,capture_output=True,timeout=20)
            assert Child.returncode==Code,(Child.returncode,Child.stdout[:100],Child.stderr[:180])
            return Child
        Platform=b'Windows' if os.name=='nt' else b'Linux'
        assert Run([*Command(Context),'--context','true']).stdout==Platform+b'\tunnamed\n'
        assert Run([*Command(Context),'--context','true','--env-keys','LUI_PROJECT_NAME']).stdout==Platform+b'\tgranted-name\n'
        Run([*Command(Context),'--env-keys','LUI_PROJECT_NAME'],1)
        Run([*Command(App),'--','inspect'],1)
        Literal=['','two words','a"b','é','d\\','x&y']
        Expected=b''.join(str(len(X.encode())).encode()+b':'+X.encode()+b'\n' for X in Literal)
        Child=Run([*Command(Root/'tool-checkStage'),*Grant,'--','inspect',*Literal],7)
        assert (Child.stdout,Child.stderr)==(b'work|unset|absent\n'+Expected,b'stderr\0bytes'),(Child.stdout,Child.stderr)
        Source=App/'Src/Main.luau'
        Source.write_text("""--!strict
local P=require("@Process/Process")
local S=require("@Stdio/Stdio")
local Mode=S.Arguments()[1]
S.SetExitStatus(3)
local Chunks={stdout="",stderr=""}
local Request: P.Request?=nil
local Options: P.Options={Arguments={Mode},Input="a"..string.char(0).."z",TimeoutMilliseconds=if Mode=="wait" then 100 else 5000,
    MaxOutputBytes=if Mode=="flood" then 1024 else 65536,
    Environment=if Mode=="inspect" then {LUI_LABEL="explicit"} else {},
    OnOutput=function(Stream,Bytes)
        Chunks[Stream] ..= Bytes
        if Mode=="treewait" and Stream=="stdout" then assert(Request):Cancel() end
    end}
Request=P.Run(Options,function(Result)
    assert(Chunks.stdout==Result.Stdout and Chunks.stderr==Result.Stderr)
    if Mode=="inspect" then
        assert(Result.ExitCode==7 and Result.Stdout=="work|explicit|absent"..string.char(10).."a"..string.char(0).."z")
    end
    S.Write(Result.Status.." "..tostring(Result.ExitCode),function() S.SetExitStatus(0) end)
end)
""",encoding='utf-8')
        for Mode,Status in [('inspect',b'Exited 7'),('input-rights',b'Exited 0'),('wait',b'TimedOut nil'),
                            ('flood',b'OutputLimit nil'),('tree',b'Exited 0'),('treewait',b'Canceled nil')]:
            assert Run([*Command(App),*Grant,'--child-env-keys','LUI_LABEL','--',Mode]).stdout==Status,Mode
        Run([*Command(App),*Grant,'--','inspect'],1)
        time.sleep(2.2)
        assert not (Cwd/'late.txt').exists()
        # Give the host a deliberately inheritable test handle and prove that its child cannot see it.
        Host=Sdk/Components['HeadlessHost']
        Base=[Host,'--project',Root/'tool-checkStage/lui.json','--stdio',*Grant,'--']
        Sentinel=Cwd/'fixture-inheritance-sentinel'
        with Sentinel.open('w+b') as Owned:
            if os.name=='nt':
                import msvcrt
                Value=msvcrt.get_osfhandle(Owned.fileno())
                os.set_handle_inheritable(Value,True)
                Startup=subprocess.STARTUPINFO()
                Startup.lpAttributeList={'handle_list':[Value]}
                Transfer={'startupinfo':Startup,'close_fds':True}
            else:
                Value=Owned.fileno()
                Transfer={'pass_fds':(Value,)}
            assert subprocess.run([*Base,'nohandle',str(Value),Sentinel.name],capture_output=True,timeout=8,**Transfer).returncode==0
        Ready=Cwd/'ready.txt'
        if Ready.exists(): Ready.unlink()
        with subprocess.Popen([*Base,'treewait'],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE) as Owner:
            End=time.monotonic()+8
            while not Ready.exists() and Owner.poll() is None and time.monotonic()<End: time.sleep(.01)
            while Ready.exists() and not Ready.read_bytes().isdigit() and time.monotonic()<End: time.sleep(.01)
            Leader=int(Ready.read_bytes())
            Owner.kill(); Owner.communicate(timeout=5)
        time.sleep(2.3)
        # Both native supervisors retire descendants in the original group/job after abrupt host death.
        assert not (Cwd/'late.txt').exists()
        if os.name=='nt':
            import ctypes
            Kernel=ctypes.WinDLL('kernel32',use_last_error=True)
            Kernel.OpenProcess.restype=ctypes.c_void_p
            Handle=Kernel.OpenProcess(0x00100000,False,Leader)
            if Handle:
                try: assert Kernel.WaitForSingleObject(ctypes.c_void_p(Handle),0)==0
                finally: Kernel.CloseHandle(ctypes.c_void_p(Handle))
        else:
            Status=Path('/proc')/str(Leader)/'status'
            assert not Status.exists() or any(Line.startswith('State:') and 'Z (zombie)' in Line for Line in Status.read_text().splitlines())
        print('[LUI:Process] Unrelated handle absent; abrupt host death retired leader; descendant behavior matched platform')
        print('[LUI:Process] Staged tool, separate context grants, literal arguments, empty child environment, streaming, binary stdin, exit 7, limits, cancellation and normal descendant retirement passed')


if __name__=='__main__':
    Main()
