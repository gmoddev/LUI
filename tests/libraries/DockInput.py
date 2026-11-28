"""Replay pointer capture through the schema-v2 example using LUI's real layout and input ABI."""
import argparse
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time

from Run import Bind, Options, RunOptions


def Run(RuntimePath):
    Root=Path(__file__).resolve().parents[2]
    Lib=C.CDLL(str(RuntimePath.resolve()))
    Bind(Lib)
    for Name,Result,Args in [
        ("PointerInputV2",C.c_int,[C.c_void_p,C.c_int,C.c_int,C.c_int,C.c_uint,C.c_double,C.c_double,C.c_int]),
        ("Activate",C.c_int,[C.c_void_p,C.c_int]),
        ("TextChanged",C.c_int,[C.c_void_p,C.c_int,C.c_char_p]),
    ]:
        Fn=getattr(Lib,"Lui_"+Name)
        Fn.restype,Fn.argtypes=Result,Args
    with tempfile.TemporaryDirectory(prefix="lui-dock-") as Scratch:
        Staged=Path(Scratch)/"Analysis"
        subprocess.run([sys.executable,str(Root/"scripts/Stage-LibraryExample.py"),"--example","dock-workspace","--output",str(Staged)],check=True)
        Manifest=str(Staged/"lui.json").encode()
        Settings=Options()
        Settings.StructSize=C.sizeof(Settings); Settings.AbiVersion=1
        Project=C.c_void_p(); Errors=C.create_string_buffer(256)
        assert Lib.Lui_ProjectPrepare(C.byref(Settings),Manifest,len(Manifest),C.byref(Project),Errors,len(Errors)),Errors.value
        Runtime=Lib.Lui_Create()
        try:
            Setup=RunOptions(C.sizeof(RunOptions),1,1,0,0)
            assert Lib.Lui_RunProject(Runtime,Project,C.byref(Setup)),Lib.Lui_GetLastError(Runtime)
            def Tree(): return json.loads(Lib.Lui_GetPreviewTreeJson(Runtime))
            def Stats(): return json.loads(Lib.Lui_GetPerformanceJson(Runtime))
            def Node(Name): return next(N for N in Tree() if N["name"]==Name)
            def Activate(Name): assert Lib.Lui_Activate(Runtime,Node(Name)["id"])
            def Point(R): return R["x"]+R["width"]/2,R["y"]+R["height"]/2
            def Saved():
                Activate("Save layout")
                return json.loads(Node("LayoutRecord")["text"])
            def Pointer(Tab,Phase,X,Y):
                R=Tab["bounds"]
                Start=time.perf_counter_ns()
                assert Lib.Lui_PointerInputV2(Runtime,Tab["id"],Phase,0,7,X-R["x"],Y-R["y"],int(Phase in (0,2)))
                return (time.perf_counter_ns()-Start)/1e6
            Original=Saved()
            Editor=Node("AnalysisNotes")["id"]
            Activate("DockTabNotes")
            assert Lib.Lui_TextChanged(Runtime,Editor,b"unsaved acquisition")
            Checks=[]
            Timing=[]
            for Key in ["Tab","Left","Right","Top","Bottom","RootLeft","RootRight","RootTop","RootBottom"]:
                Activate("Reset layout")
                Tab=Node("DockTabNotes")
                StartX,StartY=Point(Tab["bounds"])
                Pointer(Tab,0,StartX,StartY)
                Pointer(Tab,1,*Point(Node("DockNodeInputs")["bounds"]))
                Guide=Node("DockGuide"+Key)
                assert Guide["visible"],Key
                X,Y=Point(Guide["bounds"])
                Before=Stats()
                Timing+= [Pointer(Tab,1,StartX+(X-StartX)*I/24,StartY+(Y-StartY)*I/24) for I in range(1,25)]
                After=Stats()
                Visible=Node("DockPreview")
                assert Visible["visible"] and Saved()==Original,(Key,"preview or atomicity")
                Pointer(Tab,2,X,Y)
                Now=Saved()
                def Locate(N):
                    if "Tabs" in N: return N["Id"] if "Notes" in N["Tabs"] else None
                    return Locate(N["First"]) or Locate(N["Second"])
                Id=Locate(Now["Main"])
                Committed=Node("DockNode"+Id)["bounds"]
                Error=max(abs(Committed[K]-Visible["bounds"][K]) for K in ("x","y","width","height"))
                assert Error<=1,(Key,Error,Committed,Visible)
                assert Node("AnalysisNotes")["id"]==Editor and Node("AnalysisNotes")["text"]=="unsaved acquisition"
                Checks.append({"Target":Key,"MaxLogicalError":Error,"LayoutsWhileHeld":After["LayoutPasses"]-Before["LayoutPasses"],
                    "NewRetainedWhileHeld":After["RetainedInstances"]-Before["RetainedInstances"],
                    "LuaBytesDeltaWhileHeld":After["LuaMemoryBytes"]-Before["LuaMemoryBytes"]})
            Activate("Reset layout")
            for Side in ("Left","Right","Tab","Top","Bottom"):
                Tab=Node("DockTabNotes")
                R=Node("DockNodeData")["bounds"]
                X=R["x"]+R["width"]*({"Left":.04,"Right":.96}.get(Side,.72))
                Y=R["y"]+({"Top":34,"Bottom":R["height"]-6}.get(Side,R["height"]*.57))
                Pointer(Tab,0,*Point(Tab["bounds"]))
                Pointer(Tab,1,X,Y)
                Preview=Node("DockPreview")
                assert Preview["visible"],("body drop missing",Side)
                Pointer(Tab,2,X,Y)
                Now=Saved()
                assert not Now["Floats"]
                Data=Node("DockNodeData")["bounds"]
                Notes=Node("AnalysisNotes")
                assert Notes["visible"] and Notes["id"]==Editor
                Target=Preview["bounds"]
                if Side=="Left": assert Target["x"]+Target["width"]<=Data["x"]+1
                elif Side=="Right": assert Target["x"]>=Data["x"]+Data["width"]-1
                elif Side=="Top": assert Target["y"]+Target["height"]<=Data["y"]+1
                elif Side=="Bottom": assert Target["y"]>=Data["y"]+Data["height"]-1
                else: assert Now["Main"]["Second"]["Second"]["Active"]=="Notes"
                assert abs(Notes["bounds"]["x"]-Target["x"])<=1
                Activate("Reset layout")
            # A divider has no destination. A release here must leave the panel docked.
            Divider=Node("DockDividerAnalysis")
            Tab=Node("DockTabNotes")
            Pointer(Tab,0,*Point(Tab["bounds"]))
            Pointer(Tab,1,*Point(Divider["bounds"]))
            Pointer(Tab,2,*Point(Divider["bounds"]))
            assert Saved()==Original
            Tab=Node("DockTabNotes")
            Pointer(Tab,0,*Point(Tab["bounds"]))
            Pointer(Tab,1,*Point(Node("DockNodeInputs")["bounds"]))
            Pointer(Tab,3,*Point(Node("DockNodeInputs")["bounds"]))
            assert Saved()==Original and not Node("DockPreview")["visible"]
            Timing.sort()
            return {"Runtime":hashlib.sha256(RuntimePath.read_bytes()).hexdigest(),"Targets":Checks,
                "Canceled":True,"RetainedEditor":True,"Moves":len(Timing),
                "IngressMs":{"P50":statistics.median(Timing),"P95":Timing[math.ceil(.95*len(Timing))-1],
                             "P99":Timing[math.ceil(.99*len(Timing))-1]}}
        finally:
            Lib.Lui_Destroy(Runtime)
            Lib.Lui_ProjectRelease(Project)


if __name__=="__main__":
    P=argparse.ArgumentParser()
    P.add_argument("--runtime",required=True,type=Path)
    P.add_argument("--output",type=Path)
    A=P.parse_args()
    Report=json.dumps(Run(A.runtime),indent=2)
    if A.output: A.output.write_text(Report+"\n",encoding="utf8")
    print(Report)
