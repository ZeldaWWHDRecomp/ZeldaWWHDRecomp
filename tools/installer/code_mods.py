"""Transactional game-code variants. No game bytes are stored in metadata."""
import errno
import hashlib
import json
import os
from pathlib import Path
import shutil
import time


# Windows renames (issue #282). os.replace is MoveFileExW(MOVEFILE_REPLACE_EXISTING), a FileRenameInformation
# rename that fails with ERROR_ACCESS_DENIED while any handle to the target is open, even one that allows
# delete sharing: "A file cannot be renamed if a file with the same name exists and has open handles"
# (FILE_RENAME_INFORMATION, Microsoft Learn). The running game polls code-mods-status.json while setup
# publishes it, so a plain replace loses whenever a poll is in flight. FileRenameInfoEx with
# FILE_RENAME_FLAG_POSIX_SEMANTICS (NTFS, Windows 10 1709 and later) "allow[s] replacing a file even if there are
# existing handles to it. Existing handles to the replaced file continue to be valid": open readers that allowed
# FILE_SHARE_DELETE (the game's reader does) keep the old contents, and new opens see the new file.
_FILE_RENAME_INFO_EX = 22  # FILE_INFO_BY_HANDLE_CLASS FileRenameInfoEx
_FILE_RENAME_FLAG_REPLACE_IF_EXISTS = 0x1
_FILE_RENAME_FLAG_POSIX_SEMANTICS = 0x2
# Older Windows, FAT32/exFAT and some network shares refuse the request itself: fall back to os.replace.
_POSIX_RENAME_UNSUPPORTED = {1, 50, 87, 124}  # invalid function, not supported, invalid parameter, invalid level
_SHARING_ERRORS = {5, 32, 33}  # access denied, sharing violation, lock violation


def _extended_path(path):
    """The extended-length absolute form of path for the Windows file APIs (no MAX_PATH limit)."""
    path = os.fspath(path)
    if path.startswith('\\\\?\\') or path.startswith('\\\\.\\'):
        return path
    # MSYS2's Python joins with "/"; an extended-length path is not normalised, so backslashes only
    path = os.path.abspath(path).replace('/', '\\')
    if path.startswith('\\\\'):
        return '\\\\?\\UNC\\' + path[2:]
    return '\\\\?\\' + path


def _windows_error(code, src, dst, message=None):
    """OSError for a Windows error code; sharing errors are PermissionError, as from os.replace."""
    cls = PermissionError if code in _SHARING_ERRORS else OSError
    return cls(errno.EACCES if cls is PermissionError else errno.EIO,
               message or 'Windows error %d' % code, os.fspath(src), code, os.fspath(dst))


def _posix_rename(src, dst):
    """Windows: replace dst with src using POSIX semantics. True when done, False when this system or
    volume does not offer it (the caller falls back to os.replace); OSError otherwise."""
    try:
        import ctypes
        from ctypes import wintypes
    except ImportError:
        return False
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    create = kernel32.CreateFileW
    create.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                       wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    create.restype = wintypes.HANDLE
    set_info = kernel32.SetFileInformationByHandle
    set_info.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    set_info.restype = wintypes.BOOL
    close = kernel32.CloseHandle
    close.argtypes = [wintypes.HANDLE]
    close.restype = wintypes.BOOL
    DELETE, SYNCHRONIZE = 0x00010000, 0x00100000
    SHARE_ALL = 0x1 | 0x2 | 0x4  # FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
    OPEN_EXISTING = 3
    handle = create(_extended_path(src), DELETE | SYNCHRONIZE, SHARE_ALL, None, OPEN_EXISTING, 0, None)
    if handle is None or handle == ctypes.c_void_p(-1).value:
        code = ctypes.get_last_error()
        raise _windows_error(code, src, dst, ctypes.FormatError(code).strip())
    try:
        target = _extended_path(dst)
        units = len(target.encode('utf-16-le')) // 2

        class RenameInfo(ctypes.Structure):  # FILE_RENAME_INFO with the Flags member of the union
            _fields_ = [('Flags', wintypes.DWORD), ('RootDirectory', wintypes.HANDLE),
                        ('FileNameLength', wintypes.DWORD), ('FileName', wintypes.WCHAR * (units + 1))]
        info = RenameInfo(_FILE_RENAME_FLAG_REPLACE_IF_EXISTS | _FILE_RENAME_FLAG_POSIX_SEMANTICS,
                          None, units * 2, target)
        if set_info(handle, _FILE_RENAME_INFO_EX, ctypes.byref(info), ctypes.sizeof(info)):
            return True
        code = ctypes.get_last_error()
    finally:
        close(handle)
    if code in _POSIX_RENAME_UNSUPPORTED:
        return False
    raise _windows_error(code, src, dst, ctypes.FormatError(code).strip())


def _replace_once(src, dst):
    if os.name == 'nt' and _posix_rename(src, dst):
        return
    os.replace(src, dst)


def atomic_replace(src, dst, timeout=5.0):
    """Publish src as dst atomically, also while the game reads dst.

    Windows: a POSIX-semantics rename first, which open readers cannot block. Where that is unavailable,
    or a reader without delete sharing (a virus scanner, an older game) holds the file, sharing errors
    are retried with backoff for up to `timeout` seconds; a poll lasts microseconds."""
    deadline = time.monotonic() + timeout
    delay = 0.01
    while True:
        try:
            _replace_once(src, dst)
            return
        except PermissionError:
            remaining = deadline - time.monotonic()
            if os.name != 'nt' or remaining <= 0:
                raise
            time.sleep(min(delay, remaining))
            delay = min(delay * 2, 0.1)


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.tmp')
    tmp.write_text(json.dumps(value), encoding='utf-8')
    atomic_replace(tmp, path)


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


def cache_name(key):
    """The folder of a cached build in code-builds: the first 16 hex digits of its fingerprint (issue #144:
    short paths on Windows; ready.json holds the full fingerprint). Releases up to v0.2.12 used all 64."""
    return key[:16]


def make_build_space(setup, data):
    """Evict only inactive variants created by this builder, oldest first."""
    minimum = 2 << 30
    if setup.free_space(str(data)) >= minimum:
        return
    try:
        active = json.loads((data / 'code-mods-active.json').read_text(encoding='utf-8'))['fingerprint']
    except FileNotFoundError:
        active = None
    except (ValueError, KeyError, TypeError):
        # An unreadable selection cannot safely identify the running cache.
        raise setup.SetupError('Not enough space; cannot identify the active code build for cache cleanup')
    candidates = []
    for cache in (data / 'code-builds').glob('*'):
        if cache.name in (active, cache_name(active or '')) or cache.is_symlink() or not cache.is_dir():
            continue
        if len(cache.name) not in (16, 64) or any(c not in '0123456789abcdef' for c in cache.name):
            continue
        try:
            ready = cache / 'ready.json'
            record = json.loads(ready.read_text(encoding='utf-8'))
            key = record.get('fingerprint')
            if isinstance(key, str) and cache.name in (key, cache_name(key)):
                candidates.append((ready.stat().st_mtime, cache))
        except (OSError, ValueError, AttributeError):
            continue
    for _, cache in sorted(candidates):
        shutil.rmtree(setup.longpath(cache))
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
            # Progress is informational: a publication that still loses to a reader after
            # atomic_replace's retries is skipped (the next one follows), never a failed build.
            try:
                atomic_json(status_file, {'state': 'building', 'stage': stage,
                                         'done': done, 'total': total})
            except PermissionError:
                pass

    try:
        check()
        progress('Getting the compiler')
        tc = setup.get_toolchain(ctx.manifest['toolchain'], ctx.data_dir, setup.UI(False))
        version = setup.run_logged(tc.cc + ['--version'], env=tc.env, what='checking the compiler')
        key = fingerprint(setup.PKG, ctx.manifest, ctx.game_dir, [tc.cc, version], hooks)
        cache = data / 'code-builds' / cache_name(key)
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
            work = data / ('code-build-' + cache_name(key) + '.partial')
            shutil.rmtree(setup.longpath(work), ignore_errors=True)
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
            shutil.rmtree(setup.longpath(work / 'gen'))
            shutil.rmtree(setup.longpath(work / 'obj'))
            archive = work / 'libgamecode.a'
            if archive.exists():
                archive.unlink()
            cache.parent.mkdir(parents=True, exist_ok=True)
            if cache.exists():
                shutil.rmtree(setup.longpath(cache))
            os.replace(setup.longpath(work), setup.longpath(cache))
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
            shutil.rmtree(setup.longpath(work), ignore_errors=True)
        lock.close()


def remember_installed(setup, ctx, tc, hooks, exe, gen, objs):
    """Keep the initial variant too, so the first round trip can use its cache."""
    data = Path(ctx.data_dir)
    if setup.free_space(str(data)) < (2 << 30):
        (data / 'code-mods-active.json').unlink(missing_ok=True)
        return
    lock = BuildLock(data / 'code-build.lock', setup.SetupError)
    stage = None
    try:
        version = setup.run_logged(tc.cc + ['--version'], env=tc.env, what='checking the compiler')
        key = fingerprint(setup.PKG, ctx.manifest, ctx.game_dir, [tc.cc, version], hooks)
        cache = data / 'code-builds' / cache_name(key)
        stage = data / ('code-build-' + cache_name(key) + '.initial')
        shutil.rmtree(setup.longpath(stage), ignore_errors=True)
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
        if cache.exists():
            shutil.rmtree(setup.longpath(cache))
        os.replace(setup.longpath(stage), setup.longpath(cache))
        stage = None
        atomic_json(data / 'code-mods-active.json', {
            'state': 'ready', 'hooks': bool(hooks), 'fingerprint': key,
            'exe': str((cache / 'bin' / ctx.manifest['exe']).resolve()),
            'user_dir': str((data / 'user').resolve()) if setup.PORTABLE else ''})
    finally:
        if stage:
            shutil.rmtree(setup.longpath(stage), ignore_errors=True)
        lock.close()
