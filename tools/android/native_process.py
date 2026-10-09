"""The installer's limited subprocess interface over Android's native bridge.

This is an adapter for disc_info/archive_info/run_extract, not a replacement
subprocess implementation. stdin is private, bounded key material; stdout and
stderr are drained concurrently with backpressure and bounded diagnostics.
"""
import io
import queue
import threading
from types import SimpleNamespace

_LIMIT = 4 * 1024 * 1024


class _Output:
    def __init__(self, owner, propagate):
        self.owner, self.propagate = owner, propagate
        self.queue = queue.Queue(maxsize=16)

    def chunks(self):
        while True:
            try:
                chunk = self.queue.get(timeout=.1)
            except queue.Empty:
                if not self.owner.done.is_set():
                    continue
                self.owner.thread.join()
                if self.propagate and self.owner.failure:
                    raise self.owner.failure
                return
            yield chunk

    def read(self):
        result = bytearray()
        for chunk in self.chunks():
            result.extend(chunk[:max(0, _LIMIT - len(result))])
        return bytes(result)

    def __iter__(self):
        pending = bytearray()
        exhausted = False
        try:
            for chunk in self.chunks():
                pending.extend(chunk)
                while b"\n" in pending:
                    line, _, rest = pending.partition(b"\n")
                    pending = bytearray(rest)
                    if len(line) > 65536: raise ValueError("Extractor progress line exceeds 64 KiB")
                    yield bytes(line) + b"\n"
                if len(pending) > 65536:
                    raise ValueError("Extractor progress line exceeds 64 KiB")
            if pending:
                yield bytes(pending)
            exhausted = True
        finally:
            if not exhausted:
                self.owner.abort.set()
                # A progress consumer can fail (for example low storage).
                # Keep draining until the native child has been killed/reaped.
                while self.owner.thread.is_alive():
                    try: self.queue.get(timeout=.1)
                    except queue.Empty: pass
                self.owner.thread.join()


class _Input(io.BytesIO):
    def __init__(self, owner):
        super().__init__()
        self.owner = owner

    def __del__(self):
        # Garbage collection is not authorization to start an abandoned job.
        io.BytesIO.close(self)

    def write(self, data):
        if self.tell() + len(data) > 4096:
            raise ValueError("Extractor key input exceeds 4 KiB")
        return super().write(data)

    def close(self):
        if not self.closed:
            data = self.getvalue()
            super().close()
            self.owner.start(data)


class _Process:
    def __init__(self, runner, command, cancel, env=None, **options):
        if set(options) != {"stdin", "stdout", "stderr"} or any(value != -1 for value in options.values()):
            raise ValueError("Extractor requires three pipe streams")
        self.runner, self.command, self.cancel, self.env = runner, command, cancel, env
        self.returncode, self.failure, self.thread = None, None, None
        self.abort = threading.Event()
        self.done = threading.Event()
        self.stdout, self.stderr = _Output(self, True), _Output(self, False)
        self.stdin = _Input(self)

    def start(self, data):
        def worker():
            try:
                def output(stream, chunk):
                    target = self.stdout if stream == "stdout" else self.stderr
                    while True:
                        if self.abort.is_set() or self.cancel(): raise InterruptedError("Extractor reader cancelled")
                        try:
                            target.queue.put(chunk, timeout=.1)
                            return
                        except queue.Full: pass
                self.returncode, _, _ = self.runner(self.command, env=self.env, input=data,
                    separate=True, on_output=output, cancel=lambda: self.abort.is_set() or self.cancel())
            except BaseException as failure:
                self.failure = failure
            finally:
                # Completion must never wait for space in an abandoned pipe.
                # Readers drain queued bytes before observing this event.
                self.done.set()
        self.thread = threading.Thread(target=worker, name="android-extractor")
        self.thread.start()

    def wait(self):
        if self.thread is None:
            raise RuntimeError("Close extractor stdin before waiting")
        self.thread.join()
        if self.failure:
            raise self.failure
        return self.returncode


class ProcessBridge:
    PIPE, STDOUT, DEVNULL = -1, -2, -3

    def __init__(self, runner, cancel=lambda: False, env=None):
        self.runner, self.cancel, self.env = runner, cancel, env

    def run(self, command, input=None, stdin=None, stdout=None, stderr=None, env=None):
        if stdout != self.PIPE or stderr != self.PIPE or stdin not in (None, self.DEVNULL):
            raise ValueError("Unexpected extractor information process options")
        code, out, err = self.runner(command, input=input, env=self.env if env is None else env,
                                     separate=True, cancel=self.cancel)
        return SimpleNamespace(returncode=code, stdout=out, stderr=err)

    def Popen(self, command, **options):
        options.setdefault("env", self.env)
        return _Process(self.runner, command, self.cancel, **options)
