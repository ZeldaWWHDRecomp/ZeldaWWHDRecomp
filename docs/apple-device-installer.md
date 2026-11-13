# Apple TV / iPhone / iPad installer: notes

Working notes for `setup.py --apple-device tv|iphone` (`tools/installer/apple_device.py`) and the release side
(`tools/release/package.py --apple-build`). Prototype on a fork first, then a discussion with the maintainers.

## Decisions

- **Paid developer team first.** Signing uses the team's development profile that Xcode makes (a wildcard
  "tvOS / iOS Team Provisioning Profile: *", a year). Free Apple IDs (7-day apps, 3 apps, a profile per bundle
  ID) come later.
- **Both apps:** the Apple TV app and the iPhone / iPad app (one iOS build for both).
- **A setup command first** (`--apple-device`, `--apple-device-id`, `--apple-team`, `--apple-bundle-prefix`,
  `--apple-no-install`, `--apple-unsigned`); a button in the setup app once it works.
- **Same model as the Mac release:** the release ships the device runtimes built against placeholder game code
  (`sdk/apple/tvos`, `sdk/apple/ios`: objects, libraries, `manifest.json` link recipe and game-code flags,
  `Info.plist`); setup compiles the player's own translated game code for the device and links it. No game code
  or files in the release.

## Entry points (what the setup app asks)

The script answers everything the setup app shows; the app only displays it and asks for the work. The same
functions serve the command line (JSON) and the GUI protocol (`setup.py --gui-protocol` requests):

| Question | GUI request | Command line | `apple_device.py` |
|---|---|---|---|
| Offered at all? (a Mac, a release with the device runtimes) | `hello`: `apple_offered` | | |
| Can each app be built and installed here, and what's missing (with what to do)? Xcode, the SDK, the icon's simulator runtime, the Metal compiler, signing (profile, team, identity, expiry), this Mac's game and shaders; which mods go in, which are left out and why; the app built here last; the devices | `apple_status` | `--apple-status` | `report()` |
| One device: is the game running there (or can't that be checked), which app version is installed, which mods an install would switch off or on | `apple_device_state {app, device}` | `--apple-status --apple-device tv --apple-device-id NAME` | `device_state()` |
| Build (and sign) the app, in steps | `apple_build {app}` (step / log events) | `--apple-device tv --apple-no-install` | `build_app()` |
| Install the built app on a device (refuses while the game runs there) | `apple_install {app, device}` | `--apple-device tv --apple-install-only` | `install_app()` |

Each check is `{ok, text, fix}`: the text says what was found, the fix what to do (only when not ok). The app's
`Info.plist` now carries the release version (`CFBundleShortVersionString`), a build stamp (`CFBundleVersion`) and
its mods (`WWHDMods`), so the installed app on a device can be compared with the one built here.

## How the build goes (what works)

Tested 2026-11-12: a test release packaged on the build Mac (`package.py --apple-build` with tvOS and iOS builds
against `stubgen` code, Metal-only Mac build, `--no-rando`), set up on an M1 Mac with Xcode 15.4 from its extracted game.

- Release size: +46 MB per device runtime (403 objects, 7 libraries each).
- Translation comes from setup's translation cache; the device's compiled objects go to the same object cache
  (keyed by compiler and flags), so a second device build only links. tvOS compile + link from scratch on an M1:
  about 70 s.
- Xcode 15.4 (tvOS 17.5 SDK) compiles and links the tvOS app fine.
- The game files go in with `cp -c` (APFS clones: no extra space); the app is 1.8 GB.
- Building on one Mac and signing on another works: `--apple-unsigned`, then sign where the certificate is.
- `find_profile` picked the laptop's "tvOS Team Provisioning Profile: *" and its Apple Development identity; `sign`
  and `codesign --verify` passed; the running check (`running()`) answered correctly for the connected TV.

- **Installed and played on the Apple TV (2026-11-12):** the app setup built on an M1 Mac with Xcode 15.4 (release runtime +
  that Mac's own translated and compiled game code), with the shader library and icon added on another Mac
  (see below) and signed there; it starts, plays and loads the save.

## Problems found, and what to do about them

| Found | Cause | To do |
|---|---|---|
| "No development profile for tvOS apps" on an M1 Mac with Xcode 15.4 | no tvOS profile there (only an old iOS one), and no signing identity at all: the certificate's key is only on the Mac that made it | Instructions: sign in to Xcode (Settings > Accounts) on the Mac that runs setup, so Xcode makes a certificate and the profiles. The error message already says so; it could also list the identities it found. |
| "The shader library needs Xcode 26 or newer" | the safe math mode (`-fmetal-math-mode=safe`) is Xcode 26+; that Mac has Xcode 15.4 | Require Xcode 26 for the Apple TV (no on-device shader compiles there). For iPhone / iPad maybe allow building without it, with a warning (shaders compile during play). |
| `make_icon.py` fails: "Failed to locate any simulator runtime matching ... appletvsimulator 17.5" | `actool` compiling a tvOS asset catalog needs a tvOS **simulator runtime** installed | Either tell players to add the simulator runtime (Xcode > Settings > Components), or ship a prebuilt `Assets.car` from the release build (project icon) and only replace the image files, or make the icon without `actool`. |
| A code mod "has no guest address yet" stopped the whole build (story notebook, installed by setup, never started) | a mod gets its guest address the first time the Mac game starts with it | Done: `bundle_mods.py` leaves such a mod out with a message and switches it off in the app's profile. Also: setup could start the game once headless to place new mods, or say so up front. |
| setup's shader preparation skipped in a fork's release | the shader template is a private payload (`fetch_shader_template.py`) | Official releases have it. The device step needs a prepared shader generation (for the Metal warm that writes the shader sources); without one it says to prepare shaders first. Tested by copying that release's generation in. |
| The shader sources for the device library aren't kept on the Mac | shader preparation deletes them after building the Mac's `shaders.metallib` | The device step runs its own Metal warm of the Mac game (`WWHD_SHADERLIB_SOURCES`), then `shader_library.py --sdk appletvos / iphoneos`. Untested end to end yet (needs Xcode 26). Check that the Mac's MSL is right for the device (the TV app's library so far came from the Mac's sources and works). |
| `devicectl list devices` lists extra "Apple TV 4K (3rd generation)" entries next to the real Apple TV; `info processes` fails on them ("Stream Process List is not supported") | they are **simulators** (`hardwareProperties.reality: simulated`), not old pairings. `tunnelState` is no guide: a working Apple TV shows "disconnected" (it connects on demand); one out of reach shows "unavailable" and has no `transportType` | Done: only physical devices are listed, each with `reachable` (a transport and not "unavailable"); only reachable ones can be chosen. |
| Installing an app built from a fresh Mac install would switch off the TV's code mods (QoL) | the app's ModManager seed replaces the device's profile when the seed changes (`install_bundled_mods`) | Done (reported): `device_state` reads the device's profile (`Library/Caches/user/ModManager/profiles.json` in the app's container) and lists the mods an install would switch off or on. Maybe later: merge instead. |
| `zsh` doesn't split `$VAR` in remote commands | (our scripts, not the installer) | Use bash scripts with arrays for multi-argument options. |

## Still to check

- The whole flow on a Mac with Xcode 26 and a signing identity: shader library, icon, signing, `devicectl`
  install, the "game is running" check (it reads `devicectl device info processes` and refuses to guess).
- The iPhone / iPad app the same way.
- Done: `bundle_mods.py`'s "left out" lines are logged on every build, and `report()` lists them (with the reason)
  before building (`bundle_mods.plan()`).
- The setup app's screen for these requests (tools/installer/gui/setup_gui.cpp): not written yet.
- `Info.plist`: the release's copy says `org.wwhdrecomp.tv`; setup replaces the bundle identifier only. iCloud
  (the iPad's save in iCloud Drive) needs the team's own container: not offered yet.
- Pipeline / shader identity: does the device app accept the Mac's `shaders.bin` from another runtime build?
- Updating: a new release means a new app build; the device keeps its saves (app container) across installs.
