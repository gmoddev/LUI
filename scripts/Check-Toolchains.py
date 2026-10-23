"""Reject unqualified tooling before running a qualification profile."""
import argparse
import json
from pathlib import Path
import subprocess
import tomllib

Root = Path(__file__).resolve().parent.parent

def Check(Command, Expected):
    Actual = subprocess.check_output(Command, text=True, cwd=Root).strip()
    if Actual != Expected:
        raise SystemExit(f"[LUI:Build] Expected {Expected}; found {Actual}")
    print(f"[LUI:Build] {Actual}")


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument("--node", action="store_true")
    Parser.add_argument("--rust", action="store_true")
    Arguments = Parser.parse_args()
    Check(["dotnet", "--version"], json.loads((Root / "global.json").read_text())["sdk"]["version"])
    if Arguments.node:
        Check(["node", "--version"], "v" + (Root / ".node-version").read_text().strip())
    if Arguments.rust:
        Version = tomllib.loads((Root / "rust-toolchain.toml").read_text())["toolchain"]["channel"]
        Actual = subprocess.check_output(["rustc", "--version"], text=True, cwd=Root).split()[1]
        if Actual != Version:
            raise SystemExit(f"[LUI:Build] Expected Rust {Version}; found {Actual}")
        print(f"[LUI:Build] Rust {Actual}")


if __name__ == "__main__":
    Main()
