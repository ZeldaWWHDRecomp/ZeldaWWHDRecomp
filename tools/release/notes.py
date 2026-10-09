#!/usr/bin/env python3
"""Release notes from README.md: install instructions + the "What's new" section + checksums.

usage: notes.py README.md VERSION SHA256SUMS.txt > notes.md

"What's new" has one "### vX.Y.Z" block per release; the notes take the block for VERSION (or the
whole section when there is none).
"""
import re
import sys


def section(text, title_prefix):
    m = re.search(r"^## %s.*?$\n(.*?)(?=^## )" % re.escape(title_prefix), text, re.S | re.M)
    return m.group(1).strip() if m else ""


def main():
    readme, version, sums = sys.argv[1:4]
    with open(readme, encoding="utf-8") as f:
        text = f.read()
    new = section(text, "What's new")
    # only this release's own changes: the "### vX.Y.Z" block of "What's new"
    m = re.search(r"^### %s\s*$\n(.*?)(?=^### |\Z)" % re.escape(version), new, re.S | re.M)
    if m:
        new = m.group(1).strip()
    with open(sums) as f:
        checksums = f.read().strip()
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
- Windows (x86-64): `Wind Waker HD.exe` (SmartScreen: "More info" > "Run anyway")
- Linux (glibc 2.35+, Vulkan): `wind-waker-hd`; `linux-x86_64` for x86-64, `linux-aarch64` for arm64
  (Raspberry Pi 5, Asahi Linux, ARM laptops). Or one file: `chmod +x` the `.AppImage` and start it
  from anywhere (Steam Deck included); its game, code, saves and settings go to `~/.local/share/wwhd`
  and `~/.config/wwhd` instead of beside it (Ubuntu 24.04+: `libfuse2t64`, or
  `--appimage-extract-and-run`)

See "Install (releases)" in the README for details.

## What's new

%s

## Checksums (SHA-256)

```
%s
```
""" % (version, new or "See the README.", checksums))


if __name__ == "__main__":
    main()
