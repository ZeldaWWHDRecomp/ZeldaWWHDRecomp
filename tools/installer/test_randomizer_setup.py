# SPDX-License-Identifier: MPL-2.0
"""Randomized game folders: setup routing (Choose your game sends them to the Randomizer page), metadata checks,
child hash pinning, and installs made the old way (the output folder installed as the game)."""
import hashlib
from io import StringIO
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest import mock

import setup


class RandomizerSetup(unittest.TestCase):
    def test_windows_path_budget_includes_seed_backups(self):
        limit = setup.MAX_DATA_DIR_CHARS - setup.RANDOMIZER_PATH_EXTRA
        root = 'C:\\' + 'x' * (limit-3)
        self.assertIsNone(setup.path_length_problem(root, windows=True, extra_path_chars=setup.RANDOMIZER_PATH_EXTRA))
        self.assertIsNotNone(setup.path_length_problem(root+'x', windows=True, extra_path_chars=setup.RANDOMIZER_PATH_EXTRA))
        self.assertIsNone(setup.path_length_problem(root+'x', windows=True))
        backup = 'save-backups-randomizer-'+'a'*64+'\\2026-10-12_03-15-00-mod-abcdefg-99\\save\\user\\cking_pic_order0.sav'
        self.assertLessEqual(len(root+'\\'+backup), setup.WINDOWS_MAX_PATH)

    def folder(self, root, title=None):
        root = Path(root)
        for name in ('code', 'content', 'meta'):
            (root/name).mkdir(parents=True, exist_ok=True)
        title = title or setup.RANDOMIZER_TITLE
        (root/'code/cking.rpx').write_bytes(b'Own synthetic setup fixture')
        for file in ('code/app.xml', 'meta/meta.xml'):
            (root/file).write_text('<app><title_id>%s</title_id><title_version>0</title_version></app>' % title)
        return root

    def build(self, folder):
        b = setup.game_builds.Build(dict(name='USA', title_id=setup.RANDOMIZER_TITLE,
                                         rpx_sha256=hashlib.sha256((folder/'code/cking.rpx').read_bytes()).hexdigest()))
        b.randomizer = True
        b.randomizer_report = {'profile':'synthetic', 'rpx_sha256':b.sha256}
        return b

    def test_select_complete_folder_or_its_rpx(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.folder(Path(tmp)/'Game [seed]')
            for path in (root, root/'code', root/'code/cking.rpx'):
                self.assertEqual(Path(setup.selected_game_folder(str(path))), root)
            self.assertIsNone(setup.selected_game_folder(str(root/'code/missing.rpx')))
            (root/'other').mkdir();(root/'other/cking.rpx').write_bytes(b'unrelated')
            self.assertIsNone(setup.selected_game_folder(str(root/'other/cking.rpx')))
            (root/'meta/meta.xml').unlink()
            self.assertIsNone(setup.selected_game_folder(str(root/'code/cking.rpx')))

    def test_title_recognition_does_not_skip_compatibility(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = self.folder(tmp)
            setup.check_title(setup.RANDOMIZER_TITLE)
            with mock.patch.object(setup.randomizer_compat, 'identify', side_effect=ValueError('changed hook instruction')) as check:
                with self.assertRaisesRegex(setup.SetupError, 'not compatible'):
                    setup.check_game_version(str(folder))
                check.assert_called_once()

    def test_compatible_seed_returns_its_checked_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = self.folder(tmp);build = self.build(folder)
            with mock.patch.object(setup.randomizer_compat, 'identify', return_value=build) as check:
                self.assertIs(setup.check_game_version(str(folder)), build)
                check.assert_called_once_with(str(folder/'code/cking.rpx'), expected_sha256=build.sha256)

    def test_mismatched_metadata_or_update_is_rejected(self):
        for title, version in (('0005000010143500', '0'), (setup.RANDOMIZER_TITLE, '10')):
            with self.subTest(title=title, version=version), tempfile.TemporaryDirectory() as tmp:
                folder = self.folder(tmp)
                (folder/'code/app.xml').write_text('<app><title_id>%s</title_id><title_version>%s</title_version></app>' % (title, version))
                with mock.patch.object(setup.randomizer_compat, 'identify') as check:
                    with self.assertRaisesRegex(setup.SetupError, 'do not match'):
                        setup.check_game_version(str(folder))
                    check.assert_not_called()

    def test_other_modified_game_remains_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = self.folder(tmp, '0005000010143500')
            with mock.patch.object(setup.randomizer_compat, 'identify') as check:
                with self.assertRaises(setup.SetupError):
                    setup.check_game_version(str(folder))
                check.assert_not_called()

    def test_translation_pins_the_seed_and_avoids_decomp_replacements(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = self.folder(Path(tmp)/'seed');build = self.build(folder);output = Path(tmp)/'gen';commands=[]
            def run(command, **kwargs):
                commands.append(command);output.mkdir();(output/'code_000.c').write_text('/* synthetic */')
            with mock.patch.object(setup.randomizer_compat, 'identify', return_value=build), \
                 mock.patch.object(setup, 'decomp_hooks_file', return_value='decomp.txt') as decomp, \
                 mock.patch.object(setup, 'run_logged', side_effect=run):
                self.assertEqual(setup.recompile(str(folder), str(output), hooks=False), 1)
                decomp.assert_not_called()
            command = commands[0]
            self.assertIn('--randomizer', command)
            self.assertEqual(command[command.index('--expected-rpx-sha256')+1], build.sha256)
            self.assertNotIn('--decomp-hooks', command)
            self.assertNotIn('--mod-hooks', command)

    def test_archive_selection_includes_randomizer_title(self):
        listed = {'titles':[{'id':setup.RANDOMIZER_TITLE}]}
        with mock.patch.object(setup, '_archive_info_one', side_effect=[(None,'',listed),(None,'',{})]) as read:
            setup.archive_info('synthetic.wua')
        self.assertEqual(read.call_args_list[1].args, ('synthetic.wua', setup.RANDOMIZER_TITLE))

    def probe(self, ctx, path, **patches):
        requests = StringIO(json.dumps({'id':1, 'cmd':'probe', 'path':str(path)})+'\n')
        output = StringIO()
        with mock.patch.object(setup, 'Ctx', return_value=ctx), \
             mock.patch.object(setup, 'GUI', None), \
             mock.patch.object(setup, 'LOG', SimpleNamespace(f=None, write=lambda line: None)), \
             mock.patch.object(setup, 'import_sources', return_value=(None, [])), \
             mock.patch.object(setup.sys, 'stdin', requests), mock.patch.object(setup.sys, 'stdout', output):
            for name, value in patches.items():
                mock.patch.object(setup, name, value).start()
            try:
                self.assertEqual(setup.gui_main(ctx.args), 0)
            finally:
                mock.patch.stopall()
        return [json.loads(line) for line in output.getvalue().splitlines() if json.loads(line).get('cmd') == 'probe'][0]

    def test_choose_your_game_sends_randomized_games_to_the_randomizer_page(self):
        """the setup window's game choice no longer installs a randomized game as the game: the Randomizer page's
        Import seed... adds it beside the installed game (test_randomizer_seeds.py Import)"""
        with tempfile.TemporaryDirectory() as tmp:
            folder = self.folder(Path(tmp)/'seed')
            ctx = SimpleNamespace(args=SimpleNamespace(code_mods='0', decomp=False, model_sweep=False),
                                  manifest={'platform':'test', 'toolchain':{}, 'version':'test'}, version='test',
                                  data_dir=tmp, app_dir=tmp, game_dir=str(folder), state=lambda: {}, installed=lambda: True)
            for path in (folder, folder/'code/cking.rpx'):
                with mock.patch.object(setup.randomizer_compat, 'identify') as check:
                    reply = self.probe(ctx, path)
                    check.assert_not_called()
                self.assertFalse(reply['ok'])
                self.assertEqual(reply['problem'], 'randomized')
                self.assertIn('Import seed', reply['message'])
            listed = {'title_id':setup.RANDOMIZER_TITLE, 'version':'0', 'selected':'00050000/10143599', 'titles':[]}
            (Path(tmp)/'seed.wua').write_bytes(b'')
            reply = self.probe(ctx, Path(tmp)/'seed.wua', archive_info=mock.Mock(return_value=(None, '', listed)))
            self.assertEqual(reply['problem'], 'randomized')

    def test_game_dir_and_other_new_sources_refuse_a_randomized_game(self):
        """setup --game-dir (and --image / --archive) with a randomized game: refused with the pointer to
        --import-seed before anything is asked, downloaded or built; the game as it is still installs"""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed = self.folder(root/'seed')
            data = root/'data'
            for path in (seed, seed/'code/cking.rpx'):
                args = mock.Mock(game_dir=str(path), gen_dir=None, archive=None, image=None, decomp=None,
                                 start_decomp=None, remove_language_source=None, language_source=None,
                                 randomize=False, list_seeds=False, play_seed=None, delete_seed=None, import_seed=None,
                                 repair_guest_python=False, rebuild_code_mods=False)
                ctx = SimpleNamespace(args=args, data_dir=str(data), game_dir=str(data/'game'), version='test',
                                      manifest={'platform':'test'}, state=lambda: {}, installed=lambda: False)
                lines = []
                with mock.patch.object(setup, 'Ctx', return_value=ctx), mock.patch.object(setup, 'say', lines.append), \
                     mock.patch.object(setup, 'install') as install:
                    with self.assertRaises(setup.SetupError) as refused:
                        setup.run(args, setup.UI(False))
                    install.assert_not_called()
                self.assertIn('setup --import-seed PATH', str(refused.exception))
            self.assertFalse(data.exists())
            # install() itself (every caller): a folder, an archive or a disc image holding a randomized game
            ctx = SimpleNamespace(args=SimpleNamespace(code_mods='0'), manifest={'platform':'test', 'toolchain':{}},
                                  data_dir=str(data), game_dir=str(seed), state=lambda: {})
            patches = {'setup_code_mods': False, 'model_sweep_wanted': False, 'decomp_wanted': False,
                       'archive_info': (None, '', {'title_id': setup.RANDOMIZER_TITLE, 'version': '0', 'titles': []})}
            (root/'seed.wua').write_bytes(b'')
            (root/'seed.wux').write_bytes(b'')
            with mock.patch.multiple(setup, **{k: mock.Mock(return_value=v) for k, v in patches.items()}), \
                 mock.patch.object(setup, 'get_toolchain') as toolchain:
                for source in (('folder', str(seed)), ('archive', str(root/'seed.wua')), ('image', str(root/'seed.wux'))):
                    with self.assertRaisesRegex(setup.SetupError, 'import-seed'):
                        setup.install(ctx, source, check_keys=lambda image: ({}, {'title_id': setup.RANDOMIZER_TITLE}))
                toolchain.assert_not_called()
            self.assertFalse(setup.is_randomized_folder(str(self.folder(root/'usa', title='0005000010143500'))))

    def test_seed_installed_the_old_way_still_updates_with_its_save_folder(self):
        """an install made before the Randomizer page (the output folder installed as the game): Update and Repair
        rebuild it from the installed files as before, and setup keeps using that seed's own save folder"""
        from contextlib import ExitStack
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);game=self.folder(root/'data/game');data=root/'data'
            selected=self.build(game);identity=selected.sha256
            compiled={'profile':'synthetic','rpx_sha256':identity}
            (data/'install.json').write_text(json.dumps({'version':'old','randomizer':compiled}))
            user=data/('save-randomizer-'+identity)/'user';user.mkdir(parents=True)
            (user/'cking.sav').write_bytes(b'progress')
            (data/'save/user').mkdir(parents=True);(data/'save/user/cking.sav').write_bytes(b'vanilla')
            self.assertEqual(Path(setup.save_user_dir(str(data))), user)
            args=SimpleNamespace(code_mods='0',jobs=1,shortcuts=False,keep_work=False)
            tc=SimpleNamespace(desc='fixture',env={},cc=['fixture'])
            ctx=SimpleNamespace(args=args,manifest={'platform':'test','toolchain':{},'exe':'fixture'},
                                data_dir=str(data),game_dir=str(game),exe=str(data/'bin/fixture'),
                                exe_dir=str(data/'bin'),version='test',state=lambda:json.loads((data/'install.json').read_text()))
            translated=[]
            def translate(game_dir,gen,hooks=False):
                translated.append(game_dir)
                out=Path(gen);out.mkdir(parents=True)
                (out/'randomizer.json').write_text(json.dumps(compiled))
                (out/'table.c').write_text('const char g_guest_build_title_id[] = "'+setup.RANDOMIZER_TITLE+'";\nconst char g_guest_build_rpx_sha256[] = "'+identity+'";\n')
                return 1
            def link(tc,manifest,objs,work,target):Path(target).write_bytes(b'synthetic executable')
            with ExitStack() as stack:
                stack.enter_context(mock.patch.multiple(setup,PKG=str(root),PORTABLE=True))
                stack.enter_context(mock.patch.object(setup.randomizer_compat,'identify',return_value=selected))
                for name,value in {'get_toolchain':tc,'free_space':20<<30,
                                   'compile_gamecode':[],'prepare_shaders_nonfatal':{'status':'skipped'},
                                   'model_sweep_wanted':False,'decomp_wanted':False,'setup_code_mods':False,
                                   'write_guest_build_config':None,'install_story_notebook':None}.items():
                    stack.enter_context(mock.patch.object(setup,name,return_value=value))
                stack.enter_context(mock.patch.object(setup,'recompile',side_effect=translate))
                stack.enter_context(mock.patch.object(setup,'link_game',side_effect=link))
                stack.enter_context(mock.patch.object(setup.code_mods,'remember_installed'))
                state=setup.install(ctx,('installed',str(game)))
            self.assertEqual(translated,[str(game)])
            self.assertEqual(state['randomizer'],compiled)
            self.assertEqual(Path(setup.save_user_dir(str(data))), user)
            self.assertEqual((user/'cking.sav').read_bytes(), b'progress')
            self.assertEqual((data/'save/user/cking.sav').read_bytes(), b'vanilla')
            self.assertTrue(setup.have_save(str(data)))

    def test_compiled_identity_report_matches_actual_build_map(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.assertIsNone(setup.translated_randomizer_identity(tmp))
            for identity in ('a'*64, 'b'*64):
                report = {'profile':'synthetic', 'rpx_sha256':identity}
                (root/'randomizer.json').write_text(json.dumps(report))
                (root/'table.c').write_text('const char g_guest_build_title_id[] = "'+setup.RANDOMIZER_TITLE+'";\nconst char g_guest_build_rpx_sha256[] = "'+identity+'";\n')
                self.assertEqual(setup.translated_randomizer_identity(tmp), report)
            (root/'randomizer.json').write_text(json.dumps({'rpx_sha256':'a'*64}))
            with self.assertRaisesRegex(setup.SetupError, 'differ'):
                setup.translated_randomizer_identity(tmp)
            (root/'randomizer.json').write_text('{}')
            with self.assertRaises(setup.SetupError):
                setup.translated_randomizer_identity(tmp)
            (root/'randomizer.json').unlink()
            with self.assertRaisesRegex(setup.SetupError, 'no identity report'):
                setup.translated_randomizer_identity(tmp)

    def test_install_records_translated_seed_instead_of_preliminary_seed(self):
        from contextlib import ExitStack
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);game=self.folder(root/'game');data=root/'data';data.mkdir()
            selected=self.build(game);selected.randomizer_report['rpx_sha256']='a'*64
            compiled={'profile':'synthetic','rpx_sha256':'b'*64}
            args=SimpleNamespace(code_mods='0',jobs=1,shortcuts=False,keep_work=False)
            tc=SimpleNamespace(desc='fixture',env={},cc=['fixture'])
            ctx=SimpleNamespace(args=args,manifest={'platform':'test','toolchain':{},'exe':'fixture'},
                                data_dir=str(data),game_dir=str(game),exe=str(data/'bin/fixture'),
                                exe_dir=str(data/'bin'),version='test',state=lambda:{})
            # (an install made the old way, rebuilt from its installed files: Update / Repair)
            def translate(game,gen,hooks=False):
                out=Path(gen);out.mkdir(parents=True)
                (out/'randomizer.json').write_text(json.dumps(compiled))
                (out/'table.c').write_text('const char g_guest_build_title_id[] = "'+setup.RANDOMIZER_TITLE+'";\nconst char g_guest_build_rpx_sha256[] = "'+'b'*64+'";\n')
                return 1
            def link(tc,manifest,objs,work,target):Path(target).write_bytes(b'synthetic executable')
            with ExitStack() as stack:
                stack.enter_context(mock.patch.multiple(setup,PKG=str(root),PORTABLE=True))
                for name,value in {'check_game_version':selected,'get_toolchain':tc,'free_space':20<<30,
                                   'compile_gamecode':[],'prepare_shaders_nonfatal':{'status':'skipped'},
                                   'model_sweep_wanted':False,'decomp_wanted':False,'setup_code_mods':False,
                                   'write_guest_build_config':None,'install_story_notebook':None}.items():
                    stack.enter_context(mock.patch.object(setup,name,return_value=value))
                stack.enter_context(mock.patch.object(setup,'recompile',side_effect=translate))
                stack.enter_context(mock.patch.object(setup,'link_game',side_effect=link))
                stack.enter_context(mock.patch.object(setup.code_mods,'remember_installed'))
                state=setup.install(ctx,('installed',str(game)))
            self.assertEqual(state['randomizer'],compiled)
            self.assertEqual(json.loads((data/'install.json').read_text())['randomizer'],compiled)

    def test_legacy_nested_save_is_not_abandoned(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); identity = 'a'*64
            (root/'install.json').write_text(json.dumps({'randomizer': {'rpx_sha256':identity}}))
            legacy = root/'save'/('randomizer-'+identity)/'user'
            legacy.mkdir(parents=True);(legacy/'cking.sav').write_bytes(b'keep')
            with self.assertRaisesRegex(setup.SetupError, 'older randomized save folder'):
                setup.save_user_dir(tmp)
            self.assertEqual((legacy/'cking.sav').read_bytes(), b'keep')
            self.assertFalse((root/('save-randomizer-'+identity)).exists())

    def test_save_import_targets_current_seed_without_touching_vanilla_or_other_seed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            vanilla = root/'save/user/cking.sav'
            vanilla.parent.mkdir(parents=True)
            vanilla.write_bytes(b'vanilla')
            self.assertTrue(setup.have_save(tmp))
            for identity in ('a'*64, 'b'*64):
                (root/'install.json').write_text(json.dumps({'randomizer': {'rpx_sha256': identity}}))
                expected = root/('save-randomizer-'+identity)/'user'
                self.assertEqual(Path(setup.save_user_dir(tmp)), expected)
                self.assertFalse(setup.have_save(tmp))
                expected.mkdir(parents=True)
                (expected/'cking.sav').write_bytes(identity.encode())
                self.assertTrue(setup.have_save(tmp))
            self.assertEqual(vanilla.read_bytes(), b'vanilla')
            self.assertEqual((root/('save-randomizer-'+'a'*64)/'user/cking.sav').read_bytes(), b'a'*64)


if __name__ == '__main__':
    unittest.main()
