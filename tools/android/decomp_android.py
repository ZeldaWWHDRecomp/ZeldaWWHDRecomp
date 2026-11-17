"""The decomp version in the Android setup app (docs/decomp-build.md, "In setup"; tools/installer/decomp_setup.py).

decomp_setup.build() runs unchanged with a stand-in for the setup module (Host): Android cannot start Python
processes (the embedded interpreter has no executable) nor executables from writable storage, so
  - the Python tools (gen.py, recomp.py, decl_table.py -> decl_lint.py, native_rules.py, unit_ir.py) run in this
    process (runpy), one at a time, with their arguments, folder, environment and output as they would have had;
    their process pools become thread pools (decl_lint's clang workers then still run in parallel);
  - native tools (the APK's clang, ld.lld, llvm-objcopy, ...) go through the app's process bridge (native_run);
    decl_lint.py's `clang++` is the APK's clang with --driver-mode=g++; a working folder is set through /system/bin/sh;
  - capstone is the port's stand-in (tools/decomp_build/capstone_shim, decomp_setup.fetch_capstone);
  - the result is a second game library (libwwhdgame.so linked with the release's decomp parts, the game units and
    hooks), kept in its own checkpoint stage (decomp-<identity>) next to the normal game's link stage.
The units are linked one by one (ld.lld -r + llvm-objcopy --localize-hidden) by default; the whole-image ThinLTO link
only with the job's "decomp_whole_lto" switch (its final link peaks about 5.5 GB).
Everything here is Android-only; the desktop setup never imports it.
"""
import concurrent.futures
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import threading
import time
from types import SimpleNamespace
import uuid

SHELL = "/system/bin/sh"
CA_PATHS = ("/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts")


def android_ssl_context(capath):
    """An ssl context that trusts the certificates in Android's store folder. Not capath=: Android names those files
    by OpenSSL's old (MD5) subject hash, and OpenSSL looks them up by the new one, so it finds none
    (CERTIFICATE_VERIFY_FAILED, "unable to get local issuer certificate"). Every file's PEM block is loaded instead."""
    import ssl
    begin, end = "-----BEGIN CERTIFICATE-----", "-----END CERTIFICATE-----"
    pem = []
    for name in sorted(os.listdir(capath)):
        try:
            with open(os.path.join(capath, name), encoding="ascii", errors="replace") as stream:
                text = stream.read()
        except OSError:
            continue
        start = text.find(begin)
        stop = text.find(end, start)
        if start >= 0 and stop > start:
            pem.append(text[start:stop + len(end)])
    if not pem:
        raise OSError("no certificates in %s" % capath)
    return ssl.create_default_context(cadata="\n".join(pem) + "\n")


class Host:
    """The setup module as decomp_setup.build() uses it, for the Android app. setup: the loaded installer module
    (setup_adapter.Adapter.setup); tools: {"ld.lld", "llvm-objcopy", ["llvm-link", "llvm-dis", "llvm-as"]: paths};
    native_run(command, env=, cancel=, separate=) -> (code, output) or (code, out, err) with separate (wwhd_native.run);
    output: where the decomp library goes; log: a text file for the tools' output."""
    IS_ANDROID = True
    PORTABLE = True  # (no runtime files are copied next to the library)

    def __init__(self, setup, package, compiler, tools, native_run, output, log, cancel=lambda: False,
                 progress=lambda event: None, shell=SHELL):
        self.setup, self.PKG = setup, str(package)
        self.compiler, self.DECOMP_TOOLS = compiler, dict(tools)
        self.native, self.cancel, self.progress, self.shell = native_run, cancel, progress, shell
        self.DECOMP_OUTPUT = str(output)
        self.log = log
        self.lock = threading.RLock()  # one in-process Python tool at a time (sys.argv, os.environ, the folder)

    def __getattr__(self, name):  # LOG, SetupError, game_builds, free_space, human, debug_info_flags, ...
        if name == "translate_cached":  # (its cache copies with cp/clonefile, outside the bridge: translated each time)
            raise AttributeError(name)
        return getattr(self.setup, name)

    # ---- output
    def say(self, text):
        self.log.write(text + "\n")
        self.log.flush()
        self.progress({"stage": "decomp", "detail": text.strip()})

    def Progress(self, what):
        host = self

        class P:
            def update(self, done, total, text="", force=False):
                host.progress({"stage": "decomp", "detail": "%s: %s" % (what, text), "done": done, "total": total})

            def done(self, text=""):
                host.progress({"stage": "decomp", "detail": "%s: %s" % (what, text)})
        return P()

    # ---- native tools
    def run_native(self, cmd, env=None, cwd=None, separate=False, input=None):
        """-> (code, output bytes) or with separate (code, stdout, stderr)"""
        if self.cancel():
            raise InterruptedError("setup paused")
        cmd = [str(c) for c in cmd]
        if cwd:
            cmd = [self.shell, "-c", 'cd "$0" && exec "$@"', str(cwd)] + cmd
        kw = {"env": env, "cancel": self.cancel}
        if separate:
            kw["separate"] = True
        if input is not None:
            kw["input"] = input
        return self.native(cmd, **kw)

    def tool_command(self, name):
        """a tool name a Python tool runs -> its command on the device, or None"""
        if name in ("clang++", "c++"):
            # (decl_lint's -fsyntax-only pass: as the game units on Android, with android_compat.h first, which keeps
            # bionic's bzero / bcopy macros and stdio's remove() out of the decompilation's names; cmake/Decomp.cmake)
            return list(self.compiler["cxx"]) + ["-include", os.path.join(self.PKG, "tools", "decomp_build",
                                                                          "android_compat.h")]
        if name in ("clang", "cc"):
            return list(self.compiler["cc"])
        if name in self.DECOMP_TOOLS:
            return [self.DECOMP_TOOLS[name]]
        return None

    def subprocess_run(self, args, *, cwd=None, env=None, input=None, capture_output=False, stdout=None, stderr=None,
                       text=False, check=False, timeout=None, **_):
        """subprocess.run for the Python tools while they run here"""
        args = [os.fspath(a) for a in ([args] if isinstance(args, str) else args)]
        if len(args) > 1 and args[0] == sys.executable and args[1].endswith(".py"):
            out = io.StringIO()
            sink_out = stdout if hasattr(stdout, "write") else out
            sink_err = sink_out if stderr == subprocess.STDOUT else (stderr if hasattr(stderr, "write") else out)
            code = self.run_python(args, cwd=cwd, env=env, stdout=sink_out, stderr=sink_err)
            text_out = out.getvalue()
            res_out = text_out if text else text_out.encode()
            r = subprocess.CompletedProcess(args, code, res_out if (capture_output or stdout == subprocess.PIPE) else None,
                                            (res_out[:0] if capture_output else None))
        else:
            base = self.tool_command(args[0]) if not os.path.isabs(args[0]) else [args[0]]
            if base is None:
                raise FileNotFoundError(2, "not available on Android", args[0])
            data = input.encode() if isinstance(input, str) else input
            code, out, err = self.run_native(base + args[1:], env=env, cwd=cwd, separate=True, input=data)
            if stderr == subprocess.STDOUT:
                out, err = out + err, b""
            dec = (lambda b: b.decode("utf-8", "replace")) if text else (lambda b: b)
            for sink, data_ in ((stdout, out), (stderr, err)):
                if hasattr(sink, "write"):
                    sink.write(dec(data_) if "b" not in getattr(sink, "mode", "") else data_)
            r = subprocess.CompletedProcess(args, code, dec(out) if (capture_output or stdout == subprocess.PIPE) else None,
                                            dec(err) if (capture_output or stderr == subprocess.PIPE) else None)
        if check and r.returncode:
            raise subprocess.CalledProcessError(r.returncode, args, r.stdout, r.stderr)
        return r

    # ---- Python tools in this process
    def run_python(self, args, cwd=None, env=None, stdout=None, stderr=None):
        """[sys.executable, script.py, args...] -> exit code"""
        script = os.path.abspath(args[1])
        with self.lock:
            saved = (list(sys.argv), list(sys.path), dict(os.environ), os.getcwd(), set(sys.modules),
                     subprocess.run, concurrent.futures.ProcessPoolExecutor)
            try:
                if env is not None:
                    os.environ.clear()
                    os.environ.update(env)
                extra = [p for p in os.environ.get("PYTHONPATH", "").split(os.pathsep) if p]
                sys.path[:0] = [os.path.dirname(script)] + extra
                sys.argv = [script] + list(args[2:])
                if cwd:
                    os.chdir(cwd)
                subprocess.run = self.subprocess_run
                concurrent.futures.ProcessPoolExecutor = _ThreadPool
                code = 0
                with contextlib.redirect_stdout(stdout or self.log), contextlib.redirect_stderr(stderr or stdout or self.log):
                    try:
                        runpy.run_path(script, run_name="__main__")
                    except SystemExit as e:
                        if e.code is None or e.code == 0:
                            code = 0
                        elif isinstance(e.code, int):
                            code = e.code
                        else:
                            print(e.code, file=sys.stderr)
                            code = 1
                return code
            finally:
                argv, path, environ, folder, modules, run, pool = saved
                sys.argv[:] = argv
                sys.path[:] = path
                os.environ.clear()
                os.environ.update(environ)
                os.chdir(folder)
                subprocess.run, concurrent.futures.ProcessPoolExecutor = run, pool
                # the tools' own modules go with them (autoglue, decl_lint, capstone: their state is per run)
                for name in set(sys.modules) - modules:
                    f = getattr(sys.modules.get(name), "__file__", None) or ""
                    if not f.startswith(sys.base_prefix) or "site-packages" in f:
                        sys.modules.pop(name, None)

    def run_logged(self, cmd, cwd=None, env=None, what="command"):
        """setup.run_logged: Python tools here, native tools through the bridge; SetupError with the output's end"""
        cmd = [os.fspath(c) for c in cmd]
        if len(cmd) > 1 and cmd[0] == sys.executable and cmd[1].endswith(".py"):
            out = _Tee(self.log)
            code = self.run_python(cmd, cwd=cwd, env=env, stdout=out, stderr=out)
            text = out.tail()
        else:
            code, raw = self.run_native(cmd, env=env, cwd=cwd)
            text = raw.decode("utf-8", "replace")
            self.log.write("$ %s\n%s" % (" ".join(cmd), text))
        if code:
            raise self.setup.SetupError("%s failed:\n%s" % (what, "\n".join(text.strip().splitlines()[-20:])))
        return text

    # ---- the installer's compile and link with the bridge
    def compile_gamecode(self, tc, manifest, gen_dir, obj_dir, jobs, cache_dir=None):
        original = self.setup.subprocess
        host = self

        def run(command, env=None, stdout=None, stderr=None, **_):
            code, out = host.run_native(command, env=env)
            return SimpleNamespace(returncode=code, stdout=out)
        self.setup.subprocess = SimpleNamespace(run=run, PIPE=subprocess.PIPE, STDOUT=subprocess.STDOUT,
                                                DEVNULL=subprocess.DEVNULL, CalledProcessError=subprocess.CalledProcessError)
        try:
            return self.setup.compile_gamecode(tc, manifest, gen_dir, obj_dir, jobs, cache_dir=cache_dir)
        finally:
            self.setup.subprocess = original

    def link_game(self, *a, **k):
        original = self.setup.run_logged
        self.setup.run_logged = self.run_logged
        try:
            return self.setup.link_game(*a, **k)
        finally:
            self.setup.run_logged = original

    def download(self, url, dest, sha256, size, what):
        """setup.download with Android's certificate store (Python's ssl finds none by itself there)"""
        import urllib.request
        capath = next((p for p in CA_PATHS if os.path.isdir(p)), None)
        if capath is None:
            return self.setup.download(url, dest, sha256, size, what)
        try:
            ctx = android_ssl_context(capath)
        except (OSError, ValueError) as e:
            raise self.setup.SetupError("download failed: the certificate store could not be read (%s)" % e)
        self.say("  " + what + "...")
        h = hashlib.sha256()
        req = urllib.request.Request(url, headers={"User-Agent": "wwhd-setup"})
        try:
            with urllib.request.urlopen(req, timeout=120, context=ctx) as r, open(dest + ".part", "wb") as f:
                for chunk in iter(lambda: r.read(1 << 20), b""):
                    if self.cancel():
                        raise InterruptedError("setup paused")
                    f.write(chunk)
                    h.update(chunk)
        except OSError as e:
            raise self.setup.SetupError("download failed: %s" % e)
        if sha256 and h.hexdigest() != sha256:
            os.remove(dest + ".part")
            raise self.setup.SetupError("the download does not match its checksum")
        os.replace(dest + ".part", dest)


class _ThreadPool(concurrent.futures.ThreadPoolExecutor):
    """ProcessPoolExecutor's interface on threads (no worker processes in the app)"""

    def __init__(self, max_workers=None, mp_context=None, initializer=None, initargs=(), **_):
        super().__init__(max_workers=max_workers, initializer=initializer, initargs=initargs)

    def map(self, fn, *iterables, timeout=None, chunksize=1):
        return super().map(fn, *iterables, timeout=timeout)


class _Tee(io.TextIOBase):
    def __init__(self, log):
        self.log, self.buf = log, []

    def write(self, text):
        self.log.write(text)
        self.buf.append(text)
        if len(self.buf) > 4000:
            self.buf = self.buf[-2000:]
        return len(text)

    def flush(self):
        self.log.flush()

    def tail(self):
        return "".join(self.buf)[-20000:]


def identity(adapter, manifest, compiler_identity, game, code_mods, mode, whole):
    """the decomp library's inputs: the release (its runtime SDK and installer, the decomp parts and pin), the
    compiler, the game, the code-mod choice, the mode and the link kind"""
    return hashlib.sha256(json.dumps({
        "port_revision": adapter.port_revision, "decomp": manifest.get("decomp"),
        "installer": adapter.hash_file(adapter.package / "tools/installer/decomp_setup.py"),
        "host": adapter.hash_file(Path(__file__)), "toolchain": compiler_identity,
        "rpx": adapter.hash_file(Path(game) / "code/cking.rpx"), "code_mods": bool(code_mods), "mode": mode,
        "whole": bool(whole)}, sort_keys=True).encode()).hexdigest()


def build(adapter, compiler, manifest, game, native_run, jobs=1, code_mods=False, mode="fast", whole=False,
          mod_hooks=None):
    """The decomp version's library for the Android app: a checkpoint stage decomp-<identity> in the adapter's storage
    (reused when complete), its work data in decomp-data (the decompilation's source, the declaration table, the object
    cache: kept for the next build). Returns (library path, info). compiler: the job's compiler description (cc, cxx,
    ar, env, identity, decomp_tools). mod_hooks: the decomp version's own decomp-mod-hooks.txt (its data folder), merged
    into setup's list."""
    import decomp_setup
    adapter.boundary()
    if not isinstance(manifest.get("decomp"), dict):
        raise adapter.setup.SetupError("this app has no decomp version parts")
    tools = compiler.get("decomp_tools") or {}
    missing = [t for t in ("llvm-objcopy",) if not tools.get(t)]
    if missing:
        raise adapter.setup.SetupError("this app's compiler cannot build the decomp version (no %s)" % ", ".join(missing))
    tools = dict(tools, **{"ld.lld": compiler["ld"]}) if compiler.get("ld") else tools
    key = identity(adapter, manifest, compiler["identity"], game, code_mods, mode, whole)
    stage = adapter.storage / ("decomp-" + key)
    stage.mkdir(exist_ok=True)
    library, marker = stage / "libgame.so", stage / "complete.json"
    try:
        record = json.loads(marker.read_text())
        if record["identity"] == key and record["sha256"] == adapter.hash_file(library):
            adapter.emit({"stage": "decomp", "state": "reused", "bytes": library.stat().st_size})
            return library, record["info"]
    except (OSError, ValueError, KeyError, TypeError):
        pass
    marker.unlink(missing_ok=True)
    for leftover in stage.glob("pending-*"):
        shutil.rmtree(leftover)
    pending = stage / ("pending-" + uuid.uuid4().hex)
    pending.mkdir()
    data = adapter.storage / "decomp-data"
    data.mkdir(exist_ok=True)
    if mod_hooks and os.path.isfile(mod_hooks):  # (the game's own record of hooks it did not know)
        known = decomp_setup.read_hook_list(str(data / decomp_setup.MOD_HOOKS_FILE))
        found = decomp_setup.read_hook_list(str(mod_hooks))
        if not found <= known:
            (data / decomp_setup.MOD_HOOKS_FILE).write_text(
                "# the game functions installed code mods hook (the decomp version's record)\n" +
                "".join("%08X\n" % a for a in sorted(known | found)))
    started = time.monotonic()
    # identity: the compiler bundle's (AndroidToolchain.java); the object cache knows the compiler by it, not by the
    # APK's native-lib folder its programs are in (a new random name on every app update; setup.object_cache)
    tc = SimpleNamespace(cc=list(compiler["cc"]), cxx=list(compiler["cxx"]), ar=list(compiler["ar"]),
                         env=compiler.get("env"), rsp=compiler.get("rsp", True), identity=compiler["identity"])
    env_switch = os.environ.get("WWHD_DECOMP_WHOLE_LTO")
    os.environ["WWHD_DECOMP_WHOLE_LTO"] = "1" if whole else "0"
    adapter.emit({"stage": "decomp", "state": "running", "mode": mode, "whole_lto": bool(whole)})
    log = (stage / "decomp.log").open("w", encoding="utf-8")
    host = Host(adapter.setup, adapter.package, compiler, tools, native_run, pending / "libgame.so", log,
                cancel=adapter.pause_requested, progress=adapter.emit)
    host.LOG = SimpleNamespace(write=lambda text: (log.write(text + "\n"), log.flush()))
    ctx = SimpleNamespace(manifest=manifest, data_dir=str(data), game_dir=str(Path(game).resolve()),
                          args=SimpleNamespace(keep_work=False))
    # the object cache asks the compiler for its version and preprocessed sources: through the bridge too
    import object_cache
    cache_subprocess = object_cache.subprocess
    object_cache.subprocess = SimpleNamespace(run=host.subprocess_run, PIPE=subprocess.PIPE, STDOUT=subprocess.STDOUT,
                                              DEVNULL=subprocess.DEVNULL, CalledProcessError=subprocess.CalledProcessError)
    try:
        try:
            _, rev, nunits, info = decomp_setup.build(host, ctx, tc, jobs, mode=mode, code_mods=code_mods)
        except InterruptedError:
            if adapter.pause_requested():
                adapter.emit({"stage": "decomp", "state": "paused", "restart_on_resume": True})
                from setup_adapter import Paused
                raise Paused() from None
            raise
        candidate = pending / "libgame.so"
        if not candidate.is_file() or candidate.stat().st_size == 0:
            raise RuntimeError("the decomp link produced no library")
        with candidate.open("rb") as stream:
            os.fsync(stream.fileno())
        candidate.chmod(0o400)
        os.replace(candidate, library)
        info = dict(info, revision=rev, units=nunits, seconds=round(time.monotonic() - started))
        from setup_adapter import atomic_json
        atomic_json(marker, {"identity": key, "sha256": adapter.hash_file(library), "info": info})
    finally:
        object_cache.subprocess = cache_subprocess
        log.close()
        shutil.rmtree(pending, ignore_errors=True)
        shutil.rmtree(data / "decomp-work", ignore_errors=True)
        if env_switch is None:
            os.environ.pop("WWHD_DECOMP_WHOLE_LTO", None)
        else:
            os.environ["WWHD_DECOMP_WHOLE_LTO"] = env_switch
    adapter.emit({"stage": "decomp", "state": "complete", "seconds": info["seconds"], "bytes": library.stat().st_size})
    adapter.boundary()
    return library, info
