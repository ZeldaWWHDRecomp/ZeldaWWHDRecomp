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


if __name__=='__main__':
    unittest.main()
