# iPhone GamePad (experimental)

Play Wind Waker HD on your Mac and TV with an iPhone as the Wii U GamePad. Attach a controller to the
phone (Backbone, Kishi, Razer, or a paired DualSense or Xbox controller). The phone sends the game:

- the controller's buttons and sticks: a controller in the game's Controls, named **iPhone GamePad**,
  with its own bindings like any other controller;
- touches on its screen: the GamePad's touch screen (items, map, sea chart);
- its gyro: GamePad aiming (bow, hookshot, telescope, Picto Box).

This first version does not show the GamePad picture on the phone yet. Keep the game's GamePad window
(or picture-in-picture) on the Mac or TV and touch the matching spot on the phone. Video, sound and
rumble come next (see Roadmap).

## Use it

1. **The game, on the Mac:** set `WWHD_REMOTE_PAD=1` for apps you open, then start Wind Waker HD as
   usual:
   ```sh
   launchctl setenv WWHD_REMOTE_PAD 1      # later: launchctl unsetenv WWHD_REMOTE_PAD
   ```
   (A build of your own: `WWHD_REMOTE_PAD=1 build/wwhd`.) The log says `[remote pad] waiting for the
   iPhone GamePad on UDP port 26770`. Allow incoming connections if macOS asks.
   `WWHD_REMOTE_PAD=<port>` picks another port.
2. **Gyro:** Settings > Controls > Gyro... > **Device gyro (phone/tablet)**.
3. **The phone,** on the same Wi-Fi network: open GamePad, allow local network access, and tap the Mac
   under *Games on this network*. If it doesn't appear, enter the Mac's address (System Settings >
   Wi-Fi > Details) under *Or enter the computer's address*.
4. Hold the phone in landscape in the controller. The status line shows the Mac's name. **Disconnect**
   (or leaving the app) gives the GamePad back to the Mac's keyboard and controllers.

Only one phone can be the GamePad at a time. While the game listens, anyone on the local network can
send it GamePad input, so set `WWHD_REMOTE_PAD` only on networks you trust.

## Build it

You need Xcode 16 or newer and [XcodeGen](https://github.com/yonaskolb/XcodeGen) (`brew install xcodegen`).

```sh
cd ios
xcodegen                       # writes WWHDGamePad.xcodeproj (not committed)
open WWHDGamePad.xcodeproj
```

Choose your team under Signing & Capabilities (a free Apple ID works; its builds expire after 7
days), pick your iPhone and press Run. From the command line:

```sh
xcodebuild -project WWHDGamePad.xcodeproj -scheme WWHDGamePad -destination 'generic/platform=iOS' DEVELOPMENT_TEAM=<team id>
```

Tests: the protocol tests compare the phone's packets with the game's own encoder byte for byte.

```sh
xcodebuild test -project WWHDGamePad.xcodeproj -scheme WWHDGamePad -destination 'platform=iOS Simulator,name=iPhone 15 Pro'
```

On the game side, `remote_pad_test` (packets, controller mapping, the server over loopback) and
`motion_test` (the phone's gyro) are CTest tests.

For testing, `-connect <address>` as a launch argument connects at once (Xcode: Product > Scheme >
Edit Scheme > Arguments; `xcrun simctl launch <device> org.wwhdrecomp.gamepad -connect 127.0.0.1`
in the Simulator, against a game on the same Mac).

## How it works

The protocol is documented in [`runtime/src/remote_pad/protocol.h`](../runtime/src/remote_pad/protocol.h).
It uses UDP on port 26770, advertised over Bonjour as `_wwhd-gamepad._udp`. The phone says Hello until
the game welcomes it, then sends its whole state about 125 times a second (8 ms). A lost packet costs
one sample. The game answers once a second. Each side treats silence as a disconnect: 1.5 s on the
game, 3 s on the phone, which then says Hello again.

| Phone | Game |
| --- | --- |
| `GamePadLink.swift`: Bonjour, connection, 125 Hz sender | `remote_pad/server.cpp`: one phone at a time, sessions, newest input only |
| `Inputs.swift`: GameController, CoreMotion, touch | `remote_pad/remote_pad.cpp`: Bonjour record, controller values, gyro feed |
| `Protocol.swift` (mirrors `protocol.h`) | `gfx/input.mm`: the phone as a controller and the touch screen; `motion/motion.cpp`: its gyro under *Device gyro* |

Motion is sent in the phone's natural axes with the screen's rotation. The game turns it into
controller axes the same way it does the Android device gyro (`motion::device_to_sdl`).

## Roadmap

1. ~~Input: controller, touch, gyro~~ (this version)
2. The GamePad picture on the phone: hardware H.264/HEVC from the GamePad screen (VideoToolbox),
   shown with `AVSampleBufferDisplayLayer`
3. GamePad sound on the phone's speaker, rumble through Core Haptics or the controller
4. A setting in the game instead of `WWHD_REMOTE_PAD`, a pairing code, the SDL host (Linux, Windows)
