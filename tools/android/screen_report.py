#!/usr/bin/env python3
"""The game's "Android screen:" reports (WwhdActivity.reportScreen, issue #349): was the game surface made at its
full-screen size?

A surface made while the window was still laid out with the system bars, and only grown afterwards, left the bottom
rows black on a Retroid Pocket Nova. Since the full-screen window style is applied before the first layout
(SDLActivity.startsFullscreen), the surface is made at the size it keeps. display_smoke.py checks that on the
emulator; `python3 tools/android/screen_report.py` runs the self-test, `... screen_report.py LOG` checks a logcat
dump or a wwhd.log.
"""
import re
import sys

REPORT = re.compile(r"(?:screen: |Android screen: )view (\d+)x(\d+) at -?\d+,-?\d+.*?buffer (\d+)x(\d+) "
                    r"\(made at (\d+)x(\d+)\).*?full screen (on|off)")


def reports(text):
    """Every report with a known surface: dicts with view, buffer, made (width, height) and full_screen."""
    found = []
    for m in REPORT.finditer(text):
        view, buffer, made = (int(m[1]), int(m[2])), (int(m[3]), int(m[4])), (int(m[5]), int(m[6]))
        if buffer[0] <= 0 or made[0] <= 0: continue  # the surface not made yet
        found.append({"view": view, "buffer": buffer, "made": made, "full_screen": m[7] == "on"})
    return found


def first_surface_problem(text):
    """None when the game's first surface was made full screen at the size the view keeps; else what is wrong."""
    found = reports(text)
    if not found: return "no screen report with a surface"
    first, last = found[0], found[-1]
    if not first["full_screen"]: return "the first surface was made with full screen off"
    if first["made"] != last["view"]:
        return "the first surface was made at %dx%d, the view is %dx%d" % (first["made"] + last["view"])
    return None


def self_test():
    full = ("I wwhd: screen: view 1280x960 at 0,0, root 1280x960, window [0,0][1280,960] (cutout mode 3); "
            "surface on screen [0,0][1280,960], buffer 1280x960 (made at 1280x960), visible frame [0,0][1280,960], "
            "stable 0,48,0,48, full screen on (ui flags 0x1706, window flags 0x81000500), navigation mode 2; "
            "left out 0,0,0,0\n")
    # #349 before the fix: laid out with the bars first, then grown
    grown = ("I wwhd: screen: view 1280x912 at 0,0, root 1280x960; surface on screen [0,0][1280,912], buffer 0x0 "
             "(made at 0x0), full screen off (ui flags 0x0, window flags 0x81000100), navigation mode 2; left out 0,0,0,0\n"
             "I wwhd: screen: view 1280x912 at 0,0, root 1280x960; surface on screen [0,0][1280,912], buffer 1280x912 "
             "(made at 1280x912), full screen off (ui flags 0x0, window flags 0x81000100), navigation mode 2; left out 0,0,0,0\n"
             + full.replace("(made at 1280x960)", "(made at 1280x912)"))
    assert first_surface_problem(full) is None
    assert first_surface_problem(full + full) is None
    assert first_surface_problem(grown) == "the first surface was made with full screen off", first_surface_problem(grown)
    assert first_surface_problem(full.replace("(made at 1280x960)", "(made at 1280x912)")) == \
        "the first surface was made at 1280x912, the view is 1280x960"
    assert first_surface_problem("") == "no screen report with a surface"
    # the game's own log (wwhd.log) has the same reports
    assert first_surface_problem(full.replace("I wwhd: screen: ", "[display] Android screen: ")) is None
    assert len(reports(grown)) == 2  # the report before the surface existed is left out
    print("screen_report self-test PASS")


if __name__ == "__main__":
    if len(sys.argv) == 1:
        self_test()
    else:
        with open(sys.argv[1], encoding="utf-8", errors="replace") as f:
            problem = first_surface_problem(f.read())
        print(problem or "the first surface was made full screen at the view's size")
        sys.exit(1 if problem else 0)
