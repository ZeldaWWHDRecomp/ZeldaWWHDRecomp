#!/usr/bin/env python3
"""Create authored local catalogue fixtures; never package game data.

The optional heart-ticker ELF must be compiled from examples/guest-mods/heart-ticker.
Outputs belong in a build directory, not the source tree.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

TOOL = '''from pathlib import Path
import sys
source = Path(sys.argv[1])
if source.is_dir(): source = source / "sys" / "boot.bin"
with source.open("rb") as stream: header = stream.read(32)
if header[:6] != b"GZLE01" or header[28:32] != bytes.fromhex("c2339f3d"):
    raise SystemExit("Expected a USA GameCube source")
# Authored synthetic output: no source bytes are copied.
Path("pilot-prepared.txt").write_bytes(b"Synthetic preparation complete\\n")
print("Synthetic preparation complete")
'''


def generate(output, heart_elf=None):
    output.mkdir(parents=True, exist_ok=True)
    entries = []

    def package(ident, name, kind, files, setup=None, licences=None, **extra):
        manifest = dict(format_version=1, id=ident, name=name, version="1.0.0",
                        game_id="wwhd-usa", kind=kind, author="WWHD catalogue test fixtures",
                        description="Authored synthetic catalogue pilot; contains no game assets.", **extra)
        if setup:
            manifest["setup"] = setup
        archive = output / (ident + ".zip")
        payload = dict(files)
        payload["manifest.json"] = json.dumps(manifest, indent=2).encode() + b"\n"
        payload.setdefault("LICENSE.txt", b"Authored synthetic fixture material is dedicated to the public domain under CC0-1.0.\n")
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
            for path, data in sorted(payload.items()):
                info = zipfile.ZipInfo(path, date_time=(2026, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                bundle.writestr(info, data)
        data = archive.read_bytes()
        entries.append(dict(id=ident, name=name, version=manifest["version"], kind=kind,
                            description=manifest["description"], authors=[manifest["author"]],
                            licences=licences or ["CC0-1.0"], requires=[], builds=["USA", "EU"],
                            port_versions={"minimum": "0.2.10"}, setup=setup or [],
                            downloads={"all": dict(url=archive.name, size=len(data), sha256=hashlib.sha256(data).hexdigest())}))

    package("catalogue-content-pilot", "Synthetic content pilot", "content",
            {"content/catalogue-pilot.txt": b"Authored synthetic content replacement fixture\n"}, content_dir="content")
    setup = [dict(id="source", type="game_path", title="Choose synthetic USA GameCube source", game="gc_usa"),
             dict(id="colour", type="choice", title="Choose a pilot colour", option="colour", choices=["blue", "green"]),
             dict(id="consent", type="confirm", title="Prepare this synthetic fixture", option="consent"),
             dict(id="prepare", type="run_tool", title="Prepare synthetic data", tool="tools/prepare.py",
                  arguments=["{game:gc_usa}"], outputs=["pilot-prepared.txt"])]
    package("catalogue-setup-pilot", "Synthetic setup pilot", "settings", {"tools/prepare.py": TOOL.encode()}, setup,
            settings={"wall-climb": True}, options=[
                dict(id="colour", name="Pilot colour", type="enum", choices=["blue", "green"], default="blue"),
                dict(id="consent", name="Prepare fixture", type="bool", default=False)])
    if heart_elf:
        elf = heart_elf.read_bytes()
        if elf[:6] != b"\x7fELF\x01\x02" or len(elf) < 20 or struct.unpack_from(">H", elf, 18)[0] != 20:
            raise ValueError("Heart pilot requires a 32-bit big-endian PowerPC ELF")
        # Preserve the example's actual hook names and options; only add catalogue setup.
        source = Path(__file__).resolve().parents[2] / "examples/guest-mods/heart-ticker/manifest.json"
        original = json.loads(source.read_text())
        package("heart-ticker", "Heart ticker guest pilot", "guest", {"mod.elf": elf, "src/mod.c": source.with_name("mod.c").read_bytes(),
                    "LICENSE.txt": (source.parents[3] / "LICENSE").read_bytes()},
                [dict(id="build", type="build_guest_mod", title="Build heart ticker for this install")],
                licences=["MPL-2.0"], guest=original["guest"], options=original["options"])
    # A minimal synthetic disc header is enough for ID validation; it is not a playable game.
    header = bytearray(32)
    header[:6] = b"GZLE01"
    struct.pack_into(">I", header, 28, 0xC2339F3D)
    (output / "synthetic-gc-usa.iso").write_bytes(header)
    (output / "index.json").write_text(json.dumps(dict(format_version=1, mods=entries), indent=2) + "\n")
    return entries


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--heart-elf", type=Path)
    args = parser.parse_args()
    generate(args.out, args.heart_elf)
