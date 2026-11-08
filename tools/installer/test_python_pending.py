#!/usr/bin/env python3
"""Issue #472: setup's private-Python download fails (GitHub answering HTTP 504). Setup still finishes, records
the missing Python, and the first feature that needs it downloads it again. A local HTTP server plays GitHub.
No game files, no internet: python3 test_python_pending.py"""
import hashlib
import http.server
import io
import json
import os
import sys
import tarfile
import tempfile
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup  # noqa: E402


def python_archive():
    """A stand-in for python-build-standalone's install_only archive: python/bin/python3."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        body = b"#!/bin/sh\n"
        info = tarfile.TarInfo("python/bin/python3")
        info.size, info.mode = len(body), 0o755
        tar.addfile(info, io.BytesIO(body))
    return buf.getvalue()


class FakeGitHub:
    """Answers 504 Gateway Time-out while `broken`, else the archive."""
    def __init__(self, body):
        self.body, self.broken, self.requests = body, True, 0
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                owner.requests += 1
                if owner.broken:
                    self.send_error(504, "Gateway Time-out")
                    return
                self.send_response(200)
                self.send_header("Content-Length", str(len(owner.body)))
                self.end_headers()
                self.wfile.write(owner.body)

            def log_message(self, *args):
                pass
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = "http://127.0.0.1:%d/cpython-install_only.tar.gz" % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()


class PythonPending(unittest.TestCase):
    def setUp(self):
        body = python_archive()
        self.github = FakeGitHub(body)
        self.addCleanup(self.github.close)
        pins = {"python": {"macos": {"url": self.github.url, "sha256": hashlib.sha256(body).hexdigest(),
                                     "size": len(body)}}}
        self.waits = []
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.data = self.tmp.name
        for patch in (mock.patch.multiple(setup, IS_WIN=False, IS_MAC=True, IS_LINUX=False, PORTABLE=False),
                      mock.patch.object(setup, "host_arch", return_value="aarch64"),
                      mock.patch.object(setup, "load_toolchains", return_value=pins),
                      # the system Python lacks 3.14; the unpacked one is good
                      mock.patch.object(setup, "python_setup_capable",
                                        side_effect=lambda exe: exe != sys.executable and os.path.isfile(exe)),
                      mock.patch.object(setup, "local_copy", return_value=None),
                      mock.patch.object(setup.time, "sleep", side_effect=self.waits.append)):
            patch.start()
            self.addCleanup(patch.stop)

    def config(self):
        with open(os.path.join(self.data, "guest-sdk.json"), encoding="utf-8") as f:
            return json.load(f)

    def test_failed_download_is_recorded_and_retried_by_a_later_feature(self):
        tc = setup.Toolchain(["cc"], [], [])
        pending = setup.write_guest_build_config(self.data, tc)  # no SetupError: setup goes on
        self.assertIn("504", pending)
        self.assertEqual(self.github.requests, setup.DOWNLOAD_ATTEMPTS)
        self.assertEqual(self.waits, [2.0, 4.0, 8.0, 16.0, 32.0])  # about a minute, not 5 quick tries
        config = self.config()
        private = setup.private_python_path(self.data)
        self.assertEqual(config["python"], [private])
        self.assertFalse(os.path.exists(private))  # the game reports it missing and asks setup_python for it
        self.assertIn("504", config["python_pending"])
        self.assertEqual(config["setup_python"], [sys.executable])
        self.assertEqual(setup.pending_python(self.data), config["python_pending"])

        # a mod needs it while GitHub is still down: a clear message, nothing changed
        with self.assertRaisesRegex(setup.DownloadFailed, "could not be downloaded.*next time a mod needs it"):
            setup.pending_python_retry(self.data)
        self.assertEqual(self.config(), config)

        # GitHub works again: the next feature downloads it
        self.github.broken = False
        setup.pending_python_retry(self.data)
        config = self.config()
        self.assertEqual(config["python"], [private])
        self.assertTrue(os.path.isfile(private))
        self.assertNotIn("python_pending", config)
        self.assertIsNone(setup.pending_python(self.data))
        requests = self.github.requests
        setup.pending_python_retry(self.data)  # nothing pending: no download
        self.assertEqual(self.github.requests, requests)

    def test_code_mod_rebuild_retries_first_and_reports_failure(self):
        setup.write_guest_build_config(self.data, setup.Toolchain(["cc"], [], []))
        status = os.path.join(self.data, "status.json")
        args = SimpleNamespace(rebuild_code_mods=True, code_mods="1", code_mods_status=status,
                               code_mods_cancel=os.path.join(self.data, "cancel"), repair_guest_python=False)
        ctx = SimpleNamespace(data_dir=self.data, game_dir=os.path.join(self.data, "game"))
        with mock.patch.object(setup, "Ctx", return_value=ctx), mock.patch.object(setup.code_mods, "rebuild") as rebuild:
            with self.assertRaises(setup.DownloadFailed):
                setup.run(args, None)
            rebuild.assert_not_called()
            report = json.loads(Path(status).read_text())
            self.assertEqual(report["state"], "error")
            self.assertIn("could not be downloaded", report["message"])
            self.github.broken = False
            with mock.patch.object(setup, "valid_game_folder", return_value=True):
                self.assertEqual(setup.run(args, None), 0)
            rebuild.assert_called_once()
        self.assertNotIn("python_pending", self.config())

    def test_required_download_still_stops_setup(self):
        # only the setup Python is optional: download() itself still raises (toolchain, decomp source ...)
        with self.assertRaises(setup.DownloadFailed):
            setup.download(self.github.url, os.path.join(self.data, "x"), None, None, "compiler")

    def test_install_finishes_and_records_missing_python(self):
        root = Path(self.data)
        game = root / "game"
        (game / "code").mkdir(parents=True)
        (game / "code/cking.rpx").write_bytes(b"synthetic fingerprint input")
        data = root / "data"
        data.mkdir()
        tc = SimpleNamespace(cc=["test-compiler"], env={}, desc="fixture")
        args = SimpleNamespace(code_mods="0", jobs=1, shortcuts=False, no_shortcuts=True, keep_work=False)
        ctx = SimpleNamespace(data_dir=str(data), game_dir=str(game), args=args,
                              manifest={"platform": "fixture", "toolchain": {}, "exe": "fixture"},
                              exe=str(data / "bin/fixture"), exe_dir=str(data / "bin"),
                              version="test", state=lambda: {})

        def translate(game, gen, hooks=False):
            Path(gen).mkdir()
            return 1

        def link(tc, manifest, objs, work, target):
            Path(target).write_bytes(b"synthetic executable")
        with mock.patch.multiple(setup, PKG=root), \
             mock.patch.object(setup, "valid_game_folder", return_value=True), \
             mock.patch.object(setup, "game_folder_title", return_value=None), \
             mock.patch.object(setup, "check_game_version"), \
             mock.patch.object(setup, "get_toolchain", return_value=tc), \
             mock.patch.object(setup, "run_logged", return_value="fixture compiler"), \
             mock.patch.object(setup, "free_space", return_value=20 << 30), \
             mock.patch.object(setup, "recompile", side_effect=translate), \
             mock.patch.object(setup, "compile_gamecode", return_value=[]), \
             mock.patch.object(setup, "link_game", side_effect=link), \
             mock.patch.object(setup, "prepare_shaders_nonfatal", return_value={"status": "skipped"}), \
             mock.patch.object(setup, "install_story_notebook", return_value=None):
            state = setup.install(ctx, ("installed", str(game)))
        self.assertTrue(Path(state["exe"]).is_file())
        self.assertIn("504", state["setup_python_missing"])
        saved = json.loads((data / "install.json").read_text())
        self.assertIn("504", saved["setup_python_missing"])
        self.assertIn("504", setup.pending_python(str(data)))


if __name__ == "__main__":
    unittest.main()
