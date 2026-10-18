# Wind Waker HD on Android

**English** · [Português](android.pt.md) · [Español](android.es.md)

The Wind Waker HD runs natively on Android devices. It isn't an emulator: the game's own
code is recompiled for ARM processors and draws with Vulkan. You need your own copy of the Wii U
game; this project never provides game files.

## What the Android version does

| | |
|---|---|
| **On-screen controls** | Both sticks, the D-pad, A B X Y, L R ZL ZR and + −. The 🎮 button under the view button (top left) shows or hides them. They hide while a controller is connected and come back on the next touch. A Bluetooth or USB controller works too. Hold 🎮 to edit the touch layout: drag a control, tap it and use Size −/+ to resize it, then tap Done. Reset restores the original layout. Positions and sizes are saved on this device and stay on screen after rotation. Editing releases held controls and does not send touches to the game. |
| **Phone vibration** | Controls → Phone vibration defaults to **Touch only**: game rumble vibrates the phone while playing by touch. Controller or keyboard input stops phone feedback; using touch controls selects the phone again. **Touch or no rumble controller** also vibrates the phone when no connected controller has rumble. **Off** disables phone feedback; the main **Rumble** switch silences both phone and controllers. Android's vibration settings and touch-feedback switch apply. `WWHD_PHONE_RUMBLE=0`, `touch` or `fallback` overrides the saved startup choice. |
| **The GamePad screen on pause** | Press + and the picture switches to the GamePad screen (map and items), like the pause menu of the GameCube version. Press + again to go back. |
| **60 fps that keeps the game's speed** | On fast Android devices the game shows 60 frames a second. When the Android device gets hot it switches to a steady 30 by itself, so the game never slows down. |
| **Older Android devices** | If your Adreno GPU's driver is too old, the error screen offers to install a custom driver such as Mesa Turnip. |
| **GPU recovery** | After a GPU device-lost error, the next start names the driver and offers the system driver, another installed driver, or the existing driver ZIP picker before starting Vulkan. Continuing keeps your current choice; an unfinished first-start probe still falls back to the system driver. |
| **Saves** | Export and import your save files and keep them in Android's backup. Saves from a real Wii U (USA or Europe) work too. |

Android requests a 60 Hz display mode for 30 fps, interpolated 60 fps and true 60 fps, or the selected 120/240 Hz cadence for faster interpolation. Android may choose another supported rate. The request is reapplied after returning to the game. To leave refresh-rate selection entirely to Android, set `WWHD_NO_60HZ=1` in a developer build (`0` keeps the request enabled).

With 60 fps or faster on, frames are spaced evenly: a frame that is ready early waits a few
milliseconds so that each one stays on screen for the same time. This matters most with **Record graphics on a second
thread** on: on a 120 Hz phone screen a third of the frames used to stay for one or three refreshes
instead of two, which looked like a slight judder; now almost all stay for two. The game itself never waits for this, so its speed does not
change. To turn it off, set `WWHD_INTERP_EVEN_PACING=0` in a developer build.

## Tested phones

Measured with the game's own profiler in the busiest part of Outset Island (about 3,900 draws per
frame). *Game speed* is the game's logic steps per second: 30 is full speed.

| Device | SoC / GPU | Frame rate | Game speed | Status |
|---|---|---|---|---|
| Galaxy S25 Ultra | Snapdragon 8 Elite, Adreno 830 | about 59 fps | 29.7 / 30 | great |
| Galaxy Z Fold 8 | SM8850, Adreno 840 | 60 fps, 30 when hot | 26–30 / 30 | great, on both screens |
| Lenovo Legion Tab | Snapdragon 8 Gen 3, Adreno 750 | steady 30 fps | 28.7–30 / 30 | good |
| OnePlus 8 Pro | Snapdragon 865, Adreno 650 (Mesa Turnip) | 30 fps | 26–30 / 30 | runs; one GPU crash still being looked at |
| Galaxy Tab S6 Lite | Snapdragon 720G, Adreno 618 (Mesa Turnip), 4 GB | 15–24 fps | 15–24 / 30 | runs slowly: about half speed in Outset |

You need a 64-bit Android device with Android 13 or newer and Vulkan 1.3 (or a custom driver on Adreno),
and a few GB of free storage.

## 60 fps and heat

60 fps draws twice as many pictures as 30 fps, so the device works harder and gets warmer. When the
device says it is close to getting too hot, the game pauses 60 fps and shows a steady 30 fps at full
game speed. Once the device has cooled down, 60 fps comes back by itself.

Handhelds with a cooling fan (AYANEO, KONKR, AYN Odin and Thor, Retroid) can stay at 60 fps longer
than phones, so the game waits longer on them before it pauses 60 fps.

You can change this in **Settings → Graphics → Heat guard**, next to the frame rate:

- **Normal** (default): the right limit for your kind of device.
- **Relaxed**: waits longer before pausing 60 fps, on any device.
- **Off**: never pauses 60 fps because of heat.

Relaxed and Off keep 60 fps longer, but the device may get warm, and when it gets too hot it slows
itself down, which can make the game stutter or run slower. If that happens, go back to Normal.

## Gyro aiming with your phone

Open **Settings > Controls > Gyro**, then choose **Device gyro (phone/tablet)**.
This is off by default and needs both a gyroscope and an accelerometer. Rest the
phone for a second to calibrate. Enable **Options > Gyro** in the game and aim in
first person, keeping the right stick at rest. The axes follow the displayed screen
in either landscape direction. It works with touch controls or a controller.
Sensors stop in the background and while settings block gameplay; rest the phone
again after returning. Sensitivity, inversion and recalibration are explained in
the [gyro guide](gyro.md). The status line says if sensors are unavailable.

## How to play

### The setup app (no PC needed)

The setup app comes with every release: download `WindWakerHD-<version>-android-arm64.apk` from the
[Releases](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/releases) page (the `android-x86_64` APK is for emulators and Chromebooks). It carries no game
code: it builds the game on your Android device from your own copy. Later versions install over it and keep
your saves and settings (the Android device prepares the game again when an update needs it).

1. Install the APK. If you already have a version you built on your PC, export your save first
   (long-press the app icon), then uninstall it: the two are signed differently.
2. Open the app, tap **Choose your game** and pick what you have:
   - **Extracted game folder**: put the game in a folder of its own, for example `Download/WindWakerHD`,
     and choose that folder (the one that contains `code`, `content` and `meta`). The folder picker opens in
     Download. Android doesn't allow choosing Download itself, the top of the internal storage or
     `Android/data`, so the game needs a folder of its own. If you choose the folder just above the game,
     the app finds the game inside it; if you choose a wrong folder, it tells you which one to choose
     before anything is copied.
   - **Cemu archive (.wua)** or **Disc image (.wud / .wux) with keys**: a single file, which you can
     choose from any folder. A disc image also needs the disc key and the common key (Cemu's `keys.txt`
     covers both).

   When you choose a different game, the copy the app made of the earlier choice is deleted to free the
   space; the game you can already play stays.
3. Tap **Start / resume / retry setup**. The Android device extracts and compiles the game once; the screen
   shows the progress, for example "Compiling: 30/80 · about 12 min left". You can leave the app
   meanwhile, and **Pause setup** stops it safely.
4. When it says the setup is complete, tap **Play current build**.

Code mods (for example the story quests) need the game built with code-mod support: turn on
**Code mods** in the setup app, before or after the first setup. See
[Choosing code-mod support](setup-code-mods.md#android).

### Or: build it on your PC

1. Install the Android SDK and NDK, JDK 17 or newer, CMake, Ninja and Python 3.
2. Extract your game and generate its code (steps 1 and 2 under *Building* in the [README](../README.md)).
3. Run `android/build_native.sh`, then `./gradlew assembleRelease` in `android/`.
4. Install the APK over USB with `adb install -r app/build/outputs/apk/release/app-release.apk`.

The full steps are in the README, section *Android (build it yourself)*.

Moon jump is an optional cheat in **Gameplay → Cheats**, off by default. Enable it and hold **L** to rise; the left stick steers relative to the camera while airborne, including after you release L. Release the stick to stop horizontal flight. **No fall damage after moon jump** is a separate option that applies only to a flight started with this cheat. Both options work with 30 fps, frame interpolation and true 60; loading a state clears the current flight.

## Fair play

- Use your own copy of the game, dumped from your own disc or console.
- An APK you build on your PC contains your recompiled game. Keep it for your own Android device and never
  share it.
- If someone offers a ready-made APK with the game inside, it isn't from this project.

## Questions

**Do I need a controller?**
No. The on-screen controls cover the whole GamePad. A controller works too and hides the touch
buttons while it is connected.

**Which version of the game?**
The Wii U release of The Wind Waker HD, USA or Europe. Saves from a real Wii U work too.

**My Android device gets hot. Is that normal?**
The game is demanding. When the Android device gets warm, the game drops from 60 to a steady 30 fps on its
own and keeps its full speed. See [60 fps and heat](#60-fps-and-heat) for the Heat guard setting.

**What happens when I save a full state?**
By default, Android pauses the game while memory is compressed in small chunks and written to the
slot, avoiding a second full memory image. Slow storage can make this pause longer. Desktop keeps
the existing fast path with background compression. All new builds load streaming v3 and fast v1/v2
states from any platform; old builds reject v3 states.

**Full-state save path:** In Save states, choose **Auto** (default), **Streaming** or **Fast**. Auto uses less-memory streaming on Android and the existing fast background-compression path on Windows, macOS and Linux, including Crash Recovery. The choice is stored as `auto`, `streaming` or `fast` in `full_save_path.cfg` beside `full_save_states.cfg` in the states folder. All new builds load v3 streaming states regardless of this setting; compatible legacy states remain readable.

**Something doesn't work.**
Please open an [issue](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/issues) with your Android device
model, Android version and what happened. After a crash, the next start offers the crash report
to share. This also covers Android-recorded foreground low-memory kills, signals and ANRs, when
Android makes the exit history available. System summaries include the reason, timestamp,
status/signal, process importance and last PSS/RSS memory sample. They omit system descriptions,
traces, process-state bytes and logcat to avoid including names, save data or private paths.
Normal quits, background memory reclamation and save-tool stops do not trigger a system report.
A nearby native crash report is included in the same share file when available. Sharing is optional.

### Optional memory diagnostics

For diagnostic launches, `WWHD_VK_MEMORY_LOG_FRAMES=300` adds aggregate `[gfx] memory`
reports every 300 renderer frames to regular and crash logs. Unset, zero or invalid
values disable collection. Reports contain current RSS, cached image allocations,
upload blocks and retained shader payload capacities; they contain no shader source
or save data. RSS is not a heap total or peak measurement.

After compiling, the game keeps only the SPIR-V and a hash of each shader's GLSL text; the
text itself is released (set `WWHD_VK_SHADER_SOURCE_VERIFY=1` to keep it for diagnostics).
Counters exclude compiler results, disk snapshots, metadata, allocator overhead and
retired/non-cache/driver images. Use aggregate heap summaries with a repeatable scene
for retention investigations, and disable diagnostics for performance comparisons.


---

The Android port is by [rhemfur](https://github.com/rhemfur), part of ZeldaWWHDRecomp. A fan
project, not affiliated with or endorsed by Nintendo. The Legend of Zelda and The Wind Waker are
trademarks of Nintendo.

For developer crash diagnostics, `WWHD_CRASH_GUEST_MEMORY=1` adds a bounded 128-byte window for each guest register only when the complete window belongs to an executable, non-writable RPX section. A load-time executable baseline is retained after loader copies. At a crash, bounded volatile reads verify all 128 current guest bytes against that baseline; only a complete match emits baseline bytes. Modified or unavailable code is wholly redacted, without exposing changed bytes or hashes. This sample is not atomic with guest writes; heap, stack, writable data, unknown ranges and section boundaries are redacted. No ASCII, thread names or save contents are added. The report also includes a recent numeric snapshot of guest address regions, registered thread count and process RSS (which may be unavailable). This option is off by default and adds no memory queries or image copies while off. The retained original code is capped at 16 MiB and 64 non-overlapping sections; larger or ambiguous ranges stay redacted. Original code bytes may still identify the game build; review the report before sharing.
