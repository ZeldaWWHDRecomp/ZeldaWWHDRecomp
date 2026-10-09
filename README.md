# The Legend of Zelda: The Wind Waker HD — native port (macOS, Linux, Windows, Android)

A static recompilation of the Wii U version (USA or European) that runs natively on **macOS** (Apple Silicon),
**Linux**, **Windows** and **Android** (arm64; [build it yourself](#android-build-it-yourself)). The
game's PowerPC code is translated to C ahead of time, the Cafe OS libraries the game uses are
reimplemented natively, and GX2 graphics are implemented directly on Metal (macOS) or Vulkan (all
platforms), with no Cemu runtime and no GPU command emulation.

How it works and how it differs from running the game in Cemu: [docs/how-it-works.md](docs/how-it-works.md).

## What's new

The changes in each update are in [CHANGELOG.md](CHANGELOG.md) and in the release notes on the
[Releases](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/releases) page.

## Legal notice

This is an unofficial fan project. It is not affiliated with, endorsed or sponsored by Nintendo.
"The Legend of Zelda", "The Wind Waker", "Wii U" and related names are trademarks of their
respective owners and are used here only to describe what this software is compatible with.

This repository contains **no game code, no game assets and no keys**: no executable, no
recompiled or disassembled game code, no textures, models, audio, shaders, screenshots or other
material from the game, and no console encryption keys. It contains only the tools and the
runtime written for this project (plus the third-party code listed under Credits).

To use it you need your own, legally obtained copy of the game, dumped from your own Wii U disc
and console. Everything game-specific (the extracted files, the recompiled code in `build/gen/`,
shader caches) is generated locally on your machine from your dump, and must not be
redistributed. The `.gitignore` keeps all of it out of the repository.

## Install (releases)

Releases are portable: unzip, start **Wind Waker HD**, choose your dump. Releases contain only this
project's runtime, tools and setup: **no game files, no game code and no keys**. The game's code can
only exist once it is built from your own dump, so the first start prepares the game once, on your
computer (about two minutes); every later start launches the game directly.

1. Download the zip for your system from the
   [Releases](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/releases) page and unzip it anywhere
   (a games folder, an external drive):
   - **macOS**: Apple Silicon, macOS 14 or newer (Metal renderer)
   - **Windows**: x86-64, Windows 10 or 11, a GPU with Vulkan 1.3 drivers (or Vulkan 1.1 / 1.2 drivers
     with `VK_KHR_dynamic_rendering`)
   - **Linux**: x86-64 (`linux-x86_64`) or arm64 (`linux-aarch64`, e.g. Raspberry Pi 5, Asahi Linux
     on Apple Silicon, other ARM boards and laptops), glibc 2.35 or newer (Ubuntu 22.04+, Debian 12+,
     Fedora 36+, Arch, SteamOS 3, Raspberry Pi OS 12), a GPU with Vulkan 1.3 drivers (or 1.1 / 1.2 with
     `VK_KHR_dynamic_rendering`). Take the zip
     that matches `uname -m` (`x86_64` or `aarch64`); setup says so if it doesn't. Or take the
     single-file **AppImage** (same two architectures) instead of the zip (issue #55): `chmod +x` it
     and start it from anywhere, Steam Deck included. Nothing is unzipped and the file itself is never
     written to.
2. Start **Wind Waker HD** (`Wind Waker HD.app`, `Wind Waker HD.exe`, or `wind-waker-hd` /
   `Wind Waker HD.desktop` on Linux). The first start asks for:
   - your **disc image** (`.wux` or `.wud`), a **Cemu archive** (`.wua`), or an already **extracted
     game folder** (with `code`, `content` and `meta`, e.g. from dumpling or Cemu). An extracted folder
     is used where it is, nothing is copied; a disc image or Cemu archive is extracted into the
     release folder (about 1.7 GB);
   - for a disc image, its **disc key** (a `.key` file with the image's name next to it is used
     automatically) and the **Wii U common key** (16 bytes, the same on every console; choose a key
     file or paste the 32 hex digits into the hidden field; a `common.key` next to the image or in the
     release folder is used automatically). Keys are checked before anything is extracted, never
     stored, and not part of any log. A Cemu archive or an extracted folder needs no keys.
   - A Cemu archive (Cemu's "Convert to compressed Wii U archive (.wua)") often holds the game, its
     update and DLC together. Setup uses the game itself, title 00050000-10143500 (USA) or
     00050000-10143600 (Europe), version 0, and says
     so in its log; an update in the archive is not used: the port is built for the code of version 0,
     and the update's files belong to its newer code. The archive's checksum is verified before
     anything is extracted.

   Then it prepares the game (extract, translate the code to C, compile with a pinned compiler) and
   offers to bring in a save: a Wind Waker HD `cking.sav` folder (Cemu, Wii U), a GameCube `.gci`
   (converted to HD), or the saves and settings of an earlier installation or another Wind Waker HD
   folder (copied, never moved). The USA version (title 00050000-10143500) and the European one
   (00050000-10143600) are supported, version 0 of either (the disc or eShop release, without the
   update): before translating, setup checks the game's code (`code/cking.rpx`) against the SHA-256
   of each and explains what to use instead when it matches neither (e.g. a game folder with an
   update copied over it). The European game plays in English, French, German, Italian or Spanish —
   its own text, chosen in the settings' Language tab. How one port serves both:
   [docs/builds.md](docs/builds.md).
3. That's it: start Wind Waker HD to play. To repair, update or change the game, hold **Shift** while
   starting it (macOS, Windows) or start it with `--setup` (Linux; also the "Setup" action of its
   menu entry).

First start, per system:
- **macOS**: the release is not signed by Apple, so the first time macOS says the app "cannot be
  opened". macOS 14: right-click (Ctrl-click) the app, **Open**, **Open**. macOS 15 and newer: click
  **Done**, then **System Settings › Privacy & Security › Open Anyway**. If Apple's Command Line Tools
  (the free compiler, which also brings Python) are missing, Wind Waker HD offers Apple's installer.
  Start the app **inside the unzipped folder** and keep it there: it needs `tools/`, `sdk/` and
  `portable.txt` next to it. Don't drag only `Wind Waker HD.app` into Applications; to keep the game
  somewhere else, move the whole folder. The folder must be writable (not a disk image or a read-only
  drive). If macOS asks whether Wind Waker HD may access your Downloads (or Desktop, Documents) folder,
  click **Allow**: the game is prepared in that folder. Releases up to v0.2.3 stopped with "Setup could
  not continue … must stay in the unpacked release folder" when started straight from the unzipped
  download (issue #48, macOS App Translocation); with those, run
  `xattr -dr com.apple.quarantine <the unzipped folder>` in Terminal once, then open the app again.
  When setup cannot continue it says what failed and what to do, and writes it to
  `data/setup-window.log` (or `~/Library/Logs/Wind Waker HD setup.log` when the folder is not writable).
- **Windows**: the release is not code-signed, so SmartScreen may say "Windows protected your PC":
  **More info › Run anyway**. Python comes with the release (`tools/python`, the official embeddable
  Python). The first start downloads the compiler (llvm-mingw, 190 MB) into the release folder, SHA-256 checked, no administrator rights; at the end you can remove
  the compiler again (it is only needed to repair, and downloaded again then).
- **Linux**: start `wind-waker-hd` (or `Wind Waker HD.desktop`; some desktops ask to allow launching
  it first). It uses your Python 3 and downloads the compiler (zig, 55 MB; the x86-64 or arm64 build
  matching your system) into the release folder;
  you can remove it at the end.
- **Linux (AppImage)**: `chmod +x WindWakerHD-*-linux-*.AppImage`, then start that file (from a file
  manager, a terminal or Steam as a non-Steam game). It needs no unzipping and no writable folder of
  its own. Ubuntu 24.04 and newer have no FUSE by default: install `libfuse2t64` (`libfuse2` on older
  releases), or start it with `--appimage-extract-and-run`. Repair or change the game with `--setup`.
  Everything it creates is listed under "Everything stays in the release folder" below.

**Everything stays in the release folder** (in `data/`): the built game, the extracted game files,
saves (`data/save`), settings, controls, save states and shader caches (`data/user`), crash logs
(`data/captures`), the setup log and the downloaded compiler. Nothing is written to your user folders
(Application Support, AppData, .config, Applications, Start menu) unless you tick "add a shortcut" at
the end. To remove everything, delete the folder. Starting a newer release: unzip it next to the old
one, start it, choose your game (the old folder's `data/game` can be used in place) and copy your saves
and settings from the old folder.

The **Linux AppImage is not a portable folder** (issue #55): its file is read-only, so the game, its
code and saves go to `~/.local/share/wwhd` and the settings, controls, save states and caches to
`~/.config/wwhd` (or `$XDG_DATA_HOME` / `$XDG_CONFIG_HOME`; `--data-dir FOLDER` chooses another place
for the first, for example an SD card on a Steam Deck). Nothing is written beside the `.AppImage`; the
menu entry the setup adds starts the same game. To remove everything, delete the file and those two
folders.

The setup also runs in a terminal (the fallback): `tools/Setup in Terminal.command` (macOS),
`tools/Setup in a console window.bat` (Windows), `tools/setup-in-terminal.sh` (Linux). How it works
and the interface between the window and `tools/installer/setup.py`:
[tools/installer/README.md](tools/installer/README.md). Scripted use: `tools/installer/setup.py --help`.

Source builds (below) and the Linux AppImage are not portable: they keep using the per-user folders
(`~/Library/Application Support/wwhd`, `%LOCALAPPDATA%\WWHD` for the game and saves,
`~/.config/wwhd` for the settings on Linux), as before.

## Requirements (building from source)

- **macOS** on Apple Silicon, **Linux** (x86-64 or arm64, Vulkan) or **Windows** (x86-64, Vulkan); the
  platform-specific build steps are under "Building" below
- macOS: Xcode command line tools (`xcode-select --install`)
- zstd for the extractor's `.wua` support: a system one if installed (`brew install zstd`, `apt install
  libzstd-dev`; found through its CMake package or pkg-config), otherwise CMake downloads the pinned
  source (`-DWWHD_BUNDLED_ZSTD=ON` always does, as release builds do)
- CMake 3.20 or newer
- Python 3 with `pycryptodome` for `tools/wudextract.py` (`pip3 install pycryptodome`); the native
  `wwhd-extract` built with the project (`build/cmake/wwhd-extract --help`) needs neither
- optional: `capstone` (`pip3 install capstone`) for the disassembler helper `tools/ppcdis.py`
- optional, decompilation tools only: `ninja` and the requirements of the zeldaret/tww build
  (see below)

You also need, from your own console and disc:

- a disc image of The Wind Waker HD (USA or Europe) in `.wud` or `.wux` format (or a Cemu archive,
  `.wua`: `build/cmake/wwhd-extract --title 0005000010143500 extract game.wua game`, with
  `0005000010143600` for the European game, no keys);
- its disc key (16 bytes) in a `.key` file next to the image, with the same base name;
- the Wii U common key, either in a file `common.key` (16 raw bytes or 32 hex digits) next to
  the image or in the current directory, or in the `WIIU_COMMON_KEY` environment variable
  (32 hex digits). As a text file it is one line of 32 hex digits, nothing else; a wrong or
  malformed common key also makes the extraction fail with a decryption error.

None of these are included or will be provided.

## Building

For the Vulkan renderer also: `brew install vulkan-headers vulkan-loader molten-vk glslang` (the
build needs them; the app still runs with Metal on a Mac without them). `-DWWHD_RENDERER=METAL`
builds a Metal-only app without any Vulkan dependency.

```sh
# 1. extract the game into game/ (game.wux with game.key next to it, plus your common key)
python3 tools/wudextract.py game.wux extract game
#    -> game/code/cking.rpx, game/content/..., game/meta/...

# 2. recompile the game code to C (writes build/gen/; stays on your machine)
python3 tools/recomp/recomp.py game/code/cking.rpx build/gen

# 3. build
cmake -S . -B build/cmake && make -C build/cmake -j$(sysctl -n hw.ncpu) wwhd
```

### Linux

The Linux build uses the Vulkan renderer with the SDL3 host (windows, input, audio). On Ubuntu 24.04:

```sh
sudo apt install clang cmake ninja-build zlib1g-dev liblz4-dev libvulkan-dev glslang-dev \
  mesa-vulkan-drivers libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
  libxfixes-dev libxkbcommon-dev libwayland-dev libasound2-dev libpulse-dev libudev-dev libdbus-1-dev
# SDL3 is not packaged in 24.04: build it from source (https://github.com/libsdl-org/SDL, release-3.2.x)
cmake -S . -B build/linux -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/linux
./build/linux/wwhd --renderer-smoke     # checks the Vulkan renderer, no game files needed
```

If linking fails with unwinder errors (missing `_Unwind_*` symbols or `-lunwind`), your clang is set up to
use LLVM's libunwind. Configure with the GCC unwinder instead:

```sh
cmake -S . -B build/linux -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_EXE_LINKER_FLAGS="--unwindlib=libgcc"
```

Closing the TV window ends the game; closing the GamePad window only hides it. F11 or Alt+Enter toggles
full screen for the focused window.

On Arch-based systems (Arch, CachyOS, Manjaro):

```sh
sudo pacman -S clang cmake ninja sdl3 vulkan-headers vulkan-icd-loader glslang shaderc python-pycryptodome
# plus the Vulkan driver for your GPU, e.g. vulkan-radeon (AMD) or vulkan-intel
```

Wii U volumes are case-insensitive and the game asks for paths in a different case than the
extracted folders (e.g. `Audiores` vs `AudioRes`); the runtime resolves such paths itself on
case-sensitive file systems. When the game asks for text (your name), a text window appears over
the game (see *Entering text* under Playing).

On Linux and Windows, **F11** or **Alt+Enter** switches the focused window (TV or GamePad) to full
screen and back; the TV window's full screen (also the settings overlay's *Display > Full screen*) is
remembered in `settings.ini` and the next start begins the same way. Clicking/dragging with the left mouse button in the GamePad window uses the
touch screen. The GamePad screen has the macOS modes (settings overlay, *Display*): a separate
window, a picture-in-picture overlay in a corner of the TV window (corner, size and opacity
selectable; click it to touch), the automatic overlay, off, or the GamePad picture alone in the TV
window (click it to touch); **Ctrl+G** shows/hides it, and the choices are saved in `settings.ini`. The macOS menus (Graphics, Display, Input, Save States) don't exist in these builds
yet; their settings are available as environment variables (below) and the number-key shortcuts.

Settings, controls and save states live under `~/.config/wwhd` (or `$XDG_CONFIG_HOME/wwhd`).
To check the build without the game, `python3 tools/recomp/stubgen.py build/gen-stub` writes
placeholder guest code and `-DGEN_DIR=$PWD/build/gen-stub` builds against it (the result cannot
run the game).

### Windows

The Windows build uses the same Vulkan renderer and SDL3 host as Linux. Both native LLVM and
MSYS2 CLANG64 builds are supported. Choose either method below and use separate build directories
when switching toolchains. Both require the generated `build/gen` from the recompiler steps above.

#### Native LLVM (PowerShell, without MSYS2)

Install LLVM (with `clang` and `clang++`), CMake, Ninja, Visual Studio's **Desktop development
with C++** workload (for the Windows headers and runtime libraries), and the Vulkan SDK. Ensure
`clang`, `clang++`, `cmake` and `ninja` are on `PATH`, and `VULKAN_SDK` points to the SDK installation.
Run from PowerShell:

```powershell
cmake -S . -B build/windows -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows
ctest --test-dir build/windows --output-on-failure
./build/windows/wwhd.exe --renderer-smoke
```

CMake uses installed native dependency packages where available and downloads pinned source
releases of missing glslang, SDL3, zlib and LZ4 dependencies into the build directory. The first
configure therefore needs internet access. SDL3's DLL is copied next to the executable. To use
an existing zlib installation, set `ZLIB_ROOT` or the standard `ZLIB_INCLUDE_DIR`,
`ZLIB_LIBRARY_RELEASE` and `ZLIB_LIBRARY_DEBUG` cache variables.

#### MSYS2 (CLANG64 shell)

Install [MSYS2](https://www.msys2.org/), open its **CLANG64** shell, and install the toolchain and
dependencies with `pacman`. This method uses MSYS2 packages and does not require Visual Studio
Build Tools or the separate Vulkan SDK:

```sh
pacman -S mingw-w64-clang-x86_64-{clang,cmake,ninja,python,vulkan-headers,vulkan-loader,glslang,sdl3,lz4,zlib}
cmake -S . -B build/windows-msys2 -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows-msys2
ctest --test-dir build/windows-msys2 --output-on-failure
./build/windows-msys2/wwhd.exe --renderer-smoke   # checks the Vulkan renderer, no game files needed
```

Run it from the CLANG64 shell, or copy the DLLs it needs (`SDL3.dll`, `libc++.dll`,
`libunwind.dll`, zlib) from `C:\msys64\clang64\bin` next to `wwhd.exe`; `vulkan-1.dll` comes with
your GPU driver. Settings, controls and save states live in `%APPDATA%\WWHD`. Python for the
extraction and recompiler steps can be the MSYS2 one (`pip install pycryptodome`).

For either method, use Release for gameplay performance. Microsoft's `cl.exe` compiler is not
supported because the recompiled game requires Clang's `musttail` support.

The build fails with a clear message if `build/gen` has not been generated. The runtime checks at
startup that `game/code/cking.rpx` matches the recompiled code.

### Android (build it yourself)

The Android port is by [rhemfur](https://github.com/rhemfur) (issue #23): the Vulkan renderer on
an arm64 phone through SDL3's Android activity, measured at 30–32 fps in the heaviest scenes on a
Galaxy S25 Ultra (Snapdragon 8 Elite). There is no download: **releases never contain an APK,
`libmain.so` or anything derived from the game files**, and they never will. You build the APK
yourself from your own dump, and since it contains your recompiled game, it is for your own phone
only: don't share it. (CI builds the APK only with placeholder code, to check that it compiles.)

You need:
- a phone with arm64, Android 13 or newer and Vulkan 1.3;
- **a game controller** (Bluetooth or USB). It is the GamePad's buttons and sticks; the touch
  screen is only the GamePad's touch screen (no on-screen buttons). Keyboards only type text
  (`WWHD_ANDROID_KEYBOARD=1` in `env.txt` makes them a GamePad too);
- on the computer: the Android SDK (platform 36, build tools 35.0.0), NDK 30.0.16248370, JDK 17
  or newer, CMake 3.20+ and Ninja, Python 3, and your own `build/gen` (steps 1 and 2 under
  Building).

```sh
android/build_native.sh            # libmain.so, libSDL3.so, libc++_shared.so -> android/app/libs
#   CPU=generic (default): any arm64 phone; CPU=oryon-1: tuned for the Snapdragon 8 Elite (e.g.
#   Galaxy S25), runs only there. ANDROID_SDK, NDK_VERSION, GEN_DIR, JOBS: see the script.
python3 android/make_icon.py       # optional: the launcher icon from your game/meta/iconTex.tga (Pillow)
cd android && ./gradlew assembleRelease
adb install -r app/build/outputs/apk/release/app-release.apk
```

Start the app once: it creates `Android/data/org.wwhdrecomp.wwhd/files/` on the phone's storage
(reachable over USB). Copy your extracted game there as `game/` (`code/`, `content/`, `meta/`);
`save/` takes a save (`user/cking.sav`, the same layout as on the computer); `config/wwhd/` holds
`settings.ini` and the shader caches; an optional `env.txt` takes `WWHD_` options, one `NAME=value`
per line. Long-press the app icon to export or import the save as a zip.

In the game, the button in the top left corner switches the view: a tap cycles TV with the GamePad
picture in a corner, the GamePad picture alone (Minus in the game switches to Off-TV Play, the game
on the GamePad) and the TV alone; a long press switches 60 fps (its dot: green on, yellow while the
phone pauses it because it is too slow or too hot). Touches on the GamePad picture touch the
GamePad. The settings overlay (hold Select or press Home) has the other GamePad screen modes and
the graphics options.

## Playing

```sh
./build/cmake/wwhd                 # options: --game DIR (default game), --save DIR (default save)
```

`--renderer=metal` or `--renderer=vulkan` (or `WWHD_RENDERER_RUNTIME=metal|vulkan`) overrides the
saved renderer choice for one start.

**Vulkan presentation** (settings overlay › Graphics › Presentation): *Vsync (smooth)* (FIFO, the
default), *Low latency* (MAILBOX: the newest frame at each refresh, no tearing) or *Off (may tear)*
(IMMEDIATE). Only modes the driver offers can be chosen (MoltenVK on macOS offers vsync and
immediate, no mailbox); a change applies at once and is saved with the other graphics options.
`WWHD_VK_PRESENT_MODE=fifo|mailbox|immediate` overrides it for one start (not saved). The log says
which mode is in use and which the driver offers (`[vulkan] TV present mode fifo (available: …)`).

Two windows open: the TV and the GamePad screen (map, items, menus). Click and drag in the
GamePad window to use the touch screen. Saves go to `save/`.

**Settings overlay:** press **F1** in the game window, or **Cmd+,** on macOS (also *Settings…* in the app
menu; most Mac keyboards send F1 only with **Fn+F1** unless "Use F1, F2, etc. keys as standard
function keys" is on), or hold Select / Minus for half a second, or press Home, on a controller, for an in-game menu over the picture: save states, graphics, display (full screen, remembered for
the next start; picture scaling; the GamePad screen),
gameplay mods and cheats (Graphics also has the Vulkan presentation mode), controls (the same controller drawing as Input > Controls…: select a
button or chip and press the key or controller input to use; also on Windows and Linux) and the
console language (only the languages your game contains can be chosen; the USA game has English,
French and Spanish, the European one English, French, German, Italian and Spanish; experimental:
those languages from your own European or Japanese copy of the game played with the USA code, see
[docs/language-packs.md](docs/language-packs.md); fan translations
into Arabic or Hebrew are drawn right to left, see [docs/rtl-text.md](docs/rtl-text.md)).
Mouse, keyboard (arrows, Enter, Esc) and controller (D-pad / stick, A, B; L / R switch tabs) all work.
The game keeps running but gets no input while it is open; Esc, F1 or B closes it. On macOS it shows
the same options as the menu bar, and both stay in sync. Shift+F1 still saves state slot 1; slot 1 is
loaded from the overlay (F1 used to load it).

**Entering text (the name screen):** when the game asks for text — your name when you start a new
file — a text window appears under the game's own name field (also in the GamePad-only and
picture-in-picture modes; it scales down to fit, and on very small windows moves up over the field),
with a field, a character counter (the name takes up to 8 characters) and an on-screen keyboard. The
game's field shows the name in the game's font as you type. Only characters the game's font can draw
are offered: the keys and typed characters are checked against the font the name is drawn with
(`CKingMsg.bffnt`, read from your own game files at run time); a missing one is refused with a short
note. Type on the keyboard (any layout, accents and dead keys, input methods), **Enter** = OK,
**Esc** = Cancel, Backspace / Delete / arrows edit. With a controller: D-pad or left stick choose a
key, **A** types it, **B** deletes, **X** adds a space, **Y** is Shift (once, then Caps), **L / R**
switch between letters, accented letters and symbols (kana on a Japanese game), **ZL / ZR** move the
caret, **Start** or the OK key confirms. The mouse clicks keys too; on Android the system keyboard
also opens. The game gets no input while the window is open, and none after it closes until the
button that confirmed is released. `WWHD_SWKBD_TEXT=<name>` answers automatically (test runs); only
where the overlay can't show does the old prompt remain (the typed text in the window title on
Windows/Linux, a dialog on macOS).

### Controls

Default keyboard layout:

| Keyboard | Wii U GamePad |
|---|---|
| W A S D | left stick (move) |
| arrow keys | right stick (camera) |
| K or Space | A |
| J | B |
| L | X |
| I | Y |
| Q / E | L / R |
| Left Shift | ZL (target) |
| C | ZR |
| Enter / Tab | + / − |
| H | Home |
| 1 2 3 4 | D-pad up / down / left / right |
| X / V | left / right stick click |

**Input › Controls…** remaps everything on a drawing of the controller: click a button, stick
direction or stick click, then press a key or a controller button / stick direction (each input
has a key, an alternate key and a controller binding); Esc cancels, right-click clears. Pressed
buttons light up and the sticks show their deflection, so you can test the mapping; a key bound
twice is marked with a warning. Changes apply immediately, also while playing. A dead zone for
controller sticks and an option to invert the camera's up/down are at the bottom, with **Reset to
Defaults…**. The mapping is saved to `~/Library/Application Support/WWHD/controls.json`
(`WWHD_CONTROLS=<file>` uses another file); deleting it restores the defaults. The app's
single-key shortcuts (R, O, M, N, 6–9, P, F1–F5, F12) and Esc can't be bound. The **Screenshot**
key (row "Photo", **F10** by default) is bound here like a button, to a key or a controller input.

The **Graphics** menu in the menu bar switches fixes and enhancements while playing (the TV
window title shows what is active and the current frame rate): 60 fps by frame interpolation
(**6**; 120 and 240 fps in the menu and the settings overlay), true 60 fps (**7**, experimental), internal resolution 1x / 1.5x / 2x / 3x (**R**
cycles; the game renders at 1280x720, 2x renders at 2560x1440), edge smoothing (FXAA, **8**),
ambient-occlusion mode (**O** cycles), full-size occlusion depth (**M**), 16x anisotropic
filtering (**N**), the aspect ratio, the renderer (Metal or Vulkan), **Take Screenshot** (**F10**,
rebindable in Controls: the TV picture as a PNG at the internal resolution, without the settings
overlay, in `~/Library/Application Support/wwhd/screenshots`; also *Open Screenshots Folder* and an
option to save the GamePad screen too), and a frame capture for debugging (**P** or fn+F12, written to `captures/`;
captures contain game imagery, so keep them to yourself). The Graphics choices are remembered
between launches (macOS preferences; `defaults delete wwhd` resets them).
True 60 (**7**) computes Link and the camera at 60 Hz while the game state after every 30 Hz step
stays bit-identical to the 30 fps game, except the random-number sequence, which drifts because
drawing code draws random numbers too (later drops and ambient behaviour differ like in any other
session; see docs/decomp-notes.md, "True 60 fps").

The **Gameplay** menu has optional changes to how the game plays, all off by default: climb any
wall (with a stamina wheel; B or A lets go), a direct right-stick camera (no easing, adjustable
speed), a mouse camera (click the picture to capture the pointer, Esc releases it), first person
on the mouse wheel, quick doors and fast scene changes.
It also has cheats: all items, the full-power Master Sword and Mirror Shield, 20 hearts / double
magic / 5000 rupees, and infinite health, magic or ammo. Story cheats (all songs, Triforce shards,
dungeon map/compass/boss key, a small key) can change or break story events, so use a spare save
file. Cheats edit the live save data; save in game to keep them.

The **Save States** menu (and the Saves tab of the settings overlay) saves to one of 5 slots and
loads it back (**Shift+F1–F5** save, **F2–F5** load; slot 1 loads from the F1 settings overlay);
each slot shows its time and area. Slots are kept in `~/Library/Application Support/wwhd/states/`
and survive restarts. By default a slot holds a **portable state** (`slotN.wwstate`, a few KB):
your progress and Link's place, no game data, safe to attach to bug reports. Loading one works
once a Quest Log is being played (it waits until then): the progress is put into the game and Link
enters the saved stage at the saved spot (on the boat if he was on it). It is not an exact snapshot:
enemies, cutscenes and other actors start fresh. A portable state can only be saved while you
control Link (not during cutscenes, dialogue or stage changes). A state from another Quest Log is
loaded into the one being played, with a notice.
**Full save states** (Save States menu or Saves tab, off by default, for debugging;
`WWHD_FULL_SAVE_STATES=1`) save the whole running game instead (`slotN.bin`, about 270 MB) and
restore it exactly; they contain game code and data, **never share them**. A full state made by an
incompatible build is refused. Loading a slot loads whichever kind it holds.

**Crash Recovery** (Save States menu, off by default, or `WWHD_CRASH_RECOVERY=1`): every 2 minutes the
game is saved into one of three automatic states (`states/auto/`, about 260 MB each; the save
freezes the game for about 0.1 s), and the controller input since the latest one is recorded. After a
crash, the crash log names them, and `WWHD_REPLAY=<n> ./build/cmake/wwhd` loads automatic state n and
plays the recorded input back to reproduce the crash. Automatic states can also be loaded from the menu.

Game controllers (Xbox, PlayStation, Switch Pro, MFi) work too; by default buttons map by
position (the bottom face button is the Wii U's B), and they can be remapped in the Controls window.
**Face buttons** in the settings overlay (F1 → Controls) or the Input menu switches that preset:
*by position (Nintendo)* is the default; *by label (Xbox)* makes the button named A drive the Wii
U's A instead — on an Xbox pad that means A accepts/acts and B goes back (issue #78), and the X/Y
items follow the printed labels too. The choice rewrites the four face bindings only; keyboard keys
and the other inputs stay as they are, and a hand-edited face binding shows as *custom*.
In *by label*, in-game dialog icons and HUD face-button backgrounds use Xbox-style colours
(green A, red B, blue X, yellow Y); changing the setting updates cached prompts without a restart.
The HUD button cluster also follows the Xbox positions: A bottom, B right, X left, Y top.
In-game shoulder prompts use R1/R2 for R/ZR and L1/L2 for L/ZL in this mode.
*By position* and *custom* keep the game's colours and positions. See [in-game glyphs](docs/button-glyphs.md).
The **Input** menu switches whether keyboard and controllers act as the Wii U GamePad (default)
or as a Wii U Pro Controller (`WWHD_PRO_CONTROLLER=1` starts in that mode); with the Pro
Controller, the GamePad window keeps its screen and touch input.
When the game asks for text (e.g. your name), a macOS text field opens.

Rumble works too (SDL hosts): what the game asks its controller's motor to do is passed to the
connected controllers that have one, which includes the Pro Controller's. A host controller has a
single motor, so a GamePad rumble pattern plays as on/off (or half strength where it alternates),
and the motors stay still while the settings overlay is open, while no game window has focus and
once the app quits. **Controls > Rumble** in the settings overlay (F1) turns it off and is
remembered (`WWHD_RUMBLE=0` starts with it off). The macOS app does not drive controller motors yet.

**Gyro aiming** (settings overlay → **Controls** → **Gyro…**, off by default): in first person the
game turns the camera when the GamePad moves. The port turns its virtual GamePad with a host
controller's gyro (SDL3; on the macOS app through GameController.framework), a Cemuhook (DSU) server
or the mouse (Steam Input "gyro to mouse": while the game aims, the pointer is captured and the mouse
turns the GamePad); it works in GamePad and Pro Controller mode. The axis mode (player space, yaw or
roll), sensitivity and invert per axis, a recalibrate button or key, and the Cemuhook server, port and
slot are saved; `WWHD_GYRO=off|controller|cemuhook|mouse` overrides the source at start. Details,
troubleshooting and what to test: `docs/gyro.md`.

The **Display** menu: full screen for the TV window (**⌘F**, **⌃⌘F** or the green button; the
pointer hides after 2 s without movement), picture scaling (smooth, sharp, or integer scale) and
where the GamePad screen goes: a separate window (which can be put on another display, also in
full screen there), a picture-in-picture overlay in a corner of the TV picture (size, corner and
opacity selectable; click it to touch), an automatic overlay that appears for a few seconds when
the GamePad picture changes a lot (a page or menu switches; **⌘G** keeps it up), off, or *GamePad
only* (the GamePad picture alone in the TV window, click it to touch; Minus in the game switches to
Off-TV Play).
**⌘G** shows/hides the GamePad screen in any mode. Window positions, full screen, these choices
and the renderer are remembered in `~/Library/Application Support/wwhd/display.plist`
(delete it to reset).

Closing the TV window (its close button or **⌘W**) quits the game, as on Linux and Windows; closing
the GamePad window only hides it (**⌘G** brings it back). While a save file is being played, closing
the TV window or **⌘Q** first asks *Quit Wind Waker HD?*: **Quit**, **Cancel** (keep playing), or
**Save State and Quit**, which writes save state slot 1 (Save States menu) and then quits. On the
title screen and the file select, before a file is loaded, it quits without asking.
`WWHD_QUIT_PROMPT=0` turns the question off; scripted and hidden test runs never ask.

## Notes

- Shaders are translated on first use and cached in `~/Library/Caches/wwhd/shaders.bin`; later
  runs replay that cache at startup.
- [docs/performance.md](docs/performance.md) covers how to profile the port, measured fixes and
  open performance leads.
- Useful environment variables: `WWHD_NO_AUDIO=1`, `WWHD_NO_GAMEPAD=1` (no second window), `WWHD_NO_CONTROLLERS=1` (SDL builds: ignore host game controllers), `WWHD_LANGUAGE=<code>` (console language: 1 English, 2 French, 5 Spanish, … — the USA/Asia disc carries English, French and Spanish; a language the game doesn't contain starts in English; with `WWHD_LANGUAGE_REGION=eu` or `jp` from a language source, docs/language-packs.md), `WWHD_RTL=0` / `1` (right-to-left text for Arabic and Hebrew packs off / forced on, docs/rtl-text.md),
  `WWHD_DRC_MODE=window|pip|auto|off|gamepad`, `WWHD_FULLSCREEN=0|1` (the TV window starts windowed / in
  full screen this time instead of as it was left; that session's full screen is not remembered), `WWHD_ASPECT=16:9|window|16:10|21:9|32:9|<w:h>`,
  `WWHD_AUDIO_VOLUME=0..1`, `WWHD_AUDIO_OUTPUT=auto|tv|gamepad` (the host plays the TV's sound, plus the GamePad's in Off-TV Play: auto; or only one of them), `WWHD_SHADER_CACHE=<file>|0`, `WWHD_AO_MODE=0..2`, `WWHD_AO_HIRES=0|1`, `WWHD_ANISO=0|1`, `WWHD_RES_SCALE=1|1.5|2|3`,
  `WWHD_FXAA=0|1`, `WWHD_QUIT_PROMPT=0` (macOS: quit without asking, also during a game), `WWHD_INTERP=1`, `WWHD_INTERP_FPS=60|120|240` (frame interpolation at that rate),
  `WWHD_INTERP_PACED=0|1`, `WWHD_TRUE60=1` (start values for the Graphics menu; they
  override the remembered choices); `WWHD_DISPLAY_HZ=n` replaces the detected display refresh rate
  that 120/240 fps are capped to (0: no cap); `WWHD_UNCAPPED=1` starts with the debug switch
  "Uncapped" on (no frame limit, no vsync; the game runs faster than real time);
  `WWHD_SHADOW_SCALE=n` gives the shadow maps their own resolution factor (by default they scale with
  the internal resolution; since v0.2.9 both look practically the same, issue #67). `=1` keeps the
  console's 1024x1024 and uses much less GPU memory at 2x/3x, useful on weaker hardware; `WWHD_STATE_DIR=<dir>`
  stores save states elsewhere; `WWHD_FULL_SAVE_STATES=0|1` full or portable save states for this
  start; `WWHD_PORTABLE_LOAD=<file.wwstate>` loads that portable state (e.g. from a bug report) as
  soon as a Quest Log is being played; `WWHD_RUMBLE=0|1` (SDL builds) start value for Controls > Rumble (overrides the remembered
  choice); `WWHD_LOG_RUMBLE=1` logs the game's motor requests and what the motors do.
  `WWHD_STRICT_MUL=0` turns off the GPU's 0×anything=0 multiply rule in shaders (on by default, as in
  Cemu; off only for performance comparisons, it brings back e.g. the black letter in the Rito mail sorting game).
- Crashes and game halts write `captures/crash-<time>.log` (crash address, registers, the guest call
  chain, a host backtrace and the last log lines; useful for bug reports, it contains only addresses,
  function names, the file names of the program's modules and log text). A crash address outside the
  game code names its module and offset (`in amdvlk64.dll+0x1A2A01`): a graphics driver, or an
  overlay's Vulkan layer; the log lists the Vulkan layers at start (`[vulkan] layers:`).
- Debugging aids (frame/draw dumps, traces, scheduler statistics) are documented next to their
  code: grep for `WWHD_` in `runtime/src`.

## Optional: bring your GameCube save to HD

`tools/savegame/gc2hd.py` converts a GameCube Wind Waker save (`.gci`, USA GZLE01 or Japanese GZLJ01,
e.g. from a memory-card dump or Dolphin) into a Wind Waker HD save (`cking.sav`), all three files
with their progress, items, songs, charts and story state. It works for this port and for Cemu.

```sh
python3 tools/savegame/gc2hd.py "My Save.gci" -o converted     # writes converted/cking.sav
python3 tools/savegame/hd_save_info.py converted/cking.sav      # shows what is in it
```

Back up your old `cking.sav`, then put the new one in `save/user/` (Cemu:
`mlc01/usr/save/00050000/10143500/user/80000001/` for the USA game). The Tingle Tuner becomes the
Tingle Bottle (HD's item in the same slot); Picto Box photos and HD-only statistics are not carried
over; Japanese player names become "Link". Details and options: [tools/savegame/README.md](tools/savegame/README.md).
Tested with 167 GameCube saves across the whole story (100% and Any% routes).

## Optional: shader head start

The first time a shader is needed it is translated and compiled, which can cause short hitches.
A "head start" pre-translates shaders from the game's own shader archives so later sessions
start with them. It is built locally from your files and is never distributed; without it the
game simply translates shaders on first use.

Building it needs a state template (the GPU register states each shader was used with), which
is recorded from your own play: play for a while (the further you get, the more it covers),
then

```sh
python3 tools/shaderprep.py template ~/Library/Caches/wwhd/shaders.bin   # -> game/shadercache/template.bin
python3 tools/shaderprep.py build                                          # -> game/shadercache/headstart.bin
./build/cmake/wwhd --warm-shaders    # optional: compile everything once to fill the macOS shader cache
```

`--merge game/shadercache/template.bin` adds a later session to an existing template. The runtime
picks up `game/shadercache/headstart.bin` automatically (`WWHD_HEADSTART=<file>|0` overrides it).
See the comment at the top of `tools/shaderprep.py` for the file formats.

## Optional: decompilation tools

`tools/decomp/` names WWHD functions by matching them against the
[zeldaret/tww](https://github.com/zeldaret/tww) GameCube decompilation (CC0); the 60 fps features
are built on those names (`tools/recomp/hooks.txt`, `runtime/src/interp*.cpp`, `runtime/src/true60*.cpp`).
Findings are in `docs/decomp-notes.md`.

`tools/verify/` is a differential test harness for a functionally verified decompilation: it runs
hand-written C++ next to the recompiled original on generated and recorded inputs and compares
return values, memory effects and call sequences (see `tools/verify/README.md`). The verified
source itself is derived from the game and is **not** part of this repository.

```sh
git clone https://github.com/zeldaret/tww tww     # git-ignored reference checkout
python3 tools/decomp/match.py game/code/cking.rpx tww build/names.tsv
```

The assert, string and actor-profile stages only need the decompilation's sources. The
call-graph stage additionally needs the decompilation built (`tww/build/GZLE01`), which in turn
needs `ninja` and your own GameCube disc image of The Wind Waker (USA) as described in the tww
README. `build/names.tsv` is derived from the game and stays on your machine.

## Status

The opening of the game (title, file select, intro, Outset Island) is tested and matches Cemu
side by side, at a steady 30 fps (the console's frame rate) and at 60 fps with interpolation;
true 60 is experimental. Known gaps: geometry shaders and rectangle
primitives are not implemented yet (not encountered so far), shadow edges are harder than on
the console, startup sometimes sits on a black screen for up to a minute before the logo
(a timing-dependent wait during audio initialization, under investigation), and later parts of
the game are untested.

## License

The code of this project is licensed under the Mozilla Public License 2.0 (see `LICENSE`).
Vendored third-party code keeps its own license: Cemu (MPL-2.0), metal-cpp (Apache-2.0), {fmt} (MIT) and Dear ImGui (MIT); see Credits. The extractor links zstd (BSD-3-Clause); releases build it from its pinned release source. The game itself is Nintendo's property and is not included.

## Credits

European-game builds and the executable-derived address map are by
[ElFDA](https://github.com/ElFDA), contributed in [PR #77](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/pull/77).
The combined integration adds the runtime address audit and regional regression scenarios.
Thanks also to [GreenNaugahyde](https://github.com/GreenNaugahyde) and the MPL-2.0
[ZeldaWWHDRecompAndroid](https://github.com/GreenNaugahyde/ZeldaWWHDRecompAndroid) fork: its EUR
address mapping informed the desktop prototype, and the rendered mip chains and sun depth peeks
(bloom, haze, the sun's glare), the GPU decoder for compressed textures, custom Adreno driver
support and the run/swim speed boost were adapted from or modelled on its work.

The Android port (`android/`, the Android parts of the runtime, the single-screen view) is by
[rhemfur](https://github.com/rhemfur), who also contributed the paced frame interpolation, the
Vulkan presentation and feedback-image work, and Linux/Windows fixes.

GPU address library, shader decompiler and a few reference structures are vendored from
[Cemu](https://github.com/cemu-project/Cemu) (MPL-2.0, see `runtime/third_party/cemu/LICENSE.txt`);
`tools/wudextract.py`, `runtime/src/espresso_fp.c` and parts of the OS layer are ported from or
follow Cemu as noted in those files (in the Vulkan renderer: the vertex-format table and the
shader parser glue). Also vendored: [metal-cpp](https://developer.apple.com/metal/cpp/)
(Apache-2.0, `runtime/third_party/metal-cpp/LICENSE.txt`), [{fmt}](https://github.com/fmtlib/fmt)
(MIT, `runtime/third_party/fmt/LICENSE`) and [Dear ImGui](https://github.com/ocornut/imgui) v1.92.9b by
Omar Cornut and contributors, for the settings overlay (MIT, `runtime/third_party/imgui/LICENSE.txt`).
The Cemu archive (`.wua`) reader in `tools/wudextract/zarchive.cpp` is written from the format of
[ZArchive](https://github.com/Exzap/ZArchive) by Exzap (MIT No Attribution; nothing of it is
vendored); [zstd](https://github.com/facebook/zstd) by Meta Platforms (BSD-3-Clause) decompresses
it: release builds compile its pinned 1.5.7 release (URL + SHA-256 in `cmake/Zstd.cmake`) statically
into `wwhd-extract` and include its license in `third-party-licenses/`; source builds use a system
zstd when there is one.
