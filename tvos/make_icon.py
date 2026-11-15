#!/usr/bin/env python3
"""Gives the Apple TV app its icon and Top Shelf images (a tvOS brand asset), compiled into the bundle; the
iPhone / iPad app (its Info.plist names those devices) its icon.

usage: python3 tvos/make_icon.py WindWakerHD.app [picture]

The picture: the given one, else the game's own TV boot image when the game files are in the bundle
(game/meta/bootTvTex.tga, as the app packages them: the picture is the game's, like the rest of the
bundle, so the app stays on your devices), else the project's own icon (android/icon). Any picture is
scaled to cover each size and cut to it. The iPhone / iPad icon is the boot image's middle panel (the hero raising
the sword), apart from the GamePad app's icon on the same phone (ios/make_icon.py: the game's own small icon).
Runs after every build of the app (CMakeLists.txt); run it
again after putting the game files in, then sign. Only macOS's own tools: sips, and Xcode's actool.
"""
import json
import os
import plistlib
import struct
import subprocess
import sys
import tempfile
import zlib

here = os.path.dirname(os.path.abspath(__file__))
STACK = "App Icon & Top Shelf Image"


def size(path):
    out = subprocess.run(["sips", "-g", "pixelWidth", "-g", "pixelHeight", path],
                         check=True, capture_output=True, text=True).stdout
    vals = [int(l.split(":")[1]) for l in out.splitlines() if "pixel" in l]
    return vals[0], vals[1]


def cover(src, w, h, out):
    """`src` scaled to cover w x h and cut to it, centred, as a PNG"""
    sw, sh = size(src)
    scale = max(w / sw, h / sh)
    rw, rh = max(w, round(sw * scale)), max(h, round(sh * scale))
    subprocess.run(["sips", "-s", "format", "png", "-z", str(rh), str(rw), src, "--out", out],
                   check=True, capture_output=True)
    subprocess.run(["sips", "-c", str(h), str(w), out, "--out", out], check=True, capture_output=True)


def clear_png(w, h, out):
    """a fully transparent w x h PNG (a stack's front layer: tvOS wants two layers at least)"""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + bytes(w * 4) for _ in range(h))
    with open(out, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def contents(folder, data):
    os.makedirs(folder, exist_ok=True)
    data["info"] = {"author": "xcode", "version": 1}
    with open(os.path.join(folder, "Contents.json"), "w") as f:
        json.dump(data, f, indent=2)


def imageset(folder, picture, w, h, scales=(1, 2), clear=False):
    os.makedirs(folder, exist_ok=True)
    images = []
    for s in scales:
        name = f"image@{s}x.png"
        if clear:
            clear_png(w * s, h * s, os.path.join(folder, name))
        else:
            cover(picture, w * s, h * s, os.path.join(folder, name))
        images.append({"filename": name, "idiom": "tv", "scale": f"{s}x"})
    contents(folder, {"images": images})


def stack(folder, picture, w, h, scales):
    contents(folder, {"layers": [{"filename": "Front.imagestacklayer"}, {"filename": "Back.imagestacklayer"}]})
    contents(os.path.join(folder, "Front.imagestacklayer"), {})
    contents(os.path.join(folder, "Back.imagestacklayer"), {})
    imageset(os.path.join(folder, "Front.imagestacklayer", "Content.imageset"), picture, w, h, scales, clear=True)
    imageset(os.path.join(folder, "Back.imagestacklayer", "Content.imageset"), picture, w, h, scales)


IOS_ICON = "AppIcon"
# the boot image's middle panel, as fractions of the picture (bootTvTex.tga, 1280x720): left, top, side / height
HERO = (0.390, 0.26, 0.41)


def square_icon(picture, boot, out):
    """the 1024 x 1024 icon picture: the boot image's middle panel, or the picture covered"""
    if boot:
        w, h = size(picture)
        side = round(HERO[2] * h)
        subprocess.run(["sips", "-s", "format", "png", "-c", str(side), str(side), "--cropOffset",
                        str(round(HERO[1] * h)), str(round(HERO[0] * w)), picture, "--out", out],
                       check=True, capture_output=True)
        subprocess.run(["sips", "-z", "1024", "1024", out, "--out", out], check=True, capture_output=True)
    else:
        cover(picture, 1024, 1024, out)


def ios_icon(cat, picture, boot):
    """iPhone / iPad: one square picture"""
    folder = os.path.join(cat, IOS_ICON + ".appiconset")
    os.makedirs(folder, exist_ok=True)
    square_icon(picture, boot, os.path.join(folder, "icon.png"))
    contents(folder, {"images": [{"filename": "icon.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"}]})
    return IOS_ICON


def vision_icon(cat, picture, boot):
    """Apple Vision Pro: a layered icon (the picture at the back, a clear front layer)"""
    stack_dir = os.path.join(cat, IOS_ICON + ".solidimagestack")
    contents(stack_dir, {"layers": [{"filename": "Front.solidimagestacklayer"}, {"filename": "Back.solidimagestacklayer"}]})
    for layer in ("Front", "Back"):
        folder = os.path.join(stack_dir, layer + ".solidimagestacklayer", "Content.imageset")
        contents(os.path.dirname(folder), {})
        os.makedirs(folder, exist_ok=True)
        out = os.path.join(folder, "icon.png")
        if layer == "Front":
            clear_png(1024, 1024, out)
        else:
            square_icon(picture, boot, out)
        contents(folder, {"images": [{"filename": "icon.png", "idiom": "vision", "scale": "2x"}]})
    return IOS_ICON


def tv_icon(cat, picture, boot):
    """Apple TV: the layered app icons and the Top Shelf images, from the whole picture"""
    brand = os.path.join(cat, STACK + ".brandassets")
    contents(brand, {"assets": [
        {"filename": "App Icon - App Store.imagestack", "idiom": "tv", "role": "primary-app-icon", "size": "1280x768"},
        {"filename": "App Icon.imagestack", "idiom": "tv", "role": "primary-app-icon", "size": "400x240"},
        {"filename": "Top Shelf Image Wide.imageset", "idiom": "tv", "role": "top-shelf-image-wide", "size": "2320x720"},
        {"filename": "Top Shelf Image.imageset", "idiom": "tv", "role": "top-shelf-image", "size": "1920x720"},
    ]})
    stack(os.path.join(brand, "App Icon - App Store.imagestack"), picture, 1280, 768, (1,))
    stack(os.path.join(brand, "App Icon.imagestack"), picture, 400, 240, (1, 2))
    imageset(os.path.join(brand, "Top Shelf Image Wide.imageset"), picture, 2320, 720)
    imageset(os.path.join(brand, "Top Shelf Image.imageset"), picture, 1920, 720)
    return STACK


# UIDeviceFamily -> (label, catalog layout, actool platform, target devices)
FAMILIES = {3: ("Apple TV", tv_icon, "appletvos", ["tv"]), 7: ("Vision Pro", vision_icon, "xros", ["vision"]),
            1: ("iPhone / iPad", ios_icon, "iphoneos", ["iphone", "ipad"])}


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    app = sys.argv[1]
    picture = sys.argv[2] if len(sys.argv) > 2 else None
    boot = False
    if not picture:
        picture = os.path.join(app, "game", "meta", "bootTvTex.tga")
        boot = os.path.exists(picture)
        if not boot:
            picture = os.path.join(here, "..", "android", "icon", "app_icon_1024.png")
    info_path = os.path.join(app, "Info.plist")
    with open(info_path, "rb") as f:
        info = plistlib.load(f)
    family = info.get("UIDeviceFamily", [3])
    label, layout, platform, devices = FAMILIES[3 if 3 in family else 7 if 7 in family else 1]
    with tempfile.TemporaryDirectory() as tmp:
        cat = os.path.join(tmp, "Assets.xcassets")
        contents(cat, {})
        icon = layout(cat, picture, boot)
        partial = os.path.join(tmp, "partial.plist")
        r = subprocess.run(["xcrun", "actool", cat, "--compile", app, "--platform", platform,
                            "--minimum-deployment-target", info.get("MinimumOSVersion", "17.0"),
                            *[a for d in devices for a in ("--target-device", d)],
                            "--app-icon", icon, "--output-partial-info-plist", partial,
                            "--errors", "--warnings", "--output-format", "human-readable-text"],
                           capture_output=True, text=True)
        if r.returncode or not os.path.exists(partial):
            sys.exit("actool failed:\n" + r.stdout + r.stderr)
        for line in r.stdout.splitlines():
            if "warning" in line or "error" in line:
                print(line)
        with open(partial, "rb") as f:
            info.update(plistlib.load(f))
        if platform != "appletvos":
            info.pop("TVTopShelfImage", None)  # (left by an older run of this script)
        with open(info_path, "wb") as f:
            plistlib.dump(info, f)
    print(f"{os.path.basename(app)}: {label} icon from {os.path.basename(picture)}")


if __name__ == "__main__":
    main()
