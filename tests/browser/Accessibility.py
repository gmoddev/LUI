"""Inspect only the named synthetic controls on an isolated qualification bus."""
import json
import time
import pyatspi
from gi.repository import GLib


def Inspect(Node, Found, Depth=0):
    if Node is None or Depth > 30:
        return
    try:
        if Node.name in ("Qualification action", "Qualification input") and Node.getRoleName() in ("push button", "entry", "text"):
            Found[Node.name] = {"Role": Node.getRoleName(), "Description": Node.description,
                                "Focused": Node.getState().contains(pyatspi.STATE_FOCUSED)}
        for Child in Node:
            Inspect(Child, Found, Depth + 1)
    except GLib.Error:
        return


for Attempt in range(100):
    Context = GLib.MainContext.default()
    for Event in range(64):
        if not Context.pending():
            break
        Context.iteration(False)
    Found = {}
    for App in pyatspi.Registry.getDesktop(0):
        if any(Name in App.name.casefold() for Name in ("chrom", "firefox")):
            Inspect(App, Found)
    if len(Found) == 2 and Found["Qualification input"]["Focused"]:
        break
    time.sleep(0.05)
assert Found.get("Qualification action", {}).get("Role") == "push button", "AT-SPI action missing"
assert Found.get("Qualification input", {}).get("Role") in ("entry", "text"), "AT-SPI input missing"
assert Found["Qualification input"]["Focused"], "AT-SPI focus missing"
assert Found["Qualification input"]["Description"] == "Synthetic qualification input", "AT-SPI description missing"
print(json.dumps(Found, sort_keys=True))
