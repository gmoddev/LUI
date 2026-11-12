#!/usr/bin/env python3
"""Stage the pinned Microsoft release x64 CRT beside Windows executables."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def ReadRuntime(Root, Source):
    Metadata = json.loads((Root / 'cmake/WindowsCppRuntime.json').read_text(encoding='utf-8'))
    if Metadata['SchemaVersion'] != 1 or Metadata['Architecture'] != 'x64':
        raise ValueError('[LUI:Package] Unsupported C++ runtime profile')
    Files = {}
    for Name, Expected in Metadata['Files'].items():
        if not re.fullmatch(r'[a-z0-9_]+\.dll', Name) or not re.fullmatch(r'[a-f0-9]{64}', Expected):
            raise ValueError('[LUI:Package] Invalid C++ runtime inventory')
        File = Source / Name
        if not File.is_file() or File.is_symlink():
            raise ValueError('[LUI:Package] Missing C++ runtime file: ' + Name)
        Data = File.read_bytes()
        if hashlib.sha256(Data).hexdigest() != Expected:
            raise ValueError('[LUI:Package] C++ runtime differs from the pinned vendor payload: ' + Name)
        if len(Data) < 64 or Data[:2] != b'MZ':
            raise ValueError('[LUI:Package] Invalid C++ runtime PE: ' + Name)
        Offset = struct.unpack_from('<I', Data, 0x3c)[0]
        if (Offset + 24 > len(Data) or Data[Offset:Offset + 4] != b'PE\0\0'
                or struct.unpack_from('<H', Data, Offset + 4)[0] != 0x8664
                or not struct.unpack_from('<H', Data, Offset + 22)[0] & 0x2000):
            raise ValueError('[LUI:Package] C++ runtime must be a release x64 DLL: ' + Name)
        Files[Name] = Data
    if not {'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'} <= Files.keys():
        raise ValueError('[LUI:Package] Incomplete C++ runtime inventory')
    return Metadata, Files


def Stage(Root, Source, Destinations):
    Metadata, Files = ReadRuntime(Root, Source)
    Notice = (Root / 'third_party/licenses/VisualCppRuntime.txt').read_bytes()
    Provenance = (json.dumps(Metadata, indent=2) + '\n').encode('utf-8')
    Payload = {**Files, 'THIRD-PARTY-LICENSES/VisualCppRuntime.txt': Notice,
               'THIRD-PARTY-LICENSES/VisualCppRuntime.json': Provenance}
    Targets = []
    for Directory in Destinations:
        if not Directory.is_dir() or Directory.resolve() != Directory.absolute():
            raise ValueError('[LUI:Package] Existing unlinked host directory required')
        for Name, Data in Payload.items():
            Target = Directory / Name
            if Target.resolve() != Target.absolute() or (Target.exists() and
                    (not Target.is_file() or Target.read_bytes() != Data)):
                raise ValueError('[LUI:Package] Conflicting C++ runtime destination: ' + Name)
            for Parent in Target.parents:
                if Parent == Directory:
                    break
                if Parent.exists() and not Parent.is_dir():
                    raise ValueError('[LUI:Package] Conflicting C++ runtime destination: ' + Name)
            Targets.append((Target, Data))
    # Complete validation before mutating any host. Never replace existing bytes.
    for Target, Data in Targets:
        if not Target.exists():
            Target.parent.mkdir(parents=True, exist_ok=True)
            with Target.open('xb') as Stream:
                Stream.write(Data)
    return Metadata


if __name__ == '__main__':
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    Parser.add_argument('--runtime', type=Path, required=True)
    Parser.add_argument('--destination', type=Path, action='append', default=[])
    Parser.add_argument('--check', action='store_true')
    Options = Parser.parse_args()
    if Options.check:
        ReadRuntime(Options.root, Options.runtime)
    else:
        if not Options.destination:
            Parser.error('at least one --destination is required')
        Stage(Options.root, Options.runtime, Options.destination)
    print('[LUI:Package] Pinned release x64 C++ runtime verified')
