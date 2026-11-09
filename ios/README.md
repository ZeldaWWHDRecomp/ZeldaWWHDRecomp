# iPhone GamePad (experimental)

Play Wind Waker HD on your Mac and TV with an iPhone as the Wii U GamePad. Attach a controller to the
phone (Backbone, Kishi, Razer, or a paired DualSense or Xbox controller). The phone sends the game:

- the controller's buttons and sticks: a controller in the game's Controls, named **iPhone GamePad**,
  with its own bindings like any other controller;
- touches on its screen: the GamePad's touch screen (items, map, sea chart);
- its gyro: GamePad aiming (bow, hookshot, telescope, Picto Box).

The game sends the phone the GamePad screen, so you touch the picture itself, as on a real GamePad.
The GamePad screen on the Mac can stay in any mode (even *off*); the phone gets the picture either way.
Sound and rumble come next (see Roadmap). The picture needs the Metal renderer (the default on macOS).

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
5. Leaving the app and coming back rejoins the game by itself if it is still there: a game from the
   list must be listed again, a typed address must answer, within 5 seconds. After **Disconnect** the
   app stays on the list.

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

For testing, `-join <name>` as a launch argument joins the listed game of that name (for example
`-join Mac-mini`), and `-connect <address>` connects at once (Xcode: Product > Scheme >
Edit Scheme > Arguments; `xcrun simctl launch <device> org.wwhdrecomp.gamepad -connect 127.0.0.1`
in the Simulator, against a game on the same Mac; on a phone, `xcrun devicectl device process launch
--console --device <id> org.wwhdrecomp.gamepad -- -join Mac-mini` also shows the app's `[link]` log).

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
| `Video.swift`: frames from their pieces, `AVSampleBufferDisplayLayer` | `gfx/remote_video.mm`: the GamePad picture into IOSurfaces, H.264 (VideoToolbox, low latency) |
| `Protocol.swift` (mirrors `protocol.h`) | `gfx/input.mm`: the phone as a controller and the touch screen; `motion/motion.cpp`: its gyro under *Device gyro* |

Video: each present, the game draws the GamePad picture as it does for its GamePad window into an
IOSurface (854 x 480, or that shape up to 1280 wide when the picture is rendered larger), and the
hardware encoder turns it into H.264 without frame reordering (about 6 Mbit/s). Frames go out in pieces
of at most 1400 bytes; a keyframe carries its SPS and PPS. The phone shows a frame as soon as it has all
of it, and after a lost piece it drops frames and asks for a keyframe (at most every 250 ms).

Motion is sent in the phone's natural axes with the screen's rotation. The game turns it into
controller axes the same way it does the Android device gyro (`motion::device_to_sdl`).

## Roadmap

1. ~~Input: controller, touch, gyro~~
2. ~~The GamePad picture on the phone~~ (Metal renderer; the Vulkan renderer later)
3. GamePad sound on the phone's speaker, rumble through Core Haptics or the controller
4. A setting in the game instead of `WWHD_REMOTE_PAD`, a pairing code, the SDL host (Linux, Windows)
