#!/usr/bin/env python3
"""Regenerate or check SDK declarations from the pinned, clean public decomp."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

REVISION = "47e1dbc3886cfd8233859dffd73efc41a04a9130"
ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--public-clone", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    revision = subprocess.check_output(["git", "-C", str(args.public_clone), "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISION:
        parser.error("public checkout must be at pinned revision " + REVISION)
    target = ROOT / "runtime/guest/include/game"
    with tempfile.TemporaryDirectory() as tmp:
        output = Path(tmp) / "game"
        output.mkdir()
        subprocess.run([sys.executable, str(ROOT / "tools/guestmod/public_sdk_index.py"),
                        str(args.public_clone), "--out", str(Path(tmp) / "index.json"),
                        "--symbols-header", str(output / "functions.h"), "--layouts-dir", str(output)], check=True)
        generated = sorted(output.glob("*.h"))
        different = [p.name for p in generated if not (target / p.name).is_file() or
                     p.read_bytes() != (target / p.name).read_bytes()]
        if args.check:
            if different:
                raise SystemExit("SDK declarations differ: " + ", ".join(different))
            print("Pinned public SDK: all %d headers match" % len(generated))
        else:
            for path in generated:
                (target / path.name).write_bytes(path.read_bytes())


if __name__ == "__main__":
    main()
