"""Transactional game-code variants. No game bytes are stored in metadata."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import time


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.tmp')
    tmp.write_text(json.dumps(value), encoding='utf-8')
    os.replace(tmp, path)


def file_sha256(stream):
    digest = hashlib.sha256()
    while chunk := stream.read(1024 * 1024):
        digest.update(chunk)
    return digest.hexdigest()


def file_hashes(paths, base=None):
    result = {}
    for path in paths:
        path = Path(path)
        with path.open('rb') as stream:
            name = path.relative_to(base).as_posix() if base else path.name
            result[name] = file_sha256(stream)
    return result


def hooks_option(value=None):
    value = os.environ.get('WWHD_CODE_MODS', '0') if value is None else str(value)
    if value not in ('0', '1'):
        raise ValueError('WWHD_CODE_MODS / --code-mods must be 0 or 1')
    return value == '1'


def fingerprint(pkg, manifest, game_dir, compiler, hooks):
    """Include every packaged build input, game hash, host compiler and hooks mode."""
    h = hashlib.sha256()
    h.update(json.dumps({'format': 1, 'manifest': manifest, 'compiler': compiler,
                         'hooks': bool(hooks)}, sort_keys=True).encode())
    paths = [Path(game_dir) / 'code/cking.rpx']
    for directory in ('sdk', 'tools/recomp'):
        paths += sorted(p for p in (Path(pkg) / directory).rglob('*')
                        if p.is_file() and '__pycache__' not in p.parts)
    for p in paths:
        name = 'game-code' if p == paths[0] else p.relative_to(pkg).as_posix()
        h.update(name.encode() + b'\0')
        with p.open('rb') as f:
            while chunk := f.read(1024 * 1024):
                h.update(chunk)
    return h.hexdigest()


def make_build_space(setup, data):
    """Evict only inactive variants created by this builder, oldest first."""
    minimum = 2 << 30
    if setup.free_space(str(data)) >= minimum:
        return
    try:
        active = json.loads((data / 'code-mods-active.json').read_text())['fingerprint']
    except FileNotFoundError:
        active = None
    except (ValueError, KeyError, TypeError):
        # An unreadable selection cannot safely identify the running cache.
        raise setup.SetupError('Not enough space; cannot identify the active code build for cache cleanup')
    candidates = []
    for cache in (data / 'code-builds').glob('*'):
        if cache.name == active or cache.is_symlink() or not cache.is_dir():
            continue
        if len(cache.name) != 64 or any(c not in '0123456789abcdef' for c in cache.name):
            continue
        try:
            ready = cache / 'ready.json'
            record = json.loads(ready.read_text())
            if record.get('fingerprint') == cache.name:
                candidates.append((ready.stat().st_mtime, cache))
        except (OSError, ValueError, AttributeError):
            continue
    for _, cache in sorted(candidates):
        shutil.rmtree(cache)
        if setup.free_space(str(data)) >= minimum:
            return
    raise setup.SetupError('Not enough space for rebuilding game code (2 GB needed); previous build retained')


class BuildLock:
    """OS-owned lock: a crashed setup cannot leave a permanently busy directory."""
    def __init__(self, path, error_type):
        self.file = Path(path).open('a+b')
        try:
            if os.name == 'nt':
                import msvcrt
                self.file.seek(0, os.SEEK_END)
                if not self.file.tell():
                    self.file.write(b'0'); self.file.flush()
                self.file.seek(0)
                msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.file.close()
            raise error_type('Another code-mod rebuild is running; wait for it to finish')

    def close(self):
        if os.name == 'nt':
            import msvcrt
            self.file.seek(0)
            msvcrt.locking(self.file.fileno(), msvcrt.LK_UNLCK, 1)
        self.file.close()


def rebuild(setup, ctx, hooks, status_file=None, cancel_file=None):
    """Publish a ready variant only after translation, compilation and link succeed.

    The installed/running executable is never overwritten. Selection is a single
    atomic metadata replacement, consumed by the runtime at its next restart.
    """
    data = Path(ctx.data_dir)
    data.mkdir(parents=True, exist_ok=True)
    try:
        lock = BuildLock(data / 'code-build.lock', setup.SetupError)
    except Exception as e:
        if status_file:
            atomic_json(status_file, {'state': 'error', 'message': str(e)})
        raise
    started = time.monotonic()
    work = None

    def check():
        if cancel_file and Path(cancel_file).exists():
            raise setup.SetupError('Code-mod rebuild cancelled; previous build retained')

    def progress(stage, done=0, total=0):
        if status_file:
            atomic_json(status_file, {'state': 'building', 'stage': stage,
                                     'done': done, 'total': total})

    try:
        check()
        progress('Getting the compiler')
        tc = setup.get_toolchain(ctx.manifest['toolchain'], ctx.data_dir, setup.UI(False))
        version = setup.run_logged(tc.cc + ['--version'], env=tc.env, what='checking the compiler')
        key = fingerprint(setup.PKG, ctx.manifest, ctx.game_dir, [tc.cc, version], hooks)
        cache = data / 'code-builds' / key
        exe = cache / 'bin' / ctx.manifest['exe']
        ready = cache / 'ready.json'
        cached = False
        if ready.is_file() and exe.is_file():
            try:
                record = json.loads(ready.read_text(encoding='utf-8'))
                with exe.open('rb') as f:
                    cached = record.get('sha256') == file_sha256(f)
                cached = cached and record.get('hooks') is bool(hooks) and record.get('fingerprint') == key
            except (OSError, ValueError, AttributeError):
                # Interrupted or damaged metadata is a cache miss, never a reason
                # to discard the previous working selection.
                cached = False
        if not cached:
            work = data / ('code-build-' + key + '.partial')
            shutil.rmtree(work, ignore_errors=True)
            work.mkdir(parents=True)
            make_build_space(setup, data)
            progress('Translating game code')
            setup.recompile(ctx.game_dir, str(work / 'gen'), hooks=hooks)
            check()
            progress('Compiling game code')
            objs = setup.compile_gamecode(tc, ctx.manifest, str(work / 'gen'), str(work / 'obj'),
                                         ctx.args.jobs or setup.default_jobs(), cancel=check,
                                         progress=lambda n, total: progress('Compiling game code', n, total))
            check()
            progress('Linking game code')
            target = work / 'bin' / ctx.manifest['exe']
            target.parent.mkdir()
            setup.link_game(tc, ctx.manifest, objs, str(work), str(target))
            for name in ctx.manifest.get('runtime_files', []):
                shutil.copy2(Path(setup.PKG) / 'sdk/runtime' / name, target.parent / name)
            if setup.PORTABLE:
                (target.parent / 'portable.txt').write_text('')
            with target.open('rb') as f:
                digest = file_sha256(f)
            record = {'fingerprint': key, 'hooks': bool(hooks), 'sha256': digest,
                      'gamecode_objects': file_hashes(objs),
                      'generated': file_hashes(sorted(p for p in (work / 'gen').rglob('*') if p.is_file()), work / 'gen')}

            atomic_json(work / 'ready.json', record)
            check()
            # Completed outputs only; caches remain small enough to keep both modes.
            shutil.rmtree(work / 'gen')
            shutil.rmtree(work / 'obj')
            archive = work / 'libgamecode.a'
            if archive.exists():
                archive.unlink()
            cache.parent.mkdir(parents=True, exist_ok=True)
            if cache.exists():
                shutil.rmtree(cache)
            os.replace(work, cache)
            work = None
        check()
        result = {'state': 'ready', 'hooks': bool(hooks), 'fingerprint': key,
                  'exe': str(exe.resolve()), 'cached': cached,
                  'seconds': round(time.monotonic() - started, 2),
                  'user_dir': str((data / 'user').resolve()) if setup.PORTABLE else ''}
        if status_file:
            atomic_json(status_file, result)
        # Selection is the last fallible write: a reporting error must not switch
        # builds while telling the caller that the rebuild failed.
        atomic_json(data / 'code-mods-active.json', result)
        return result
    except Exception as e:
        if status_file:
            atomic_json(status_file, {'state': 'error', 'message': str(e)})
        raise
    finally:
        if work:
            shutil.rmtree(work, ignore_errors=True)
        lock.close()


def remember_installed(setup, ctx, tc, hooks, exe, gen, objs):
    """Keep the initial variant too, so the first round trip can use its cache."""
    data = Path(ctx.data_dir)
    if setup.free_space(str(data)) < (2 << 30):
        return
    lock = BuildLock(data / 'code-build.lock', setup.SetupError)
    stage = None
    try:
        version = setup.run_logged(tc.cc + ['--version'], env=tc.env, what='checking the compiler')
        key = fingerprint(setup.PKG, ctx.manifest, ctx.game_dir, [tc.cc, version], hooks)
        cache = data / 'code-builds' / key
        if cache.exists():
            return
        stage = data / ('code-build-' + key + '.initial')
        shutil.rmtree(stage, ignore_errors=True)
        (stage / 'bin').mkdir(parents=True)
        source = Path(exe)
        shutil.copy2(source, stage / 'bin' / ctx.manifest['exe'])
        for name in ctx.manifest.get('runtime_files', []):
            shutil.copy2(source.parent / name, stage / 'bin' / name)
        if setup.PORTABLE:
            (stage / 'bin/portable.txt').touch()
        with source.open('rb') as stream:
            digest = file_sha256(stream)
        atomic_json(stage / 'ready.json', {
            'fingerprint': key, 'hooks': bool(hooks), 'sha256': digest,
            'gamecode_objects': file_hashes(objs),
            'generated': file_hashes(sorted(p for p in Path(gen).rglob('*') if p.is_file()), Path(gen))})
        cache.parent.mkdir(parents=True, exist_ok=True)
        os.replace(stage, cache)
        stage = None
    finally:
        if stage:
            shutil.rmtree(stage, ignore_errors=True)
        lock.close()
