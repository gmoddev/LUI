"""Exercise SDK-staged applications through GTK accessibility in isolated X11.

Run with xvfb-run/dbus-run-session and the existing pyatspi/xfwm4/xdotool tools.
This qualifies software input and native clipboard/storage, not physical input,
Wayland, OS trust prompts or file selection (the native backend tests cover it).
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

import pyatspi


def Wait(Probe, Message, Seconds=10):
    Deadline = time.monotonic() + Seconds
    while time.monotonic() < Deadline:
        Value = Probe()
        if Value:
            return Value
        time.sleep(0.05)
    raise TimeoutError(Message)


def RetireProcess(Child):
    if Child.poll() is None:
        Child.terminate()
        try:
            Child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            Child.kill()
            Child.wait(timeout=5)


def Descendants(Node, Depth=0):
    if Depth > 20:
        return []
    Items = [Node]
    for Index in range(min(Node.childCount, 256)):
        Child = Node.getChildAtIndex(Index)
        if Child:
            Items.extend(Descendants(Child, Depth + 1))
    return Items


class Application:
    def __init__(Self, Host, Project, Environment, Log, Options=()):
        Self.Child = subprocess.Popen([str(Host), '--project', str(Project / 'lui.json'), *Options],
            cwd=Project, env=Environment, stdin=subprocess.DEVNULL,
            stdout=Log, stderr=subprocess.STDOUT)

    def Nodes(Self):
        assert Self.Child.poll() is None, 'Native application exited unexpectedly'
        Desktop = pyatspi.Registry.getDesktop(0)
        Items = []
        for Index in range(Desktop.childCount):
            Node = Desktop.getChildAtIndex(Index)
            if Node and Node.get_process_id() == Self.Child.pid:
                Items.extend(Descendants(Node))
        return Items

    def Texts(Self):
        return [Node.name or '' for Node in Self.Nodes()]

    def Has(Self, Text):
        return any(Text in Value for Value in Self.Texts())

    def Click(Self, Name):
        def Find():
            for Node in Self.Nodes():
                if Node.name == Name and Node.getState().contains(pyatspi.STATE_SHOWING):
                    try:
                        Action = Node.queryAction()
                        if Action.nActions:
                            return Action
                    except NotImplementedError:
                        pass
            return None
        Action = Wait(Find, 'Native action missing: ' + Name)
        assert Action.doAction(0), 'Native action rejected: ' + Name

    def Editors(Self):
        Values = []
        for Node in Self.Nodes():
            if not Node.getState().contains(pyatspi.STATE_SHOWING):
                continue
            try:
                if Node.queryComponent().getExtents(pyatspi.DESKTOP_COORDS).height > 150:
                    Node.queryEditableText()
                    Values.append(Node)
            except NotImplementedError:
                pass
        return Values

    def Text(Self, Editor):
        return Editor.queryText().getText(0, -1)

    def Windows(Self):
        Result = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(Self.Child.pid)],
            capture_output=True, text=True, timeout=5)
        return Result.stdout.splitlines()

    def Close(Self, Title):
        Window = Wait(lambda: next((Value for Value in Self.Windows()
            if subprocess.check_output(['xdotool', 'getwindowname', Value], text=True, timeout=5).strip() == Title), None),
            'Native window missing: ' + Title)
        subprocess.run(['xdotool', 'windowactivate', '--sync', Window], check=True, timeout=5)
        subprocess.run(['xdotool', 'key', '--clearmodifiers', 'alt+F4'], check=True, timeout=5)
        assert Self.Child.wait(timeout=5) == 0, 'Native close failed'

    def Retire(Self):
        RetireProcess(Self.Child)


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--sdk', type=Path, required=True)
    Parser.add_argument('--projects', type=Path, required=True)
    Parser.add_argument('--output', type=Path, required=True)
    Args = Parser.parse_args()
    Metadata = json.loads((Args.sdk / 'SdkManifest.json').read_text(encoding='utf-8'))
    Host = (Args.sdk / Metadata['Components']['NativeHost']).resolve(strict=True)
    Manager = subprocess.Popen(['xfwm4', '--replace', '--compositor=off'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    Results, Current = [], None
    Report = {'Status': 'Running', 'Boundary': 'Native packaged GTK; AT-SPI and software X11 input', 'Results': Results}
    Args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix='lui-services-native-') as Temporary:
            Profile = Path(Temporary)
            (Profile / 'Home').mkdir()
            Environment = dict(os.environ, GDK_BACKEND='x11', GSK_RENDERER='cairo', GTK_USE_PORTAL='0',
                HOME=str(Profile / 'Home'),
                XDG_DATA_HOME=str(Profile / 'Data'), XDG_CONFIG_HOME=str(Profile / 'Config'),
                XDG_CACHE_HOME=str(Profile / 'Cache'))
            Environment.pop('GTK_A11Y', None)
            with (Args.output.parent / (Args.output.stem + '.log')).open('x') as Log:
                def Launch(Name):
                    App = Application(Host, (Args.projects / Name).resolve(), Environment, Log)
                    try:
                        Wait(App.Nodes, 'Application missing from accessibility tree')
                    except Exception:
                        try:
                            App.Retire()
                        except Exception as CleanupFailure:
                            Report.setdefault('CleanupErrors', []).append(str(CleanupFailure))
                        raise
                    return App
                Current = Launch('file-desk')
                Editor = Wait(Current.Editors, 'File editor absent')[0]
                assert Editor.queryEditableText().setTextContents('Native SDK clipboard and storage')
                Current.Click('Copy note'); Wait(lambda: Current.Has('Note copied.'), 'Clipboard write failed')
                Current.Click('Discard edits'); Wait(lambda: Current.Text(Editor) == '', 'Discard failed')
                Current.Click('Paste note'); Wait(lambda: Current.Text(Editor) == 'Native SDK clipboard and storage', 'Clipboard read failed')
                Current.Click('Save note'); Wait(lambda: Current.Has('Note saved in this app.'), 'Note save failed')
                Current.Close('File Desk'); Current = Launch('file-desk')
                Current.Click('Restore note'); Wait(lambda: Current.Has('Note restored.'), 'Storage reopen failed')
                assert Current.Text(Current.Editors()[0]) == 'Native SDK clipboard and storage'
                Current.Close('File Desk'); Results.append({'Application': 'File Desk', 'Status': 'PASS', 'Checks': ['native edit', 'clipboard round trip', 'private save', 'close/reopen/restore']})
                Current = Launch('network-desk')
                Current.Click('Send JSON'); Wait(lambda: Current.Has('Server received and echoed:'), 'Native HTTP failed')
                Current.Click('WebSocket'); Wait(lambda: Current.Has('WebSocket received and echoed:'), 'Native WebSocket failed')
                Current.Click('Invalid JSON'); Wait(lambda: Current.Has('Invalid JSON was rejected.'), 'Malformed JSON recovery failed')
                Current.Click('Slow request'); Wait(lambda: Current.Has('Request in progress.'), 'Slow request did not start')
                Current.Click('Cancel'); Wait(lambda: Current.Has('Request failed or was cancelled.'), 'Native cancellation failed')
                Current.Click('Send JSON'); Wait(lambda: Current.Has('Server received and echoed:'), 'Native network retry failed')
                Current.Close('Network Desk'); Results.append({'Application': 'Network Desk', 'Status': 'PASS', 'Checks': ['HTTP echo', 'WebSocket echo', 'invalid JSON', 'cancel/retry', 'native close']})
                Current = Launch('window-desk')
                Wait(lambda: Current.Has('Ready. All views edit the same note.'), 'Workspace startup failed')
                Editor = Wait(Current.Editors, 'Shared editor absent')[0]
                assert Editor.queryEditableText().setTextContents('Native shared note')
                Current.Click('Open shared note / another view')
                Wait(lambda: len(Current.Editors()) == 2 and all(Current.Text(Value) == 'Native shared note' for Value in Current.Editors()), 'Shared views did not synchronize')
                SharedWindow = next(Value for Value in Current.Windows()
                    if subprocess.check_output(['xdotool', 'getwindowname', Value], text=True, timeout=5).startswith('Shared note'))
                subprocess.run(['xdotool', 'windowactivate', '--sync', SharedWindow], check=True, timeout=5)
                Bounds = Current.Editors()[0].queryComponent().getExtents(pyatspi.DESKTOP_COORDS)
                subprocess.run(['xdotool', 'mousemove', '--sync', str(Bounds.x + 20), str(Bounds.y + 20), 'click', '1'], check=True, timeout=5)
                subprocess.run(['xdotool', 'key', '--clearmodifiers', 'ctrl+s'], check=True, timeout=5)
                Wait(lambda: Current.Has('Note saved. Any newer edits still need saving.'), 'Focused editor Save shortcut failed')
                Current.Click('Save workspace'); Wait(lambda: Current.Has('Workspace saved.'), 'Workspace save failed')
                Current.Close('Window Desk'); Current = Launch('window-desk')
                Wait(lambda: len(Current.Editors()) == 2 and all(Current.Text(Value) == 'Native shared note' for Value in Current.Editors()), 'Workspace/note restore failed')
                Current.Close('Window Desk'); Results.append({'Application': 'Window Desk', 'Status': 'PASS', 'Checks': ['shared native edits', 'focused Ctrl+S', 'workspace save', 'two-view close/reopen']})
                Current = None
            Report['Status'] = 'PASS'
    except Exception as Failure:
        Report.update(Status='FAIL', Reason=str(Failure))
        if Current and Current.Child.poll() is None:
            try:
                Report['ObservedTexts'] = Current.Texts()
                Report['ObservedEditors'] = [Current.Text(Editor) for Editor in Current.Editors()]
            except Exception as InspectionFailure:
                Report['InspectionFailure'] = str(InspectionFailure)
        raise
    finally:
        try:
            if Current:
                Current.Retire()
        except Exception as CleanupFailure:
            Report.update(Status='FAIL')
            Report.setdefault('CleanupErrors', []).append(str(CleanupFailure))
        finally:
            try:
                RetireProcess(Manager)
            except Exception as CleanupFailure:
                Report.update(Status='FAIL')
                Report.setdefault('CleanupErrors', []).append(str(CleanupFailure))
            finally:
                Args.output.write_text(json.dumps(Report, indent=2) + '\n', encoding='utf-8')
        if Report['Status'] != 'PASS' and 'Reason' not in Report:
            raise RuntimeError('Native GTK cleanup failed; see receipt')


if __name__ == '__main__':
    Main()
