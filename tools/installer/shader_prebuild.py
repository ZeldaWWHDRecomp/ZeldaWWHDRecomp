"""Prepare private shader caches from the player's files; never package the output.

A failed/cancelled generation never replaces a previously usable generation.
The runtime consumes active.json; native warming is deliberately separate from
head-start assembly so Android can finish warming inside its renderer lifecycle.
"""
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

SCHEMA = 1


def longpath(path):
    """setup.longpath (issue #144): an extended-length \\\\?\\ path with backslashes, so a cache folder is
    deleted on Windows whatever its depth; unchanged elsewhere and with a Python that joins paths with "/"
    (MSYS2's). This module also runs on its own (no setup import)."""
    if os.name != 'nt' or os.path.join('a', 'b') != 'a\\b':
        return path
    path = os.fspath(path)
    if path.startswith('\\\\?\\') or path.startswith('\\\\.\\'):
        return path
    path = os.path.abspath(path).replace('/', '\\')
    return '\\\\?\\UNC\\' + path[2:] if path.startswith('\\\\') else '\\\\?\\' + path


def file_hash(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def identity(game, template, package, renderer):
    game = Path(game)
    archives = sorted(p for p in game.rglob('*') if p.is_file() and
                      p.suffix.lower() in ('.sharcfb', '.rpx', '.szs', '.pack', '.sarc'))
    if not archives:
        raise ValueError('No game shader archives or executable found')
    return dict(schema=SCHEMA, renderer=renderer,
                template=file_hash(template), translator=file_hash(Path(package) / 'tools/shaderprep.py'),
                format=file_hash(Path(package) / 'tools/shaderprep_format.py') if (Path(package) / 'tools/shaderprep_format.py').is_file() else None,
                game={str(p.relative_to(game)): file_hash(p) for p in archives})


def metadata(path):
    stat = Path(path).stat()
    return {'size': stat.st_size, 'mtime': stat.st_mtime_ns // 1_000_000_000,
            'mtime_ns': stat.st_mtime_ns % 1_000_000_000}


def launch_identity(game, template, package, runtime):
    game = Path(game)
    return dict(game={p.relative_to(game).as_posix(): metadata(p) for p in sorted(game.rglob('*'))
                      if p.is_file() and p.suffix.lower() in ('.sharcfb', '.rpx', '.szs', '.pack', '.sarc')},
                template=metadata(template), translator=metadata(Path(package) / 'tools/shaderprep.py'),
                format=metadata(Path(package) / 'tools/shaderprep_format.py') if (Path(package) / 'tools/shaderprep_format.py').is_file() else None,
                runtime=metadata(runtime) if runtime and Path(runtime).is_file() else None)


def read_marker(path):
    try:
        value = json.loads(Path(path).read_text(encoding='utf-8'))
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError, TypeError):
        return {}


def opted_out(marker, wanted):
    saved = marker.get('identity', {})
    return (marker.get('schema') == SCHEMA and isinstance(saved, dict)
            and saved.get('template') == wanted.get('template') and saved.get('game') == wanted.get('game'))


def atomic_json(path, value):
    path = Path(path)
    # Different setup/startup processes must never share a temporary filename.
    # short (issue #144: cache paths stay far below Windows' MAX_PATH): manifest.json.t<8 random>
    descriptor, name = tempfile.mkstemp(prefix=path.name + '.t', dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, 'w') as stream:
            stream.write(json.dumps(value, sort_keys=True, indent=2) + '\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


@contextmanager
def activation_lock(root, timeout=5.0):
    """Shared with native publication: exclusive first-byte OS lock, bounded wait.

    The file persists; only the OS lock conveys ownership and process death
    releases it. Compilation deliberately happens outside this short lock.
    """
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    with (root / 'activation.lock').open('a+b') as stream:
        if os.fstat(stream.fileno()).st_size == 0:
            stream.write(b'\0')
            stream.flush()
        if os.name == 'nt':
            import msvcrt
            def acquire():
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            def release():
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            def acquire():
                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            def release():
                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)
        deadline = time.monotonic() + timeout
        while True:
            try:
                acquire()
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise TimeoutError('Shader cache activation is busy')
                time.sleep(.05)
        try:
            yield
        finally:
            release()


def migrate_legacy_caches(root):
    """Carry old generation caches forward without replacing any live entry."""
    root = Path(root)
    previous = read_marker(root / 'active.json')
    name = previous.get('generation')
    if not isinstance(name, str) or not name.startswith('generation-') or Path(name).name != name:
        return
    source = root / name
    if source.is_symlink():
        return
    live = root / 'live'
    for relative in ('shaders.bin', 'vulkan-shaders', 'vulkan-pipelines.bin', 'vulkan-pipelines.bin.recipes'):
        old = source / relative
        if old.is_symlink():
            continue
        paths = [old] if old.is_file() else list(old.rglob('*')) if old.is_dir() else []
        for path in paths:
            if not path.is_file() or path.is_symlink():
                continue
            target = live / relative / path.relative_to(old) if old.is_dir() else live / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            try:
                with path.open('rb') as incoming, target.open('xb') as outgoing:
                    shutil.copyfileobj(incoming, outgoing)
            except FileExistsError:
                pass


def cleanup_generations(root, selected):
    """Delete only completed, superseded generations without active readers."""
    root = Path(root)
    for directory in root.glob('generation-*'):
        if directory.name == selected or directory.is_symlink() or not directory.is_dir():
            continue
        # In-progress builders have no completed manifest yet.
        if not (directory / 'manifest.json').is_file():
            continue
        try:
            with (directory / 'in-use.lock').open('a+b') as stream:
                if os.fstat(stream.fileno()).st_size == 0:
                    stream.write(b'\0'); stream.flush()
                if os.name == 'nt':
                    import msvcrt
                    stream.seek(0); msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                # Windows cannot unlink an open lock; close only after exclusive
                # ownership is established. Publication lock excludes new selection.
                if os.name == 'nt':
                    stream.close()
                shutil.rmtree(longpath(directory))
        except OSError:
            pass


def cache_environment(generation, game, renderer):
    generation = Path(generation)
    live = generation.parent / 'live'
    live.mkdir(parents=True, exist_ok=True)
    return dict(WWHD_GAME=str(game), WWHD_HEADSTART=str(generation / 'headstart.bin'),
                WWHD_SHADER_CACHE=str(live / 'shaders.bin'),
                WWHD_VK_SHADER_CACHE=str(live / 'vulkan-shaders'),
                WWHD_VK_PIPELINE_CACHE_DIR=str(live),
                WWHD_RENDERER_RUNTIME=renderer, WWHD_NO_AUDIO='1', WWHD_HIDDEN_WINDOWS='1')


PROGRESS_PREFIX = 'WWHD-PROGRESS '
# Share of the bar the head-start assembly takes when a native warm follows it
# (several renderers: each warm takes an equal part of the rest).
ASSEMBLY_SHARE = 0.35
ASSEMBLY_SHARE_SEVERAL = 0.25


def parse_progress(line):
    """'WWHD-PROGRESS <done> <total> [detail]' (shaderprep.py build --progress, wwhd --warm-shaders)
    -> (done, total, detail); None for any other line."""
    if not line.startswith(PROGRESS_PREFIX):
        return None
    parts = line[len(PROGRESS_PREFIX):].strip().split(' ', 2)
    try:
        done, total = int(parts[0]), int(parts[1])
    except (IndexError, ValueError):
        return None
    if done < 0 or total < 0:
        return None
    return done, total, parts[2] if len(parts) > 2 else ''


# wwhd --warm-shaders exit status when the requested renderer cannot start on this computer
# (no MoltenVK/Vulkan driver): the game fell back to another renderer and warmed nothing.
RENDERER_UNAVAILABLE = 3
RENDERER_NAMES = {'metal': 'Metal', 'vulkan': 'Vulkan'}


class CommandFailed(RuntimeError):
    def __init__(self, returncode):
        super().__init__('Shader preparation command failed (%d)' % returncode)
        self.returncode = returncode


def default_renderers(platform=None):
    """Renderers setup prepares: macOS lets the player choose Metal or Vulkan (MoltenVK), so both;
    elsewhere Vulkan only."""
    return ['metal', 'vulkan'] if (platform or os.sys.platform) == 'darwin' else ['vulkan']


def renderer_states(manifest):
    """{renderer: 'ready'|'failed'|'unavailable'} of a manifest; older manifests name one
    renderer in their identity."""
    states = manifest.get('renderers')
    if isinstance(states, dict):
        return {k: v for k, v in states.items() if isinstance(k, str) and isinstance(v, str)}
    ident = manifest.get('identity')
    renderer = ident.get('renderer') if isinstance(ident, dict) else None
    return {renderer: 'ready'} if isinstance(renderer, str) and manifest.get('status') == 'ready' else {}


def run_process(command, env, cancel, progress):
    """Runs command; each output line reaches progress(line) while it runs."""
    # Pipe-free log prevents a blocked child when verbose compilers fill stdout. The child
    # writes through its own handle; a separate reader never moves the child's file offset.
    try:  # Windows: a terminated helper's workers may briefly keep the output open
        scratch = tempfile.TemporaryDirectory(prefix='wwhd-shader-output-', ignore_cleanup_errors=True)
    except TypeError:  # Python < 3.10
        scratch = tempfile.TemporaryDirectory(prefix='wwhd-shader-output-')
    with scratch as directory:
        path = Path(directory) / 'output.log'
        with path.open('wb') as output, path.open('rb') as reader:
            child = subprocess.Popen(command, env=env, stdout=output, stderr=subprocess.STDOUT)
            pending = b''
            def forward(final=False):
                nonlocal pending
                pending += reader.read()
                lines = pending.split(b'\n')
                pending = b'' if final else lines.pop()
                for line in lines:
                    text = line.decode(errors='replace').rstrip('\r')
                    if text:
                        progress(text)
            try:
                while child.poll() is None:
                    if cancel():
                        raise InterruptedError('Shader preparation skipped')
                    forward()
                    time.sleep(.1)
                forward(final=True)
                if child.returncode:
                    raise CommandFailed(child.returncode)
            finally:
                if child.poll() is None:
                    child.terminate()
                    try:
                        child.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait()


def _accepts_keyword(function, name):
    import inspect
    try:
        parameters = inspect.signature(function).parameters.values()
    except (TypeError, ValueError):
        return False
    return any(p.name == name or p.kind == p.VAR_KEYWORD for p in parameters)


class _Reporter:
    """Turns phase messages and machine progress into progress(text[, fraction=...]) calls.

    The fraction covers the whole preparation and never moves backwards; callers whose
    progress takes only text get the phase messages. Raw subprocess output goes to log."""

    def __init__(self, progress, log, cancel):
        self.progress = progress
        self.log = log
        self.cancel = cancel
        self.fractions = _accepts_keyword(progress, 'fraction')
        self.fraction = None
        self.phase = ''
        self.range = (0.0, 1.0)
        self.last = 0.0

    def message(self, text):
        if self.fractions and self.fraction is not None:
            self.progress(text, fraction=self.fraction)
        else:
            self.progress(text)

    def begin(self, text, low, high):
        self.phase, self.range = text, (low, high)
        if self.fraction is not None:
            self.fraction = max(self.fraction, low)
        self.message(text)

    def update(self, done, total, detail=''):
        if self.cancel():
            raise InterruptedError('Shader preparation skipped')
        if total <= 0:
            return
        low, high = self.range
        fraction = max(self.fraction or 0.0, low + (high - low) * min(1.0, done / total))
        now = time.monotonic()
        # At most ~5 updates per second (Android publishes each one to a file).
        if self.fraction is not None and done < total and now - self.last < 0.2:
            self.fraction = fraction
            return
        self.last = now
        self.fraction = fraction
        if self.fractions:
            self.progress('%s: %s' % (self.phase, detail) if detail else self.phase, fraction=fraction)

    def line(self, text):
        parsed = parse_progress(text)
        if parsed:
            self.update(*parsed)
        else:
            self.log(text)


MOD_TEMPLATE_MAX = 4 * 1024 * 1024


def package_templates(cache_root):
    """The code-free shader templates the mod manager's enabled packages ship with their stages
    (docs/mod-stages.md, "Shaders": a stage's "shaders" file): paths inside each package, by package id.
    The mod manager lives next to this cache root (the game's user folder: ModManager/)."""
    manager = Path(cache_root).parent / 'ModManager'
    try:
        profiles = json.loads((manager / 'profiles.json').read_text(encoding='utf-8'))
        enabled = profiles['profiles'][profiles['active']]['enabled']
    except (OSError, ValueError, KeyError, TypeError):
        return []
    out = []
    for ident in sorted(k for k, on in enabled.items() if on is True and isinstance(k, str)):
        folder = manager / 'Mods' / ident
        if Path(ident).name != ident or not folder.is_dir() or folder.is_symlink():
            continue
        try:
            manifest = json.loads((folder / 'manifest.json').read_text(encoding='utf-8'))
        except (OSError, ValueError):
            continue
        stages = manifest.get('stages') if isinstance(manifest, dict) else None
        for stage in stages if isinstance(stages, list) else []:
            rel = stage.get('shaders') if isinstance(stage, dict) else None
            if not isinstance(rel, str) or not rel.endswith('.wwht'):
                continue
            path = (folder / rel).resolve()
            try:
                inside = path.relative_to(folder.resolve())
            except ValueError:
                continue
            if inside.parts and path.is_file() and not path.is_symlink() and path.stat().st_size <= MOD_TEMPLATE_MAX and path not in out:
                out.append(path)
    return out


def merge_templates(package, base, extras, out, message):
    """The base template and the packages' templates (each passing the release guard's code-free check,
    or left out with a message) as one template at `out`; returns the extras used."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('shaderprep_for_mods', str(Path(package) / 'tools/shaderprep.py'))
    shaderprep = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(shaderprep)
    builder = shaderprep.TemplateBuilder()
    builder.add_template(str(base))
    used = []
    for path in extras:
        try:
            shaderprep.guard_template(Path(path).read_bytes())
            builder.add_template(str(path), allow_legacy=False)
            used.append(path)
        except (OSError, ValueError) as error:
            message('A mod\'s shader template is left out (%s): %s' % (Path(path).name, error))
    shaderprep.write_template(builder.document(), str(out))
    return used


def prepare(game, template, executable, cache_root, package, renderer='vulkan',
            progress=lambda text: None, cancel=lambda: False, skip=False,
            runner=run_process, python=None, builder=None, runtime_identity=None, log=None,
            renderers=None, extra_templates=None):
    """Return a nonfatal status. executable=None assembles Android's pending warm.

    renderers: the renderers the native warm runs for, one after the other, on one shared head
    start (default [renderer]; macOS setup passes ['metal', 'vulkan']). The manifest records each
    result in "renderers"; the game uses the generation only for a renderer that is "ready". With
    several renderers, one that cannot start (RENDERER_UNAVAILABLE) or fails is recorded and the
    others go on; only when none succeeds is the preparation a failure. renderer stays the
    identity's renderer, so a generation an older setup prepared for it is kept and only the
    missing renderers are warmed (failed/unavailable ones wait for Rebuild shader cache).

    progress(text) gets the phase messages; a progress that takes a fraction keyword also gets
    progress(text, fraction=0..1) as the work advances (assembly, then the native warm).
    log(line) receives the helpers' raw output (default: progress).
    extra_templates: the mods' stage templates prepared with the game's own (default: the enabled packages'
    of the mod manager next to cache_root, package_templates); the identity names them, so a change of mods
    makes a new generation, while the launch record keeps the release template the game checks.
    builder(game, output, template[, progress=callback(done, total, detail)]) assembles in-process.
    Paths are private local output only. No caller-supplied source is removed.
    """
    reporter = _Reporter(progress, log or progress, cancel)
    renderers = list(renderers or [renderer])
    if not Path(template).is_file():
        message = 'Shader template unavailable; shaders will compile during play'
        progress(message)
        return dict(status='skipped', message=message)
    root = Path(cache_root)
    staging = None
    wanted = None
    launch = None

    def warm(generation, names, low, high, keep_going):
        """wwhd --warm-shaders for each renderer in turn over [low, high] of the bar ->
        {renderer: state}. keep_going: a failed renderer is recorded instead of raised."""
        states = {}
        share = (high - low) / len(names)
        for index, name in enumerate(names):
            title = ('Compiling shaders and pipelines for this computer' if len(renderers) == 1 else
                     'Compiling shaders and pipelines for %s' % RENDERER_NAMES.get(name, name))
            reporter.begin(title, low + index * share, low + (index + 1) * share)
            env = os.environ.copy()
            for key in list(env):
                if key.startswith('WWHD_VK_TEST_') or key == 'WWHD_VK_GRAPH_DIAGNOSTICS':
                    env.pop(key)
            # Preparation owns its live directory; a caller's exact-file override
            # must not redirect the warm subprocess away from that directory.
            env.pop('WWHD_VK_PIPELINE_CACHE', None)
            env.update(cache_environment(generation, game, name))
            try:
                runner([str(executable), '--game', str(game), '--warm-shaders'], env, cancel, reporter.line)
                states[name] = 'ready'
            except InterruptedError:
                raise
            except Exception as error:
                if not keep_going:
                    raise
                unavailable = getattr(error, 'returncode', None) == RENDERER_UNAVAILABLE
                states[name] = 'unavailable' if unavailable else 'failed'
                label = RENDERER_NAMES.get(name, name)
                reporter.message('%s is not available on this computer; skipped' % label if unavailable else
                                 'Preparing shaders for %s failed (%s); they compile during play' % (label, error))
        return states

    extras = package_templates(cache_root) if extra_templates is None else [Path(p) for p in extra_templates]
    try:
        wanted = identity(game, template, package, renderer)
        if extras:
            wanted['mod_templates'] = sorted(file_hash(p) for p in extras)
        runtime = runtime_identity or executable
        if runtime and Path(runtime).is_file():
            wanted['runtime'] = file_hash(runtime)
        launch = launch_identity(game, template, package, runtime)
        if skip or cancel():
            with activation_lock(root):
                atomic_json(root / 'skip.json', dict(schema=SCHEMA, identity=wanted, launch=launch))
                (root / 'rebuild.request').unlink(missing_ok=True)
            return dict(status='skipped', message='Shaders will compile during play')
        if (root / 'skip.json').is_file() and not (root / 'rebuild.request').exists():
            skipped = read_marker(root / 'skip.json')
            if opted_out(skipped, wanted):
                return dict(status='skipped', message='Shaders will compile during play')
        active = root / 'active.json'
        if active.is_file() and not (root / 'rebuild.request').exists():
            previous = read_marker(active)
            name = previous.get('generation', '')
            if not isinstance(name, str):
                name = ''
            generation = root / name
            if (previous.get('identity') == wanted and previous.get('status') == 'ready'
                    and name.startswith('generation-') and Path(name).name == name
                    and generation.parent == root and (generation / 'headstart.bin').is_file()):
                states = renderer_states(previous)
                missing = [r for r in renderers if r not in states] if executable else []
                if missing:
                    try:
                        states.update(warm(generation, missing, 0.0, 1.0, keep_going=True))
                    except InterruptedError:
                        # skipping the added renderer keeps the prepared ones in use
                        states.update({r: 'skipped' for r in missing if r not in states})
                    updated = dict(previous, renderers=states)
                    with activation_lock(root):
                        if read_marker(active) == previous:
                            atomic_json(generation / 'manifest.json', updated)
                            atomic_json(active, updated)
                return dict(status='ready', generation=str(generation), reused=True, renderers=states)
        failed = read_marker(root / 'failed.json')
        if not (root / 'rebuild.request').exists() and failed.get('identity') == wanted:
            return dict(status='skipped', message='Previous preparation failed; request Rebuild shader cache to retry')
        root.mkdir(parents=True, exist_ok=True)
        migrate_legacy_caches(root)
        staging = Path(tempfile.mkdtemp(prefix='generation-', dir=root))
        assembly = (ASSEMBLY_SHARE if len(renderers) == 1 else ASSEMBLY_SHARE_SEVERAL) if executable else 1.0
        reporter.begin('Resolving shaders from your game files', 0.0, assembly)
        assembled = template
        if extras:
            # the mods' stages: their render states join the release template's for this generation only
            assembled = staging / 'template.bin'
            used = merge_templates(package, template, extras, assembled, reporter.message)
            reporter.message('Including the shaders of %d mod stage template(s)' % len(used))
        import sys
        command = [python or sys.executable, str(Path(package) / 'tools/shaderprep.py'),
                   'build', str(staging / 'headstart.bin'), '--game', str(game), '--template', str(assembled),
                   '--max', '0', '--progress']
        env = os.environ.copy()
        for key in list(env):
            if key.startswith('WWHD_VK_TEST_') or key == 'WWHD_VK_GRAPH_DIAGNOSTICS':
                env.pop(key)
        if builder:
            if _accepts_keyword(builder, 'progress'):
                builder(game, staging / 'headstart.bin', assembled, progress=reporter.update)
            else:
                builder(game, staging / 'headstart.bin', assembled)
        else:
            runner(command, env, cancel, reporter.line)
        if cancel():
            raise InterruptedError('Shader preparation skipped')
        if not (staging / 'headstart.bin').is_file():
            raise RuntimeError('Shader preparation produced no head start')
        status = 'warm_pending'
        states = None
        if executable:
            states = warm(staging, renderers, assembly, 1.0, keep_going=len(renderers) > 1)
            if not any(state == 'ready' for state in states.values()):
                raise RuntimeError('no renderer could be prepared')
            status = 'ready'
        result = dict(schema=SCHEMA, status=status, identity=wanted, launch=launch, generation=staging.name)
        if states is not None:
            result['renderers'] = states
        with activation_lock(root):
            atomic_json(staging / 'manifest.json', result)
            atomic_json(active, result)
            if status == 'ready':
                (root / 'rebuild.request').unlink(missing_ok=True)
            (root / 'skip.json').unlink(missing_ok=True)
            (root / 'failed.json').unlink(missing_ok=True)
            if status == 'ready':
                cleanup_generations(root, staging.name)
        done = dict(status=status, generation=str(staging), reused=False)
        if states is not None:
            done['renderers'] = states
        return done
    except Exception as error:
        if staging:
            shutil.rmtree(longpath(staging), ignore_errors=True)
        if isinstance(error, InterruptedError) and wanted is not None:
            try:
                with activation_lock(root):
                    atomic_json(root / 'skip.json', dict(schema=SCHEMA, identity=wanted, launch=launch))
                    (root / 'rebuild.request').unlink(missing_ok=True)
            except OSError:
                pass
        if not isinstance(error, InterruptedError) and wanted is not None:
            try:
                with activation_lock(root):
                    atomic_json(root / 'failed.json', dict(schema=SCHEMA, identity=wanted, launch=launch))
            except OSError:
                pass
        progress('Shader preparation unavailable: %s. The game can still run.' % error)
        return dict(status='skipped' if isinstance(error, InterruptedError) else 'failed', message=str(error))


def default_cache_root(data_dir=None, portable=False):
    if os.environ.get('WWHD_USER_DIR'):
        base = Path(os.environ['WWHD_USER_DIR'])
    elif portable:
        base = Path(data_dir) / 'user'
    elif os.sys.platform == 'darwin':
        base = Path.home() / 'Library/Application Support/WWHD'
    elif os.sys.platform.startswith('win'):
        base = Path(os.environ.get('APPDATA', '.')) / 'WWHD'
    else:
        base = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config'))) / 'wwhd'
    return base / 'shader-prebuild'


def main(argv=None):
    import argparse
    import sys
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-prepare', action='store_true', required=True)
    parser.add_argument('--game', required=True, type=Path)
    parser.add_argument('--template', required=True, type=Path)
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--cache-root', required=True, type=Path)
    parser.add_argument('--package', required=True, type=Path)
    parser.add_argument('--renderer', choices=('metal', 'vulkan'), required=True)
    parser.add_argument('--skip', action='store_true')
    parser.add_argument('--cancel-file', type=Path)
    args = parser.parse_args(argv)
    result = prepare(args.game, args.template, args.executable, args.cache_root, args.package,
                     renderer=args.renderer, skip=args.skip,
                     progress=lambda text, fraction=None: print(
                         text if fraction is None else '%3d%% %s' % (100 * fraction, text), file=sys.stderr, flush=True),
                     log=lambda line: print(line, file=sys.stderr, flush=True),
                     cancel=lambda: bool(args.cancel_file and args.cancel_file.exists()))
    print(json.dumps(result), flush=True)
    # Skipping is a successful optional operation; actual preparation failures
    # retain a nonzero exit status for the runtime's fallback diagnostic.
    return 0 if result['status'] in ('ready', 'skipped') else 1


if __name__ == '__main__':
    raise SystemExit(main())
