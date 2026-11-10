#!/usr/bin/env python3
"""Stage fixed signed test applications, reusing the pinned shared Luau modules."""
import argparse
import base64
import json
import pathlib
import shutil
import struct
import zlib


def Png(Width, Height):
    def Chunk(Name, Data):
        return struct.pack('>I', len(Data)) + Name + Data + struct.pack('>I', zlib.crc32(Name + Data))
    Rows = (b'\0' + b'\x38\x78\xb8\xff' * Width) * Height
    return b'\x89PNG\r\n\x1a\n' + Chunk(b'IHDR', struct.pack('>IIBBBBB', Width, Height, 8, 6, 0, 0, 0)) + Chunk(b'IDAT', zlib.compress(Rows)) + Chunk(b'IEND', b'')


def Stage(Source, Output):
    for Name in ('Conversation', 'Modular'):
        Target = Output / Name
        Target.mkdir(parents=True, exist_ok=True)
        Base = Source / 'examples' / Name.lower()
        Manifest = json.loads((Base / 'lui.json').read_text())
        Manifest['Capabilities'] = []
        Manifest['Diagnostics'] = {'Mode': 'Restricted'}
        Manifest['Sandbox'] = {'MaxMemoryBytes': 32 * 1024 * 1024, 'MaxInterrupts': 1000000}
        Modules = Manifest['Modules']
        for File in Modules['Files']:
            Path = Target / 'Src' / File
            Path.parent.mkdir(parents=True, exist_ok=True)
            Data = (Source / 'tests/mobile/workloads' / (Name + '.luau')).read_bytes() if File == 'Main.luau' else (Base / 'Src' / File).read_bytes()
            Path.write_bytes(Data.replace(b'\r\n', b'\n'))
        if Name == 'Conversation':
            for File in ('Modules.lock.json',):
                shutil.copyfile(Base / File, Target / File)
            (Target / 'Avatar.png').write_bytes(base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII='))
            for File in ('Scope.luau', 'State.luau', 'VirtualList.luau'):
                Path = Target / 'Vendor/LuiUI' / File
                Path.parent.mkdir(parents=True, exist_ok=True)
                Path.write_bytes((Base / 'Vendor/LuiUI' / File).read_bytes().replace(b'\r\n', b'\n'))
            Manifest['Assets'] += ['Pressure.png', 'Oversize.png', 'Broken.png']
            (Target / 'Pressure.png').write_bytes(Png(1024, 1024))
            (Target / 'Oversize.png').write_bytes(Png(1025, 1025))
            (Target / 'Broken.png').write_bytes(Png(1, 1)[:24])
        (Target / 'lui.json').write_text(json.dumps(Manifest, sort_keys=True), encoding='utf-8')


if __name__ == '__main__':
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--source', type=pathlib.Path, required=True)
    Parser.add_argument('--output', type=pathlib.Path, required=True)
    Args = Parser.parse_args()
    Stage(Args.source.resolve(), Args.output.resolve())
