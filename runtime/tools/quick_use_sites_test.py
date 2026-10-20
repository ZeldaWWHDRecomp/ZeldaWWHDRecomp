#!/usr/bin/env python3
"""Check quick-use hooks against every checked regional map, without game files."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "recomp"))
from builds import all_builds  # noqa: E402

source = (ROOT / "runtime/src/mods/quick_use.cpp").read_text()
hooks = (ROOT / "tools/recomp/hooks_mods.txt").read_text()
sites = {int(address, 16) for address in re.findall(r'void (?:site|hook)_([0-9A-F]{8})\(', source)}
listed = {int(address, 16) for address in re.findall(r'^@?([0-9A-F]{8})\b', hooks, re.M)}
# Both bow redraw/release and bomb publication/throw predicate must be wired. Counting
# old grappling/boomerang hooks alone must not make an all-applicable-items build pass.
required = {0x023E88B0, 0x023EB2B0, 0x023EB434, 0x023DF7C0, 0x023EA474,
            0x023EB2C4, 0x0242E14C, 0x0242E198, 0x023F05EC, 0x0243CB70, 0x0243C6F4}
assert required <= sites, required - sites
assert len(sites) >= 24 and sites <= listed, sites - listed
calls = {int(address, 16) for address in re.findall(r'GC\(0x([0-9A-F]+)\)', source)}
for build in all_builds():
    for address in sites | calls:
        assert not build.body_differs(address), (build.name, hex(address))
        mapped = build.code(address)
        assert build.canon_code(mapped) == address, (build.name, hex(address))
    for address in (0x1046F0B0, 0x101F36CC, 0x101EA069):
        build.data(address)
    print(f"{build.name}: {len(sites)} hook sites and {len(calls)} guest helpers compatible")
