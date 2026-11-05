"""Source-order guard for startup safety; filesystem behavior has native tests."""
from pathlib import Path
import re
import unittest


class SaveNamespaceStartup(unittest.TestCase):
    def test_dual_seed_identity_stops_before_save_io_and_activation(self):
        source=(Path(__file__).resolve().parents[1]/'src/main.cpp').read_text()
        selected=source.index('save_profile=mods::packages::startup_save_profile();')
        rejected=re.search(
            r'if\s*\(randomizer::compiled_seed\(\)\s*&&\s*!save_profile.empty\(\)\)\s*'
            r'throw std::runtime_error\("[^"]*original game executable[^"]*"\);',
            source[selected:])
        self.assertIsNotNone(rejected)
        stop=selected+rejected.end()
        for operation in ('save_folder::legacy_randomizer_preflight(',
                          'config::save_dir = randomizer::save_directory(',
                          'save_paths=mods::save_profile::paths(',
                          'ss::set_save_profile(', 'save_folder::startup(',
                          'mods::saves::startup(', 'mods::packages::initialize();',
                          'guestmods::init();'):
            with self.subTest(operation=operation):
                self.assertLess(stop,source.index(operation,selected))
        # Launcher-supplied --save must not bypass the selected profile path.
        self.assertIn("config::save_dir=save_paths.save.string();", source[selected:])
        self.assertNotRegex(source[selected:], r"if\s*\(\s*!save_arg\s*\)\s*config::save_dir\s*=\s*save_paths.save")
        error_handler=source.index('catch(const std::exception& e)',stop)
        self.assertLess(error_handler,source.index('save_folder::startup(',stop))
        self.assertIn('fatal("Cannot select save profile:',source[error_handler:])

    def test_randomizer_save_folder_ignores_launcher_default_save(self):
        # #408: setup/launchers pass "--save save"; that is no player choice, so a
        # randomized game must still get its per-seed folder. The profile paths
        # are derived from the result afterwards (ordinary_base strips suffixes).
        source=(Path(__file__).resolve().parents[1]/'src/main.cpp').read_text()
        selected=source.index('save_profile=mods::packages::startup_save_profile();')
        line=re.search(
            r'config::save_dir = randomizer::save_directory\(\s*config::save_dir,\s*'
            r'randomizer::explicit_save_folder\(\s*save_arg,\s*config::save_dir\s*\)\s*\);',
            source[selected:])
        self.assertIsNotNone(line)
        self.assertNotRegex(source, r'randomizer::save_directory\(\s*config::save_dir,\s*save_arg\s*\)')
        at=selected+line.start()
        self.assertLess(source.index('save_folder::legacy_randomizer_preflight(',selected),at)
        self.assertLess(at,source.index('save_paths=mods::save_profile::paths(save_profile,config::save_dir,',selected))
        self.assertEqual(source.count('randomizer::save_directory('),1)

    def test_activation_errors_are_caught_with_platform_recovery(self):
        source=(Path(__file__).resolve().parents[1]/'src/main.cpp').read_text()
        for call,message in [('mods::packages::initialize();','Cannot start mods:'),
                             ('guestmods::init();','Cannot load code mods:')]:
            at=source.index(call)
            window=source[at-12:at+300]
            self.assertIn('try {',window)
            self.assertIn('catch(const std::exception& e)',window)
            self.assertIn(message,window)
            self.assertIn('mod_recovery',window)
        self.assertIn('WWHD_CODE_MODS=0',source)
        self.assertIn('return to setup, turn code mods off and rebuild',source)


if __name__=='__main__':
    unittest.main()
