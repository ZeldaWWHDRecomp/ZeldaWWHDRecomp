# Trying the Apple TV, iPhone, iPad and Vision Pro build (team testing)

This is experimental and not part of any release. Setup can build the game for your own Apple TV, iPhone, iPad or
Apple Vision Pro from your game and install it there. To try it, build a test release folder from this branch, then run setup from
that folder. The app is made on your Mac from your game, so it is for your own devices only: don't share it.

## What you need

- A Mac with Apple silicon, with your game dump.
- **Xcode 26 or newer**, with the tvOS, iOS and visionOS platforms (Xcode > Settings > Components; also the Simulator for
  your device, which draws the app's icon), CMake, Ninja and Python 3.
- **A paid Apple Developer account** signed in to Xcode (Xcode > Settings > Accounts). Free accounts aren't
  supported yet.
- The device **paired with the Mac** (Xcode > Window > Devices and Simulators). An Apple TV pairs over the network
  (Settings > Remotes and Devices > Remote App and Devices on the TV). An iPhone or iPad pairs with a cable and
  needs Developer Mode; an Apple Vision Pro pairs over the network and needs Developer Mode too.
- Apple TV 4K (2022, A15) or newer, an iPhone or iPad on iOS / iPadOS 17 or newer, or an Apple Vision Pro on
  visionOS 26 or newer (tested on 27). The A15 Apple TV runs the game, but only just.

## 1. Build the test release folder

```sh
tvos/build_test_package.sh            # into build/apple-test; or pass another folder
```

This builds the Mac runtime and the tvOS, iOS and visionOS runtimes against placeholder guest code (no game is needed for
this step), then packages them into a release folder. It prints the folder's path at the end.

## 2. Set up the Mac game from that folder

In the release folder, start `Wind Waker HD` and point setup at your game, as for any release. Start the game
once, play for a bit, and switch on the code mods you want on the device. The device's shader library is made from
the shaders this Mac's game has already seen, so a game that has been played gives a much smoother start.

## 3. Build and install on the device

**In the setup window:** hold Shift while starting Wind Waker HD (or start it with `--setup`), then choose
**Apple TV, iPhone, iPad or Vision Pro...**. The page checks Xcode, signing, the game and your mods. Choose the kind of
device, **Build the app** (a few minutes), choose the device, then **Install on the device**.

**In Terminal**, from the release folder:

```sh
python3 tools/installer/setup.py --apple-status                    # what's ready, as JSON
python3 tools/installer/setup.py --apple-device tv                 # build and install on the Apple TV
python3 tools/installer/setup.py --apple-device iphone --apple-device-id "My iPad"
python3 tools/installer/setup.py --apple-device vision             # build and install on the Vision Pro
python3 tools/installer/setup.py --apple-device tv --apple-no-install   # build only
python3 tools/installer/setup.py --apple-device tv --apple-install-only # install the app built last
```

Setup won't install while the game is open on the device: save and quit it there first. Saves and settings on the
device are kept when you install a new build.

## Your app identifiers and iCloud saves

The apps are `<prefix>.tv`, `<prefix>.ipad` and `<prefix>.vision` (default prefix `org.wwhdrecomp`). Use your own: on the setup page
open **App identifiers**, or pass `--apple-bundle-prefix com.example.wwhd`. Setup remembers it.

To switch devices with one save, enter an iCloud container name of your own on the setup page (or
`--apple-icloud-container iCloud.com.example.wwhd`). The build has Xcode register the App IDs, the container and
your devices in your team, so there is nothing to set up by hand.

- **Apple TV and Vision Pro:** the save goes through CloudKit (their apps get no iCloud Drive).
- **iPhone and iPad:** the save lives in iCloud Drive's "Wind Waker HD" folder and also goes through CloudKit. The
  Mac's game has only the folder (Settings > Saves > Save folder:
  `~/Library/Mobile Documents/iCloud~com~example~wwhd/Documents/Wind Waker HD`) and the Apple TV only CloudKit, so
  an iPhone or iPad is what carries a save between them. Only the game's own save files (`cking*.sav`) are
  synced: iCloud Drive's conflict copies ("cking 2.sav") are left alone.
- **At start** the game compares its save with iCloud's. The side that changed since this device last synced is
  used. If both changed, you're asked which to keep, and the other goes to save-backups (the newest 10 are kept).
  A "Checking your save in iCloud..." message shows while it looks.
- **The Mac and the Apple TV** meet through an iPhone or iPad start in between, or the setup page's Saves buttons.
  After a copy through iCloud Drive, check in the Files app that it has finished syncing before you play.

Changing the prefix installs a new app next to the old one: the old app and its save stay until you delete it.
The setup page's Saves section takes the save and settings from it ("Take the save and settings from ...").

## Apple Vision Pro

The game window shows the TV picture in 3D: the game draws a picture for each eye, and the HUD, prompts and
dialogs float just in front of whatever is nearest in view. Settings > 3D has the depth, the HUD distance and
"Behind the glass", which keeps the whole scene behind the window's surface. The GamePad has a window of its own,
where a pinch is a touch; the GamePad button on the right of the game window opens it again. The sound is 5.1,
spatialized by the headset. Closing the game window ends the game (after saving to iCloud).

## Saves

The setup page's Saves section (or `--apple-save backup|to-device|from-device`) copies the save between this Mac
and the device's app, and backs the device up. Whatever a copy replaces is backed up on the Mac first, in the
install's `data/apple/backups`. The game must be closed on the device.

Without iCloud, back up the Apple TV now and then: tvOS keeps an app's files in a folder it may empty when the
TV runs short of space, and the save lives there.

## Mods

The code mods switched on in the Mac's game are built into the app. A mod that has never run on the Mac gets its
place in memory when the build briefly starts the Mac's game. Mods on the device that aren't in the new app are
switched off when you install, and the setup page warns you first.

## Known limits

- Shaders the Mac's game hasn't met are compiled on the device when they first appear (a stutter).
- The Apple TV's and Vision Pro's iCloud saves are new: tell us how they go.
- Vision Pro: 3D costs about a third more of the render thread's time.
- Provisioning profiles expire after a year: build and install again then.

Please report what you find, with the device, the setup log and what you did. How it works:
[apple-device-installer.md](apple-device-installer.md).
