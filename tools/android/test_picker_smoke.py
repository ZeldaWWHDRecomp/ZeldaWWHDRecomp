"""Setup feedback must remain reachable on a small, slow emulator screen."""
import unittest

from picker_smoke import wait_for_feedback


class FeedbackScreen:
    def __init__(self, feedback="Private dump retained", seconds_per_dump=4):
        # The CI failure ended above Details with Code mods at the bottom.
        self.pages = ["Setup stopped", "Disc key chosen (change)", "Code mods", "Details"]
        self.feedback = feedback
        self.seconds_per_dump = seconds_per_dump
        self.elapsed = 0
        self.page = 0
        self.dumps = 0
        self.rewinds = 0
        self.expansions = 0

    def read(self):
        self.dumps += 1
        self.elapsed += self.seconds_per_dump
        return [{"text": self.pages[self.page], "bounds": "[0,0][320,640]"}]

    def expand(self):
        self.expansions += 1
        self.pages[-1] = "Hide details"
        self.pages.append(self.feedback)

    def scroll(self, nodes):
        self.page = min(self.page + 1, len(self.pages) - 1)
        return True  # Android can report a scrollable view even at its end.

    def rewind(self):
        self.rewinds += 1
        self.page = 0

    def wait(self, **kwargs):
        wait_for_feedback("Private dump retained", self.read, self.expand,
                          self.scroll, self.rewind, clock=lambda: self.elapsed, **kwargs)


class PickerFeedbackTests(unittest.TestCase):
    def test_slow_small_screen_reaches_collapsed_record(self):
        screen = FeedbackScreen()
        screen.wait()
        self.assertGreater(screen.elapsed, 15)
        self.assertEqual(screen.dumps, 6)
        self.assertEqual(screen.expansions, 1)
        self.assertEqual(screen.page, 0)  # following picker actions start at top

    def test_missing_feedback_still_fails_and_stops_at_bottom(self):
        screen = FeedbackScreen(feedback="Synthetic invalid disc key file", seconds_per_dump=0)
        with self.assertRaisesRegex(AssertionError, "Missing feedback: Private dump retained"):
            screen.wait()
        self.assertLessEqual(screen.dumps, 14)
        self.assertEqual(screen.expansions, 1)

    def test_changing_screen_cannot_loop_forever(self):
        screen = FeedbackScreen(seconds_per_dump=0)
        def changing():
            screen.dumps += 1
            return [{"text": str(screen.dumps)}]
        screen.read = changing
        with self.assertRaises(AssertionError):
            screen.wait()
        self.assertEqual(screen.dumps, 24)

    def test_wall_clock_budget_bounds_slow_reads(self):
        screen = FeedbackScreen(seconds_per_dump=31)
        with self.assertRaises(AssertionError):
            screen.wait()
        self.assertEqual(screen.dumps, 2)

    def test_already_visible_feedback_needs_no_scrolling(self):
        screen = FeedbackScreen()
        screen.pages = [screen.feedback]
        screen.wait()
        self.assertEqual(screen.dumps, 1)
        self.assertEqual(screen.expansions, 0)


if __name__ == "__main__":
    unittest.main()
