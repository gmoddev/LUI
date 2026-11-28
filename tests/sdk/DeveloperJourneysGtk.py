"""Drive two standalone Developer Journeys windows on an isolated GTK X11 desktop."""

import argparse
import json
from pathlib import Path
import subprocess
import time

import pyatspi


def Wait(Probe, Stage, Limit=8):
    Deadline = time.monotonic() + Limit
    while time.monotonic() < Deadline:
        Result = Probe()
        if Result:
            return Result
        time.sleep(0.04)
    raise AssertionError(Stage)


def Tree(Node, Depth=0):
    if Depth < 14:
        yield Node
        for Index in range(min(Node.childCount, 200)):
            Child = Node.getChildAtIndex(Index)
            if Child:
                yield from Tree(Child, Depth + 1)


def X(*Arguments):
    return subprocess.run(["xdotool", *map(str, Arguments)], capture_output=True, text=True, timeout=6, check=True).stdout.strip()


def HasText(Text):
    try:
        return any(Text in (Node.name or "") for Node in Tree(pyatspi.Registry.getDesktop(0)))
    except (RuntimeError, LookupError):
        return False


def Peak(Pid):
    Status = Path(f"/proc/{Pid}/status").read_text()
    return int(next(Line for Line in Status.splitlines() if Line.startswith("VmHWM:")).split()[1]) * 1024


def Run(Root, Name, Title, Ready, Work, *Flags):
    App = Root / (Name + "Bundle")
    Began = time.monotonic()
    with (Root / f"{Name}-gtk.log").open("ab") as Log:
        with subprocess.Popen([App / "Start-LUI.sh", *Flags], cwd=Root / "Outside", stdout=Log, stderr=subprocess.STDOUT) as Child:
            def Window():
                try:
                    return X("search", "--onlyvisible", "--name", "^" + Title + "$").splitlines()[0]
                except (subprocess.CalledProcessError, IndexError):
                    if Child.poll() is not None:
                        raise RuntimeError(f"{Title} exited with {Child.returncode}")
                    return None
            try:
                Handle = Wait(Window, f"{Title} did not open")
                try:
                    Wait(lambda: HasText(Ready), f"{Title} did not report {Ready}")
                except AssertionError:
                    print([Node.name for Node in Tree(pyatspi.Registry.getDesktop(0)) if Node.name], flush=True)
                    raise
                Startup = (time.monotonic() - Began) * 1000
                X("windowactivate", "--sync", Handle)
                Work(Handle)
                Report = {"ReadyMs": round(Startup, 2), "PeakBytes": Peak(Child.pid)}
                X("key", "alt+F4")
                assert Child.wait(timeout=8) == 0, Title
                return Report
            finally:
                if Child.poll() is None:
                    Child.terminate()
                    try:
                        Child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        Child.kill()
                        Child.wait(timeout=3)


def Click(Handle, XPos, YPos):
    X("mousemove", "--window", Handle, XPos, YPos, "click", 1)


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("root", type=Path)
    Root = Parser.parse_args().root.resolve()

    def Inventory(Handle):
        Click(Handle, 110, 34)
        X("type", "Gasket")
        Click(Handle, 560, 34)
        Wait(lambda: HasText("Gasket") and HasText("Inventory ready"), "The item was not saved")
        Click(Handle, 125, 85)
        X("type", "Missing")
        Wait(lambda: HasText("No matching items."), "Inventory search did not filter")

    def Reload(Handle):
        Wait(lambda: HasText("Gasket"), "The item did not persist after restart")

    def Workbench(Handle):
        Click(Handle, 100, 27)
        X("key", "ctrl+a")
        X("type", "--", "-- TODO replacement")
        Wait(lambda: HasText("Lines 1 · TODO 1"), "The worker did not analyze the edit")
        Click(Handle, 635, 145)
        X("type", "replacement")
        Wait(lambda: HasText("Found at byte"), "Workbench search did not find the edit")

    Results = {
        "InventoryDesk": Run(Root, "InventoryDesk", "Inventory Desk", "Inventory ready", Inventory, "--allow-native"),
        "InventoryRestart": Run(Root, "InventoryDesk", "Inventory Desk", "Inventory ready", Reload, "--allow-native"),
        "SourceWorkbench": Run(Root, "SourceWorkbench", "Source Workbench", "Lines 2 · TODO 1", Workbench),
    }
    (Root / "GtkReport.json").write_text(json.dumps(Results, indent=2) + "\n", encoding="utf-8")
    print("[LUI:DeveloperJourneys] Native SQLite create/search/restart and CodeEditor/worker/search passed")


if __name__ == "__main__":
    Main()
