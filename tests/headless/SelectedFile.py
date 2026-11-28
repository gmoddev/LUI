"""Exercise selected-file authority, file positions and the public packaged checker."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def Sparse(File):
    if os.name == 'nt':
        import msvcrt
        Returned = ctypes.c_ulong()
        Handle = ctypes.c_void_p(msvcrt.get_osfhandle(File.fileno()))
        if not ctypes.windll.kernel32.DeviceIoControl(Handle, 0x900C4, None, 0, None, 0, ctypes.byref(Returned), None):
            raise OSError('The sparse-file fixture requires an NTFS volume supporting sparse files')


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--sdk', type=Path, required=True)
    Sdk = Parser.parse_args().sdk.resolve()
    Cli = Sdk / json.loads((Sdk / 'SdkManifest.json').read_bytes())['Components']['Cli']
    with tempfile.TemporaryDirectory(prefix='lui-range-') as Temporary:
        Root = Path(Temporary)
        Project = Root / 'FileSummary'
        shutil.copytree(Sdk / 'Examples/file-summary', Project)
        Manifest = Project / 'lui.json'
        Run = [Cli, 'run', Manifest, '--sdk', Sdk, '--mode', 'headless', '--stdio', 'true']
        Selected = Root / 'data.bin'
        Selected.write_bytes(b'abc\n' * (5 * 1024 * 1024 // 4))
        Result = subprocess.run([*Run, '--read-file', Selected], capture_output=True, timeout=30)
        assert (Result.returncode, Result.stdout, Result.stderr) == (0, b'5242880 bytes, 1310720 line feeds\n', b''), Result
        Denied = subprocess.run(Run, capture_output=True, timeout=10)
        assert Denied.returncode and not Denied.stdout
        with Selected.open('wb') as File:
            Sparse(File)
            File.seek(2**32 + 7)
            File.write(b'Q')
        (Project / 'Src/Main.luau').write_text("""--!strict
local Files = require("@LuiWorkbench/Files")
local Stdio = require("@Stdio/Stdio")
local S = app:GetService("FileService")
assert(type(S.ReadRange) == "function" and type(S.ByteLength) == "function")
local Picker = Files.New(S)
Stdio.SetExitStatus(4)
Picker:OpenFile(function(File, Reason)
    assert(File and not Reason)
    local F = File :: Files.File
    F:ByteLength(function(Length, Failure)
        assert(Length == 4294967304 and not Failure)
        F:ReadRange(4294967303, 65536, function(Data, Error)
            assert(Data and buffer.tostring(Data) == "Q" and not Error)
            F:ReadRange(4294967310, 1, function(Empty, AtEnd)
                assert(Empty and buffer.len(Empty) == 0 and not AtEnd)
                F:Read(function(Whole, Limit)
                    assert(not Whole and Limit == "LimitExceeded")
                    F:Close()
                    Picker:Dispose()
                    Stdio.SetExitStatus(0)
                end)
            end)
        end)
    end)
end)
""", encoding='utf-8')
        subprocess.run([Cli, 'check', Manifest, '--sdk', Sdk], check=True, timeout=16)
        State = subprocess.run([*Run, '--read-file', Selected], capture_output=True, timeout=16)
        assert (State.returncode, State.stdout, State.stderr) == (0, b'', b''), State
    print(json.dumps({'Status': 'PASS', 'Checks': ['5MiB chunked summary', 'missing grant', 'strict service types',
        'sparse offset above 4GiB', 'short and past-EOF reads', 'whole-file ceiling']}))


if __name__ == '__main__':
    Main()
