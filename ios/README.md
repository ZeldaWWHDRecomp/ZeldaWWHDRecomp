# iPhone GamePad (experimental)

Play Wind Waker HD on your Mac and TV with an iPhone as the Wii U GamePad. Attach a controller to the
phone (Backbone, Kishi, Razer, or a paired DualSense or Xbox controller). The phone sends the game:

- the controller's buttons and sticks: a controller in the game's Controls, named **iPhone GamePad**,
  with its own bindings like any other controller. The phone also sends what its buttons are called, so
  the game's prompts show that controller's buttons (✕ ○ □ △ on a PlayStation-style one) and by-label
  bindings work as for a controller on the Mac;
- touches on its screen: the GamePad's touch screen (items, map, sea chart);
- its gyro: GamePad aiming (bow, hookshot, telescope, Picto Box).

The game sends the phone the GamePad screen, so you touch the picture itself, as on a real GamePad.
The GamePad screen on the Mac can stay in any mode (even *off*); the phone gets the picture either way.
Rumble works too: the game's GamePad motor runs on the controller when it has one (the Backbone One
PlayStation Edition does), else on the phone. The GamePad speaker plays on the phone too: the Mac then
plays the TV sound alone, as a Wii U does, so GamePad-only sounds (and everything in Off-TV Play) come
from the phone. Turn off *GamePad sound* in the app to hear them from the Mac again. The picture needs the Metal renderer (the default on macOS).

## Use it

1. **The game, on the Mac:** Settings > Controls > iPhone GamePad > **Use an iPhone as the GamePad**
   (remembered). The status line below it says *Waiting for the phone*. Allow incoming connections if
   macOS asks. (`WWHD_REMOTE_PAD=1`, or `=<port>` for another UDP port, keeps it on for one launch.)
2. **Gyro:** Settings > Controls > Gyro... > **Device gyro (phone/tablet)**.
3. **The phone,** on the same Wi-Fi network: open GamePad, allow local network access, and tap the Mac
   under *Games on this network*. If it doesn't appear, enter the Mac's address (System Settings >
   Wi-Fi > Details) under *Or enter the computer's address*.
4. **Pair, the first time:** Settings opens on the Mac with a 6-digit code; type it on the phone. The
   code works for 2 minutes and 3 tries; **Don't pair** on the Mac refuses the phone. After that the
   phone connects by itself. **Forget paired phones** (same section) makes every phone pair again.
5. Hold the phone in landscape in the controller. The picture takes the screen's whole height, over the
   game's own GamePad boot image. Beside it, on the left: the Mac's name, and the phone's battery (and the
   controller's, if it has its own); on the right, how old the picture is when it arrives (*picture*) and
   the network round trip (*ping*). **✕** (Disconnect), or leaving the app, gives the GamePad back to the
   Mac's keyboard and controllers. In portrait and on an iPad the status line sits above the picture.
6. **Text:** when the game asks for text (the name of a new file), the phone shows the game's hint and
   its own keyboard, as a Wii U GamePad does. The name appears in the game as you type; **Done** or
   **OK** confirms. The prompt on the TV keeps working too.
7. Leaving the app and coming back rejoins the game by itself if it is still there: a game from the
   list must be listed again, a typed address must answer, within 5 seconds. After **Disconnect** the
   app stays on the list.

Only one phone can be the GamePad at a time, and only a paired one. Pairing keeps other devices on the
network from taking the GamePad; it is not encryption: the phone's pairing token and its input travel
in the clear, so someone capturing your Wi-Fi traffic could copy them. Turn the option off on networks
you don't trust.

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

**The icon:** the project's own icon (the sailing boat, `android/icon/app_icon_1024.png`) by default. To
give the app the game's own icon, as the Mac app has, make it from your game files before building:

```sh
python3 ios/make_icon.py [game_dir | path/to/iconTex.tga]   # default: game; needs Pillow
```

It writes `GameIcon` and `ios/local.xcconfig`, which selects it. Both are git-ignored, since the
picture is the game's. Delete them to go back to the boat.

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
| `Speaker.swift`: GamePad speaker, 40 ms jitter buffer, `AVAudioSourceNode` | `hle/ax.cpp`: the GamePad mix after the game's final mix, to the phone; the host keeps the TV mix |
| `Haptics.swift`: the motor's strength as a continuous haptic (controller, else phone) | `remote_pad.cpp` `RumbleSender`: rumble.h's level on change, refreshed while it runs |
| `Video.swift`: frames from their pieces, `AVSampleBufferDisplayLayer` | `gfx/remote_video.mm`: the GamePad picture into IOSurfaces, H.264 (VideoToolbox, low latency) |
| `TextEntry.swift`: the game's text prompt kept in step (numbered edits, resent until acked) | `overlay/text_entry.cpp`: the phone's text through the prompt's own rules, acked back |
| `BootImage.swift`: the game's GamePad boot image from its pieces, kept per game | `gfx/remote_video.mm` `load_boot_image`: `meta/bootDrcTex.tga` as a JPEG, sent to a phone that shows another |
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
3. ~~Rumble~~ (Core Haptics on the controller, else the phone); ~~GamePad sound~~ (48 kHz PCM, 40 ms jitter buffer)
4. ~~A setting in the game, pairing with a code~~, ~~an app icon~~, ~~the phone's keyboard for the game's text~~
5. Not planned: the picture from Linux and Windows (the SDL host). They have no system video encoder
   like VideoToolbox, so it would need a third-party encoder library. Input, rumble and sound could
   follow there without one.
