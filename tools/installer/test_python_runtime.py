#!/usr/bin/env python3
"""Release integration smoke: download verified private Python and exercise actual codecs.

Runs only when explicitly invoked by release CI; regular installer unit tests stay offline.
"""
import tempfile
from unittest import mock
import setup

with tempfile.TemporaryDirectory(prefix="wwhd-python-") as data:
    python = setup.ensure_setup_python(data, prefer_system=False)
    if not setup.python_setup_capable(python):
        raise SystemExit("private setup Python failed isolated codec probe")
    with mock.patch.object(setup, "download", side_effect=AssertionError("cache redownload")):
        if setup.ensure_setup_python(data, prefer_system=False) != python:
            raise SystemExit("private Python cache selection changed")
print("Private setup Python: isolated Zstd round trip and cached reuse PASS")
