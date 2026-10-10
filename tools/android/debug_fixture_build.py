"""Exact authored build registration, packaged only for debug smoke tests.

Each installer gets a private lookup. The embedded translator uses the shared
module, so its exact-digest override is scoped to recompile and restored even
on pause/failure. The app serializes Python execution in its setup process.
"""
import hashlib
import importlib.util
from pathlib import Path
from types import SimpleNamespace

_spec = importlib.util.spec_from_file_location(
    "wwhd_debug_rpx", Path(__file__).resolve().parents[1] / "recomp/android_fixture.py")
_fixture = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_fixture)
synthetic_rpx = _fixture.synthetic_rpx


def register(setup):
    digest = hashlib.sha256(synthetic_rpx()).hexdigest()
    original = setup.game_builds
    build = original.Build({"name": "USA", "title_id": setup.SUPPORTED_TITLE,
                            "rpx_sha256": digest})
    setup.game_builds = SimpleNamespace(**vars(original))
    lookup = original.by_sha256
    fixture_lookup = lambda value: build if value == digest else lookup(value)
    setup.game_builds.by_sha256 = fixture_lookup
    setup.SUPPORTED_BUILDS = {**setup.SUPPORTED_BUILDS, build.title_id: build}
    recompile = setup.recompile
    def translate(*args, **kwargs):
        with _fixture.fixture_build_context(Path(setup.PKG) / "tools/recomp"):
            return recompile(*args, **kwargs)
    setup.recompile = translate
