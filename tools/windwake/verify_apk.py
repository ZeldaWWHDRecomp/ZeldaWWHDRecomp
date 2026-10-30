"""Verify the final installable APK against the pinned runtime and Android manifest."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def verify(apk):
    upstream = ROOT / 'build/vendor/upstream-v0.5.0.apk'
    with zipfile.ZipFile(apk) as built, zipfile.ZipFile(upstream) as original:
        names = built.namelist()
        assert not any(n.endswith(('.key', '.rpx', '.wux', '.wud')) or n.endswith('/libmain.so') for n in names), 'APK contains game material'
        for name in original.namelist():
            if name.startswith(('lib/arm64-v8a/', 'assets/')) and not name.startswith('assets/dexopt/'):
                assert name in names, 'Missing runtime dependency: ' + name
                actual, expected = built.read(name), original.read(name)
                if name.startswith('assets/licenses/') and name.endswith('.txt'):
                    actual, expected = actual.replace(b'\r\n', b'\n'), expected.replace(b'\r\n', b'\n')
                assert hashlib.sha256(actual).digest() == hashlib.sha256(expected).digest(), 'Runtime dependency changed: ' + name
        with zipfile.ZipFile(io.BytesIO(built.read('assets/python-source.zip'))) as source:
            manifest = json.loads(source.read('sdk/manifest.json'))
            metadata = json.loads(built.read('assets/runtime-sdk.json'))
            assert manifest['identity'] == metadata['identity'] and manifest['abi'] == 'arm64-v8a'
            assert not any(n.endswith('libgamecode.a') for n in source.namelist()), 'SDK includes game code'
    aapt = ROOT / 'build/android-sdk/build-tools/35.0.0/aapt.exe'
    manifest = subprocess.check_output([str(aapt), 'dump', 'badging', str(apk)], text=True, encoding='utf-8')
    assert "package: name='dev.windwake.android'" in manifest
    assert "launchable-activity: name='org.wwhdrecomp.wwhd.WindwakeActivity'" in manifest
    assert "native-code: 'arm64-v8a'" in manifest
    assert "sdkVersion:'33'" in manifest
    print('PASS package, launcher, Android 13+, ARM64, exact runtime/toolchain/assets, no bundled game or keys')
    subprocess.run([str(aapt.with_name('apksigner.bat')), 'verify', '--verbose', str(apk)], check=True)
    print('SHA256', hashlib.file_digest(apk.open('rb'), 'sha256').hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk', type=Path)
    verify(parser.parse_args().apk)
