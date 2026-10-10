#!/usr/bin/env python3
"""Release notes from CHANGELOG.md: install instructions + the release's changes + checksums.

usage: notes.py CHANGELOG.md VERSION SHA256SUMS.txt [--devel COMMIT] > notes.md

--devel: notes for the rolling development pre-release; VERSION is then "Next update", the block
of unreleased changes.

CHANGELOG.md has one "## vX.Y.Z" section per release; the notes take the section for VERSION
(none found: a pointer to the changelog).
"""
import re
import sys


def section(text, title):
    m = re.search(r"^## %s\s*$\n(.*?)(?=^## |\Z)" % re.escape(title), text, re.S | re.M)
    return m.group(1).strip() if m else ""


def main():
    changelog, version, sums = sys.argv[1:4]
    devel = sys.argv[5] if len(sys.argv) > 5 and sys.argv[4] == "--devel" else None
    with open(changelog, encoding="utf-8") as f:
        text = f.read()
    new = section(text, version)
    with open(sums) as f:
        checksums = f.read().strip()
    if devel:
        print("""**Development build of devel @ %s: not a release.** It has the changes below that are not
released yet, and has had less testing than a release. Use it to try a fix, and include the commit
(%s) when you report a problem. The latest release is on the Releases page.
""" % (devel[:7], devel[:7]))
        version = "development build"
    print("""**The Wind Waker HD, native PC port, %s**

This release contains **no game files, no game code and no keys**. You need your own disc dump
(.wux/.wud with its disc key, plus the Wii U common key from your console), a Cemu .wua archive
(no keys needed), or an already extracted game folder. The installer builds the game from it on your machine.

**Install:** download the zip for your system, unzip it anywhere and start **Wind Waker HD**. The
first start prepares the game once from your dump (about two minutes); later starts launch it directly.
Everything stays in that folder.
- macOS (Apple Silicon, macOS 14+): `Wind Waker HD.app`. The release is not signed by Apple:
  macOS 15+: System Settings > Privacy & Security > Open Anyway; macOS 14: right-click > Open.
  Keep the app inside the unzipped folder (move the whole folder, not just the app)
- Windows: `Wind Waker HD.exe` (SmartScreen: "More info" > "Run anyway"); `windows-x86_64` for x86-64,
  `windows-arm64` for Windows on ARM (Snapdragon X)
- Linux (glibc 2.35+, Vulkan): `wind-waker-hd`; `linux-x86_64` for x86-64, `linux-aarch64` for arm64
  (Raspberry Pi 5, Asahi Linux, ARM laptops). Or one file: `chmod +x` the `.AppImage` and start it
  from anywhere (Steam Deck included); its game, code, saves and settings go to `~/.local/share/wwhd`
  and `~/.config/wwhd` instead of beside it (Ubuntu 24.04+: `libfuse2t64`, or
  `--appimage-extract-and-run`)
- Android (arm64, Android 13+, Vulkan 1.3): install the `android-arm64.apk` (`android-x86_64` is for
  emulators and Chromebooks), choose your game in the app, and it builds the game on the phone (once,
  a few minutes). Signing certificate SHA-256:
  `7F:F6:D3:5E:4E:45:11:5E:E4:93:65:EF:5B:40:C0:EB:83:EA:F7:6C:C1:1C:E9:67:F2:64:5C:50:4F:58:B2:13`

See "Install (releases)" in the README for details.

## What's new

%s

## Checksums (SHA-256)

```
%s
```
""" % (version, new or "See CHANGELOG.md.", checksums))


if __name__ == "__main__":
    main()
