"""Prepare the synthetic declared asset and reproducible local module pins."""
import base64
import hashlib
import json
from pathlib import Path

Root = Path(__file__).resolve().parent
(Root / "Avatar.png").write_bytes(base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
))
Names = ("Scope.luau", "State.luau", "VirtualList.luau")
Pins = [{"Path": Name, "Sha256": hashlib.sha256(
    (Root / "Vendor" / "LuiUI" / Name).read_bytes()).hexdigest()} for Name in Names]
Lock = {"SchemaVersion": 1, "Packages": [{"Alias": "LuiUI", "Provenance": {
    "Kind": "LocalVendor", "Id": "LuiUI-0.1.0"}, "Files": Pins}]}
(Root / "Modules.lock.json").write_text(json.dumps(Lock, indent=2) + "\n", encoding="utf-8")
print("[LUI:Conversation] Synthetic asset and module lock prepared")
