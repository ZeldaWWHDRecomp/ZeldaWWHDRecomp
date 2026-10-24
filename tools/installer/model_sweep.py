"""Preparing shaders: models. The game draws every model of every object archive once, hidden and muted, from
its title screen (no save needed), so the renderer records the pipelines those models need for this build and
this computer; in play they are compiled when their archive loads instead of when a model first appears.

Nothing is shipped: the archive list is made of names from the player's own game files, and the results are the
player's normal shader caches (the live folder of the shader preparation). The sweep keeps its progress (names
only) next to those caches, so a cancelled or crashed sweep resumes where it stopped; a model that crashes the
game is skipped on the next attempt (skip.txt, names only).
"""
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

SCHEMA = 1
# Rooms, stages and the sea's island LODs are stage resources (<stage>_<archive>.szs), loaded by the stage loader.
NOT_OBJECTS = re.compile(r'(_Stage|_Room\d+|_LOD\d+|_LODALL)$')
STAGE_ARCHIVE = re.compile(r'^(.+)_(Stage|Room\d+|LOD\d+|LODALL)$')
MAX_ATTEMPTS = 40          # attempts in a row that finish no archive or actor (each skips what it stopped at)
MAX_ATTEMPTS_TOTAL = 1000  # attempts in all (actors that stop the game: a few seconds each)
DEFAULT_SKIP = Path(__file__).with_name('model_sweep_skip.txt')
# models drawn per batch (two frames each); WWHD_SETUP_MODEL_SWEEP_BATCH=n for measurements only
# stages whose rooms are separate places (islands): visited at every room with a player start, not only the first
ISLAND_STAGES = ('sea',)
MODELS_PER_BATCH = max(1, min(8, int(os.environ.get('WWHD_SETUP_MODEL_SWEEP_BATCH', '1') or 1)))
STARTUP_SECONDS = 300     # until an attempt's first progress line (the game starting up)
MAX_PROCESSES = 4          # game processes of the models step at once on a desktop (user decision 2026-10-23)
STALL_SECONDS = 60         # no new line in progress.txt for this long: the attempt counts as a crash (a model, room or
                           # actor takes well under a second; a stuck game writes nothing more)


def _pack_entries(path):
    """Names in a SARC .pack file (the bundled .szs archives); no file data is read beyond the name table."""
    with open(path, 'rb') as stream:
        data = stream.read()
    if data[:4] != b'SARC' or len(data) < 0x20:
        return []
    order = '>' if data[6:8] == b'\xfe\xff' else '<'
    header = struct.unpack_from(order + 'H', data, 4)[0]
    count = struct.unpack_from(order + 'H', data, header + 6)[0]
    names_at = header + 12 + 16 * count
    out = []
    for i in range(count):
        _, attributes, _, _ = struct.unpack_from(order + 'IIII', data, header + 12 + 16 * i)
        if attributes & 0x01000000:
            at = names_at + 8 + (attributes & 0xFFFF) * 4
            end = data.find(b'\0', at)
            if end > at:
                out.append(data[at:end].decode('ascii', errors='replace'))
    return out


def object_archives(game):
    """Object archive names of the player's game: content/Common/Object/*.szs and the .szs archives bundled in
    content/Common/Pack/*.pack (not the 2D layout packs), without rooms, stages and island LODs."""
    common = Path(game) / 'content' / 'Common'
    names = set()
    objects = common / 'Object'
    if objects.is_dir():
        names.update(p.stem for p in objects.iterdir() if p.suffix == '.szs' and p.is_file())
    packs = common / 'Pack'
    if packs.is_dir():
        for pack in sorted(packs.glob('*.pack')):
            if pack.name.startswith('permanent_2d'):
                continue
            for entry in _pack_entries(pack):
                if entry.endswith('.szs'):
                    names.add(entry.rsplit('/', 1)[-1][:-4])
    return sorted(n for n in names if n and len(n) < 0x40 and n.isprintable() and not NOT_OBJECTS.search(n))


def stage_archives(game):
    """The stage resources of the player's game, {stage: [archive, ...]}: content/Common/Stage/<stage>_<archive>.szs
    and those bundled in the packs (Stage, Room<n>, LOD<n>, LODALL); names only."""
    common = Path(game) / 'content' / 'Common'
    names = set()
    stages = common / 'Stage'
    if stages.is_dir():
        names.update(p.stem for p in stages.iterdir() if p.suffix == '.szs' and p.is_file())
    packs = common / 'Pack'
    if packs.is_dir():
        for pack in sorted(packs.glob('*.pack')):
            if not pack.name.startswith('permanent_2d'):
                names.update(e.rsplit('/', 1)[-1][:-4] for e in _pack_entries(pack) if e.endswith('.szs'))
    out = {}
    for name in names:
        match = STAGE_ARCHIVE.match(name)
        if match and len(name) < 0x40 and name.isprintable() and ' ' not in name:
            out.setdefault(match.group(1), []).append(match.group(2))

    def order(archive):  # the stage archive first, then rooms by number, then LODs (LODALL last)
        digits = re.sub(r'\D', '', archive)
        return (0 if archive == 'Stage' else 1 if archive.startswith('Room') else 2, int(digits) if digits else 1 << 30)
    return {stage: sorted(archives, key=order) for stage, archives in sorted(out.items())}


def _lines(path):
    try:
        return [l.strip() for l in Path(path).read_text(encoding='utf-8', errors='replace').splitlines() if l.strip()]
    except OSError:
        return []


def culprit(progress_lines, skip):
    """The model, archive or actor the last attempt stopped at (the last one it started), unless it is skipped
    already."""
    start = len(progress_lines) - 1 - progress_lines[::-1].index('start') if 'start' in progress_lines else 0
    for line in reversed(progress_lines[start:]):
        if line.startswith(('model ', 'load ', 'actor ', 'env ', 'visit ', 'particles ')):
            name = line.split(' ', 1)[1]
            return None if name in skip else name
    return None


def stopped_group(progress_lines):
    """The actors of the group (WWHD_DSWEEP_ACTOR_GROUP) the last attempt stopped in, if it stopped in one of more than
    one actor; else None."""
    start = len(progress_lines) - 1 - progress_lines[::-1].index('start') if 'start' in progress_lines else 0
    lines = progress_lines[start:]
    at = max((i for i, l in enumerate(lines) if l.startswith('actor-group ')), default=None)
    if at is None:
        return None
    members = [l.split(' ', 1)[1] for l in lines[at + 1:] if l.startswith('actor ')]
    done = {l.split(' ', 1)[1] for l in lines[at + 1:] if l.startswith('actor-done ')}
    left = [m for m in members if m not in done]
    return left if len(left) > 1 else None


def state_dir(cache_root, renderer):
    return Path(cache_root) / 'model-sweep' / renderer


def status(cache_root, renderer, identity):
    """'done', 'partial' (progress kept for this identity) or 'none'."""
    state = state_dir(cache_root, renderer)
    try:
        saved = json.loads((state / 'identity.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return 'none'
    if saved != identity:
        return 'none'
    return 'done' if 'sweep-done' in _lines(state / 'progress.txt') else 'partial'


def environment(base, game, cache_env, renderer, state, archives, scratch, stages=None):
    """The sweep's environment: hidden, muted, no host input, settings and states in a scratch folder (the
    player's own are never touched), the shader caches of the preparation (cache_env)."""
    env = {k: v for k, v in base.items()
           if not (k.startswith('WWHD_') and k not in ('WWHD_USER_DIR', 'WWHD_INSTALL_DIR'))}
    env.update(cache_env)
    env.update(WWHD_RENDERER_RUNTIME=renderer, WWHD_NO_AUDIO='1', WWHD_HIDDEN='1', WWHD_HIDDEN_WINDOWS='1',
               WWHD_NO_HOST_INPUT='1', WWHD_NO_GAMEPAD='1', WWHD_LAST_SESSION='0',
               WWHD_SETTINGS=str(scratch / 'settings.ini'), WWHD_DISPLAY_SETTINGS=str(scratch / 'display.plist'),
               WWHD_CONTROLS=str(scratch / 'controls.json'), WWHD_STATE_DIR=str(scratch / 'states'),
               WWHD_DSWEEP='title,%d,2' % MODELS_PER_BATCH, WWHD_DSWEEP_ARCHIVES=str(archives),
               WWHD_DSWEEP_STATE=str(state),
               WWHD_DSWEEP_EXIT='1',
               # no frame limit: the game runs as fast as it can draw (faster than real time; nobody watches)
               WWHD_UNCAPPED='1')
    default_skip = [n for n in _lines(DEFAULT_SKIP) if not n.startswith('#')]
    if default_skip:
        env['WWHD_DSWEEP_SKIP'] = ','.join(default_skip)
    if stages:
        env['WWHD_DSWEEP_STAGES'] = str(stages)
        # then the title's scene at other times of day and in a storm, every actor the rooms place, and the file
        # select (dsweep.cpp)
        env.update(WWHD_DSWEEP_ENV='1', WWHD_DSWEEP_ACTORS='1', WWHD_DSWEEP_FILE_SELECT='1')
        # the particle sweep (dsweep.cpp) only on request: measured on 19 places it recorded nothing they still build
        if os.environ.get('WWHD_SETUP_MODEL_SWEEP_PARTICLES') == '1':
            env['WWHD_DSWEEP_PARTICLES'] = '1'
    # speed settings (each measured on its own; dsweep.cpp): actors shown in groups of that many; record only
    for setting, game_var in (('WWHD_SETUP_MODEL_SWEEP_ACTOR_GROUP', 'WWHD_DSWEEP_ACTOR_GROUP'),
                              ('WWHD_SETUP_MODEL_SWEEP_RECORD_ONLY', 'WWHD_DSWEEP_RECORD_ONLY')):
        # actors in groups of 8 by default (same recipes as one at a time, measured 2026-10-23; 16 changed the skip
        # list); WWHD_SETUP_MODEL_SWEEP_ACTOR_GROUP=1 shows them one at a time again
        value = os.environ.get(setting, '')
        if re.fullmatch(r'\d+', value) and value not in ('0', '1'):
            env[game_var] = value
    # measurements only: the Vulkan renderer's key log (one line per pipeline it creates, with the sweep's batch)
    if os.environ.get('WWHD_SETUP_MODEL_SWEEP_KEY_LOG'):
        env['WWHD_VK_KEY_LOG'] = os.environ['WWHD_SETUP_MODEL_SWEEP_KEY_LOG']
    env['WWHD_HEADSTART'] = '0'  # the sweep records pipelines; it warms nothing (and holds no generation)
    return env


def run(executable, game, cache_root, renderer, identity, cache_env, line=lambda text: None,
        cancel=lambda: False, log=lambda text: None, popen=subprocess.Popen, clock=time.monotonic,
        sleep=time.sleep, attempts=MAX_ATTEMPTS, stall=STALL_SECONDS, processes=1):
    """Sweeps (or resumes) for one renderer, in that many game processes at once (processes()). line(text) gets the game's output (its 'WWHD-PROGRESS done total
    archive' lines carry the progress). Returns 'done' or 'failed'; raises InterruptedError when cancel()."""
    state = state_dir(cache_root, renderer)
    state.mkdir(parents=True, exist_ok=True)
    try:
        saved = json.loads((state / 'identity.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        saved = None
    if saved != identity:
        # another build or game: its pipelines do not match; start over (crashing models stay skipped for the
        # same game files)
        (state / 'progress.txt').unlink(missing_ok=True)
        (state / 'actor-groups.txt').unlink(missing_ok=True)
        shutil.rmtree(state / 'parts', ignore_errors=True)
        if not isinstance(saved, dict) or saved.get('game') != identity.get('game'):
            (state / 'skip.txt').unlink(missing_ok=True)
        (state / 'identity.json').write_text(json.dumps(identity, sort_keys=True), encoding='utf-8')
    archives = state / 'archives.txt'
    archives.write_text(''.join(n + '\n' for n in object_archives(game)), encoding='utf-8')
    # stage visits: enter every stage at its first room and draw it under its own environment (dsweep.cpp);
    # WWHD_SETUP_MODEL_SWEEP_VISITS=0 leaves them out
    visits = None
    if os.environ.get('WWHD_SETUP_MODEL_SWEEP_VISITS', '1') != '0':
        visits = state / 'visits.txt'
        rooms = {s: sorted(int(a[4:]) for a in names if a.startswith('Room')) for s, names in stage_archives(game).items()}
        # the sea: every island (each its own room, reached by sailing) at its player start
        visits.write_text(''.join('%s *\n' % s if s in ISLAND_STAGES else '%s %d\n' % (s, r[0])
                                  for s, r in rooms.items() if r and s not in ('sea_T', 'Name')), encoding='utf-8')
    stages = state / 'stages.txt'
    stages.write_text(''.join('%s %s\n' % (stage, ' '.join(names)) for stage, names in stage_archives(game).items()),
                      encoding='utf-8')
    common = dict(line=line, cancel=cancel, log=log, popen=popen, clock=clock, sleep=sleep, attempts=attempts,
                  stall=stall)
    if processes > 1:
        return _run_parts(executable, game, state, renderer, cache_env, archives, stages, visits, processes, **common)
    return _attempts(executable, game, state, renderer, cache_env, archives, stages, visits, **common)


def _attempts(executable, game, state, renderer, cache_env, archives, stages, visits, line, cancel, log, popen, clock,
              sleep, attempts, stall, tweak=lambda env: None):
    """The attempts of one game process (restarted after a stop, skipping what stopped it) until its progress says
    'sweep-done' -> 'done' or 'failed'; tweak(env) adjusts each attempt's environment."""
    progress_file = state / 'progress.txt'
    def finished():
        return sum(1 for l in _lines(progress_file) if l.startswith(('archive-done ', 'actor-done ')))
    without_progress, before = 0, finished()
    for attempt in range(1, MAX_ATTEMPTS_TOTAL + 1):
        if without_progress >= attempts:
            break
        if 'sweep-done' in _lines(progress_file):
            return 'done'
        if cancel():
            raise InterruptedError('Model preparation paused')
        with ExitStack() as stack:
            try:  # Windows: a terminated game may briefly keep its output open
                folder = tempfile.TemporaryDirectory(prefix='wwhd-model-sweep-', ignore_cleanup_errors=True)
            except TypeError:  # Python < 3.10
                folder = tempfile.TemporaryDirectory(prefix='wwhd-model-sweep-')
            scratch = Path(stack.enter_context(folder))
            env = environment(os.environ, game, cache_env, renderer, state, archives, scratch, stages)
            tweak(env)
            if visits and 'WWHD_DSWEEP_VISIT' not in env:
                # each stage's first room; WWHD_SETUP_MODEL_SWEEP_VISITS=all: every room with a player start, once per
                # scene layer (measured on 19 places: no gain over the first rooms, 22 more minutes)
                env['WWHD_DSWEEP_VISIT'] = 'all' if os.environ.get('WWHD_SETUP_MODEL_SWEEP_VISITS') == 'all' else str(visits)
            (scratch / 'save').mkdir()
            output_path = scratch / 'output.log'
            output = stack.enter_context(output_path.open('wb'))
            reader = stack.enter_context(output_path.open('rb'))
            child = popen([str(executable), '--game', str(game), '--save', str(scratch / 'save')], env=env,
                          stdout=output, stderr=subprocess.STDOUT, cwd=str(scratch))
            pending = b''
            last_change, last_size, started = clock(), -1, False
            try:
                last_size = progress_file.stat().st_size  # lines of earlier attempts are not this one's start
            except OSError:
                pass

            def forward(final=False):
                nonlocal pending
                pending += reader.read()
                parts = pending.split(b'\n')
                pending = b'' if final else parts.pop()
                for part in parts:
                    text = part.decode(errors='replace').rstrip('\r')
                    if text:
                        line(text)
            stalled = False
            try:
                while child.poll() is None:
                    if cancel():
                        raise InterruptedError('Model preparation paused')
                    forward()
                    try:
                        size = progress_file.stat().st_size
                    except OSError:
                        size = -1
                    if size != last_size:
                        last_size, last_change = size, clock()
                        started = True
                    # (before the attempt's first line the game is still starting: a busy machine takes longer)
                    elif clock() - last_change > (stall if started else max(stall, STARTUP_SECONDS)):
                        stalled = True
                        log('Model preparation: no progress for %d s; restarting' % stall)
                        break
                    sleep(.2)
                forward(final=True)
            finally:
                if child.poll() is None:
                    child.terminate()
                    try:
                        child.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait()
        done_lines = _lines(progress_file)
        if 'sweep-done' in done_lines:
            return 'done'
        if done_lines and done_lines[-1] == 'restart':  # an orderly restart the sweep asked for (not a stop)
            continue
        group = stopped_group(done_lines)
        if group:
            # a group of actors stopped the game: its actors go again in groups half as large, down to single actors
            # (which are skipped as before), so the same actors end up skipped as when shown one at a time
            log('Model preparation attempt %d stopped (%s); splitting a group of %d actors' % (
                attempt, 'stalled' if stalled else 'exit %s' % child.returncode, len(group)))
            with (state / 'actor-groups.txt').open('a', encoding='utf-8') as stream:
                stream.write(''.join('%d %s\n' % (len(group) // 2, name) for name in group))
            now = finished()
            without_progress = 0 if now > before else without_progress + 1
            before = now
            continue
        skip = set(_lines(state / 'skip.txt'))
        name = culprit(done_lines, skip)
        log('Model preparation attempt %d stopped (%s); %s' % (
            attempt, 'stalled' if stalled else 'exit %s' % child.returncode,
            'skipping %s' % name if name else 'nothing new to skip'))
        if not name:
            return 'failed'
        if 'actor ' + name in done_lines:  # an actor: the second placement of a name that stops the game skips them all
            base = name.split(':', 1)[0]
            if any(s.split(':', 1)[0] == base for s in skip if s.count(':') == 3):
                name = base
        with (state / 'skip.txt').open('a', encoding='utf-8') as stream:
            stream.write(name + '\n')
        now = finished()
        without_progress = 0 if now > before else without_progress + 1
        before = now
    return 'failed'


def processes(renderers=1):
    """Game processes per renderer for the models step: desktops share up to MAX_PROCESSES between the renderers
    (by cores and memory: a sweeping game uses about 2 cores and 2 GB); Android one.
    WWHD_SETUP_MODEL_SWEEP_PROCESSES=n sets it per renderer, =auto chooses (one process unless set)."""
    setting = os.environ.get('WWHD_SETUP_MODEL_SWEEP_PROCESSES', '')
    if setting.isdigit() and int(setting) > 0:
        return min(int(setting), MAX_PROCESSES)
    if setting != 'auto':
        return 1
    if hasattr(sys, 'getandroidapilevel') or 'ANDROID_ROOT' in os.environ:
        return 1
    cores = os.cpu_count() or 1
    try:
        memory = os.sysconf('SC_PAGE_SIZE') * os.sysconf('SC_PHYS_PAGES')
    except (AttributeError, ValueError, OSError):
        memory = 0
    if not memory and sys.platform == 'darwin':
        try:
            memory = int(subprocess.run(['sysctl', '-n', 'hw.memsize'], capture_output=True, text=True,
                                        timeout=5).stdout.strip() or 0)
        except (OSError, ValueError, subprocess.SubprocessError):
            memory = 0
    total = max(1, min(MAX_PROCESSES, cores // 4, int(memory // (4 << 30)) if memory else 1))
    return max(1, total // max(1, renderers))


# the files of the live caches each renderer's sweep adds to (copied for each process, then merged)
CACHE_FILES = {'vulkan': ('WWHD_VK_PIPELINE_CACHE_DIR', 'WWHD_VK_SHADER_CACHE'), 'metal': ('WWHD_SHADER_CACHE',)}


def _part_caches(cache_env, renderer, folder):
    """Copies of the renderer's live caches in folder -> (the environment naming them, [(live file, copy)])."""
    env, pairs = dict(cache_env), []
    folder.mkdir(parents=True, exist_ok=True)
    if renderer == 'vulkan':
        live = Path(cache_env['WWHD_VK_PIPELINE_CACHE_DIR'])
        for path in sorted(live.glob('*.recipes')):
            pairs.append((path, folder / path.name))
            driver = path.with_suffix('')  # the driver's pipeline cache: the part's own, never merged
            if driver.is_file() and not (folder / driver.name).exists():
                shutil.copyfile(driver, folder / driver.name)
        spirv = Path(cache_env['WWHD_VK_SHADER_CACHE']) / 'spirv.bin'
        (folder / 'vulkan-shaders').mkdir(exist_ok=True)
        pairs.append((spirv, folder / 'vulkan-shaders' / 'spirv.bin'))
        env.update(WWHD_VK_PIPELINE_CACHE_DIR=str(folder), WWHD_VK_SHADER_CACHE=str(folder / 'vulkan-shaders'),
                   WWHD_SHADER_CACHE=str(folder / 'shaders.bin'))
        if Path(cache_env['WWHD_SHADER_CACHE']).is_file():
            pairs.append((Path(cache_env['WWHD_SHADER_CACHE']), folder / 'shaders.bin'))
    else:
        pairs.append((Path(cache_env['WWHD_SHADER_CACHE']), folder / 'shaders.bin'))
        env['WWHD_SHADER_CACHE'] = str(folder / 'shaders.bin')
    for live, copy in pairs:
        if live.is_file() and not copy.exists():
            shutil.copyfile(live, copy)
    return env, pairs


def _merge_caches(pairs_of_parts, log):
    """Each live file := the merge of itself and its parts' copies (sweep_merge.py)."""
    import sweep_merge
    merges = {'.recipes': sweep_merge.merge_recipes, 'spirv.bin': sweep_merge.merge_spirv,
              'shaders.bin': sweep_merge.merge_metal}
    for live in {pair[0] for pairs in pairs_of_parts for pair in pairs}:
        copies = [copy for pairs in pairs_of_parts for original, copy in pairs if original == live and copy.is_file()]
        merge = next((m for suffix, m in merges.items() if live.name.endswith(suffix)), None)
        if not merge or not copies:
            continue
        try:
            base = live.read_bytes() if live.is_file() else None
            if base is None:  # nothing before the sweep: the first copy is the base
                base, copies = copies[0].read_bytes(), copies[1:]
            sweep_merge._replace(live, merge(base, [c.read_bytes() for c in copies]))
        except (OSError, ValueError) as error:
            log('Model preparation: %s not merged (%s)' % (live.name, error))


def _run_parts(executable, game, state, renderer, cache_env, archives, stages, visits, count, line, cancel, log,
               popen, clock, sleep, attempts, stall):
    """The sweep in count game processes at once (dsweep.cpp WWHD_DSWEEP_PART): each its own state and copies of the
    caches. First the archives and stage resources (every count-th each); the placements and places the rooms list
    are then merged into the lists one process would have made, and the actors and visits follow (every count-th
    each). At the end the caches, skip lists and the progress are merged."""
    import threading
    import sweep_merge
    root = state / 'parts'
    parts = [root / str(i) for i in range(count)]
    if not (root / 'count').is_file() or (root / 'count').read_text().strip() != str(count):
        shutil.rmtree(root, ignore_errors=True)  # another split: start the parts over
        root.mkdir(parents=True)
        (root / 'count').write_text(str(count))
    for part in parts:
        part.mkdir(exist_ok=True)
        for name in ('skip.txt', 'actor-groups.txt'):  # what the one-process sweep found already stays
            if (state / name).is_file() and not (part / name).exists():
                shutil.copyfile(state / name, part / name)
    caches = [_part_caches(cache_env, renderer, part / 'cache') for part in parts]
    totals = [None] * count
    lock = threading.Lock()

    def line_for(i):
        def forward(text):
            if not text.startswith('WWHD-PROGRESS '):
                return line(text)
            fields = text.split(' ', 3)
            with lock:
                try:
                    totals[i] = (int(fields[1]), int(fields[2]))
                except (IndexError, ValueError):
                    return
                if all(totals):
                    line('WWHD-PROGRESS %d %d %s' % (sum(t[0] for t in totals), sum(t[1] for t in totals),
                                                     fields[3] if len(fields) > 3 else ''))
        return forward

    def run_stage(second):
        results = [None] * count

        def tweak_for(i):
            def tweak(env):
                env['WWHD_DSWEEP_PART'] = '%d/%d' % (i, count)
                if not second:  # archives and stage resources only
                    env['WWHD_DSWEEP_VISIT'] = ''
                    env.pop('WWHD_DSWEEP_ACTORS', None)
                    env.pop('WWHD_DSWEEP_FILE_SELECT', None)
                elif i:
                    env.pop('WWHD_DSWEEP_FILE_SELECT', None)  # the title's file select: part 0
            return tweak

        def work(i):
            try:
                results[i] = _attempts(executable, game, parts[i], renderer, caches[i][0], archives, stages, visits,
                                       line_for(i), cancel, log, popen, clock, sleep, attempts, stall, tweak=tweak_for(i))
            except InterruptedError:
                results[i] = 'paused'
            except Exception as error:  # noqa: BLE001 - one part failing leaves the others' work usable
                results[i] = 'failed'
                log('Model preparation part %d failed: %s' % (i, error))
        threads = [threading.Thread(target=work, args=(i,), daemon=True) for i in range(count)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        if 'paused' in results:
            raise InterruptedError('Model preparation paused')
        return 'failed' if 'failed' in results else 'done'

    result = 'done'
    if not (root / 'second').is_file():
        result = run_stage(False)
        if result == 'done':
            actors = sweep_merge.merge_placements([p / 'actors-indexed.txt' for p in parts])
            places = sweep_merge.merge_places([p / 'places-indexed.txt' for p in parts])
            for part in parts:
                (part / 'actors.txt').write_text(''.join(l + '\n' for l in actors), encoding='utf-8')
                (part / 'places.txt').write_text(''.join(l + '\n' for l in places), encoding='utf-8')
                progress = [l for l in _lines(part / 'progress.txt') if l != 'sweep-done']
                (part / 'progress.txt').write_text(''.join(l + '\n' for l in progress), encoding='utf-8')
            (root / 'second').write_text('1')
    if result == 'done':
        result = run_stage(True)
    _merge_caches([pairs for _, pairs in caches], log)
    skip = []
    for part in [state] + parts:
        skip += [n for n in _lines(part / 'skip.txt') if n not in skip]
    (state / 'skip.txt').write_text(''.join(n + '\n' for n in skip), encoding='utf-8')
    with (state / 'progress.txt').open('a', encoding='utf-8') as stream:
        for i, part in enumerate(parts):
            stream.write(''.join('part%d %s\n' % (i, l) for l in _lines(part / 'progress.txt') if l.startswith('phase ')))
        if result == 'done':
            stream.write('sweep-done\n')
    if result == 'done':
        shutil.rmtree(root, ignore_errors=True)
    else:  # the parts' caches are merged already: a later attempt starts them from the merged caches
        for part in parts:
            shutil.rmtree(part / 'cache', ignore_errors=True)
    return result


def identity_of(wanted, renderer):
    """The sweep's identity: the preparation identity (game files, runtime, template) and the renderer."""
    digest = hashlib.sha256(json.dumps(wanted, sort_keys=True).encode()).hexdigest()
    return {'schema': SCHEMA, 'renderer': renderer, 'preparation': digest,
            'game': hashlib.sha256(json.dumps(wanted.get('game'), sort_keys=True).encode()).hexdigest()}


def clear(cache_root):
    shutil.rmtree(Path(cache_root) / 'model-sweep', ignore_errors=True)
