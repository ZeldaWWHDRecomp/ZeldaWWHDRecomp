# Apple TV / iPhone / iPad setup: how it works

`setup.py --apple-device tv|iphone` (`tools/installer/apple_device.py`) builds the game for an Apple TV, iPhone or
iPad from the player's own game and installs it on a paired device. Using it: [apple-devices.md](apple-devices.md).

## What the release carries

A release folder made with `tools/release/package.py --apple-build <tvOS or iOS build>` (no release job builds
these yet; `tvos/build_test_package.sh` makes a test release) holds each device runtime in `sdk/apple/tvos` and
`sdk/apple/ios`:

- the objects and libraries;
- a `manifest.json` with the link recipe and game-code flags;
- the app's `Info.plist`.

They are built against placeholder game code, so the release holds no game code or files, as on the Mac.

## The build

Each stage reports a step (`BUILD_STEPS`):

1. **Game code.** The game code from setup's translation cache is compiled for the device with Xcode's SDK. The
   objects go to the same object cache as the Mac's, keyed by compiler and flags.
2. **Link.** The objects are linked with the release's runtime.
3. **Game files.** The files are copied in with `cp -c` (APFS clones, so no extra space).
4. **Shader library.** A Metal warm of the Mac's game writes its shaders' sources, and
   `shader_library.py --sdk appletvos|iphoneos` compiles them (Xcode 26 or newer). Shaders the Mac's game hasn't
   met compile on the device when first drawn.
5. **Icon.** Made from the game's boot image (`tvos/make_icon.py`, `actool`).
6. **Code mods.** The mods switched on in the Mac's game are built in (`tvos/bundle_mods.py`). A mod without a
   guest address yet first gets one from a short start of the Mac's game; one that still has none is left out
   and switched off in the app.
7. **Signing.** The app is signed with the team's development profile (`find_profile`). When the App ID, the
   iCloud container or the device isn't registered yet, a throwaway Xcode project registers them with
   `-allowProvisioningUpdates` (`register_with_xcode`).

`Info.plist` carries the release version, a build stamp and the app's mods (`WWHDMods`), so the app on a device
can be compared with the one built here. The setup log never shows the team or signing identity in full.

## What the setup app asks

The script answers everything the setup app shows. The same functions serve the command line and the GUI protocol
(`setup.py --gui-protocol`):

| Question | GUI request | Command line | `apple_device.py` |
|---|---|---|---|
| Can each app be built and installed here, and what's missing? | `apple_status` | `--apple-status` | `report()` |
| One device: is the game running, which version is installed, which mods an install switches off or on | `apple_device_state` | `--apple-status --apple-device tv --apple-device-id NAME` | `device_state()` |
| Build (and sign) the app | `apple_build` | `--apple-device tv --apple-no-install` | `build_app()` |
| Install the built app (refused while the game runs there) | `apple_install` | `--apple-device tv --apple-install-only` | `install_app()` |
| Copy or back up the save | `apple_save` | `--apple-save backup\|to-device\|from-device` | `save_to_device()` and so on |

Each check is `{ok, text, fix}`.

## Saves

The app keeps its data in its container:

- **Apple TV:** in `Library/Caches`, as tvOS allows.
- **iPhone and iPad:** in `Library/Application Support`.

**Setup's copies** (`devicectl device copy`) back up whatever they replace on the Mac first, in `data/apple/backups`.
Because devicectl can't remove files, a copy to the device ends with a list of its files (`save/setup-copy.txt`),
and the app removes the save files that aren't on it when it next starts.

**With an iCloud container** (`--apple-icloud-container`), the app is signed with CloudKit, plus iCloud Documents on
iPhone and iPad:

- **CloudKit** (`runtime/src/gfx/cloud_save_uikit.mm`): the save is a manifest record and one record per distinct
  file content. A sync is decided by content hashes and the hash this device last synced
  (`cloud_save_sync.h`), never by clocks.
- **iCloud Drive** (iPhone and iPad): the save folder is the container's "Wind Waker HD" folder, which a Mac's
  game can use as its save folder.
