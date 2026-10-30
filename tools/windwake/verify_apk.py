"""Verify the APK against pinned upstream dependencies and one authored touch object."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import zipfile

import runtime_patch

ROOT = Path(__file__).resolve().parents[2]


def verify(apk):
    upstream = ROOT / 'build/vendor/upstream-v0.5.0.apk'
    assert runtime_patch.file_sha(upstream) == runtime_patch.PIN.apk_sha256, 'Upstream APK checksum mismatch'
    patch, patched_archive, patched_metadata = runtime_patch.verified_patch(
        ROOT / 'build/vendor/runtime-v0.5.0',
        ROOT / 'tools/windwake/runtime-patches/startup-touch-v1.zip', ROOT)
    replacements = {'assets/python-source.zip': patched_archive, 'assets/runtime-sdk.json': patched_metadata}
    assert set(replacements) == runtime_patch.MODIFIED_FILES, 'Unexpected patched asset allowlist'
    with zipfile.ZipFile(apk) as built, zipfile.ZipFile(upstream) as original:
        names = built.namelist()
        assert len(names) == len(set(names)), 'APK contains duplicate entries'
        assert not any(n.endswith(('.key', '.rpx', '.wux', '.wud', '.wua', '.iso', '.rvz', '.sav')) or
                       n.endswith(('/libmain.so', '/libgamecode.a')) for n in names), 'APK contains game material'
        def runtime_name(name):
            return name.startswith(('lib/', 'assets/')) and not name.startswith('assets/dexopt/') and not name.endswith('/')
        expected_names = {name for name in original.namelist() if runtime_name(name)}
        actual_names = {name for name in names if runtime_name(name)}
        assert actual_names == expected_names, 'Runtime dependency inventory changed'
        for name in sorted(expected_names):
            actual = built.read(name)
            expected = replacements[name] if name in replacements else original.read(name)
            if name.startswith('assets/licenses/') and name.endswith('.txt'):
                actual, expected = actual.replace(b'\r\n', b'\n'), expected.replace(b'\r\n', b'\n')
            assert hashlib.sha256(actual).digest() == hashlib.sha256(expected).digest(), 'Runtime dependency changed: ' + name
        with zipfile.ZipFile(io.BytesIO(built.read('assets/python-source.zip'))) as source:
            entries = {name: source.read(name) for name in source.namelist()}
            assert len(source.namelist()) == len(entries), 'SDK archive contains duplicate entries'
            metadata = json.loads(built.read('assets/runtime-sdk.json'))
            manifest = runtime_patch.validate_sdk(entries, metadata)
            runtime_patch.validate_elf(entries[runtime_patch.TARGET])
            assert manifest['identity'] == patch['result']['runtime_sdk']['identity'], 'Patched SDK identity mismatch'
            assert not any(n.endswith('libgamecode.a') for n in entries), 'SDK includes game code'
    aapt = ROOT / 'build/android-sdk/build-tools/35.0.0/aapt.exe'
    manifest = subprocess.check_output([str(aapt), 'dump', 'badging', str(apk)], text=True, encoding='utf-8')
    assert "package: name='dev.windwake.android'" in manifest
    assert "launchable-activity: name='org.wwhdrecomp.wwhd.WindwakeActivity'" in manifest
    assert "native-code: 'arm64-v8a'" in manifest
    assert "sdkVersion:'33'" in manifest
    print('PASS package, launcher, Android 13+, ARM64; pinned upstream runtime/toolchain/assets plus one verified authored startup-touch object; no bundled game or keys')
    subprocess.run([str(aapt.with_name('apksigner.bat')), 'verify', '--verbose', str(apk)], check=True)
    print('SHA256', hashlib.file_digest(apk.open('rb'), 'sha256').hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk', type=Path)
    verify(parser.parse_args().apk)
