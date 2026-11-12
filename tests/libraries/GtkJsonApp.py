"""Run the staged JSON example as a native GTK recipient in an isolated desktop."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

import pyatspi


def Texts(Node, Depth=0):
    if Depth > 16:
        return []
    Values = [Node.name or '']
    for Index in range(min(Node.childCount, 128)):
        Child = Node.getChildAtIndex(Index)
        if Child:
            Values.extend(Texts(Child, Depth + 1))
    return Values


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--host', required=True, type=Path)
    Parser.add_argument('--checker', required=True, type=Path)
    Parser.add_argument('--output', required=True, type=Path)
    Arguments = Parser.parse_args()
    Root = Path(__file__).resolve().parents[2]
    Spec = importlib.util.spec_from_file_location('Examples', Root / 'scripts/Stage-LibraryExample.py')
    Examples = importlib.util.module_from_spec(Spec); Spec.loader.exec_module(Examples)
    Manager = subprocess.Popen(['xfwm4', '--replace', '--compositor=off'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    Results = []
    try:
        with tempfile.TemporaryDirectory(prefix='lui-json-recipient-') as Temporary:
            Project = Examples.Stage(Root, 'json-client', Path(Temporary) / 'App')
            Checked = subprocess.run([str(Arguments.checker.resolve()), str(Root / 'types/LUI.d.luau'), '--project', str(Project / 'lui.json'), '--diagnostics', 'development'], capture_output=True, text=True)
            assert Checked.returncode == 0, Checked.stdout + Checked.stderr
            for Generation in (1, 2):
                with tempfile.TemporaryFile(mode='w+') as ErrorLog:
                    Environment = dict(os.environ, GDK_BACKEND='x11', GSK_RENDERER='cairo', GTK_USE_PORTAL='0')
                    Child = subprocess.Popen([str(Arguments.host.resolve()), '--project', str(Project / 'lui.json')], cwd=Project, env=Environment, stdout=subprocess.DEVNULL, stderr=ErrorLog)
                    try:
                        Deadline = time.monotonic() + 15
                        while time.monotonic() < Deadline:
                            assert Child.poll() is None, 'Native host exited before Ready'
                            if 'Local JSON status: Ready' in Texts(pyatspi.Registry.getDesktop(0)):
                                break
                            time.sleep(0.05)
                        else:
                            raise TimeoutError('Native JSON label did not become Ready')
                        Deadline = time.monotonic() + 5
                        Window = []
                        while time.monotonic() < Deadline:
                            Found = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(Child.pid)], capture_output=True, text=True)
                            Window = Found.stdout.splitlines()
                            if Window:
                                break
                            time.sleep(0.05)
                        assert len(Window) == 1
                        Title = subprocess.check_output(['xdotool', 'getwindowname', Window[0]], text=True).strip()
                        subprocess.run(['xdotool', 'windowactivate', '--sync', Window[0]], check=True)
                        subprocess.run(['xdotool', 'key', '--clearmodifiers', 'alt+F4'], check=True)
                        assert Child.wait(timeout=5) == 0
                        ErrorLog.seek(0)
                        assert '[LUI:Diagnostic]' not in ErrorLog.read()
                        Results.append({'Generation': Generation, 'Title': Title, 'NativeStatus': 'Local JSON status: Ready', 'Close': 'PASS'})
                    finally:
                        if Child.poll() is None:
                            Child.terminate(); Child.wait(timeout=5)
            Report = {'Status': 'PASS', 'Boundary': 'Actual GTK host, AT-SPI label and native close under isolated X11', 'Runs': Results,
                'Modules': {str(File.relative_to(Project)): hashlib.sha256(File.read_bytes()).hexdigest() for File in (Project / 'Vendor').rglob('*.luau')}}
            Arguments.output.write_text(json.dumps(Report, indent=2) + '\n')
            print('[LUI:GtkJsonApp] Staged native startup, JSON result, close and restart passed')
    finally:
        Manager.terminate(); Manager.wait(timeout=5)


if __name__ == '__main__':
    Main()
