import os
import subprocess
import sys
import time
import tempfile
import pyatspi
from gi.repository import GLib

def Inspect(Node, Found, Actions, Depth=0):
    if Node is None or Depth > 20:
        return
    try:
        Name = Node.name
        Role = Node.getRoleName()
        if (Name == "Qualification input" and Role == "text") or (Name in ("Qualification action", "Fallback action") and Role == "push button"):
            # A GTK button's label child can share its name. Record the control,
            # never overwrite its evidence with that descriptive child.
            Found[Name] = (Role, Node.description, Node.getState().contains(pyatspi.STATE_FOCUSED))
            if Name == "Qualification action":
                Actions[Name] = Node
        for Child in Node:
            Inspect(Child, Found, Actions, Depth + 1)
    except GLib.Error:
        # Text changes replace GTK label children. A stale D-Bus reference is
        # retried by the bounded outer probe, never treated as passing evidence.
        return

Environment = dict(os.environ, GTK_A11Y="atspi", GDK_BACKEND="x11", GSK_RENDERER="cairo", G_DEBUG="fatal-criticals")
Errors = tempfile.TemporaryFile(mode="w+t", encoding="utf-8")
Child = subprocess.Popen([sys.argv[1], "--project", sys.argv[2]], env=Environment, stdout=subprocess.DEVNULL, stderr=Errors)
Samples = []
def Refresh():
    Context = GLib.MainContext.default()
    for Event in range(64):
        if not Context.pending():
            break
        Context.iteration(False)
try:
    for Attempt in range(100):
        Refresh()
        Found = {}
        Actions = {}
        Identity = None
        for App in pyatspi.Registry.getDesktop(0):
            if App.get_process_id() == Child.pid:
                Identity = App.name
                if Identity in ("LUI", "lui-gtk"):
                    Inspect(App, Found, Actions)
        Samples.append((Attempt, Child.poll(), Identity, Found.copy()))
        Samples = Samples[-4:]
        if Found.get("Qualification input", ())[:2] == ("text", "Synthetic accessibility description") and Found.get("Qualification action", (None,))[0] == "push button":
            break
        time.sleep(0.05)
    assert Found.get("Qualification input", ())[:2] == ("text", "Synthetic accessibility description"), "Input role/description missing from AT-SPI"
    assert Found.get("Qualification action", (None,))[0] == "push button", "Button missing from AT-SPI"
    if "--focused" in sys.argv:
        assert Found["Qualification input"][2], "Input focus missing from AT-SPI"
    # Advance only after the initial metadata is observed, through the owned
    # fixture's accessibility action rather than a startup-time race.
    assert Actions["Qualification action"].queryAction().doAction(0), "AT-SPI action refused"
    for Attempt in range(100):
        Refresh()
        Found = {}
        for App in pyatspi.Registry.getDesktop(0):
            if App.name in ("LUI", "lui-gtk") and App.get_process_id() == Child.pid:
                Inspect(App, Found, {})
        if Found.get("Fallback action", (None,))[0] == "push button":
            break
        time.sleep(0.05)
    assert Found.get("Fallback action", (None,))[0] == "push button", "Cleared label did not restore native text"
    print("[LUI:GtkTest] Live AT-SPI roles, names, description and label updates passed")
except Exception:
    Errors.flush()
    Errors.seek(0, os.SEEK_END)
    Errors.seek(max(0, Errors.tell() - 4096))
    print("[LUI:GtkTest] Bounded synthetic discovery evidence:", Samples)
    print("[LUI:GtkTest] Display configured:", bool(Environment.get("DISPLAY")), "authorization readable:", os.access(Environment.get("XAUTHORITY", ""), os.R_OK))
    print("[LUI:GtkTest] Child status:", Child.poll(), "stderr tail:", Errors.read())
    raise
finally:
    Child.terminate()
    try:
        Child.wait(timeout=5)
    except subprocess.TimeoutExpired:
        Child.kill()
        Child.wait()
    Errors.close()
