"""Fetch the pinned upstream Android runtime, then apply the authored touch object.

The v0.5.0 runtime SDK contains runtime objects, never translated game code. The
player's game is compiled locally by the setup already included in that SDK.
The base remains separate and unchanged; the patch verifies its source hashes,
ELF/JNI payload, SDK identity and the two permitted modified assets.
"""
import hashlib
from pathlib import Path
import shutil
import urllib.request
import zipfile

import runtime_patch

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build' / 'vendor'
SDK = ROOT / 'build' / 'android-sdk'
APK_URL = 'https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/releases/download/v0.5.0/WindWakerHD-v0.5.0-android-arm64.apk'
APK_SHA256 = '6af660aac1c67e5d99bbc653b1c2b9f0e429a8513e0e9ca0c497b0c37177f791'
PATCH = ROOT / 'tools/windwake/runtime-patches/startup-touch-v1.zip'
PATCHED_RUNTIME = BUILD / 'runtime-v0.5.0-windwake'


def download(url, output, expected, algorithm='sha256'):
    output.parent.mkdir(parents=True, exist_ok=True)
    def digest(path):
        with path.open('rb') as stream:
            return hashlib.file_digest(stream, algorithm).hexdigest()
    if output.is_file() and digest(output) == expected:
        return output
    temporary = output.with_suffix(output.suffix + '.part')
    print('Downloading', output.name, flush=True)
    try:
        with urllib.request.urlopen(url, timeout=120) as src, temporary.open('wb') as dst:
            shutil.copyfileobj(src, dst, 1024 * 1024)
        if digest(temporary) != expected:
            raise ValueError('Checksum mismatch: ' + output.name)
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return output


def extract_checked(archive, destination, prefix=''):
    with zipfile.ZipFile(archive) as bundle:
        for entry in bundle.infolist():
            name = entry.filename
            if not name.startswith(prefix) or entry.is_dir():
                continue
            name = name[len(prefix):]
            path = (destination / name).resolve()
            if not path.is_relative_to(destination.resolve()):
                raise ValueError('Unsafe archive path')
            if prefix == 'assets/' and name.startswith('dexopt/'):
                path.unlink(missing_ok=True)  # ART profiles describe the upstream DEX, not this fork
                continue
            original = ROOT / 'android/app/src/main/assets' / name
            if prefix == 'assets/' and original.is_file():
                source_bytes, packaged_bytes = original.read_bytes(), bundle.read(entry)
                if name.endswith('.txt'):
                    source_bytes, packaged_bytes = source_bytes.replace(b'\r\n', b'\n'), packaged_bytes.replace(b'\r\n', b'\n')
                if source_bytes != packaged_bytes:
                    raise ValueError('Runtime asset differs from checked-out source: ' + name)
                path.unlink(missing_ok=True)  # source assets, including licences, are packaged once
                continue
            if path.is_file() and path.stat().st_size == entry.file_size:
                with path.open('rb') as installed, bundle.open(entry) as expected:
                    if hashlib.file_digest(installed, 'sha256').digest() == hashlib.file_digest(expected, 'sha256').digest():
                        continue  # also avoids replacing an in-use, verified adb.exe on Windows
            path.parent.mkdir(parents=True, exist_ok=True)
            with bundle.open(entry) as src, path.open('wb') as dst:
                shutil.copyfileobj(src, dst, 1024 * 1024)


def main():
    apk = download(APK_URL, BUILD / 'upstream-v0.5.0.apk', APK_SHA256)
    runtime = BUILD / 'runtime-v0.5.0'
    with zipfile.ZipFile(apk) as bundle:
        if 'lib/arm64-v8a/libmain.so' in bundle.namelist():
            raise ValueError('Runtime-only dependency unexpectedly contains a game library')
    extract_checked(apk, runtime / 'assets', 'assets/')
    extract_checked(apk, runtime / 'jniLibs' / 'arm64-v8a', 'lib/arm64-v8a/')
    patched = runtime_patch.apply(runtime, PATCH, ROOT, PATCHED_RUNTIME)
    print('Runtime ready: pinned upstream v0.5.0 plus one verified authored startup-touch object; identity',
          patched['runtime_identity'], flush=True)
    for name, relative, sha1, prefix, target in [
        ('platform-36.zip', 'platform-36_r02.zip', '2c1a80dd4d9f7d0e6dd336ec603d9b5c55a6f576', 'android-36/', SDK / 'platforms' / 'android-36'),
        ('build-tools-35.zip', 'build-tools_r35_windows.zip', 'af059bb67cf7786f45ee0db85e2d24985df1b4b6', 'android-15/', SDK / 'build-tools' / '35.0.0'),
        ('platform-tools.zip', 'platform-tools_r37.0.1-win.zip', 'e03e78b1d80b396f1c3358e31251cb31740e1110', 'platform-tools/', SDK / 'platform-tools'),
    ]:
        package = download('https://dl.google.com/android/repository/' + relative, BUILD / name, sha1, 'sha1')
        with zipfile.ZipFile(package) as bundle:
            if not any(n.startswith(prefix) for n in bundle.namelist()):
                raise ValueError('Unexpected SDK archive layout: ' + name)
        extract_checked(package, target, prefix)
    (ROOT / 'android' / 'local.properties').write_text('sdk.dir=' + SDK.as_posix() + '\n')
    print('Runtime and Android SDK ready:', SDK, flush=True)


if __name__ == '__main__':
    main()
