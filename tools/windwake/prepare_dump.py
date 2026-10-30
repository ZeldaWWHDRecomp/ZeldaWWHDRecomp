"""Extract only the user's WUX from its outer ZIP, bounded and with CRC + header validation.

This does not decrypt the disc or read any README included in the archive.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import zipfile

MAX_WUX = 16 * 1024**3


def prepare(source, destination):
    if destination.exists():
        raise ValueError('The destination already exists; choose a new .wux path')
    with zipfile.ZipFile(source) as bundle:
        entries = bundle.infolist()
        if len(entries) > 64:
            raise ValueError('Unexpected outer ZIP entry count')
        images = [e for e in entries if not e.is_dir() and e.filename.lower().endswith('.wux')]
        if len(images) != 1 or not 32 <= images[0].file_size <= MAX_WUX:
            raise ValueError('Expected exactly one bounded WUX disc image')
        entry = images[0]
        if shutil.disk_usage(destination.parent if destination.parent.exists() else source.parent).free < entry.file_size + 512 * 1024**2:
            raise ValueError('Not enough free disk space')
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_suffix('.wux.part')
        sha = hashlib.sha256()
        written = 0
        try:
            with bundle.open(entry) as incoming, temporary.open('xb') as outgoing:
                header = incoming.read(32)
                if len(header) != 32 or struct.unpack('<II', header[:8]) != (0x30585557, 0x1099D02E):
                    raise ValueError('The selected entry is not a WUX image')
                outgoing.write(header); sha.update(header); written += len(header)
                while block := incoming.read(1024 * 1024):
                    written += len(block)
                    if written > entry.file_size or written > MAX_WUX:
                        raise ValueError('WUX exceeds its declared size')
                    outgoing.write(block); sha.update(block)
                outgoing.flush()
                import os
                os.fsync(outgoing.fileno())
            if written != entry.file_size:
                raise ValueError('Incomplete disc image')
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)
    return {'path': str(destination.resolve()), 'bytes': written, 'sha256': sha.hexdigest()}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    import json
    print(json.dumps(prepare(args.source, args.destination), indent=2))
