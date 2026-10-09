"""Regression for the CI emulator preview intercepting swapped primary touches."""
import unittest

from display_smoke import primary_preview_clear


def dispatcher(region, transform='IDENTITY', rows='', flags='NOT_FOCUSABLE', display=0):
    return f'''Input Dispatcher State:
  Display: {display}
    logicalSize=640x320
        transform (ROT_0) ({transform})
{rows}    Windows:
      0: name=authored Overlay #1: 800x480, 160 dpi, id=1, displayId={display}, inputConfig={flags}, frame=[0,0][0,0], touchableRegion={region}, ownerPid=1
'''


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
