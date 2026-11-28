"""Drive owned GTK windows on Weston. The pointer case uses a private outer Xvfb seat."""
import argparse
import contextlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time


def Wait(Probe,Seconds=8):
    Deadline=time.monotonic()+Seconds
    while time.monotonic()<Deadline:
        Value=Probe()
        if Value: return Value
        time.sleep(.04)
    raise TimeoutError("Private compositor or client did not become ready")


@contextlib.contextmanager
def Process(Command,Env,Log,Error=None):
    Child=subprocess.Popen(Command,env=Env,stdout=Log,stderr=Error or Log)
    try: yield Child
    finally:
        if Child.poll() is None:
            Child.terminate()
            try: Child.wait(timeout=3)
            except subprocess.TimeoutExpired: Child.kill(); Child.wait()


def Trial(Host,Project,Out,Case):
    Pointer=Case=="DockPointer"
    Out.mkdir(parents=True,exist_ok=False)
    with tempfile.TemporaryDirectory(prefix="lui-wayland-") as Temp,contextlib.ExitStack() as Stack:
        Env={**os.environ,"XDG_RUNTIME_DIR":Temp,"WAYLAND_DISPLAY":"wayland-dock",
             "GDK_BACKEND":"wayland","GSK_RENDERER":"cairo","GTK_A11Y":"none"}
        Command=["weston","--backend="+("x11" if Pointer else "headless"),"--renderer=pixman",
                 "--width=1040","--height=700","--socket=wayland-dock","--log="+str(Out/"Weston.log")]
        Sink=Stack.enter_context((Out/"Server.log").open("w"))
        if Pointer:
            Read,Write=os.pipe()
            try:
                Xvfb=subprocess.Popen(["Xvfb","-displayfd",str(Write),"-screen","0","1200x850x24","-nolisten","tcp"],
                                      pass_fds=(Write,),stdout=Sink,stderr=Sink)
                Stack.callback(lambda: (Xvfb.terminate(),Xvfb.wait(timeout=3)) if Xvfb.poll() is None else None)
                os.close(Write); Write=-1
                Env["DISPLAY"]=":"+os.read(Read,32).decode().strip()
            finally:
                os.close(Read)
                if Write>=0: os.close(Write)
            Command.append("--shell=kiosk-shell.so")
        Stack.enter_context(Process(Command,Env,Sink))
        Wait(lambda: (Path(Temp)/"wayland-dock").is_socket())
        Log=Stack.enter_context((Out/"Client.log").open("w"))
        Protocol=Stack.enter_context((Out/"Protocol.log").open("w"))
        Client=Stack.enter_context(Process(["stdbuf","-oL",str(Host),"--project",str(Project)],
                                           {**Env,"WAYLAND_DEBUG":"1"},Log,Protocol))
        if Pointer:
            Gesture=Wait(lambda: re.search(r"Gesture (\d+) (\d+) (\d+) (\d+)",(Out/"Client.log").read_text()),5)
            Tree=subprocess.check_output(["xwininfo","-root","-tree"],env=Env,text=True)
            Win=re.search(r"(0x[0-9a-f]+)[^\n]*\s1040x700[+-]",Tree).group(1)
            def X(*Args): subprocess.run(["xdotool",*Args],env=Env,check=True)
            X("windowfocus",Win)
            X("mousemove","--window",Win,*Gesture.groups()[:2],"mousedown","1")
            time.sleep(.14)
            X("mousemove","--window",Win,*Gesture.groups()[2:])
            time.sleep(.23)
            subprocess.run(["xwd","-root","-silent","-out",str(Out/"Preview.xwd")],env=Env,check=True)
            X("mouseup","1")
            time.sleep(.35)
            subprocess.run(["xwd","-root","-silent","-out",str(Out/"Commit.xwd")],env=Env,check=True)
        assert Client.wait(timeout=12)==0
        Text=(Out/"Client.log").read_text()
        Trace=(Out/"Protocol.log").read_text()
        if Pointer:
            assert "PointerPassed" in Text and re.search(r"wl_pointer@\d+\.button\([^\n]*272, 0\)",Trace)
        elif Case=="DockWorkspace":
            assert all(Mark in Text for Mark in ("NativeOwned","Float","Resize","Redock","Finished"))
            assert re.search(r"xdg_toplevel@\d+\.set_parent\(xdg_toplevel@\d+\)",Trace)
        else:
            assert "GtkSpectrumRestored" in Text
            assert re.search(r"xdg_toplevel@\d+\.set_parent\(xdg_toplevel@\d+\)",Trace)


def Main():
    P=argparse.ArgumentParser()
    P.add_argument("--host",type=Path,required=True)
    P.add_argument("--output",type=Path,required=True)
    A=P.parse_args()
    Root=Path(__file__).resolve().parents[2]
    sys.path.insert(0,str(Root/"tests/libraries"))
    from Run import Stage
    for Case in ("DockWorkspace","DockPointer","Spectrum"):
        with tempfile.TemporaryDirectory(prefix="lui-dock-project-") as Temp:
            Project=Path(Temp)
            if Case=="Spectrum":
                Source=(Root/"examples/dock-workspace/Src/Main.luau").read_text(encoding="utf8")
                Source+='''
task.delay(0.8,function()
    assert(Native and Surface and Surface.FrameState=="Ready" and #Plot:GetChildren()==1)
    assert(Host:Float("Spectrum"))
end)
task.delay(2,function()
    local _,W=Host:GetLocation("Spectrum")
    assert(W and W~="Main" and Surface.FrameState=="Ready" and Host:Redock(W))
end)
task.delay(2.5,function()
    assert(Surface.FrameState=="Ready" and Host:IsVisible("Spectrum"))
    print("[LUI:DockQualification] GtkSpectrumRestored")
    Window:RequestClose()
end)
'''
            else:
                Source=(Root/"tests/gtk4"/(Case+".luau")).read_text(encoding="utf8")
            Stage(Root,Project,Source,Include={"LuiWorkbench","Json"})
            if Case!="DockPointer":
                Decl="DesktopWindowMetrics.json"
                shutil.copyfile(Root/"libraries/LuiWorkbench"/Decl,Project/Decl)
                F=Project/"lui.json"
                M=json.loads(F.read_text())
                M["Capabilities"],M["ExtensionDeclarations"]=["HostServices"],[Decl]
                F.write_text(json.dumps(M))
            Trial(A.host.resolve(),Project/"lui.json",A.output.resolve()/Case,Case)


if __name__=="__main__": Main()
