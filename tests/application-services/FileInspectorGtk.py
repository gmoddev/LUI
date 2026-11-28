"""Run the packaged inspector on a dedicated Xvfb display and select a synthetic binary file."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile



def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--sdk', type=Path, required=True)
    Parser.add_argument('--apps', type=Path, help='Parent of independently built file-inspector and message-viewer folders')
    Parser.add_argument("--workers", action="store_true", help="Exercise the parallel viewer")
    Args = Parser.parse_args()
    Driver = "from FileInspectorGtk import Run; import sys; from pathlib import Path; Run(Path(sys.argv[1]), Path(sys.argv[2]) if len(sys.argv)>2 else None)"
    if Args.workers:
        Driver = Driver.replace('import Run;', 'import RunWorkers as Run;')
    Environment = dict(os.environ)
    for Name in ('DISPLAY','WAYLAND_DISPLAY','AT_SPI_BUS_ADDRESS','DBUS_SESSION_BUS_ADDRESS'):
        Environment.pop(Name,None)
    subprocess.run(['xvfb-run','-a','dbus-run-session','--',sys.executable,'-c',Driver,str(Args.sdk.resolve()),*([str(Args.apps.resolve())] if Args.apps else [])],
        cwd=Path(__file__).resolve().parent,env=Environment,check=True,timeout=80)


def Run(Sdk, Packages=None):
    from NativeGtk import Application, RetireProcess, Wait
    import pyatspi
    from gi.repository import GLib
    Host = Sdk / json.loads((Sdk / 'SdkManifest.json').read_bytes())['Components']['NativeHost']
    def Project(Name):
        if Packages:
            return Packages/Name/'bin/lui-gtk', Packages/Name/'Application'
        return Host, Sdk/'Examples'/Name
    Session = subprocess.Popen(['xfwm4','--compositor=off'], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        with tempfile.TemporaryDirectory(prefix='lui-file-inspector-') as Dir:
            Root = Path(Dir)
            Sample = Root / 'sample.bin'
            Sample.write_bytes(bytes(range(32)) + b'x' * 1048576)
            Environment = dict(os.environ, HOME=str(Root), XDG_DATA_HOME=str(Root/'Data'), XDG_CONFIG_HOME=str(Root/'Config'),
                XDG_CACHE_HOME=str(Root/'Cache'), GDK_BACKEND='x11', GSK_RENDERER='cairo', GTK_USE_PORTAL='0')
            Environment.pop('GTK_A11Y',None)
            with (Root/'Gtk.log').open('w') as Log:
                App = Application(*Project('file-inspector'),Environment,Log)
                try:
                    Wait(lambda: App.Has('Linux') and App.Has('x86_64'),'Platform fields missing')
                    App.Click('Choose a file')
                    Picker = Wait(lambda: next((Window for Window in App.Windows() if subprocess.check_output(
                        ['xdotool','getwindowname',Window],text=True).strip()=='Open file'),None),'Picker did not open')
                    def Keys(*Values):
                        subprocess.run(['xdotool',*Values],check=True,timeout=5)
                    Keys('windowactivate','--sync',Picker)
                    Wait(lambda: subprocess.check_output(['xdotool','getactivewindow'],text=True).strip()==Picker,'Picker focus missing')
                    Keys('key','--clearmodifiers','ctrl+l')
                    def Entry():
                        for Node in App.Nodes():
                            if not Node.getState().contains(pyatspi.STATE_FOCUSED): continue
                            try: return Node.queryEditableText()
                            except NotImplementedError: pass
                        return None
                    Editor = Wait(Entry,'Chooser location entry missing')
                    Editor.setTextContents(str(Sample))
                    Keys('key','Return')
                    Expect = bytes(range(32)).hex().upper()
                    def Selected():
                        try:
                            return any(all(Text in Label for Text in (Sample.name,'1048608 bytes',Expect)) for Label in App.Texts())
                        except GLib.Error:
                            # AT-SPI may invalidate the chooser tree during its close transition.
                            return False
                    Wait(Selected,'Selected byte length/prefix missing',15)
                    App.Close('File inspector')
                except Exception:
                    Log.flush()
                    print('Visible controls', [(N.getRoleName(), N.name) for N in App.Nodes() if N.name or N.getState().contains(pyatspi.STATE_FOCUSED)] if App.Child.poll() is None else [], flush=True)
                    print((Root/'Gtk.log').read_text(errors='replace')[-1200:],flush=True)
                    raise
                finally: App.Retire()
                Viewer = Application(*Project('message-viewer'),Environment,Log)
                try:
                    Wait(lambda: Viewer.Has('"Items":[1,2,3]'),'Initial message missing')
                    def MessageInput():
                        for Node in Viewer.Nodes():
                            if Node.getState().contains(pyatspi.STATE_SHOWING):
                                try: return Node.queryEditableText()
                                except NotImplementedError: pass
                        return None
                    Edit = Wait(MessageInput,'Message input missing')
                    Edit.setTextContents('invalid')
                    Wait(lambda: Viewer.Has('Enter a supported JSON document.'),'Validation missing')
                    Edit.setTextContents('{"x":1}')
                    Wait(lambda: Viewer.Has('12 bytes') and Viewer.Has('{"x":1}'),'Decoded message missing')
                    Viewer.Close('Message viewer')
                finally: Viewer.Retire()
    finally: RetireProcess(Session)
    print('[LUI:FileInspector] GTK picker, size, prefix, platform, message validation, recovery and close passed')


def RunWorkers(Sdk, Packages=None):
    from NativeGtk import Application, RetireProcess, Wait
    import pyatspi
    import time
    Host = Sdk / json.loads((Sdk / 'SdkManifest.json').read_bytes())['Components']['NativeHost']
    Project = Sdk / 'Examples/parallel-viewer'
    if Packages:
        Project = Packages / 'parallel-viewer/Application'
        Host = Packages / 'parallel-viewer/bin/lui-gtk'
    Manager = subprocess.Popen(['xfwm4','--compositor=off'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        with tempfile.TemporaryDirectory() as Temp:
            Environment = dict(os.environ, HOME=Temp, XDG_CONFIG_HOME=Temp, GDK_BACKEND='x11', GSK_RENDERER='cairo')
            Environment.pop('GTK_A11Y',None)
            App = Application(Host, Project, Environment, sys.stderr)
            try:
                def Count():
                    return next((int(Text[8:]) for Text in App.Texts() if Text.startswith('Batches ') and Text[8:].isdigit()),0)
                def Editor():
                    for Node in App.Nodes():
                        if Node.getState().contains(pyatspi.STATE_SHOWING):
                            try: return Node.queryEditableText()
                            except NotImplementedError: pass
                    return None
                Before = Wait(Count, 'No worker completions')
                Edit = Wait(Editor, 'Text box missing')
                Start = time.monotonic()
                Edit.setTextContents('active')
                Wait(lambda: App.Has('Edited active'), 'Owner edit missing')
                Latency = round(1000*(time.monotonic()-Start))
                After = Wait(lambda: (Value if (Value := Count()) > Before else 0), 'Workers stopped during edit')
                App.Click('Stop')
                Wait(lambda: App.Has('Stopped'), 'Actor shutdown missing')
                IdleStart = time.monotonic()
                Edit.setTextContents('idle')
                Wait(lambda: App.Has('Edited idle'), 'Idle edit missing')
                IdleLatency = round(1000*(time.monotonic()-IdleStart))
                App.Close('Parallel viewer')
                print(f'[LUI:Gtk] workers batches={Before}..{After} edit-observed-ms={Latency} idle-observed-ms={IdleLatency} passed')
            finally: App.Retire()
    finally: RetireProcess(Manager)


if __name__=='__main__':
    Main()
