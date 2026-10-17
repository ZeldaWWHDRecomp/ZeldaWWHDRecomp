"""Regression for the CI emulator preview intercepting swapped primary touches."""
import unittest
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import tempfile

from display_smoke import primary_preview_clear, capture_timeout_evidence, capture_metric_trend, app_windows_on_display, startup_acquisitions


def dispatcher(region, transform='IDENTITY', rows='', flags='NOT_FOCUSABLE', display=0):
    return f'''Input Dispatcher State:
  Display: {display}
    logicalSize=640x320
        transform (ROT_0) ({transform})
{rows}    Windows:
      0: name=authored Overlay #1: 800x480, 160 dpi, id=1, displayId={display}, inputConfig={flags}, frame=[0,0][0,0], touchableRegion={region}, ownerPid=1
'''


class ReleasedDisplayTests(unittest.TestCase):
    def test_primary_mirrors_do_not_count_as_owned_secondary_windows(self):
        dump = """Input Dispatcher State:
  Display: 0
    Windows:
      0: name=game/MainActivity, displayId=0, inputConfig=0x0,
  Display: 2
    Windows:
      0: name=game/MainActivity, displayId=2, inputConfig=CLONE,
"""
        self.assertEqual(len(app_windows_on_display(dump, 0, 'game')), 1)
        self.assertEqual(app_windows_on_display(dump, 2, 'game'), [])
        self.assertEqual(app_windows_on_display(dump, 3, 'game'), [])

    def test_real_presentation_counts_even_when_nonfocusable_or_nontouchable(self):
        for flag in ('NOT_FOCUSABLE', 'NOT_TOUCHABLE', '0x0'):
            dump = f"      0: name='game', displayId=2, inputConfig={flag},\n"
            self.assertEqual(len(app_windows_on_display(dump, 2, 'game')), 1)
            self.assertEqual(app_windows_on_display(dump, 20, 'game'), [])

    def test_missing_inventory_cannot_prove_release(self):
        with self.assertRaisesRegex(AssertionError, 'inventory'):
            app_windows_on_display('unrecognized dump', 2, 'game')


class SavedOffLaunchTests(unittest.TestCase):
    released = "10-17 12:00:01.000  1  2 I wwhd-display: secondary released (GamePad screen off)\n"
    primary_only = """Input Dispatcher State:
  Display: 0
    Windows:
      0: name=game/MainActivity, displayId=0, inputConfig=0x0,
  Display: 2
    Windows:
      0: name=game/MainActivity, displayId=2, inputConfig=CLONE,
"""

    def test_released_launch_has_no_evidence(self):
        samples = [{"dual": False, "secondary_presented": 0, "touch": False}]
        self.assertEqual(startup_acquisitions(self.released, ["", self.primary_only], samples, 2, 'game'), [])

    def test_momentary_presentation_counts(self):
        log = "I wwhd-display: secondary display=2\n" + self.released
        self.assertEqual(len(startup_acquisitions(log, [], [], 2, 'game')), 1)
        self.assertEqual(len(startup_acquisitions("I wwhd-display: fold panes=[0, 0]\n", [], [], 2, 'game')), 1)
        window = self.primary_only + "      1: name='game', displayId=2, inputConfig=NOT_FOCUSABLE,\n"
        self.assertEqual(len(startup_acquisitions(self.released, [self.primary_only, window], [], 2, 'game')), 1)

    def test_secondary_picture_or_surface_counts(self):
        for sample in ({"dual": True}, {"secondary_presented": 3}, {"touch": True}):
            self.assertEqual(len(startup_acquisitions('', [], [sample], 2, 'game')), 1)


class TimeoutEvidenceTests(unittest.TestCase):
    def test_metric_trend_records_progress_and_survives_write_failure(self):
        trend = [{"elapsed": 0, "primary_presented": 10},
                 {"elapsed": 120, "primary_presented": 10}]
        with tempfile.TemporaryDirectory() as temp, redirect_stdout(io.StringIO()) as log:
            output = Path(temp) / 'nested/result.json'
            capture_metric_trend('blocked secondary', trend, output)
            self.assertEqual(json.loads(output.with_suffix('.trend.json').read_text()),
                             {'phase': 'blocked secondary', 'samples': trend})
            occupied = Path(temp) / 'occupied'
            occupied.write_text('regular file')
            capture_metric_trend('blocked secondary', trend, occupied / 'result.json')
            self.assertIn('Cannot capture metric trend', log.getvalue())
            self.assertIn('primary_presented', log.getvalue())

    def test_records_all_services_before_cleanup(self):
        calls = []
        def adb(*args):
            calls.append(args)
            return "authored " + args[-1]
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "nested/result.json"
            capture_timeout_evidence(adb, output)
            self.assertEqual(calls, [("shell", "dumpsys", service) for service in ("input", "window", "display")])
            for service in ("input", "window", "display"):
                self.assertEqual(output.with_suffix("." + service + ".txt").read_text(), "authored " + service + "\n")

    def test_failed_capture_does_not_hide_assertion_or_skip_other_services(self):
        def adb(*args):
            if args[-1] == "input":
                raise subprocess.CalledProcessError(1, "authored adb")
            return "authored " + args[-1]
        with tempfile.TemporaryDirectory() as temp, redirect_stdout(io.StringIO()) as warning:
            output = Path(temp) / "result.json"
            capture_timeout_evidence(adb, output)
            self.assertIn("Cannot capture input", warning.getvalue())
            self.assertTrue(output.with_suffix(".window.txt").is_file())
            self.assertTrue(output.with_suffix(".display.txt").is_file())

    def test_unwritable_output_does_not_replace_failed_assertion(self):
        with tempfile.TemporaryDirectory() as temp, redirect_stdout(io.StringIO()) as warning:
            parent = Path(temp) / "occupied"
            parent.write_text("authored regular file")
            capture_timeout_evidence(lambda *args: "authored dump", parent / "result.json")
            self.assertEqual(warning.getvalue().count("Cannot capture"), 3)


class PrimaryPreviewTests(unittest.TestCase):
    def test_ci_sized_preview_blocks_centre_even_with_app_keyboard_focus(self):
        dump = "  FocusedWindows:\n    displayId=0, name='DisplaySmokeActivity'\n"
        dump += dispatcher('[0,0][400,240]')
        self.assertFalse(primary_preview_clear(dump, 320, 160))

    def test_rotated_input_regions_are_transformed_before_hit_testing(self):
        rows = '            0.0000  1.0000  0.0000\n            -1.0000  0.0000  320.0000\n            0.0000  0.0000  1.0000\n'
        self.assertFalse(primary_preview_clear(dispatcher('[80,0][320,400]', 'ROTATE TRANSLATE', rows), 320, 160))
        self.assertTrue(primary_preview_clear(dispatcher('[230,0][320,160]', 'ROTATE TRANSLATE', rows), 320, 160))

    def test_waits_for_preview_geometry_and_checks_every_region(self):
        self.assertFalse(primary_preview_clear('', 320, 160))
        self.assertFalse(primary_preview_clear(dispatcher('[0,0][400,240]', display=2), 320, 160))
        self.assertTrue(primary_preview_clear(dispatcher('[0,0][160,90]'), 320, 160))
        self.assertFalse(primary_preview_clear(dispatcher('[0,0][160,90]|[300,150][400,240]'), 320, 160))

    def test_non_touchable_or_hidden_preview_does_not_obstruct(self):
        for flags in ['NOT_TOUCHABLE', 'NOT_VISIBLE', 'NO_INPUT_CHANNEL']:
            self.assertTrue(primary_preview_clear(dispatcher('[0,0][400,240]', flags=flags), 320, 160))
        self.assertTrue(primary_preview_clear(dispatcher('<empty>'), 320, 160))

    def test_unknown_dump_format_does_not_silently_pass(self):
        with self.assertRaisesRegex(AssertionError, 'touch region'):
            primary_preview_clear(dispatcher('unrecognized'), 320, 160)
        with self.assertRaisesRegex(AssertionError, 'transform'):
            primary_preview_clear(dispatcher('[0,0][160,90]', 'ROTATE', 'no matrix\n'), 320, 160)


if __name__ == '__main__':
    unittest.main()
