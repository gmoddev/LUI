#!/usr/bin/env python3
"""Check the two-ABI feasibility APK's ELF architecture and 16 KiB load alignment."""
import pathlib
import struct
import sys
import zipfile


def Check(Path):
    with zipfile.ZipFile(Path) as Archive:
        Notices = {'assets/LICENSE'} | {
            f'assets/THIRD-PARTY-LICENSES/{Name}.txt'
            for Name in ['Json', 'PicoSHA2', 'Kotlin', 'JetBrainsAnnotations', 'Luau', 'AndroidNdk', 'AndroidNdkToolchain']
        }
        if not Notices.issubset(Archive.namelist()) or any(Archive.getinfo(Name).file_size == 0 for Name in Notices):
            raise ValueError('Missing distribution notices')
        Libraries = {Name for Name in Archive.namelist() if Name.endswith('.so')}
        Expected = {'lib/arm64-v8a/libLuiMobileJni.so': 183, 'lib/x86_64/libLuiMobileJni.so': 62}
        if Libraries != set(Expected):
            raise ValueError('Unexpected native libraries')
        for Name, Machine in Expected.items():
            Entry = Archive.getinfo(Name)
            if Entry.file_size > 64 * 1024 * 1024 or Entry.compress_type != zipfile.ZIP_STORED:
                raise ValueError('Native packaging')
            Data = Archive.read(Name)
            if Data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', Data, 18)[0] != Machine:
                raise ValueError('ELF architecture')
            Offset = struct.unpack_from('<Q', Data, 32)[0]
            Stride, Count = struct.unpack_from('<HH', Data, 54)
            if Stride < 56 or Count > 256 or Offset + Stride * Count > len(Data):
                raise ValueError('ELF program headers')
            Loads = 0
            for Index in range(Count):
                Header = struct.unpack_from('<IIQQQQQQ', Data, Offset + Index * Stride)
                if Header[0] == 1:
                    Loads += 1
                    if Header[7] < 16384 or Header[2] % 16384 != Header[3] % 16384:
                        raise ValueError('ELF page alignment')
            if not Loads:
                raise ValueError('No load segments')
            print(f'[LUI:MobilePackage] {Name}: architecture and 16 KiB load alignment passed')


if __name__ == '__main__':
    try:
        Check(pathlib.Path(sys.argv[1]))
    except (ValueError, OSError, IndexError, KeyError, struct.error, zipfile.BadZipFile):
        print('[LUI:MobilePackage] Failed', file=sys.stderr)
        sys.exit(1)
