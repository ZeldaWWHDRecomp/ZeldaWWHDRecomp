"""Setup's models step on Android ("Preparing shaders: models", tools/installer/model_sweep.py).

The setup service (SetupService.runModels) runs main() in its embedded Python. model_sweep.run drives the
attempts exactly as on a computer (resume, skip what stopped the game, stall rule, orderly restarts), with one
difference: an attempt is not a child process but a start of ModelPrepareActivity in its own process
(:modelprep). Only an app in front may start an activity, so the setup screen starts each requested attempt;
the activity's progress screen covers the game, which renders hidden (WWHD_HIDDEN_WINDOWS: its own render
targets exactly as in play, nothing presented; gfx/hidden_presentation.h).

Files in the setup job (atomic replacements, names and numbers only):
  model-attempt.json         the attempt to start: id, the game's WWHD_ options, arguments, folder, output file
  model-attempt-status.json  ModelPrepareActivity: id, pid, state ("running"; "left" once the player left it)
  model-attempt-stop.json    stop that attempt in order (the game quits as from Settings > Quit Game)
  models-progress.json       for the progress screens: done, total, phase, place, fraction, eta_seconds
  models-cancel.json         the player's Cancel (written by the screens)
  models-result.json         the step's end: status done|failed|paused, reason
A cancelled step keeps what it prepared (the live shader caches and the sweep's progress): the next setup run
continues it.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import uuid

RENDERER = 'vulkan'
# The setup screen starts a requested attempt within a second; much longer and it is not in front (the player
# is elsewhere): the step pauses until the setup screen is shown again.
LAUNCH_SECONDS = 45
# Share of the bar for the stage visits (measured on a Mac: the objects, rooms, actors and plants took about
# 730 s, the 129 visits about 500 s).
VISITS_SHARE = 0.4
PROGRESS_EVERY = 0.5  # seconds between progress file updates
EXECUTABLE = 'android:ModelPrepareActivity'


def write_json(path, value):
    path = Path(path)
    temporary = path.with_name(path.name + '.t' + uuid.uuid4().hex[:8])
    try:
        temporary.write_text(json.dumps(value, sort_keys=True), encoding='utf-8')
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except OSError:
            pass


def read_json(path):
    try:
        value = json.loads(Path(path).read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return None
    return value if isinstance(value, dict) else None


def process_alive(pid, marker=b':modelprep'):
    """The activity's process still runs (its command line names the :modelprep process; a reused pid does not)."""
    try:
        with open('/proc/%d/cmdline' % int(pid), 'rb') as stream:
            return marker in stream.read()
    except (FileNotFoundError, ProcessLookupError, ValueError, TypeError):
        return False
    except OSError:  # not readable here: ask the kernel (the app's processes share one user)
        try:
            os.kill(int(pid), 0)
            return True
        except ProcessLookupError:
            return False
        except OSError:
            return True  # another user's process: the stall rule still ends a stuck attempt


# the game's staged cache writes (gfx/vulkan/atomic_temporary.h: <cache>.t<8 hex>; up to v0.2.12
# <cache>.tmp-<pid>-<clock>-<n>), renamed over the cache once written
STAGED = re.compile(r'\.t[0-9a-f]{8}$|\.tmp-[0-9]+-[0-9]+-[0-9]+$')


def remove_staged(live, log=lambda text: None):
    """Removes the staged cache writes an attempt left when it stopped mid-write (a model that stops the game, the
    app closed). Only between attempts: no game writes the live caches then (the step does not run while the game
    plays). The runtime itself removes a staged file only a day later (another process may still be writing it)."""
    removed = 0
    try:
        for path in Path(live).rglob('*'):
            if STAGED.search(path.name) and path.is_file() and not path.is_symlink():
                try:
                    size = path.stat().st_size
                    path.unlink()
                    removed += size
                    log('removed %s (%d bytes, a write the game did not finish)' % (path.name, size))
                except OSError:
                    pass
    except OSError:
        pass
    return removed


class Attempt:
    """A started (or requested) ModelPrepareActivity, with the parts of subprocess.Popen model_sweep.run uses."""

    def __init__(self, launcher, attempt_id, clock):
        self.launcher, self.id, self.clock = launcher, attempt_id, clock
        self.requested = clock()
        self.returncode = None
        self.pid = None
        self.stop_sent = False

    def status(self):
        value = read_json(self.launcher.job / 'model-attempt-status.json')
        if not value or value.get('id') != self.id or not isinstance(value.get('pid'), int):
            return None
        self.pid = value['pid']
        return value

    def started(self):
        return self.status() is not None

    def _ended(self, code):
        self.returncode = code
        self.launcher.finished(self)
        return code

    def poll(self):
        if self.returncode is not None:
            return self.returncode
        if self.status() is None:
            return None  # not started yet (Launcher.interrupted limits the wait)
        if self.launcher.alive(self.pid):
            return None
        # (Android reports no exit code to another process: the progress file tells an orderly end from a stop)
        return self._ended(-1)

    def terminate(self):
        if self.returncode is not None:
            return
        if self.status() is None:  # never started: the setup screen must not start it later
            self._ended(-signal.SIGTERM)
            return
        if not self.stop_sent:
            write_json(self.launcher.job / 'model-attempt-stop.json', {'schema': 1, 'id': self.id})
            self.stop_sent = True

    def wait(self, timeout=None):
        # an orderly quit writes the shader caches first: give it longer than a computer's terminate
        limit = None if timeout is None else max(timeout, self.launcher.quit_seconds)
        deadline = None if limit is None else self.clock() + limit
        while self.poll() is None:
            if deadline is not None and self.clock() > deadline:
                raise subprocess.TimeoutExpired(EXECUTABLE, limit)
            self.launcher.sleep(.2)
        return self.returncode

    def kill(self):
        if self.returncode is None and self.pid and self.launcher.alive(self.pid):
            try:
                os.kill(self.pid, signal.SIGKILL)
            except OSError:
                pass


class Launcher:
    """model_sweep.run's popen on Android: requests attempts from the setup screen (model-attempt.json)."""

    def __init__(self, job, clock=time.monotonic, sleep=time.sleep, alive=process_alive,
                 launch_seconds=LAUNCH_SECONDS, quit_seconds=20, live=None, log=lambda text: None):
        self.job = Path(job)
        self.live, self.log = live, log  # the live caches: their unfinished writes go before each attempt
        self.clock, self.sleep, self.alive = clock, sleep, alive
        self.launch_seconds, self.quit_seconds = launch_seconds, quit_seconds
        self.current = None
        self.reason = None

    def __call__(self, args, env=None, stdout=None, stderr=None, cwd=None):
        attempt = Attempt(self, uuid.uuid4().hex, self.clock)
        if self.live:  # (model_sweep.run starts an attempt only after the previous one ended)
            remove_staged(self.live, self.log)
        # only the game's own options: nothing of the setup process's environment reaches the game
        options = {k: str(v) for k, v in (env or {}).items() if k.startswith('WWHD_')}
        output = getattr(stdout, 'name', None)
        write_json(self.job / 'model-attempt.json', {
            'schema': 1, 'id': attempt.id, 'args': [str(a) for a in list(args)[1:]], 'env': options,
            'cwd': str(cwd) if cwd else None, 'stdout': str(output) if isinstance(output, str) else None})
        for name in ('model-attempt-stop.json',):
            try:
                (self.job / name).unlink()
            except OSError:
                pass
        self.current = attempt
        return attempt

    def finished(self, attempt):
        if self.current is attempt:
            request = read_json(self.job / 'model-attempt.json')
            if request and request.get('id') == attempt.id:
                try:
                    (self.job / 'model-attempt.json').unlink()
                except OSError:
                    pass

    def interrupted(self):
        """Why the current attempt cannot go on ('screen': not started, the setup screen is not in front; 'left':
        the player left the progress screen), or None."""
        attempt = self.current
        if attempt is None or attempt.returncode is not None:
            return None
        status = attempt.status()
        if status is None:
            if self.clock() - attempt.requested > self.launch_seconds:
                self.reason = 'screen'
        elif status.get('state') == 'left':
            self.reason = 'left'
        return self.reason if self.reason in ('screen', 'left') else None


class Progress:
    """models-progress.json for the progress screens: the game's WWHD-PROGRESS counts, the phase from the sweep's
    progress.txt, a fraction with the stage visits and a time estimate."""

    def __init__(self, job, state, clock=time.monotonic, every=PROGRESS_EVERY):
        self.path = Path(job) / 'models-progress.json'
        self.state, self.clock, self.every = Path(state), clock, every
        self.done = self.total = 0
        self.detail = ''
        self.written = None
        self.start = None  # (time, fraction) of this session's first measurement

    def line(self, text):
        if not text.startswith('WWHD-PROGRESS '):
            return False
        parts = text[len('WWHD-PROGRESS '):].strip().split(' ', 2)
        try:
            done, total = int(parts[0]), int(parts[1])
        except (IndexError, ValueError):
            return False
        if 0 <= done <= total:
            self.done, self.total = done, total
            self.detail = parts[2] if len(parts) > 2 else ''
        self.update()
        return True

    def snapshot(self):
        lines = []
        try:
            lines = [l.strip() for l in (self.state / 'progress.txt').read_text(
                encoding='utf-8', errors='replace').splitlines() if l.strip()]
        except OSError:
            pass
        phase, place = '', ''
        for line in reversed(lines):
            if line.startswith('phase '):
                phase = line.split(' ')[1]
                break
        for line in reversed(lines):
            if line.startswith('visit ') and phase == 'visits':
                place = line.split(' ', 1)[1].split(':', 1)[0]
                break
            if line.startswith(('phase ', 'start')):
                break
        try:
            visits = sum(1 for l in (self.state / 'visits.txt').read_text(encoding='utf-8').splitlines() if l.strip())
        except OSError:
            visits = 0
        visited = sum(1 for l in lines if l.startswith(('visit-done ', 'visit-failed ')))
        units = self.done / self.total if self.total else 0.0
        if 'sweep-done' in lines:
            fraction = 1.0
        elif visits:
            fraction = (1 - VISITS_SHARE) * units + VISITS_SHARE * min(1.0, visited / visits)
        else:
            fraction = units
        return phase, place, fraction

    def update(self, force=False):
        now = self.clock()
        if not force and self.written is not None and now - self.written < self.every:
            return
        self.written = now
        phase, place, fraction = self.snapshot()
        eta = None
        if self.total:
            if self.start is None:
                self.start = (now, fraction)
            elapsed, gained = now - self.start[0], fraction - self.start[1]
            if elapsed >= 20 and gained >= 0.01:
                eta = int(elapsed / gained * (1 - fraction))
        write_json(self.path, {'schema': 1, 'done': self.done, 'total': self.total, 'detail': self.detail[:80],
                               'phase': phase, 'place': place[:16], 'fraction': round(fraction, 4),
                               'eta_seconds': eta})


def _module(name, path):
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        path_before = list(sys.path)
        try:
            spec.loader.exec_module(module)
        finally:
            sys.path[:] = path_before  # (shader_prebuild.py puts its folder first; its siblings stay out here)
    return sys.modules[name]


def load_installer(package):
    """model_sweep and shader_prebuild from the package's tools/installer (as shader_prebuild_android does; the
    installer folder is not put on sys.path)."""
    installer = Path(package) / 'tools/installer'
    return (_module('android_models_model_sweep', installer / 'model_sweep.py'),
            _module('android_models_shader_prebuild', installer / 'shader_prebuild.py'))


LOG_LIMIT = 4 << 20  # models.log (the step's and the game's lines) is renamed models-previous.log beyond this


def job_log(job):
    """The step's log in the setup job (an embedded Python's output reaches no console)."""
    path = Path(job) / 'models.log'

    def log(text):
        try:
            if path.exists() and path.stat().st_size > LOG_LIMIT:
                os.replace(path, path.with_name('models-previous.log'))
            with path.open('a', encoding='utf-8', errors='replace') as stream:
                stream.write(text.rstrip('\n') + '\n')
        except OSError:
            pass
    return log


def run(job, game, cache_root, package=None, launcher=None, sweeper=None, clock=time.monotonic, log=None):
    """Runs (or resumes) the step; writes and returns models-result.json's value."""
    job = Path(job)
    log = log or job_log(job)
    package = Path(package) if package else Path(__file__).resolve().parents[2]
    model_sweep, helper = load_installer(package)
    root = Path(cache_root)
    for name in ('model-attempt.json', 'model-attempt-stop.json', 'models-progress.json', 'models-result.json'):
        try:
            (job / name).unlink()
        except OSError:
            pass

    def finish(status, reason=None, message=None):
        value = {'schema': 1, 'status': status}
        if reason:
            value['reason'] = reason
        if message:
            value['message'] = message[:500]
        write_json(job / 'models-result.json', value)
        return value

    active = read_json(root / 'active.json') or {}
    name = active.get('generation', '')
    if (active.get('status') != 'ready' or not isinstance(name, str) or not name.startswith('generation-')
            or Path(name).name != name or not (root / name / 'headstart.bin').is_file()
            or not isinstance(active.get('identity'), dict)):
        return finish('failed', message='No prepared shaders for this build')
    identity = model_sweep.identity_of(active['identity'], RENDERER)
    if model_sweep.status(root, RENDERER, identity) == 'done':
        return finish('done')
    launcher = launcher or Launcher(job, clock=clock)
    if launcher.live is None:
        launcher.live, launcher.log = root / 'live', log
    progress = Progress(job, model_sweep.state_dir(root, RENDERER), clock=clock)

    def cancelled():
        return (job / 'models-cancel.json').exists()

    def paused():
        return (job / 'pause.json').exists() or (job / 'manual-pause.json').exists()

    def cancel():  # (asked every few tenths of a second while an attempt runs)
        progress.update()
        if launcher.interrupted() is not None:
            return True
        return cancelled() or paused()

    def line(text):
        if not progress.line(text):
            log(text)
            progress.update()

    # each attempt's folder (settings, save, output) beside the job: the game process can reach it
    scratch = job / 'model-scratch'
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True, exist_ok=True)
    previous, tempfile.tempdir = tempfile.tempdir, str(scratch)
    try:
        progress.update(force=True)
        status = (sweeper or model_sweep.run)(
            EXECUTABLE, game, root, RENDERER, identity,
            helper.cache_environment(root / name, game, RENDERER), line=line, cancel=cancel, log=log,
            popen=launcher)
    except InterruptedError:
        reason = 'cancel' if cancelled() else launcher.reason or 'pause'
        log('paused (%s); what was prepared is kept' % reason)
        return finish('paused', reason)
    except Exception as error:  # the game and its prepared shaders stay usable
        log('failed: %s' % error)
        return finish('failed', message=str(error))
    finally:
        tempfile.tempdir = previous
        shutil.rmtree(scratch, ignore_errors=True)
    if status == 'done':
        progress.update(force=True)
    return finish('done' if status == 'done' else 'failed')


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--job', required=True, type=Path)
    parser.add_argument('--game', required=True, type=Path)
    parser.add_argument('--cache-root', required=True, type=Path)
    args = parser.parse_args(argv)
    return run(args.job, args.game, args.cache_root)


if __name__ == '__main__':
    print(json.dumps(main(sys.argv[1:])))
