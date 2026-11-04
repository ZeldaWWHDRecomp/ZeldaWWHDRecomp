from test_permanent_resource_heap import synthetic_permanent_hash
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

import seed_package
import seed_prepare
from test_seed_prepare import SeedPrepare
from test_seed_compass_options import synthetic_compass_hashes
from test_seed_helmaroc_rng import synthetic_helmaroc_hashes


class SeedPackage(unittest.TestCase):
    def setUp(self):
        permanent = synthetic_permanent_hash()
        permanent.__enter__(); self.addCleanup(permanent.__exit__, None, None, None)
        classic = patch('seed_prepare.extract_classic_policy', return_value=0)
        classic.start(); self.addCleanup(classic.stop)
        particle = patch('seed_prepare.extract_particle_policy', return_value=0)
        particle.start(); self.addCleanup(particle.stop)

        quiet = patch('seed_prepare.extract_quiet_sail', return_value=0)
        quiet.start()
        self.addCleanup(quiet.stop)
        rng = synthetic_helmaroc_hashes()
        rng.__enter__()
        self.addCleanup(rng.__exit__, None, None, None)
        music = patch('seed_prepare.extract_music_policy', return_value=0)
        music.start()
        self.addCleanup(music.stop)

        context = synthetic_compass_hashes()
        context.__enter__()
        self.addCleanup(context.__exit__, None, None, None)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.base, self.seed, self.prepared, self.output = [self.root / name for name in ('base', 'seed', 'prepared', 'output')]
        for game in (self.base, self.seed):
            (game / 'content').mkdir(parents=True)
            (game / 'content/unchanged').write_bytes(b'unchanged fixture')
        (self.base / 'content/item').write_bytes(b'original fixture')
        (self.seed / 'content/item').write_bytes(b'randomized fixture')
        _, first, second = SeedPrepare().images()
        self.identity = {'base_rpx_sha256': 'a' * 64, 'seed_rpx_sha256': 'b' * 64, 'compatibility': {}}
        self.readers = patch('seed_prepare._validated_readers', return_value=(first, second, self.identity))
        self.readers.start()
        self.addCleanup(self.readers.stop)
        self.record = seed_prepare.prepare_seed(self.base, self.seed, self.prepared, True)
        self.fresh_elf = b'fresh compiled fixture ELF'
        self.calls = []

    def write_record(self):
        (self.prepared / 'seed-preparation.json').write_text(json.dumps(self.record))

    def fake_compiler(self, command, **kwargs):
        self.calls.append(command)
        if command[-1] == '--version':
            return subprocess.CompletedProcess(command, 0, stdout='fixture clang', stderr='')
        output = Path(command[command.index('-o') + 1])
        self.assertFalse(output.exists(), 'compiler output must be new, never a cached/prepared ELF')
        source = Path(command[command.index('-c') + 1])
        self.assertEqual((source.parent / 'seed_init_data.h').read_bytes(),
                         (self.prepared / 'seed_init_data.h').read_bytes())
        self.assertIn('-Werror', command)
        output.write_bytes(self.fresh_elf)
        return subprocess.CompletedProcess(command, 0, stdout='', stderr='')

    def build(self, compiler=None):
        with patch('seed_package.subprocess.run', side_effect=compiler or self.fake_compiler), \
             patch('seed_package._source_revision', return_value={'commit': 'fixture', 'compiler_inputs_modified': False}), \
             patch('seed_package._inspect', side_effect=lambda package: {
                 'elf_sha256': hashlib.sha256((package / 'mod.elf').read_bytes()).hexdigest(),
                 'allocation_size': 65536}):
            return seed_package.build_package(self.prepared, self.base, self.seed, self.output, 'fixture-clang')

    def test_explicit_release_resources_and_structured_compiler(self):
        sdk=self.root/'release-sdk';sdk.mkdir();(sdk/'wwhd_guest.h').write_text('/* synthetic SDK */')
        from unittest.mock import Mock
        provenance={'release_version':'fixture-release','source_sha256':'recorded'}
        def compile_argv(command,**kwargs):
            self.assertEqual(command[:3],['/explicit/compiler','cc','literal;no-shell'])
            return self.fake_compiler(command,**kwargs)
        with patch('seed_package.subprocess.run',side_effect=compile_argv), \
             patch('seed_package._source_revision',side_effect=AssertionError('Git is forbidden')), \
             patch('seed_package._inspect',side_effect=lambda package:{'elf_sha256':hashlib.sha256((package/'mod.elf').read_bytes()).hexdigest(),'allocation_size':65536}):
            result=seed_package.build_package(self.prepared,self.base,self.seed,self.output,
                     ['/explicit/compiler','cc','literal;no-shell'],sdk_include=sdk,provenance=provenance)
        self.assertEqual(result['source_revision'],provenance)
        self.assertEqual(result['compiler_argv'],['/explicit/compiler','cc','literal;no-shell'])
        self.assertTrue((self.output/'manifest.json').exists())

    def test_release_snapshot_hash_mismatch_has_no_output(self):
        with patch('seed_package.subprocess.run',side_effect=self.fake_compiler),self.assertRaisesRegex(ValueError,'source inputs changed'):
            seed_package.build_package(self.prepared,self.base,self.seed,self.output,'fixture-clang',
                                       provenance={'release_version':'fixture'},expected_source_sha256='f'*64)
        self.assertFalse(self.output.exists())

    def test_release_compiler_timeout_has_no_completion_marker(self):
        def timed_compiler(command,**kwargs):
            self.assertEqual(kwargs['timeout'],1)
            if command[-1]=='--version':return self.fake_compiler(command,**kwargs)
            raise subprocess.TimeoutExpired(command,1)
        with patch('seed_package.subprocess.run',side_effect=timed_compiler),self.assertRaises(subprocess.TimeoutExpired):
            seed_package.build_package(self.prepared,self.base,self.seed,self.output,'fixture-clang',
                                       provenance={'release_version':'fixture'},compiler_timeout=1)
        self.assertFalse(self.output.exists())

    def test_new_module_exact_identity_manifest_and_runtime_assets(self):
        (self.prepared / 'module.elf').write_bytes(b'stale diagnostic ELF must never be used')
        receipt = self.build()
        manifest = json.loads((self.output / 'manifest.json').read_text())
        self.assertEqual(manifest['save_profile'], {'version': 1, 'provider': 'twwhd-randomizer',
                                                   'identity': self.record['seed_identity']})
        self.assertEqual(manifest['minimum_manager_version'], '1.7.0')
        self.assertEqual(manifest['supported_game_builds'], ['USA'])
        self.assertEqual(manifest['game_id'], 'wwhd')
        self.assertTrue(manifest['experimental'])
        self.assertEqual(manifest['content_dir'], 'content')
        self.assertEqual((self.output / 'content/item').read_bytes(), b'randomized fixture')
        self.assertFalse((self.output / 'content/unchanged').exists())
        self.assertEqual((self.output / 'mod.elf').read_bytes(), self.fresh_elf)
        self.assertEqual(receipt['elf_sha256'], hashlib.sha256(self.fresh_elf).hexdigest())
        self.assertTrue(receipt['complete'])
        self.assertEqual(len(self.calls), 2)
        with self.assertRaises(FileExistsError):
            self.build()
        self.assertEqual((self.output / 'mod.elf').read_bytes(), self.fresh_elf)

    def test_header_only_and_forged_identity_rejected(self):
        for value in (None, 'c' * 64):
            self.record['seed_identity'] = value
            self.write_record()
            with self.assertRaises(ValueError):
                self.build()
            self.assertFalse(self.output.exists())
        self.assertFalse(self.calls)

    def test_modified_header_cannot_be_blessed_by_editing_hash_record(self):
        path = self.prepared / 'seed_init_data.h'
        path.write_text('/* unrelated/stale header */')
        self.record['headers'][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
        self.write_record()
        with self.assertRaisesRegex(ValueError, 'independently decoded'):
            self.build()
        self.assertFalse(self.calls)

    def test_changed_prepared_asset_or_extra_asset_rejected(self):
        item = self.prepared / 'assets/content/item'
        original = item.read_bytes()
        item.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'content changed'):
            self.build()
        item.write_bytes(original)
        (item.parent / 'extra').write_bytes(b'unrecorded')
        with self.assertRaisesRegex(ValueError, 'extra assets'):
            self.build()
        self.assertFalse(self.calls)

    def test_unchanged_base_and_seed_files_are_part_of_complete_identity(self):
        # This file is absent from the delta, so an overlay-only check would miss it.
        for game in (self.base, self.seed):
            (game / 'content/unchanged').write_bytes(b'different unchanged fixture')
        with self.assertRaisesRegex(ValueError, 'complete'):
            self.build()
        self.assertFalse(self.calls)

    def test_traversal_record_and_symlink_payload_rejected(self):
        self.record['content']['files'][0]['path'] = '../outside'
        self.write_record()
        (self.prepared / 'assets/seed-content.json').write_text(json.dumps(self.record['content']))
        with self.assertRaisesRegex(ValueError, 'package-relative'):
            self.build()
        self.assertFalse(self.calls)

    def test_symlink_and_duplicate_json_rejected(self):
        item = self.prepared / 'assets/content/item'
        item.unlink()
        item.symlink_to(self.seed / 'content/item')
        with self.assertRaises(ValueError):
            self.build()
        item.unlink();item.write_bytes(b'randomized fixture')
        (self.prepared / 'seed-preparation.json').write_text('{"format_version":1,"format_version":1}')
        with self.assertRaisesRegex(ValueError, 'duplicate JSON'):
            self.build()

    def test_changed_rpx_identity_and_stale_headers_rejected(self):
        self.identity['seed_rpx_sha256'] = 'd' * 64
        with self.assertRaisesRegex(ValueError, 'game inputs'):
            self.build()
        self.assertFalse(self.calls)

    def test_inputs_change_while_compiling_never_publish(self):
        def compiler(command, **kwargs):
            result = self.fake_compiler(command, **kwargs)
            if command[-1] != '--version':
                (self.seed / 'content/unchanged').write_bytes(b'changed during compilation')
            return result
        with self.assertRaises(ValueError):
            self.build(compiler)
        self.assertFalse(self.output.exists())

    def test_success_without_new_elf_is_not_a_cache_hit(self):
        def compiler(command, **kwargs):
            return subprocess.CompletedProcess(command, 0, stdout='fixture clang', stderr='')
        with self.assertRaises(FileNotFoundError):
            self.build(compiler)
        self.assertFalse(self.output.exists())

    def test_outputs_must_not_overlap_inputs(self):
        self.output = self.seed / 'nested-package'
        with self.assertRaisesRegex(ValueError, 'separate'):
            self.build()
        self.assertFalse(self.output.exists())


class CoreHostServices(unittest.TestCase):
    def inspect_services(self, services):
        translator = SimpleNamespace(services=services, entries={}, hooks=[], imports={},
                                     emit=lambda name: 'translated fixture')
        builder = SimpleNamespace(inspect_package=lambda *args: {},
                                  package_elf=lambda *args: ({'id': seed_package.PACKAGE_ID}, b'ELF'),
                                  translator_for=lambda *args: translator)
        with patch.dict('sys.modules', {'build_guest_mod': builder}):
            return seed_package._inspect(Path('fixture-package'))

    def test_rejects_unsupported_compiler_and_named_services(self):
        for service in ('fmaf', '__muldi3', 'unreviewed_host_service'):
            with self.subTest(service=service), self.assertRaisesRegex(ValueError, service):
                self.inspect_services(['wwhd_log', service])

    def test_records_reviewed_services_and_checks_runtime_table(self):
        expected = sorted(seed_package.CORE_HOST_SERVICES)
        self.assertEqual(self.inspect_services(expected)['services'], expected)
        runtime = (seed_package.REPO / 'runtime/src/mods/guest_mods.cpp').read_text()
        for name in expected:
            self.assertIn('"' + name + '"', runtime)


if __name__ == '__main__':
    unittest.main()
