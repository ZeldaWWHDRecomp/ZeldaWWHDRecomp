# Apple TV (experimental)

The game built for tvOS: the Metal renderer with a UIKit host (`runtime/src/gfx/host_tvos.mm`), your game
files packaged in the app, and your iPhone (the GamePad app in `ios/`) as its Wii U GamePad.

**Experimental.** On the newest Apple TV available so far, the Apple TV 4K (3rd generation, A15), it is
barely acceptable. Check performance again when a newer Apple TV comes out: its chip should hold 60 fps and
run cooler. Measured on the A15:

| | |
|---|---|
| 720p, 30 fps | holds 30 fps; the render thread uses about 6 ms of each frame |
| 720p, 60 fps (frame interpolation) | about 58 fps when cool; the GPU is at its limit |
| 1080p (1.5x) | 30-35 fps at 60, with dynamic resolution at its floor: not usable |
| Heat | it has no fan: tvOS reaches its "serious" thermal state within a few minutes of play and then slows the chip until the game runs at 10 fps. The heat guard (below) keeps 30 fps instead |
| Memory | about 1.15 GB on Outset Island of tvOS's fixed limit of about 2.1 GB for the app |
| GamePad latency | about 20 ms for the picture (Mac mini M1: 10-11 ms), most of it waiting for the GPU |

Play at 720p and 30 fps (the defaults).

## What is different from the Mac

- **Textures:** the A15 has no BC textures. BC1-3 are re-encoded on upload to ETC2/EAC with etcpak
  (`runtime/third_party/etcpak`), BC4/BC5 decoded to R8/RG8 (`gfx/formats.mm`).
- **Shaders:** the app carries the shader library (`shaders.metallib`, `tools/installer/shader_library.py
  --sdk appletvos`) and the live shader cache (`shaders.bin`), so none compiles on the Apple TV. Pipelines
  are built when first drawn, not all in the background (about 400 MB less).
- **Heat guard:** from the "serious" thermal state the game pauses 60 fps, halves the occlusion depth and
  renders at 0.75x, and puts your settings back after a minute at "fair" or "nominal". Nothing it changes
  is saved.
- **Settings:** the game's Settings (controller: hold Create / Select) instead of the Mac's menus. The
  graphics options are saved; full-size occlusion depth starts off.
- **Files:** the game files are read from the app; settings, saves and the log go to the app's Caches
  folder, which tvOS may empty when it needs space. Copy saves in and out with `xcrun devicectl device
  copy`.
- **Not on an Apple TV:** restarts (renderer changes apply at the next start), code mods, file panels,
  keyboard and mouse.

## Building (by hand, for now)

The app contains code and files from your own game: it is for your own devices only.

1. Configure for tvOS with the game code setup translated (`data/code-translations/<key>/gen`, unpacked):
   `cmake -S . -B build/tvos -G Ninja -DCMAKE_SYSTEM_NAME=tvOS -DCMAKE_OSX_SYSROOT=appletvos
   -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DCMAKE_BUILD_TYPE=Release
   -DWWHD_RENDERER=METAL -DGEN_DIR=<gen> -DWWHD_BUNDLED_DEPS=ON -DWWHD_BUNDLED_ZSTD=ON`, then
   `ninja -C build/tvos wwhd`.
2. Into `build/tvos/WindWakerHD.app`: your game files as `game/` (`code`, `content`, `meta`), the shader
   library as `shaders.metallib` and the live shader cache as `shaders.bin` (both from a Mac's prepared
   shaders).
   Then `python3 tvos/make_icon.py build/tvos/WindWakerHD.app`: the icon and Top Shelf images become
   the game's TV boot image (each build makes them, from the project's icon until the game files are in).
3. Sign it with your team's development profile for tvOS and install it with `xcrun devicectl device
   install app`. Reinstalls send only what changed (seconds, not the 1.8 GB).
4. Start it from the Apple TV's home screen. The iPhone GamePad is on by default on an Apple TV (Settings >
   Controls > iPhone GamePad turns it off, and that is remembered).

Code mods: the TV can't build them, so the Mac builds them into the app. Prepare the mods in a game on the
Mac with code mods on (install them, switch them on), build the app with game code translated for code mods,
then `python3 tvos/bundle_mods.py --app build/tvos/WindWakerHD.app --manager <that ModManager folder> --sign
<identity>` before signing the app (step 3). Options change on the TV; another set of mods needs another app.

Still to do: setup building and installing it; saves kept in iCloud (CloudKit), in step with the Mac; a
settings panel for the Siri Remote.
