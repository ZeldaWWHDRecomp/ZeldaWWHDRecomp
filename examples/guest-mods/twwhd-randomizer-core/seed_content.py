"""Plan a bounded content-only overlay from player-owned original/seed folders.

Inputs are the two content directories, never whole game roots. This read-only
planner does not validate the RPX profile or publish/copy player assets.
"""
import hashlib
import os
from pathlib import Path
import stat

FILE_LIMIT = 128 * 1024 * 1024
TOTAL_LIMIT = 512 * 1024 * 1024
FILE_COUNT_LIMIT = 10000


def _inventory(root):
    root = Path(root)
    if root.is_symlink() or not root.is_dir():
        raise ValueError('content root must be a real directory')
    files, names = {}, set()
    nodes = 0

    def walk(directory, prefix):
        nonlocal nodes
        if prefix.count('/') > 64:
            raise ValueError('content directory nesting is too deep')
        entries = []
        with os.scandir(directory) as scan:
            for entry in scan:
                nodes += 1
                if nodes > FILE_COUNT_LIMIT:
                    raise ValueError('too many content entries')
                entries.append(entry)
        entries.sort(key=lambda entry: entry.name)
        for entry in entries:
            relative = prefix + entry.name
            if any(c in relative for c in ('\\', ':', '\0')) or len(relative.encode('utf-8')) > 512:
                raise ValueError('unsupported content path')
            folded = relative.casefold()
            if folded in names:
                raise ValueError('case-colliding content paths')
            names.add(folded)
            mode = entry.stat(follow_symlinks=False).st_mode
            if stat.S_ISDIR(mode):
                walk(Path(directory) / entry.name, relative + '/')
            elif stat.S_ISREG(mode):
                if len(files) >= FILE_COUNT_LIMIT:
                    raise ValueError('too many content files')
                files[relative] = Path(directory) / entry.name
            else:
                raise ValueError('content symlinks and special files are unsupported')
    walk(root, '')
    return files


def _fingerprint(path):
    # Refuse links at open too; never follow a replacement symlink.
    descriptor = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
    with os.fdopen(descriptor, 'rb') as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size > FILE_LIMIT:
            raise ValueError('content file exceeds supported size or is not regular')
        digest, size = hashlib.sha256(), 0
        while block := stream.read(1024 * 1024):
            size += len(block)
            if size > FILE_LIMIT:
                raise ValueError('content file grew past supported size')
            digest.update(block)
        after = os.fstat(stream.fileno())
        if size != before.st_size or (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (after.st_size, after.st_mtime_ns, after.st_ctime_ns):
            raise ValueError('content changed while reading')
    return size, digest.hexdigest()


def plan_content_delta(original, randomized):
    originals, seeds = _inventory(original), _inventory(randomized)
    if originals.keys() - seeds.keys():
        raise ValueError('seed removes content files; overlays cannot express deletions')
    rows, total = [], 0
    identity = hashlib.sha256(b"WWHD source-mod content v1\0")
    for name, source in sorted(seeds.items()):
        size, digest = _fingerprint(source)
        identity.update(name.encode('utf-8')+b'\0')
        identity.update(size.to_bytes(8, 'big'))
        identity.update(bytes.fromhex(digest))
        if name in originals and (size, digest) == _fingerprint(originals[name]):
            continue
        total += size
        if total > TOTAL_LIMIT:
            raise ValueError('content overlay exceeds package size limit')
        rows.append({'path': name, 'size': size, 'sha256': digest})
    return {'files': rows, 'total_size': total, 'seed_content_sha256': identity.hexdigest()}


def copy_content_delta(original, randomized, destination):
    """Copy a verified overlay into a new private directory.

    Never reuse/replace an existing destination. On failure the partial new
    directory is retained for inspection, with no completion record. Installation
    must consume only directories with seed-content.json and verified hashes.
    """
    import json
    destination = Path(destination)
    target = destination.resolve()
    for source in (Path(original).resolve(), Path(randomized).resolve()):
        if target == source or source in target.parents or target in source.parents:
            raise ValueError('overlay destination must be separate from both input trees')
    report = plan_content_delta(original, randomized)
    destination.mkdir()  # Exclusive creation; existing folders are untouched.
    content = destination / 'content'
    content.mkdir()
    source_root = Path(randomized)
    for row in report['files']:
        source = source_root / row['path']
        output = content / row['path']
        output.parent.mkdir(parents=True, exist_ok=True)
        descriptor = os.open(source, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
        with os.fdopen(descriptor, 'rb') as stream, output.open('xb') as copied:
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode) or before.st_size != row['size']:
                raise ValueError('seed content changed before copying')
            digest, size = hashlib.sha256(), 0
            while block := stream.read(1024 * 1024):
                size += len(block)
                if size > row['size']:
                    raise ValueError('seed content grew while copying')
                digest.update(block)
                copied.write(block)
            after = os.fstat(stream.fileno())
            if size != row['size'] or digest.hexdigest() != row['sha256'] or (
                before.st_mtime_ns, before.st_ctime_ns
            ) != (after.st_mtime_ns, after.st_ctime_ns):
                raise ValueError('seed content changed while copying')
    # Written last: a failed copy cannot present itself as a completed overlay.
    with (destination / 'seed-content.json').open('x', encoding='utf-8') as record:
        json.dump(report, record, indent=2)
        record.write('\n')
    return report


def main():
    import argparse
    import json
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=Path, help='original USA content directory')
    parser.add_argument('randomized', type=Path, help='matching randomized content directory')
    parser.add_argument('--output', type=Path, help='new private directory; omit to plan only')
    args = parser.parse_args()
    try:
        report = (copy_content_delta(args.original, args.randomized, args.output)
                  if args.output is not None else plan_content_delta(args.original, args.randomized))
    except (OSError, ValueError) as error:
        parser.exit(1, f'Content preparation failed: {error}\n')
    print(json.dumps({'files': len(report['files']), 'total_size': report['total_size'],
                      'copied': args.output is not None}))


if __name__ == '__main__':
    main()
