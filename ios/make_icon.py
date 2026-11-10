#!/usr/bin/env python3
"""Makes the GamePad app's icon from the game's own icon (game/meta/iconTex.tga, 128x128), as the Mac
app's icon is made at install (tools/installer/setup.py) and Android's by android/make_icon.py.

usage: python3 ios/make_icon.py [game_dir | iconTex.tga]   (default: game)

Writes ios/WWHDGamePad/Assets.xcassets/GameIcon.appiconset and ios/local.xcconfig, which selects it
(both git-ignored: the picture is the game's, like the recompiled code it stays on your machine). Build
the app again afterwards. Without them the app has the project's own icon. Remove both to go back.
Needs Pillow (pip install pillow).
"""
import json
import os
import sys

from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
arg = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "game")
src = arg if arg.lower().endswith(".tga") else os.path.join(arg, "meta", "iconTex.tga")
if not os.path.exists(src):
    sys.exit(f"{src} not found (the extracted game's meta folder)")

art = Image.open(src).convert("RGBA")
# iOS wants an opaque square; the picture fills it (iOS rounds the corners itself)
square = Image.new("RGB", art.size, (0, 0, 0))
square.paste(art, mask=art.getchannel("A"))
icon = square.resize((1024, 1024), Image.LANCZOS)

out = os.path.join(here, "WWHDGamePad", "Assets.xcassets", "GameIcon.appiconset")
os.makedirs(out, exist_ok=True)
icon.save(os.path.join(out, "GameIcon.png"))
with open(os.path.join(out, "Contents.json"), "w") as f:
    json.dump({"images": [{"filename": "GameIcon.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"}],
               "info": {"author": "xcode", "version": 1}}, f, indent=2)
with open(os.path.join(here, "local.xcconfig"), "w") as f:
    f.write("// made by make_icon.py: the game's own icon (git-ignored)\nWWHD_APP_ICON = GameIcon\n")
print("wrote", os.path.relpath(out, here), "and local.xcconfig: build the app again")
