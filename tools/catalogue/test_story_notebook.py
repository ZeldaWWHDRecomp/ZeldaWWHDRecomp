#!/usr/bin/env python3
"""The story system's required package (runtime/src/mods/required.h) from a synthetic local catalogue:
offline, then installed, confirmed and enabled; the game's start never fetches. Runs
mod_package_test --story-notebook <fixtures>."""
import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

with tempfile.TemporaryDirectory(prefix="wwhd-story-notebook-") as temporary:
    root = Path(temporary)
    manifest = dict(format_version=1, id="story-notebook", name="Synthetic Story Notebook", version="1.0.0", game_id="wwhd",
                    minimum_manager_version="1.5.0", kind="guest", guest=dict(api_version=1, elf="mod.elf"),
                    author="WWHD test fixtures", description="Authored synthetic fixture; contains no game assets.")
    payload = {"manifest.json": json.dumps(manifest).encode(), "mod.elf": b"\x7fELF\x01\x02" + bytes(64),
               "LICENSE.txt": b"Authored synthetic fixture material is dedicated to the public domain under CC0-1.0.\n"}
    archive = root / "story-notebook.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        for path, data in sorted(payload.items()):
            info = zipfile.ZipInfo(path, date_time=(2026, 1, 1, 0, 0, 0))
            info.external_attr = 0o100644 << 16
            bundle.writestr(info, data)
    data = archive.read_bytes()
    entry = dict(id="story-notebook", name=manifest["name"], version="1.0.0", kind="guest", description=manifest["description"],
                 authors=[manifest["author"]], licences=["CC0-1.0"], requires=[], builds=["USA", "EU", "JP"],
                 port_versions={"minimum": "0.2.10"}, setup=[],
                 downloads={"all": dict(url=archive.name, size=len(data), sha256=hashlib.sha256(data).hexdigest())})
    (root / "index.json").write_text(json.dumps(dict(format_version=1, mods=[entry])))
    subprocess.run([sys.argv[1], "--story-notebook", str(root)], check=True)
