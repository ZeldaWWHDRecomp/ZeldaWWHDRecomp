# Changelog

What changed in each update of the Wind Waker HD native port. Downloads and the same notes per
release: [Releases](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/releases). "Next update" lists
what is on `devel` and not released yet (also in the
[development build](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/releases/tag/devel-build)).

## Next update

- **Menu and on-screen keyboard: confirm and cancel like in the game** (PR #118, thanks @rhemfur). With
  the default face-button preset (by position) the right button confirms and the bottom one goes back,
  as in the game; the on-screen keyboard types with the game's A and deletes with B. "By label" and
  "Automatic" are followed too.
- **Android player guide** in English, Portuguese and Spanish (PR #122, thanks @rhemfur):
  [docs/android.md](docs/android.md).

- **Diagnostics for menu input problems** (issue #111): `WWHD_OVERLAY_TRACE=1` logs what the settings
  menu receives from the controller and every scroll change; `WWHD_OVERLAY_MOUSE_HANDOFF=0` turns the
  mouse takeover off for such a test. Steps in [docs/overlay-controls.md](docs/overlay-controls.md).

- **Mods tab: installed catalogue mods show where their setup stands.** Each step reads "Done" or
  "To do" (for example the GameCube game step is done once you set it), with a **Go to setup** button
  that jumps to the mod's setup; "Up to date" replaces the greyed-out Update button.

- **Fixed: the game didn't start with a content mod (for example a translation) in the development
  builds** (issue #109). The mod manager switched the replacement files on twice at startup and stopped
  with "Content overrides already activated". Content mods and code mods now start together as they should.

- **Bug reports: the log now lists every connected controller** (name, vendor and product ID, type),
  on connect and disconnect. The game combines all controllers Windows/Linux report, so this shows
  when a second device (e.g. Steam Input's virtual controller) is also sending input.

- **Android: set up the game directly on the phone, no PC needed** (PR #98, tested by @rhemfur).
  Choose your game (an extracted folder, a WUA or a WUD/WUX), and the phone translates and compiles the
  game code itself (several minutes; up to 4 compiler workers depending on the phone), with a progress
  display. Setup pauses when the phone gets too warm or the battery is low, and resumes where it
  stopped. Also new: the GamePad picture on a second screen (foldables, external displays).

- **Mods: one setting for the GameCube version of the game.** Settings → Mods → **GameCube game for
  mods**: choose your GameCube Wind Waker disc image or folder once (it shows the recognised region),
  and every mod that needs it uses it. A mod whose copy is missing can't be enabled and offers a
  button to that setting. Changing the copy marks the affected mods for setup again (prepared files
  are kept).

- **Android: on-screen controls** (PR #88, thanks @rhemfur). Phones without a controller can now
  play: the 🎮 button under the view button (top left) shows or hides two sticks, the D-pad,
  A B X Y, L R, ZL ZR and + − (remembered). They act like a controller, so the button mapping and a
  physical controller keep working; with a controller connected they hide until you touch the screen.

- **Controls: an Automatic face-button preset** (PR #102, thanks @mhbxyz). It reads the labels
  printed on the pad that was plugged in first and follows them, so an Xbox or PlayStation pad plays
  by label and a Nintendo pad by position, without touching the setting.

- **Mods tab: a "Reset to default" button for the catalogue address.** It puts back the official
  mod catalogue if the address was changed. Download errors now also show the server's status code
  (for example "HTTP 404"), so a wrong address is easier to tell apart from a connection problem.

- **Setup can now build with code-mod support right away.** A new "Build with code-mod support"
  option (off by default) saves the second rebuild when you want code mods. Updates and repairs keep
  your choice, and Settings → Mods shows it immediately. Details:
  [docs/setup-code-mods.md](docs/setup-code-mods.md).

- **Fixed: the settings menu scrolled back up after the left stick had been used** (issue #111).
  A left stick that rests slightly off-centre kept scrolling the menu, even while you scrolled with
  the mouse. Now the mouse takes over as soon as you move, click, scroll or drag the scroll bar; the
  controller takes over again with its next new input. Controls:
  [docs/overlay-controls.md](docs/overlay-controls.md).

- **Fixed: in development builds every catalogue mod showed "Unavailable for this port version"**.
  Development builds now carry the last release's version plus the commit (for example
  `v0.2.11-devel.1a2b3c4d`), so the catalogue can check compatibility.

- **macOS: the game has its icon again.** Setup now gives the installed app the game's own icon,
  made from your game files (the release can't ship it), and the game shows it in the Dock while it
  runs. Existing installations get it with the next update or repair.
- **macOS: Vulkan is now included in the release.** Pick it in Graphics > Renderer; nothing to
  install. The Vulkan loader and MoltenVK come with the game and are always the ones it uses (never a
  Homebrew copy). Metal stays the default, and if Vulkan can't start, the game falls back to Metal
  and says why. This also makes Cemu graphics packs with GLSL shaders usable on the Mac.


- **5.1 surround sound** (issue #115). Settings → Audio → Speakers: **Surround 5.1** (restart after
  changing it) sends the game's six separate surround channels, as the Wii U does with its Surround
  TV setting. **Test speakers** plays a tone on each speaker in turn and names it, so the wiring is
  easy to check. Stereo stays the default; if the output can't do 5.1, the game stays in stereo and
  says so. Details: [docs/surround-audio.md](docs/surround-audio.md).
- **Fixed: text fields in the settings overlay didn't accept typing or pasting** (for example the
  catalogue address and the mod search; only deleting worked). Typed text, Ctrl+V / Cmd+V and copy
  now work on all platforms; on Android the on-screen keyboard opens for these fields.
- **Cemu graphics packs that conflict can no longer both be switched on** (issue #68). The Mods tab
  shows "Conflicts with …" before you enable a pack; enabling it asks whether to switch (the other
  pack is turned off). Each pack shows what is active now and what changes after a restart, with a
  **Restart now** button. Profiles that had two conflicting packs on are repaired once, with a note.
- **Fixed: with Fast scene changes on, Grandma kept her back to Link when handing over the shield**
  (issue #116, found and fixed by GreenNaugahyde). The mod no longer creates the new scene's
  characters ahead of time; fades and scene changes stay almost as fast.
- **Fixed: crash when entering the Puppet Ganon room** (issue #90, Vulkan). The room uses a colour
  texture for a depth comparison, which the Vulkan renderer didn't support, so it stopped with an error.
  It now handles it. Such internal errors are also no longer lost: they are written to the log and
  the crash log with their message before the game stops, also on Windows. After updating, setup
  compiles the game code once more.
- **The mouse pointer now hides when you don't use the mouse** (issue #109). After 2 seconds without
  mouse movement it disappears from the game window, in full screen and in a window, on Windows,
  Linux and macOS; moving the mouse shows it again. It stays visible while the settings overlay is open.
- **Fixed: the aspect ratio wasn't remembered** (issue #108). The choice in Settings → Graphics →
  Aspect ratio (or the macOS menu) now stays after a restart, like the other graphics options.
  `WWHD_ASPECT` still overrides it.
- **Mod SDK v2: audio streams.** Code mods can now play their own sound: up to four 48 kHz streams
  per mod, mixed into the game's audio and following its volume and mute (and fast forward). The
  dragon example mod uses it for its melody. Mods without audio don't change anything.

- **Fixed: the settings overlay scrolled by itself, and B closed it while assigning a button** (issue
  #111). A drifting controller stick no longer scrolls the menu: the right stick no longer scrolls
  it at all (the D-pad, left stick and mouse wheel do), and the left stick has a dead zone there.
  Assigning B to a control no longer also closes the overlay.
- **Mod SDK v2: more for code mods.** New public declarations (actor profiles, save inventory,
  animation, matrix emitters, flight position and lighting, song handling) and HUD clipping, used by
  the two example mods in the mod repository: a GameCube-style minimap and the Valoo dragon ride.
- **Setup now uses Python 3.14** (bundled on Windows, downloaded on Linux and macOS as before). Mod
  setup steps can now read GameCube disc images in RVZ format, not only ISO.

- **Setup: more reliable compiler download** (issue #113). If the download of the compiler (about
  190 MB on Windows) is interrupted, setup now retries and resumes where it stopped instead of failing.
  If it still can't download it, you can download the file in your browser and put it in the Wind Waker
  HD folder: setup checks it and uses it. On Windows, when the secure connection can't be verified
  ("unable to get local issuer certificate", usually a root certificate Windows hasn't fetched yet),
  setup now downloads with Windows' own curl instead.
- **Fixed: the boat's sail fluttered too fast at 60, 120 and 240 fps** (issue #68). The sail cloth was
  drawn with its exact pose against the in-between camera, which added an extra jump on every
  in-between frame. Its drawing is now interpolated, and so are the other cloth pieces drawn the same
  way: pirate sails and skull flags, fortress flags, buoy flags and their poles, and capes such as
  Phantom Ganon's. 30 fps is unchanged.
- **New Gameplay mod: fast forward cutscenes and dialogue** (off by default; Settings → Gameplay).
  Hold ZR during a cutscene or a conversation and the game runs 2×, 3× or 4× faster (your choice; the
  button can be changed). Nothing is skipped: the game plays every frame, only faster, so the story
  and events end exactly as without it. Questions still wait for your answer, holding the button in
  normal play does nothing, and letting go returns to your frame rate setting. Sound is muted while
  fast-forwarding (it would play sped up); that can be turned off. Idea and clock from
  GreenNaugahyde's Android fork.
- **Performance overlay: GPU, driver and per-thread timing.** Settings → Graphics → Performance
  overlay now also shows the GPU and its driver, the CPU time of the game and render threads and the
  GPU time per frame (each with its average), so you can see at a glance whether the CPU or the GPU
  is the limit. On Android it adds the system's thermal state and the battery temperature next to
  the GPU load and temperatures. **Copy performance report** includes all of it; please paste it into
  performance bug reports. Ideas and the Android battery handling come from GreenNaugahyde's Android
  fork.
- **Fixed: mini-game countdowns ran too fast at 60, 120 and 240 fps** (issue #105). The letter
  sorting on Dragon Roost could not be won at 60 fps or more; its 30-second limit ran out in 15 (at
  120 fps in about 8). The game's shared countdown (also used by the Windfall auction, the boat race and
  the timed challenges) now counts game steps, not displayed frames, and its display stays smooth.
  30 fps is unchanged.
- **The log file is now off by default.** Turn it on in Settings (F1) > Graphics > Bug reports >
  **Write a log file**; from the next start the game writes `captures/wwhd.log` in its data folder
  (the previous run's is kept as `wwhd-previous.log`), with your user paths removed as in crash logs.
  Please turn it on before reproducing a bug and attach the file to the report.
  `WWHD_LOG_FILE=1` turns it on for one run, `WWHD_LOG_FILE=<path>` writes it elsewhere,
  `WWHD_LOG_FILE=0` turns it off.
- **Mod SDK v2, phase 2: HUD drawing for code mods.** PowerPC code mods can now draw on screen
  (text, images, gauges) on both Metal and Vulkan, ship their own original images and content files
  in one package, and read port settings such as the resolution. See `examples/guest-mods/hud-demo`
  and `button-icons`. Code mods stay off unless you turn on **Enable code mods**.
- **Mods panel: Browse catalogue.** Search, install and update mods from a mod catalogue inside the
  game. It only downloads when you press Refresh, checks every package's size and SHA-256, and
  installs packages disabled. Setup steps (choosing your game dump, preparing files locally,
  building a code mod for your game version) run from the panel. The official mod catalogue is not
  public yet; `WWHD_MOD_CATALOGUE` points the panel at another index.

## v0.2.11

- **Linux: one-file AppImage** (issue #55, PR #101, thanks @mhbxyz), next to the zip. Download it,
  make it executable, run it. It keeps the game, its compiled code, saves and settings in your user
  folders (`$XDG_DATA_HOME/wwhd`, usually `~/.local/share/wwhd`); `--data-dir` picks another folder.
  On distributions without libfuse2 (e.g. Ubuntu 24.04+) start it with `--appimage-extract-and-run`.
  The zip stays as it is.

- **Face buttons by label (Xbox layout)** (issue #78, PR #97, thanks @mhbxyz): settings overlay →
  Controls → *Face buttons* (also the macOS Controls window and Input menu). *By position
  (Nintendo)* stays the default; *by label (Xbox)* makes the button named A act as A, so on an Xbox pad
  A accepts and B goes back.

- **Ultrawide and tall screens: menus fixed** (issue #76, building on PR #16 by @arcadematicas): at
  21:9, 32:9, 16:10 and 4:3 the pause menu, inventory grid, cursor, map and the tabs stay centred at 16:9
  and line up again; backgrounds and the sepia filter still fill the picture.

- **Fixed: black letter card in the Rito mail-sorting game** (issues #28, #69, PR #39, thanks
  @Sean13128): the GPU's "0 × anything = 0" multiply is emulated now (as in Cemu). The shader caches
  rebuild once after updating, so expect a brief stutter on the first start.

- **Fixed: Cemu graphics packs**
  - packs that replace pixel shaders (e.g. Contrasty, NoSSAO, RemoveHUD) partly didn't apply on Vulkan
    since v0.2.10 (PR #87, thanks @rhemfur);
  - packs for the **European** version were refused with "Cemu pack does not target WWHD USA"
    (issue #103). A pack is now accepted when its `titleIds` name the version you installed;
    SDCafiine-style folders named after the title ID follow the same rule.

- **Smoother in busy views on Windows and Android:** the Vulkan buffer cache is now on by default
  everywhere. It keeps unchanged vertex, index and uniform data on the GPU instead of copying them
  every frame: on an RX 6700 XT 13–19% less render-thread time and half the uploads (issue #91, thanks
  @darklinkpower), on a Galaxy S25 Ultra about 34 → 47 fps in heavy Outset views (issue #56, thanks
  @rhemfur). If you see broken or flickering geometry, start with `WWHD_VK_BUFFER_CACHE=0` and please
  report it.

- **Less CPU work:**
  - native versions of the game's two hottest audio loops (PR #81, thanks @depende3000): the audio
    thread uses about two thirds less CPU, with bit-identical sound (also on the European version);
  - the scheduler sleeps while nothing is waiting, and GPU context switches copy only the registers
    that changed;
  - the recompiler no longer emits signed overflow for `mulli`/`mullw`/`neg` (PR #82, thanks
    @depende3000), a latent bug a newer compiler could have turned into wrong behaviour.

- **Android:**
  - when the system driver is too old for the renderer, Adreno phones offer **Install GPU driver…**
    right in the error message (PR #92, thanks @rhemfur);
  - on one-screen views the **GamePad screen appears automatically while the game is paused** (map,
    menus, save prompt) and the TV picture comes back when you resume (PR #59, thanks @rhemfur);
  - precise Vulkan synchronization (barriers) is on by default, where tile-based GPUs are expected to
    gain the most (issue #104). On desktop it's opt-in for testing: `WWHD_VK_NARROW_BARRIERS=1`.

- **Save states remember the controller mode** (Pro Controller or GamePad) and restore it on load.

- **"Keep game speed" explained:** with it off, the whole game runs in slow motion whenever the frame
  target isn't reached (e.g. 120/240 fps at 2x). The setting says so now, is marked as recommended, and
  the performance overlay warns when it happens (issue #91).

- **A log file for every run:** `captures/wwhd.log` in the game's data folder (the previous run's is
  kept as `wwhd-previous.log`), with your user paths removed as in crash logs. Please attach it to bug
  reports. `WWHD_LOG_FILE=<path>` writes it elsewhere, `WWHD_LOG_FILE=0` turns it off.

- **Code mods (Mod SDK v2, off by default):** mods written in C for the console's CPU, translated and
  built on your machine when you install them, for the USA and the European game. Turn them on in
  Settings → Mods → *Enable code mods* (this rebuilds the game code once; with it off nothing changes).
  For modders: [docs/mod-sdk-v2.md](docs/mod-sdk-v2.md).

- **Smaller fixes:** `WWHD_SHADOW_FIX` is gone, use `WWHD_SHADOW_SCALE=1` for console-sized shadow maps
  (issue #67: since v0.2.9 both sizes look practically the same); the buffer cache's verify mode no
  longer reads GPU memory (it ran at ~1 fps on some Windows drivers); a crash at exit with SDL 3.4 on
  Linux is fixed.

## v0.2.10

- **Fixed: the cheats in v0.2.9 wrote to the wrong place in the save data** (Give all items, Master
  Sword + Mirror Shield, 20 hearts / double magic / 5000 rupees) and could damage the save. They write
  to the right place again.
- **Fixed: Medli missing on Dragon Roost after using the Master Sword cheat** (issue #85). The cheat
  gave the full-power Master Sword as *owned*, which the game treats as story progress, so Medli (and
  later Makar) stayed away and the story couldn't continue. The cheat now only **equips** the Master
  Sword and Mirror Shield until the next reload. Saves already affected can be repaired with
  `tools/savegame/wwsave.py repair-medli` (it keeps a backup; see the save tools README).
- **Vulkan:** pixel shaders are now linked to their vertex shader when they are translated, so
  drivers that refused some pipelines get correct shaders from the start (PR #54 by @rhemfur, the
  proper fix for #30).

## v0.2.9

- **Play the European version directly** (title 00050000-10143600): setup now also accepts the
  European game on its own, without the USA version, and builds the port from it through an address
  map derived from the two executables (every function and call matched; contributed by **@ElFDA**,
  PR #77, with ideas from GreenNaugahyde's Android fork). German, Italian, British English, French and
  Spanish are its own languages then. Hooks, mods and save states work as on the USA version.
- **Bloom, distance haze and the sun's glare are back:** the game builds its glow from smaller
  copies of the picture, which the port never made; now it does, on Metal and Vulkan, so the picture
  looks like on the Wii U again. The sun's corona and lens flare react to whether the sun is hidden
  (adapted from GreenNaugahyde's Android fork). New **Settings → Graphics → Effects → Bloom
  strength** (0–200%, default 100%). On Macs, colours now go through the display's colour profile, as
  with Vulkan.
- **Android:** phones whose GPU can't read the game's compressed textures (many Mali and PowerVR
  GPUs) now decode them on the GPU instead of showing black or broken textures; on Snapdragon phones
  you can install and select custom Vulkan drivers such as Turnip (**Settings → Graphics**; a driver
  that crashes or hangs in its first seconds falls back to the system driver on the next start). Both
  follow the approach of [GreenNaugahyde's Android fork](https://github.com/GreenNaugahyde/ZeldaWWHDRecompAndroid),
  rebuilt on our renderer. Not yet tested on real Mali/PowerVR/Snapdragon devices: reports welcome.
- **Run and swim faster** (optional, Mods tab, off by default): hold L3 (or toggle) to boost Link's
  running and swimming speed, 1.25x to 4x; works with true 60. The idea comes from GreenNaugahyde's
  Android fork.
- **Average frame rate** in the performance overlay (and GPU load and temperatures on Android where
  readable); **crash logs** now include the settings in use, with your user paths removed, and Android
  offers to share the log after a crash.
- **Vulkan:** the compressed-texture feature is now enabled as the Vulkan specification requires.
- **Screenshot key:** **F10** saves the TV picture as a PNG in a `screenshots` folder next to the save
  states (`~/Library/Application Support/wwhd/screenshots`, `%APPDATA%\WWHD\screenshots`,
  `~/.config/wwhd/screenshots`; `data/user/screenshots` in a release folder), named
  `WindWakerHD_YYYY-MM-DD_HH-MM-SS.png`. It is the frame shown when you press the key (also at 60/120/240
  fps), at the internal resolution and aspect ratio (2x at 21:9: 3414x1440), with the game's own effects
  and FXAA when on, but without the settings overlay or notices; "Screenshot saved" shows briefly. The
  key can be changed (or a controller button added) in Controls ("Photo"); the Saves tab has **Open
  screenshots folder** and an option to also save the GamePad screen (`..._GamePad.png`) while it is
  shown. Saving happens in the background: the game does not stutter. Both renderers, all platforms.

- **Frame interpolation fixes (60/120/240 fps and true 60):** the waving flags on Dragon Roost no
  longer turn into huge stretched polygons on the in-between frames (issues #36, #70); the boat's sail
  and yard and items Link carries (a bomb) no longer jump for single frames (issue #68); the stars in
  the night sky no longer wobble when the camera turns (issue #68). 30 fps is unchanged.
- **Fixed: the pause menu would not close at 120/240 fps** (issues #64, #74; probably also #73, no
  control after an item-get message). The HD screens (the TV pause screen and others) advanced on every
  drawn frame instead of once per game step, so the TV pause screen reopened itself right after
  closing. They now update once per step, which also gives their fades their 30 fps speed back at
  60/120/240 fps.

## v0.2.8

- **Fixed: the game could freeze at startup with 0 fps on some Macs** (issue #62, MacBook Air M1).
  Newer Apple compilers turned the game's spin-wait loops into endless loops that never saw the other
  core release the lock. Every loop in the game code now re-reads memory on each pass; no measurable
  speed cost.
- **Fixed: Android crashed when changing the aspect ratio or internal resolution on some phones**
  (issue #72, Adreno 830). Phones whose driver cannot blit depth buffers now copy them with a small
  draw instead; nothing aborts any more if a device can do neither. Thanks to @Blivii for the analysis
  and the patch.
- **Fixed: grid pattern in contact shadows at higher internal resolutions** (issue #66). The game's
  shadow and ambient-occlusion blur now covers the same area as on the console at every resolution;
  1x is unchanged and there is no measurable speed cost. Shadow maps still scale with the internal
  resolution for sharp shadows; `WWHD_SHADOW_FIX=1` keeps them at the console's 1024x1024 instead,
  for soft edges that never shimmer (issue #67).
- **More languages (experimental):** with your own dump of the European or Japanese game, the port can
  use its text, fonts and menus: German, Italian, British English, European French and Spanish, or
  Japanese. See [docs/language-packs.md](docs/language-packs.md).
- **Fan translations as content mods**, including **Arabic and Hebrew** drawn right to left (issue #60),
  see [docs/mod-manager.md](docs/mod-manager.md) and [docs/rtl-text.md](docs/rtl-text.md).
- **macOS: closing the TV window quits the game** (issue #65), as on Windows and Linux. During a game
  it asks first: **Quit**, **Cancel** or **Save State and Quit**. `WWHD_QUIT_PROMPT=0` turns the
  question off.
- **Faster on Linux and Steam Deck (Vulkan):** the guest buffer cache is now on by default on desktop
  Linux, as on macOS. It keeps unchanged vertex, index and uniform data on the GPU instead of copying it
  every frame, which removed a 20 fps lock in busy views on an RK3588 board (issue #50). If you see broken
  or flickering geometry, start with `WWHD_VK_BUFFER_CACHE=0` and please report it. Windows and Android
  stay opt-in (`WWHD_VK_BUFFER_CACHE=1`).
- **Save states are now small and can go into bug reports.** The **Save state** button (and
  Shift+F1–F5, the Save States menu) now writes a *portable* state, `slotN.wwstate`: a few KB with
  your progress (the same save data the game writes into `cking.sav`) and where Link stands (stage,
  room, position, facing, time of day). It contains no game code and no game data, so you can attach
  it to an issue; **Copy save for bug report** in the Saves tab copies the paths of the state and of
  your `cking.sav`. Loading one puts that progress into the running game and takes Link there; it is
  not an exact snapshot (enemies, a running cutscene and other actors start fresh). The old full
  states (the whole running game, ~300 MB, contain game data: never share them) are still there for
  debugging: Saves › *Full save states*, or `WWHD_FULL_SAVE_STATES=1`. Loading a slot loads either
  kind; an older full state in a slot is kept and the Saves tab says so. A portable state can only be
  saved while you control Link (not during a cutscene or dialogue); one saved on the boat puts Link
  back on the boat. See [docs/portable-save-states.md](docs/portable-save-states.md).
- **Fixed: "Quick doors" could crash the game going through a door** (issue #61, "PROGRAM HALT
  J3DPacket.cpp:157"; reported in Tingle's jail on Windfall). The extra game steps that make doors
  quicker also deleted actors (a pot, a rat, Tingle) faster than the game allows: an actor that
  removed itself while a door opened was freed before its last drawn frame was done with. Deleting
  now keeps its normal pace; doors are as quick as before.
- **Gyro aiming fixes** (issues #45 and #71): a new **Turn left/right by** setting (settings overlay →
  **Controls** → **Gyro…**) with the usual gyro conventions: **Player space** (default: turn the controller
  left or right about the real vertical, however you hold it), **Yaw** (its own vertical axis) or **Roll**
  (tilt it like a steering wheel). Before, rolling the controller turned the view and turning it depended
  on how you held it. The default sensitivity is lower (0.5x, about one to one with your controller; the
  range now goes down to 0.05x), and settings that still have the old default start at the new one.
  Gyro aiming now also works in **Pro Controller** mode. For reports that the gyro stops after a while:
  the port no longer stops when a controller's sensor timestamps stall, switches to the controller you
  move when several are connected, turns a controller's sensors on again when they fall silent, and logs
  `[gyro]` lines that say why motion stopped (including a drifting right stick, which makes the game
  ignore the gyro). "Recenter" is now **Recalibrate** (learns the gyro's offset anew).

## v0.2.7

- **Fixed: taking a picture with the Picto Box crashed the game (Vulkan)** (issue #53). Right after
  the shot the game renders small 3D colour-grading textures slice by slice, which the Vulkan renderer
  did not support; on Metal the preview was black. Both renderers now render into 3D textures, and the
  saved pictures show up in colour in the Picto Box album (they were black on both renderers before).
- **Fixed: black shadows on macOS (Metal)** (issue #47). When macOS had to compile the game's shaders
  from scratch (first start, or after a macOS update cleared its shader cache), the ambient-occlusion
  pass could be skipped at the title screen while its shader was still compiling; the light buffer was
  then created with the wrong layout and stayed wrong for the whole session, turning shadowed areas
  black until a restart. Render targets no longer depend on that timing, and draws whose result the
  game reuses are never skipped. A cold start now spends about 0.7 s more on the title screen once.
- **Windows setup without PowerShell** (issue #58): the setup no longer runs a PowerShell script or
  removes the "downloaded from the internet" mark, and `Wind Waker HD.exe` downloads nothing: the
  official, signed embeddable Python now ships in the release (the Windows zip grows to about 19 MB).
  The programs carry version information and a manifest. Some antivirus engines (BitDefender and
  engines using it) may still flag the unsigned exe; that is a false positive and has been reported.
- **120 and 240 fps** (settings overlay → **Graphics** → **Frame rate**, or the macOS Graphics
  menu): frame interpolation now also draws 3 or 7 blended frames between the game's 30 logic steps
  a second, for 120 Hz and 240 Hz displays. Camera, models, particles, sea and every other blended
  effect move at even steps between two game steps; input, sound and menus behave as at 30 fps.
  The game detects the display's refresh rate (ProMotion Macs: 120 Hz; Windows, Linux and Android:
  the monitor's or phone's current rate, and Android phones are asked for their fast mode) and the
  overlay shows it ("Your display: 120 Hz"). A rate the display cannot show is capped to it:
  240 fps on a 120 Hz display draws 120, and 120 fps on a 60 Hz display draws 60. **Keep game
  speed** is on by default at 120/240 fps: when the computer cannot draw every frame, the game
  still runs at full speed with fewer in-between frames. Measured on
  an M3 Max with its 120 Hz display (Outset Island): 120 fps draws 115 frames a second on screen
  with Vulkan and 108 with Metal, at 29.1–29.7 game steps a second; without presenting, 119.3
  frames and 29.9 steps. 240 fps is limited by the renderer on that machine (about 146 frames a
  second when not capped to the display), so on a 120 Hz screen it draws the same as 120 fps.
- **"Uncapped" debug switch** (settings overlay → **Graphics**; not saved): no frame limit and no
  vsync, to see how many frames a second your computer can draw (window title and performance
  overlay). The game counts frames, so it runs faster than normal while it is on: not for playing.
  Metal and Vulkan; `WWHD_UNCAPPED=1` turns it on at start.
- **Keep game speed recovers faster after a hitch** (all frame rates): one slow frame (a shader
  compile, a scene load) no longer turns the in-between frames off for several seconds.
- **Android:** full-size occlusion depth is off by default (it halved the worst GPU waits on an
  Adreno 830, issue #56); the settings overlay can still turn it on.
- **Smaller fixes:** cheaper reuse checks of upload memory on Macs and integrated GPUs (follow-up to
  the v0.2.5 fix for issue #44), time limits for the Android CI build, and "mouse as
  gyro" no longer loses part of a movement when a frame stalls.

## v0.2.6

- **Far fewer shader translations and pipelines, fewer stutters in new areas, less memory
  (Vulkan):** the renderer now keys a translated shader only on the state that actually changes its
  translation (for example, only the textures a shader samples and the vertex inputs and outputs it
  uses), so the same shader is no longer translated again and again for different render states.
  Starting from an empty shader cache, Outset Island needed 569 translations and 204 pipelines
  instead of about 6,100–6,700 and 3,200–3,500, and Windfall 829 and 236 instead of about 4,300–5,000
  and 2,300–2,700. That takes about 100–115 MB less memory, cuts the frames that take over 50 ms
  by about a third, and lowers render-thread CPU time by 4–12%. It supersedes PR #46 by rhemfur,
  whose idea (keys that translate to the same shader share it and its pipelines) is part of it.
- **Performance reports say which build and system they come from** (issue #44): **Copy performance
  report** now starts with the version and commit, the operating system and version, the graphics card
  with its driver and Vulkan version (or the Metal device), and the rendering switches that are not at
  their defaults (buffer cache, CPU paths turned off, lazy DrawDone / async present turned off, the
  gyro source). The log's first lines name the version and system too, so crash logs carry them.

## v0.2.5

- **Fixed: much lower frame rate on Windows and Linux PCs with a dedicated graphics card (Vulkan)**
  (issue #44). Three renderer shortcuts that v0.2.4 turned on for all platforms compared new vertex
  and uniform data against the previous copy in the GPU's upload memory; reading that memory back is
  very slow on AMD and NVIDIA cards (one report went from 30 fps in v0.2.1 to 12 fps). The renderer
  now keeps those comparison copies in normal memory and never reads GPU upload memory, which also
  speeds up an older index-data path when the buffer cache is off.
- **Gyro aiming** (issue #45; settings overlay → **Controls** → **Gyro…**): aim the bow, hookshot,
  boomerang, telescope, Picto Box and grappling hook in first person by moving your controller, as
  with the Wii U GamePad. Sources: the gyro of a **DualSense, DualShock 4, Switch Pro, Joy-Con or
  Steam Deck** controller, a **Cemuhook (DSU)** server (DS4Windows, BetterJoy, phone apps;
  127.0.0.1:26760 by default), or the **mouse** (for Steam Input's "gyro to mouse"). Sensitivity,
  invert and a recenter button are adjustable. Off by default; the game's own **Options → Gyro**
  switch still applies. See `docs/gyro.md`.

## v0.2.4

- **Smoother 60 fps on slower PCs (Vulkan):** the render thread no longer waits for the GPU after
  every frame on Windows, Linux and macOS (the way Android already worked), so CPU and GPU work in
  parallel. Where the render thread is the limit, this is the difference between slow motion and
  full speed: in our load tests the game went from 35–40 fps with only a third to half of the 60 fps
  in-between frames drawn to a steady ~59 fps with almost all of them, at full game speed. Without
  a frame limit it renders 13–53% more frames per second, depending on the scene. (Issues #7, #44)
- **Less CPU work per frame (Vulkan):** 15 renderer shortcuts that were Android-only are now on
  everywhere (8–14% less render-thread time), and on macOS vertex, index and uniform data the game
  does not change stay on the GPU instead of being copied again for every draw (another 6–17% less
  render-thread time and 43–64% less data uploaded per frame). Windows, Linux and Android players
  can try that cache with `WWHD_VK_BUFFER_CACHE=1`; `docs/vulkan.md` ("Guest buffer cache") says
  what to report.
- **Performance report** (settings overlay → **Graphics** → **Copy performance report**): copies a
  breakdown of the render thread's time per frame to the clipboard, for bug reports about speed.
- **macOS: setup works when the app is opened straight from the downloaded folder** (issue #48).
  Every setup error now says what failed, what to do, and where the log is.
- **Sound in GamePad-only mode** (Off-TV Play, the Minus button): the game's sound now plays through
  your speakers instead of going silent (issue #49).

## v0.2.3

- **Mod manager** (settings overlay → **Mods**): the built-in mods (direct and mouse camera,
  first-person shortcut, wall climbing, quick doors, fast scenes) in one searchable list, plus
  **installable mod packages** from a folder or a `.wwhdmod` ZIP, with profiles, dependencies and
  per-mod options. Everything starts off; nothing from a package loads until you enable it.
  - **Content mods** replace game files without touching your game folder.
  - **Cemu graphics packs** (`rules.txt`) can be imported, with their presets and resolution rules;
    shader packs need the Vulkan renderer. Code patches from Cemu packs are not supported.
  - **Native mods** (packages with their own compiled code) ask for a one-time confirmation before
    they are enabled, because they run with the game's full permissions; only enable mods from
    sources you trust.
  See `docs/mod-manager.md` for the package format and the mod SDK.

## v0.2.2

- **Controller rumble fixed** (issue #35): rumble now follows the game's patterns exactly and always
  stops: on quit, after a crash, when the game hangs, while the settings menu is open and when the
  window is in the background. Before, a controller could keep vibrating until it was switched off.
  New **Rumble** on/off option in the settings overlay (Controls tab).
- **Older graphics drivers** (issue #37): Windows/Linux builds no longer refuse to start with
  "Entry Point Not Found" on drivers without Vulkan 1.3; they use the `VK_KHR_dynamic_rendering`
  extension where the driver offers it, and otherwise show a clear message naming the GPU, its
  driver version and what is missing.
- **Language tab:** only the languages your game contains can be chosen (the USA version has
  English, French and Spanish), and a changed language says that it applies after a restart.
- **Textures update exactly:** textures the game changes in memory are now always re-uploaded (on
  Metal and Vulkan); before, a change could show up a few frames late.
- **Full screen is remembered on Windows and Linux too** (issue #43), as it always was on macOS:
  leave the game in full screen and it starts in full screen next time.
- **Better crash logs:** a crash outside the game code names the library it happened in (for
  example the graphics driver), and the log lists the Vulkan layers that overlays add, so crash
  reports can be answered much faster.

## v0.2.1

- **Cemu archives (`.wua`)** (issue #27): the setup now also takes a Cemu `.wua` file; it is
  already decrypted, so no keys are needed. Every source (disc image, `.wua`, extracted folder) is
  checked for the right game version first (USA, version 0), with a clear message if an update is
  merged in or the region is different.
- **Linux on arm64** (aarch64): a separate `linux-aarch64` download (Raspberry Pi 5, Asahi Linux, ARM
  laptops).
- **On-screen text entry** (issue #29): the name screen now shows a text window over the game, with an
  on-screen keyboard for controllers and the mouse; typing on the keyboard goes straight into it.
- **Boot crash fixed:** the rare crash right after start ("Prepare Thread", agl shader setup) is gone.
  GX2CopySurface now completes before it returns, as the game expects; before, a late render thread
  could write into memory the game had already reused (about every 15th start under load, every start
  on some phones).
- **GamePad / Pro Controller choice is saved** between launches (issue #26).
- **Cheats** now also work on saves with heart pieces (PR #25 by Sean13128).
- **Android** (rhemfur, PRs #30, #31): pipelines the Adreno driver refuses no longer close the game,
  the game threads use at least two fast cores, and arm64 builds treat `char` as signed, as on the
  console.

## v0.2.0

- **Portable releases.** Unzip anywhere and start **Wind Waker HD**: the first start prepares
  the game once from your own dump (releases never contain game code, so it is built on your computer);
  later starts launch the game directly. Everything — the built game, the game files, saves,
  settings, save states, shader caches, logs and the downloaded compiler — stays in the release
  folder; nothing goes to your user folders unless you ask for a shortcut. An extracted game folder is
  used where it is instead of being copied. Saves and settings can be copied over from an earlier
  installation. Hold Shift while starting (or `--setup`) to repair, update or change the game.
- **Settings overlay** (Dear ImGui): an in-game menu for everything in one place — save states and
  Crash Recovery, graphics (renderer, frame rate, resolution, aspect ratio, AO, filtering, FXAA,
  performance overlay), display, gameplay mods and cheats (Graphics also has the Vulkan presentation mode), controls and language. Open it with
  **F1** (Fn+F1 on most Mac keyboards), **Cmd+,** / *Settings…* on macOS, or hold **Select** / press
  **Home** on a controller; it works with mouse, keyboard and controller on every platform and
  renderer. The Controls tab shows the controller drawing with live feedback of pressed buttons.
  Shift+F1 still saves state slot 1; slot 1 now loads from the overlay.
- **GamePad screen modes on Windows/Linux** (overlay → Display): separate window, picture-in-picture,
  automatic overlay, off, or GamePad only, as on macOS; clicks on the GamePad picture reach the game.
- **Full screen is remembered on Windows/Linux too** (issue #43): the TV window starts as it was left,
  in full screen or in a window, as the macOS app always did. `WWHD_FULLSCREEN=0|1` overrides it for
  one start.
- **60 fps "Keep game speed"** (overlay → Graphics → Frame rate, PR #21 by rhemfur): skips in-between
  frames instead of slowing the game down when the machine can't draw 60 frames a second; off by
  default. The performance overlay shows the share of in-between frames drawn.
- **Android: build it yourself** (rhemfur's port): see
  [Android (build it yourself)](README.md#android-build-it-yourself). Also by rhemfur (PRs #19–#22, #24): the game's own icon for windows and shortcuts, name typing in every game window and optional
  Vulkan paths for slow devices.
- **Performance pass** (PR #15 by Sean13128): much less render-thread CPU on Metal (no more stutter
  while the shader cache warms up), a lighter vsync wait on Vulkan, and a fix for the both-renderer
  build crashing with Homebrew boost installed.
- **Cheats** (Gameplay menu / overlay, PR #15): all items, best sword and shield, 20 hearts, double
  magic, 5000 rupees, infinite health/magic/ammo, and story cheats (songs, Triforce shards, dungeon
  items, keys) — use a spare save file for those.
- **Graphics options are remembered** between launches (macOS since PR #15; Linux/Windows in
  `settings.ini`).
- **Controller rumble** (PR #13 by arcadematicas).
- **Linux/Windows**: GamePad touch with the mouse in the GamePad window, F11 / Alt+Enter full
  screen, closing the TV window quits (closing the GamePad window hides it), the name-entry text
  prompt works again (PR #14 by rhemfur), 1 ms timer resolution on Windows for smoother frame pacing
  (PR #12 by rhemfur), and a `--unwindlib=libgcc` build note for clang setups with libunwind.
- **Console language**: `WWHD_LANGUAGE=<code>` (or the overlay's Language tab) picks the game's
  language from those on the disc.
- **Native Windows LLVM builds** (PR #17 by resadent): build with clang and Visual Studio's Windows
  SDK, without MSYS2 (missing dependencies are built from pinned sources); smoother Vulkan frame
  pacing on Windows via SDL's high-resolution sleeps.
- **Vulkan presentation mode** (overlay → Graphics): *Vsync* (FIFO, default), *Low latency*
  (MAILBOX, where the driver offers it) or *Off* (IMMEDIATE); switches live and is remembered.
  `WWHD_VK_PRESENT_MODE` overrides it.
- **Faster Vulkan on Windows/Linux** (PR #18 by resadent): bounded draw batching and a higher
  game/render thread priority are now on by default, as on macOS.
- **GameCube save converter** (`tools/savegame`): bring your GameCube save file into HD — see
  [Optional: bring your GameCube save to HD](README.md#optional-bring-your-gamecube-save-to-hd).

## Before v0.2.0

- **Linux and Windows builds** (Vulkan renderer with an SDL3 host), with automatic CI builds for both.
  Fixes from the first Linux reports: game paths are resolved case-insensitively (the game asks for
  `Audiores`, the disc folder is `AudioRes`; this crashed the game right after startup), build fixes
  for newer compilers, `WWHD_NO_GAMEPAD` only hides the GamePad window (`WWHD_NO_CONTROLLERS` turns
  off controllers), and a hint where to type when the game asks for text.
- **Crash logs and Crash Recovery**: every crash writes `captures/crash-<time>.log`. Crash Recovery
  (Save States menu, off by default) keeps automatic save states plus the recorded input, so a crash
  can be reproduced with `WWHD_REPLAY=<n>`.
- **`wudextract.py`**: the disc key file can be 16 raw bytes or 32 hex digits, with clear errors for
  a missing or non-matching key.
- **True 60 (key 7, experimental)**: every 30 Hz step is now exactly the 30 fps game's step (game
  logic, saves and quests stay as in the original); hookshot crash fixed. For smooth 60 fps,
  interpolation (key 6) is the recommended mode.

- **Vulkan renderer** (by OpenAI Codex), built into the same app next to Metal. Pick one in
  **Graphics › Renderer**; the choice is saved and used from the next start ("Restart Now"
  relaunches right away). Both share the same windows, menus, display modes, controls and mods.
  If Vulkan can't start (no Vulkan loader or MoltenVK installed), the game falls back to Metal and
  says why. Details: [docs/vulkan.md](docs/vulkan.md).
- **Full screen and GamePad screen modes** (Display menu): full screen for the TV window (⌘F),
  picture scaling (smooth, sharp, integer), and the GamePad screen as its own window, a
  picture-in-picture overlay, an automatic overlay that pops up when the GamePad picture changes,
  or off (⌘G shows/hides it).
- **Aspect ratio** (Graphics › Aspect ratio): 16:9 (original), match the window, 16:10, 21:9 or
  32:9. Wider screens see more to the sides (same vertical view); the HUD stays at the edges and
  menus stay centred.
- **Fixes**: misplaced Yes/No cursor in text boxes at 16:10, quitting with ⌘Q could hang, garbled
  characters in the window title. Community fixes from pull requests #1 and #2 (Miiverse manager
  throttling, shared shader-cache memory) are included.

- **60, 120 and 240 fps.** Modes in the Graphics menu and the settings overlay (Graphics › Frame rate):
  - **60 fps (key 6), 120 fps, 240 fps**: frame interpolation. The game logic keeps its original
    30 steps per second; the frames in between (1, 3 or 7 per step) are drawn blended between two
    steps (camera, models, particles, sea, wave crests, grass and trees, cloth, weather, lighting).
    Input, sound and menus behave as at 30 fps. The frames reach the screen up to the display's
    refresh rate (the overlay shows it, e.g. "Your display: 120 Hz"). "Keep game speed" (on by
    default at 120/240 fps, off at 60) skips in-between frames the computer or display cannot show
    instead of slowing the game down.
  - **True 60 (key 7, experimental)**: Link and the follow camera run their logic at 60 steps per
    second (for the actions that have been converted and measured against the original); everything
    else runs at 30 and is interpolated.
- **Higher internal resolution** (1x / 1.5x / 2x / 3x, key R) and **edge smoothing** (FXAA, key 8).
- **Save states**: a Save States menu with 5 slots (Shift+F1–F5 save, F1–F5 load), kept across
  sessions in `~/Library/Application Support/wwhd/states/`.
- **Controls window** (Input › Controls…): a drawing of the Wii U GamePad or Pro Controller; click
  a button to remap it to a key or a controller input, live feedback of pressed buttons and stick
  positions, conflict warnings.
- **Optional gameplay mods** (Gameplay menu, all off by default): climb any wall, direct right-stick
  camera, mouse camera, first person on the mouse wheel, quick doors, fast scene changes.
- **Fixes**: shadow streaks, flicker after loading, doubled wave sounds at 60 fps, camera issues.
- **Tools**: function naming against the GameCube decompilation (`tools/decomp/`), a differential
  harness that verifies hand-written source against the recompiled original (`tools/verify/`), and
  the 60 fps conversion tools (`tools/true60/`). See "Optional: decompilation tools" below.
