#!/usr/bin/env python3
"""Touch controls smoke test on a phone (adb input motionevent): the Flexible style's floating left
stick, the swipe camera and a clean stop when a finger is lost.

Start the game first and load a save so Link stands in the open (Outset is fine), with Settings >
Display > Touch controls > Touch style = Flexible and Right half = Swipe camera. The script only
touches the screen and reads the log; it changes no setting and installs nothing.

Checks (logcat, the game's own lines):
  - "[input] touch controls on": the virtual pad attached when the left thumb moved;
  - "[input] swipe camera": the right-half swipe reached the camera.
Pictures (output folder; mean change per pixel when Pillow is installed, else only saved):
  walk_a/b    while the left stick is held (Link walks: they differ),
  swipe_a/b   before and after the swipe (the camera turned: they differ a lot),
  lost_a/b    after a cancelled finger (Link stands: nearly equal, as idle_a/b).
"""
import argparse
import re
import shlex
import struct
import subprocess
import sys
import time
from pathlib import Path

PACKAGE = 'org.wwhdrecomp.wwhd'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--serial', required=True)
    parser.add_argument('--package', default=PACKAGE)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prefix = [args.adb, '-s', args.serial]
    args.output.mkdir(parents=True, exist_ok=True)

    def adb(*command, check=True):
        if command[0] == 'shell':
            command = ('shell', shlex.join(command[1:]))
        return subprocess.run(prefix + list(command), check=check, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=60).stdout

    if not adb('shell', 'pidof', args.package, check=False).strip():
        sys.exit(f'{args.package} is not running: start the game and load a save first')
    settings = adb('shell', 'cat', f'/sdcard/Android/data/{args.package}/files/config/wwhd/settings.ini', check=False).decode(errors='replace')
    style = re.search(r'^touchStyle\s*=\s*(\d+)', settings, re.M)
    right = re.search(r'^touchRight\s*=\s*(\d+)', settings, re.M)
    if not style or style.group(1) != '1' or (right and right.group(1) != '0'):
        print('warning: settings.ini does not say Flexible + Swipe camera (touchStyle=1, touchRight=0); '
              'the checks below expect them', file=sys.stderr)

    def shot(name):
        png = adb('exec-out', 'screencap', '-p')
        (args.output / f'{name}.png').write_bytes(png)
        return png

    first = shot('start')
    if first[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit('screencap gave no PNG')
    w, h = struct.unpack('>II', first[16:24])  # IHDR: the screen as it is shown (landscape)
    print(f'screen {w}x{h}')

    def touch(kind, x, y):
        adb('shell', 'input', 'touchscreen', 'motionevent', kind, str(int(x)), str(int(y)))

    def drag(x0, y0, x1, y1, steps, hold=0.0, end='UP'):
        touch('DOWN', x0, y0)
        for i in range(1, steps + 1):
            touch('MOVE', x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps)
        if hold:
            time.sleep(hold)
        return end

    adb('logcat', '-c')
    results = {}
    # 1. floating left stick: land on the left half, push right, hold, let go
    drag(w * 0.25, h * 0.62, w * 0.25 + h * 0.12, h * 0.62, 6)
    shot('walk_a'); time.sleep(0.4); shot('walk_b')
    touch('UP', w * 0.25 + h * 0.12, h * 0.62)
    time.sleep(1.0)
    shot('idle_a'); time.sleep(0.6); shot('idle_b')
    # 2. swipe camera: a slow slide on the right half
    shot('swipe_a')
    drag(w * 0.62, h * 0.45, w * 0.62 + w * 0.2, h * 0.45, 20)
    touch('UP', w * 0.82, h * 0.45)
    time.sleep(0.8)
    shot('swipe_b')
    # 3. finger loss: the stick is held, then the gesture is cancelled (no UP): Link must stop
    drag(w * 0.25, h * 0.62, w * 0.25 - h * 0.12, h * 0.62, 6)
    time.sleep(0.6)
    touch('CANCEL', w * 0.25 - h * 0.12, h * 0.62)
    time.sleep(1.0)
    shot('lost_a'); time.sleep(0.6); shot('lost_b')
    # 4. a new touch after the loss starts a fresh stick (no stale finger left)
    drag(w * 0.3, h * 0.6, w * 0.3, h * 0.6 - h * 0.1, 4)
    touch('UP', w * 0.3, h * 0.5)

    log = adb('logcat', '-d').decode(errors='replace')
    (args.output / 'logcat.txt').write_text(log)
    results['pad attached'] = '[input] touch controls on' in log
    results['swipe reached the camera'] = '[input] swipe camera' in log
    try:
        from PIL import Image, ImageChops, ImageStat

        def change(a, b):
            ia = Image.open(args.output / f'{a}.png').convert('L')
            ib = Image.open(args.output / f'{b}.png').convert('L')
            return ImageStat.Stat(ImageChops.difference(ia, ib)).mean[0]
        for a, b in (('walk_a', 'walk_b'), ('idle_a', 'idle_b'), ('swipe_a', 'swipe_b'), ('lost_a', 'lost_b')):
            print(f'change {a} -> {b}: {change(a, b):.2f}')
        results['camera turned (swipe change > 3x idle)'] = change('swipe_a', 'swipe_b') > 3 * max(0.5, change('idle_a', 'idle_b'))
        results['stopped after a lost finger (lost change <= 2x idle)'] = change('lost_a', 'lost_b') <= 2 * max(0.5, change('idle_a', 'idle_b'))
    except ImportError:
        print('Pillow not installed: look at the pictures in', args.output)
    for k, v in results.items():
        print(f'{"PASS" if v else "FAIL"} {k}')
    sys.exit(0 if all(results.values()) else 1)


if __name__ == '__main__':
    main()
