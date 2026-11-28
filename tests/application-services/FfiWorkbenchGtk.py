"""Exercise the packaged FFI workbench with GTK accessibility under isolated Xvfb."""
import argparse
import os
from pathlib import Path
import subprocess
import time

import pyatspi

from NativeGtk import Application, RetireProcess, Wait


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--app', required=True, type=Path)
    Args = Parser.parse_args()
    Root = Args.app.resolve(strict=True)
    Manager = subprocess.Popen(['xfwm4', '--replace', '--compositor=off'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    Workbench = None
    try:
        Environment = dict(os.environ, GDK_BACKEND='x11', GSK_RENDERER='cairo', GTK_USE_PORTAL='0')
        Workbench = Application(Root/'bin/lui-gtk', Root/'Application', Environment, None, ['--allow-native'])
        Wait(lambda: Workbench.Has('Ready'), 'Workbench did not start')
        def Action(Name):
            for Node in Workbench.Nodes():
                if Node.name == Name and Node.getRole() == pyatspi.ROLE_PUSH_BUTTON:
                    return Node.queryAction()
            raise AssertionError('Missing button ' + Name)
        Run, Cancel = Action('Run'), Action('Cancel')
        Edit = next(Node.queryEditableText() for Node in Workbench.Nodes()
                    if Node.getRole() in (pyatspi.ROLE_TEXT, pyatspi.ROLE_ENTRY)
                    and Node.getState().contains(pyatspi.STATE_EDITABLE))
        assert Run.doAction(0)
        Started = time.perf_counter()
        Edit.setTextContents('active')
        Edited = time.perf_counter()
        assert Cancel.doAction(0)
        Canceled = time.perf_counter()
        Wait(lambda: Workbench.Has('Canceled') and Workbench.Has('Native checksum active'), 'Edit or cancellation missing')
        Elapsed = (time.perf_counter() - Started) * 1000
        assert Run.doAction(0)
        Wait(lambda: Workbench.Has('Checksum 322936262'), 'Reference checksum missing')
        assert Run.doAction(0)
        Workbench.Close('Native checksum active')
        print('[LUI:Gtk] Workbench edit, cancel, native checksum, rerun and close passed '
              f'(edit request {(Edited-Started)*1000:.1f} ms, cancel request {(Canceled-Edited)*1000:.1f} ms, '
              f'combined state observed {Elapsed:.1f} ms)')
    finally:
        if Workbench: Workbench.Retire()
        RetireProcess(Manager)


if __name__ == '__main__':
    Main()
