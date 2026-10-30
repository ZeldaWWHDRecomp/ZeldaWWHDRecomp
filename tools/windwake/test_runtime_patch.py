"""Small authored SDK fixtures verify the one-object boundary and package identities."""
from pathlib import Path
import copy
import json
import struct
import tempfile
import unittest

import runtime_patch as patcher


def elf(symbols=patcher.REQUIRED_SYMBOLS, machine=183, kind=1):
    # Valid relocatable ELF with .text, .strtab and .symtab, containing authored
    # AArch64 RET bytes and JNI function aliases. No game/native vendor payload.
    strings = b'\0'
    symbols_raw = bytes(24)
    for name in sorted(symbols):
        symbols_raw += struct.pack('<IBBHQQ', len(strings), 0x12, 0, 1, 0, 4)
        strings += name.encode() + b'\0'
    text = bytes.fromhex('c0035fd6')
    str_offset = 64 + len(text)
    sym_offset = str_offset + len(strings)
    sh_offset = sym_offset + len(symbols_raw)
    ident = b'\x7fELF\x02\x01\x01' + bytes(9)
    header = ident + struct.pack('<HHIQQQIHHHHHH', kind, machine, 1, 0, 0, sh_offset, 0, 64, 0, 0, 64, 4, 0)
    sections = bytes(64)
    sections += struct.pack('<IIQQQQIIQQ', 0, 1, 6, 0, 64, len(text), 0, 0, 4, 0)
    sections += struct.pack('<IIQQQQIIQQ', 0, 3, 0, 0, str_offset, len(strings), 0, 0, 1, 0)
    sections += struct.pack('<IIQQQQIIQQ', 0, 2, 0, 0, sym_offset, len(symbols_raw), 2, 1, 8, 24)
    return header + text + strings + symbols_raw + sections


class RuntimePatchTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.base = self.root / 'base'
        assets = self.base / 'assets'; assets.mkdir(parents=True)
        native = self.base / 'jniLibs/arm64-v8a'; native.mkdir(parents=True)
        (native / 'libSDL3.so').write_bytes(b'authored unchanged native fixture')
        (assets / 'another-asset.txt').write_bytes(b'authored unchanged asset fixture')
        self.entries = {patcher.TARGET: b'original authored object fixture',
                        'sdk/obj/75-another.cpp.o': b'authored object that must not change',
                        'sdk/lib/libfixture.a': b'authored SDK library',
                        'sdk/include/cpu.h': b'authored SDK header',
                        'tools/android/setup.py': b'authored Python fixture'}
        self.manifest = {'schema': 1, 'host_api': 1, 'api': 33, 'abi': 'arm64-v8a',
                         'files': {name[4:]: patcher.sha(raw) for name, raw in self.entries.items() if name.startswith('sdk/')},
                         'link': ['-shared', '{sdk}/' + patcher.TARGET[4:], '{gamecode}']}
        self.manifest['identity'] = patcher.sha(json.dumps(self.manifest, sort_keys=True).encode())
        self.entries['sdk/manifest.json'] = patcher.json_bytes(self.manifest)
        source = patcher.archive_bytes(self.entries)
        sdk = patcher.archive_bytes({name: raw for name, raw in self.entries.items() if name.startswith('sdk/')})
        metadata = {'schema': 1, 'host_api': 1, 'identity': self.manifest['identity'], 'abi': 'arm64-v8a', 'api': 33,
                    'sha256': patcher.sha(sdk), 'archive_bytes': len(sdk),
                    'installed_bytes': sum(len(raw) for name, raw in self.entries.items() if name.startswith('sdk/'))}
        compiler = patcher.json_bytes({'abi': 'arm64-v8a', 'api': 33, 'compiler_identity': 'authored fixture'})
        (assets / 'python-source.zip').write_bytes(source)
        (assets / 'runtime-sdk.json').write_bytes(patcher.json_bytes(metadata))
        (assets / 'toolchain-apk.json').write_bytes(compiler)
        self.pin = patcher.BasePin(patcher.sha(b'authored base APK'), patcher.sha(source), patcher.sha(compiler),
                                  self.manifest['files'][patcher.TARGET[4:]], metadata)
        for name in patcher.SOURCES:
            path = self.root / name; path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'// SPDX-License-Identifier: MPL-2.0\r\n// Authored fixture\r\n')
        self.obj = self.root / 'touch.o'; self.obj.write_bytes(elf())
        self.bundle = self.root / 'patch.zip'
        patcher.pack(self.base, self.obj, self.root, self.bundle, pin=self.pin)

    def test_reproducible_patch_preserves_every_unrelated_byte_and_recomputes_sdk(self):
        second = self.root / 'same.zip'
        patcher.pack(self.base, self.obj, self.root, second, pin=self.pin)
        self.assertEqual(self.bundle.read_bytes(), second.read_bytes())
        result = patcher.apply(self.base, self.bundle, self.root, self.root / 'patched', pin=self.pin)
        entries = patcher.read_entries(self.root / 'patched/assets/python-source.zip')
        for name, raw in self.entries.items():
            if name not in (patcher.TARGET, 'sdk/manifest.json'):
                self.assertEqual(entries[name], raw, name)
        self.assertEqual(entries[patcher.TARGET], self.obj.read_bytes())
        manifest = json.loads(entries['sdk/manifest.json'])
        identity = manifest.pop('identity')
        self.assertEqual(identity, patcher.sha(json.dumps(manifest, sort_keys=True).encode()))
        original = copy.deepcopy(self.manifest); original.pop('identity')
        original['files'][patcher.TARGET[4:]] = patcher.sha(self.obj.read_bytes())
        self.assertEqual(manifest, original)
        metadata = json.loads((self.root / 'patched/assets/runtime-sdk.json').read_text())
        sdk = patcher.archive_bytes({name: raw for name, raw in entries.items() if name.startswith('sdk/')})
        self.assertEqual(metadata['sha256'], patcher.sha(sdk))
        self.assertEqual(metadata['archive_bytes'], len(sdk))
        self.assertEqual(metadata['installed_bytes'], sum(len(raw) for name, raw in entries.items() if name.startswith('sdk/')))
        self.assertEqual(result['runtime_identity'], identity)
        patcher.check(self.base, self.root / 'patched', self.bundle, self.root, pin=self.pin)

    def test_rejects_wrong_architecture_shared_library_or_wrong_runtime_object(self):
        for raw in (elf(machine=62), elf(kind=3), elf(symbols={'a_different_function'}), b'not an ELF'):
            with self.subTest(raw=raw[:24]), self.assertRaisesRegex(ValueError, 'Object'):
                patcher.validate_elf(raw)

    def test_source_hash_refuses_changed_source_but_ignores_checkout_line_endings(self):
        source = self.root / patcher.SOURCES[0]
        source.write_bytes(source.read_bytes().replace(b'\r\n', b'\n'))
        patcher.verified_patch(self.base, self.bundle, self.root, pin=self.pin)
        source.write_bytes(source.read_bytes() + b'// different logic\n')
        with self.assertRaisesRegex(ValueError, 'source hash mismatch'):
            patcher.apply(self.base, self.bundle, self.root, self.root / 'bad', pin=self.pin)
        self.assertFalse((self.root / 'bad').exists())

    def test_changed_base_sdk_entry_and_changed_applied_entry_are_rejected(self):
        patcher.apply(self.base, self.bundle, self.root, self.root / 'patched', pin=self.pin)
        entries = patcher.read_entries(self.root / 'patched/assets/python-source.zip')
        entries['sdk/include/cpu.h'] = b'corrupt unrelated header'
        (self.root / 'patched/assets/python-source.zip').write_bytes(patcher.archive_bytes(entries))
        with self.assertRaisesRegex(ValueError, 'only the touch object'):
            patcher.check(self.base, self.root / 'patched', self.bundle, self.root, pin=self.pin)
        entries = dict(self.entries, **{'sdk/include/cpu.h': b'changed original'})
        with self.assertRaisesRegex(ValueError, 'file inventory mismatch'):
            patcher.validate_sdk(entries, self.pin.metadata)
        (self.base / 'assets/python-source.zip').write_bytes(patcher.archive_bytes(entries))
        with self.assertRaisesRegex(ValueError, 'pinned'):
            patcher.load_base(self.base, pin=self.pin)

    def test_rejects_extra_patch_entry_and_native_asset_change(self):
        patcher.apply(self.base, self.bundle, self.root, self.root / 'patched', pin=self.pin)
        native = self.root / 'patched/jniLibs/arm64-v8a/libSDL3.so'
        native.write_bytes(b'different library')
        with self.assertRaisesRegex(ValueError, 'Unrelated runtime file changed'):
            patcher.check(self.base, self.root / 'patched', self.bundle, self.root, pin=self.pin)
        entries = patcher.read_entries(self.bundle)
        entries['sdk/obj/not-allowed.o'] = b'another object'
        self.bundle.write_bytes(patcher.archive_bytes(entries))
        with self.assertRaisesRegex(ValueError, 'unexpected entry inventory'):
            patcher.verified_patch(self.base, self.bundle, self.root, pin=self.pin)


if __name__ == '__main__':
    unittest.main()
