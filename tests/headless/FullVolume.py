"""Check selected-writer failure on a caller-provided tiny temporary filesystem."""
import argparse
import errno
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--sdk', required=True, type=Path)
    Parser.add_argument('--volume', required=True, type=Path)
    Args = Parser.parse_args()
    Sdk = Args.sdk.resolve()
    Volume = Args.volume.resolve(strict=True)
    Capacity = shutil.disk_usage(Volume)
    if not 65536 <= Capacity.free <= Capacity.total <= 262144:
        raise ValueError('Expected a dedicated temporary volume of 64–256 KiB')
    Inventory = json.loads((Sdk/'SdkManifest.json').read_bytes())
    Host = Sdk/Inventory['Components']['HeadlessHost']
    Manifest = Sdk/'Examples/atomic-copy/lui.json'
    with tempfile.TemporaryDirectory(dir=Volume) as Folder:
        Root = Path(Folder)
        Probe = Root/'capacity-probe'
        Data = bytes(range(256))*4096
        try:
            Probe.write_bytes(Data)
            raise AssertionError('Expected a full-volume write failure')
        except OSError as Error:
            assert Error.errno == errno.ENOSPC
        finally:
            Probe.unlink(missing_ok=True)
        Target = Root/'document'
        def Write(Data, Code):
            P = subprocess.run([Host,'--project',Manifest,'--write-file',Target,'--stdio'],
                input=Data,capture_output=True,timeout=15)
            assert (P.returncode,P.stdout,P.stderr) == (Code,b'',b'')
        Write(b'original',0)
        Write(Data,1)
        assert Target.read_bytes() == b'original' and list(Root.iterdir()) == [Target]
        Write(b'recovered',0)
        assert Target.read_bytes() == b'recovered'
    print('[LUI:Files] Real ENOSPC preserved the original, retired the draft and allowed the next commit')


if __name__ == '__main__':
    Main()
