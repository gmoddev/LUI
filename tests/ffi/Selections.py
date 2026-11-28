"""Exercise native image selection through the released-shape broker executable."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--broker', type=Path, required=True)
    Parser.add_argument('--fixture', type=Path, required=True)
    Args = Parser.parse_args()
    Broker, Fixture = Args.broker.resolve(), Args.fixture.resolve()
    Digest = hashlib.sha256(Fixture.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix='lui-ffi-selection-') as Temp:
        # A valid native library in the working directory must still require explicit selection.
        (Path(Temp) / Fixture.name).write_bytes(Fixture.read_bytes())
        def Call(Selection, Expected=None, Symbol='LuiFfiEchoU32'):
            Packet = {'Library': Selection, 'Symbol': Symbol, 'Params': ['u32'], 'Result': 'u32', 'Args': [42]}
            Result = subprocess.run([Broker, '--call'], input=json.dumps(Packet) + '\n', text=True,
                                    capture_output=True, cwd=Temp, timeout=8)
            Reply = json.loads(Result.stdout)
            if Expected:
                assert Result.returncode == 1 and Reply == {'Ok': False, 'Error': Expected}, Reply
            else:
                assert Result.returncode == 0 and Reply['Result']['Value'] == 42, Reply
        Call({'Kind':'Selected', 'Name':str(Fixture), 'Sha256':Digest})
        Call({'Kind':'Selected', 'Name':Fixture.name}, 'AbsolutePathRequired')
        Call({'Kind':'System', 'Name':Fixture.name}, 'UnknownSystemLibrary')
        Call({'Kind':'Selected', 'Name':str(Fixture), 'Sha256':'0' * 64}, 'DigestMismatch')
        Call({'Kind':'Selected', 'Name':str(Fixture), 'Sha256':'xyz'}, 'InvalidDigest')
        Call({'Kind':'Selected', 'Name':str(Path(Temp) / 'AbsentLibrary')}, 'LibraryUnavailable')
        Call({'Kind':'Packaged', 'Name':Fixture.name}, 'InvalidPackagedLibrary')
        Call({'Kind':'Packaged', 'Name':'nested/' + Fixture.name, 'Sha256':Digest}, 'InvalidPackagedLibrary')
        Call({'Kind':'Selected', 'Name':str(Fixture)}, 'SymbolUnavailable', Symbol='LuiFfiAbsent')
    print('[LUI:Ffi] Explicit selection, cwd exclusion, symbol lookup, digest and packaged-path checks passed')


if __name__ == '__main__':
    Main()
