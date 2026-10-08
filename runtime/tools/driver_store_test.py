"""Synthetic driver packages only: no downloaded driver or game material."""
import pathlib, subprocess, sys, tempfile, zipfile, json
with tempfile.TemporaryDirectory(prefix="wwhd-driver-test-") as work:
    root = pathlib.Path(work)
    elf = bytearray(64)
    elf[:7] = b"\x7fELF\x02\x01\x01"
    elf[16] = 3
    elf[18] = 183
    meta = dict(schemaVersion=1, name="Synthetic Turnip", driverVersion="test", minApi=28, libraryName="libvulkan_test.so")
    good = root / "good.zip"
    with zipfile.ZipFile(good, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("meta.json", json.dumps(meta))
        z.writestr("libvulkan_test.so", elf)
    bad = root / "bad.zip"
    cases = [
        {"../outside": b"escape"},
        {"libvulkan_test.so": b"not-elf", "meta.json": json.dumps(meta)},
        {"libvulkan_test.so": elf, "meta.json": json.dumps({**meta, "libraryName": "../outside.so"})},
        {"libvulkan_test.so": elf, "meta.json": json.dumps({**meta, "schemaVersion": 99})},
        {"libvulkan_test.so": elf, "meta.json": json.dumps({**meta, "minApi": -1})},
    ]
    for case in cases:
        with zipfile.ZipFile(bad, "w") as z:
            for name, value in case.items(): z.writestr(name, value)
        subprocess.run([sys.argv[1], str(root / "store"), str(good), str(bad)], check=True)
print("Driver store: synthetic ZIP validation and 119/120-frame recovery PASS")
