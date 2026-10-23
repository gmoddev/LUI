"""Run the real Orca event loop; record only allowlisted synthetic names."""
import json
import os
from pathlib import Path
import runpy
import signal
import sys
import gi
gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import GLib
from orca import speech

Output = Path(os.environ["LUI_ORCA_EVIDENCE"])
Original = speech.speak
RetirementRequested = False


def Retire():
    from orca import orca
    orca.shutdown()
    return False


def RequestRetirement(Signal, Frame):
    global RetirementRequested
    if not RetirementRequested:
        RetirementRequested = True
        # A signal can interrupt an AT-SPI callback. Retire after that callback
        # returns, before freeing the device that its suspended work still uses.
        GLib.idle_add(Retire)


def Record(Content, *Arguments, **Options):
    Text = str(Content)
    for Name in ("Qualification action", "Qualification input"):
        if Name.casefold() in Text.casefold():
            with Output.open("a", encoding="utf-8") as File:
                File.write(json.dumps({"Name": Name}) + "\n")
    return Original(Content, *Arguments, **Options)


def Ready():
    signal.signal(signal.SIGTERM, RequestRetirement)
    with Output.open("a", encoding="utf-8") as File:
        File.write(json.dumps({"Ready": True}) + "\n")
    return False


speech.speak = Record
GLib.timeout_add(2000, Ready)
sys.argv = ["orca", "--replace", "--user-prefs", os.environ["LUI_ORCA_PREFS"]]
runpy.run_path("/usr/bin/orca", run_name="__main__")
