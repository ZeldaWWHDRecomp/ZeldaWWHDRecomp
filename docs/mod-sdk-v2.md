# Mod SDK v2: PowerPC code mods

Code mods for this port are written in C (or C++) and compiled for the game's own CPU:
32-bit big-endian PowerPC. One package works on macOS, Windows and Linux. When a player
installs it, the port translates the mod's PowerPC code to C and compiles it with the
compiler setup already installed, then loads it at startup. Mods hook or replace game
functions at runtime; the game code itself is not translated again.

The idea follows the Zelda64Recomp / N64Recomp mod system (ideas only, no code copied).
Native SDK v1 mods (`runtime/include/wwhd_mod.h`, [mod-manager.md](mod-manager.md)) stay
supported.

## Contents

- [For players: enabling code mods](#for-players-enabling-code-mods)
- [Writing a mod](#writing-a-mod)
- [Hooks and replacements](#hooks-and-replacements)
- [Game functions, data and objects](#game-functions-data-and-objects)
- [Host services](#host-services)
- [Port settings](#port-settings)
- [Drawing on screen (HUD API v1)](#drawing-on-screen-hud-api-v1)
- [Packages with content and images](#packages-with-content-and-images)
- [Trust](#trust)
- [Save states](#save-states)
- [Limits](#limits)
- [How it works](#how-it-works)
- [For maintainers and tool authors](#for-maintainers-and-tool-authors)

## For players: enabling code mods

Code-mod support is **off** by default. Content replacements, settings presets, Cemu
graphics packs and Native SDK v1 mods don't need it.

1. Open **Settings (F1) → Mods** and choose **Enable code mods (PowerPC mods)**. On macOS the
   Gameplay menu has the same option.
2. Confirm the rebuild of the game code. It usually takes a few minutes; the dialog shows
   progress and errors. Cancel keeps the current build.
3. Choose **Restart now** (or restart later). Support is active from the next start.

Turning support off works the same way. Both variants are cached, so switching back to one
you built before is quick. Keep the complete release folder and the compiler setup
downloaded: building code mods uses those local tools. If they are missing, the Mods tab asks
you to run setup again.

You can install a code mod while support is off; the Mods tab offers to turn support on.
Enabling a code mod asks you once to confirm that you trust it (see [Trust](#trust)).
Enabling, disabling and changing options of code mods take effect after a restart.

## Writing a mod

### Toolchain

You need clang with the PowerPC backend, and lld. You don't need devkitPPC.

- **macOS:** `brew install llvm lld`, then use `$(brew --prefix llvm)/bin/clang` and
  `$(brew --prefix lld)/bin/ld.lld`. Apple's own clang has no PowerPC target.
- **Windows:** install MSYS2, open its CLANG64 shell and run
  `pacman -S mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-lld make`. The official
  Windows LLVM installer and llvm-mingw may lack the PowerPC backend.
- **Linux (Debian/Ubuntu):** `sudo apt install clang lld make`. Check that
  `clang --print-targets` lists PowerPC.

### Write

Include `wwhd_guest.h` and the generated `wwhd/` headers. A hook that runs every time Link's
per-step function runs:

```c
#include "wwhd_guest.h"
#include "wwhd/functions.h"

WWHD_HOOK(WWHD_ADDR_daPy_Execute, on_link_step, (void* link)) {
    static u32 steps;
    if (++steps == 1) wwhd_log("Link's first logic step");
}
```

### Build

```sh
clang --target=powerpc-unknown-eabi -mcpu=750 -O2 -ffreestanding \
  -fno-builtin -nostdlib -fno-jump-tables -ffunction-sections -fdata-sections \
  -I<sdk>/include -c mod.c -o mod.o
ld.lld -m elf32ppc -r mod.o -o mod.elf
```

`<sdk>/include` is `sdk/guest/include` in a release folder, or `runtime/guest/include` in a
source checkout. `-mcpu=750` keeps to the game CPU's instructions. Link with `-r` only: the
ELF must stay relocatable (the port picks its address). `-m elf32ppc` is needed on Windows,
where MSYS2's lld defaults to PE. The same `mod.elf` works on every desktop platform.

### Package and install

Put a `manifest.json` next to `mod.elf`:

```json
{
  "format_version": 1,
  "game_id": "wwhd-usa",
  "id": "hello-link",
  "name": "Hello Link",
  "version": "1.0.0",
  "kind": "guest",
  "guest": {"api_version": 1, "elf": "mod.elf", "heap_size": 262144}
}
```

Install the folder with **Mods → Installed packages → Choose folder → Install package**, or
zip it (`python3 -m zipfile -c hello-link.wwhdmod manifest.json mod.elf`) and install the
file. Enable it, confirm the trust prompt and restart. Build or load errors appear in the
package's details in the Mods tab.

Options, dependencies and conflicts use the normal manifest schema
([mod-manager.md](mod-manager.md)); read options with `wwhd_config_*`. To update a mod,
disable it, restart and install the new version with the same ID; its settings stay.
Distribute only your manifest, your ELF and resources you are allowed to share, never game
files.

The examples in [`examples/guest-mods`](../examples/guest-mods) are complete mods:

| Example | Shows |
| --- | --- |
| `heart-ticker` | entry and return hooks on Link's per-step function, calling a game function |
| `addcalc-replace` | replacing a game function (`cLib_addCalc2`) and calling the original |
| `hud-demo` | HUD drawing with package artwork and text, texture reload after a state load |
| `button-icons` | reading port settings, following the game's button panes |

## Hooks and replacements

| Macro | Runs |
| --- | --- |
| `WWHD_HOOK(target, name, (args))` | before the game function, with its arguments |
| `WWHD_HOOK_RETURN(target, name, (args))` | after it, with the same arguments; the game's result is kept |
| `WWHD_REPLACE(target, ret, name, (args))` | instead of it |
| `WWHD_GAME_ORIGINAL(...)` | the game's own code, below all mod hooks |

- Every game function can be hooked, including calls through function pointers.
- Hooks can't change the arguments or the result. To change them, replace the function and
  call the original yourself.
- Only one mod may replace a function; a second one is an error naming both mods. Any number
  of mods may hook one.
- Entry hooks run in load order, return hooks in reverse order.
- The port's own hooks (frame interpolation, 60 fps) stay outside mod hooks: a hook on a
  logic function runs once per logic step, at any frame rate; a hook on a drawing function
  runs once per displayed frame. Use `wwhd_logic_dt()` for anything time-based, and don't
  count displayed frames as game steps.
- Calling convention is the game's: arguments in r3–r10 / f1–f8, results in r3/r4/f1.

## Game functions, data and objects

The headers come from the public [HD decompilation](https://github.com/ZeldaWWHDDecomp/wwhd)
and target the USA game version; mods work on EU installs too (see below).

- `wwhd/functions.h`: every public verified function as a hook target, `WWHD_ADDR_<name>`.
  Ambiguous names have an address suffix.
- `wwhd/bindings.h`: callable declarations, `wwhd_<name>(...)`. Object pointers are `void*`.
  Functions returning a register pair use `wwhd_gpr_pair` with `WWHD_RESULT_R3` /
  `WWHD_RESULT_R4`.
- `wwhd/data.h`: named save/resource pointers, matrix stack and item tables.
- `actor.h`, `link.h`, `camera.h`, `items.h`, `messages.h`, `save.h`: partial views of game
  objects with checked field offsets; unknown parts are reachable as bytes.
- Functions not in the public decomp can still be hooked by address with
  `WWHD_GAME_FUNC(address, ret, name, (args))`; game variables with
  `WWHD_GAME_DATA(address, type)`.

**European installs.** Write your mod against USA addresses; when it is built on an EU install,
the port translates hook targets, game calls, function pointers and `WWHD_GAME_DATA`
references to the EU build. Functions that differ or don't exist in EU are refused with an
error naming them. Don't cast integers to pointers (that hard-codes a USA address), and only
use object fields that are the same in both versions.

## Host services

| Service | What it does |
| --- | --- |
| `wwhd_log`, `_int`, `_hex`, `_float` | Log lines tagged with your mod's ID |
| `wwhd_config_int`, `_bool`, `_float`, `_string` | Your mod's options (values from startup; fallback on missing or wrong type; strings copy into your buffer) |
| `wwhd_malloc`, `wwhd_free` | Per-mod heap, 16-byte aligned; size from `guest.heap_size` (default 256 KiB, max 8 MiB); null when full |
| `wwhd_input_read` | Buttons, sticks and touch (read-only) |
| `wwhd_file_read`, `wwhd_file_write` | Flat file names in your mod's own data folder, at most 1 MiB per call; write replaces the file; return bytes or -1 |
| `wwhd_logic_dt`, `wwhd_logic_step` | Length of the current logic step in seconds (60 fps modes included) and the step counter |
| `wwhd_setting_get`, `wwhd_setting_changed` | Read-only port settings, below |
| `wwhd_hud_*` | Drawing on screen, below |
| `memcpy`, `memmove`, `memset` | As usual |

Your mod can also call any game function. There is no audio stream service yet.

## Port settings

`wwhd_setting_get(key, type, buffer, capacity)` returns the bytes written, or 0 for an unknown
key, wrong type or too small buffer (the buffer stays unchanged). Pass a null buffer and 0 to
get the size. Strings include the NUL; numbers are big-endian. Settings are never changed by
mods. `wwhd_setting_changed(key)` returns a revision that changes when the value changes; read
settings once per logic step.

| Key | Type | Values |
| --- | --- | --- |
| `input.face_layout` | STRING | `position`, `labels`, `custom` |
| `input.controller_mode` | STRING | `gamepad`, `pro` |
| `game.language` | U32 | Wii U language code 0–11 in use (set at startup) |
| `game.build` | STRING | `USA`, `EU` |
| `display.drc_mode` | STRING | `window`, `pip`, `auto`, `off`, `gamepad` |
| `display.aspect` | F64 | Aspect ratio of the current frame |
| `render.interp_fps` | U32 | Frame rate shown: 30, the interpolation rate, or 60 in true 60 mode |
| `render.true60` | BOOL | 0 or 1 |

```c
static char layout[16];
if (wwhd_setting_get("input.face_layout", WWHD_SETTING_STRING, layout, sizeof layout)) {
    /* layout holds the current preset name */
}
```

## Drawing on screen (HUD API v2)

Register one draw callback with `wwhd_hud_register(draw, screen)` from a game hook (a null
callback unregisters it). The port calls `draw(list)` after each logic step; record elements
with `wwhd_hud_emit(list, &element)`. The list is shown when the callback returns and stays
on screen until the next step. Only read game state in the callback.

```c
#include "wwhd_guest.h"
#include "wwhd/functions.h"

static wwhd_hud_element box;
static void draw(u32 list) {
    box = (wwhd_hud_element){
        .kind = WWHD_HUD_RECT, .anchor = WWHD_HUD_TOP_LEFT,
        .x = 20, .y = 96, .w = 160, .h = 40,
        .thickness = 1, .u1 = 1, .v1 = 1, .rgba = 0x204060C0
    };
    wwhd_hud_emit(list, &box);
}
WWHD_HOOK(WWHD_ADDR_daPy_Execute, register_box, (void* link)) {
    (void)link;
    wwhd_hud_register(draw, WWHD_HUD_BOTH);
}
```

- **Elements** (`wwhd_hud_element` in `wwhd_guest.h`): filled or outlined rectangles and
  circles, lines, text and images. Set `thickness`, `u1` and `v1` to 1 unless you need other
  values. Circle `size` is the radius, text `size` the height. Images support UV subrects,
  tint, rotation and alpha or additive blending. Keep elements and text in static storage or
  your heap.
- **Coordinates:** TV 1280 × 720, GamePad 854 × 480 (`WWHD_HUD_BOTH` uses TV coordinates,
  scaled). Anchors keep elements at the screen edges on wide screens.
- **Images:** `wwhd_hud_texture(WWHD_HUD_PACKAGE, "assets/x.png")` loads your package's own
  art from `assets/` or `textures/`; `WWHD_HUD_DATA` loads from your mod's data folder.
  Keep the handle and free it with `wwhd_hud_release`. After a state load the handles are
  gone: compare `wwhd_hud_epoch()` in each callback and reload when it changes.
- **Limits:** per list 1024 elements and 64 KiB of text; per mod 32 images and 16 MiB of
  decoded pixels. An invalid element (bad UTF-8, bad handle, non-finite numbers, over the
  limits) drops the whole list, with a message in the Mods tab.

HUD v2 adds bounded clip rectangles without changing the 72-byte element layout.
Record `WWHD_HUD_CLIP_PUSH` and `WWHD_HUD_CLIP_POP` with
`wwhd_hud_clip(list, &element)`, using the normal element initialization above.
Push uses `x/y/w/h` and `anchor`; its screen-aligned rectangle intersects the
current parent clip and screen. Geometry, image rotation and UVs remain unchanged.
Pop restores the parent. Up to 16 nested clips are allowed; underflow, overflow or
an unbalanced callback drops the entire list. Clips belong to one recording list
and cannot affect another mod. Send ordinary drawing elements through
`wwhd_hud_emit`; that service rejects clip commands.

`WWHD_HUD_API_VERSION` is 2; the guest module ABI and manifest `guest.api_version`
remain 1. Importing `wwhd_hud_clip` is the runtime capability check: older hosts
lack that service and refuse the module during loading. Existing HUD v1 mods
continue to load without it. Do not emulate clipping by moving or shrinking the
image, since that changes registration at a map boundary.

It draws on Metal and Vulkan, in the TV picture and the GamePad screen (window or
picture-in-picture); the settings overlay stays on top. Positions are not interpolated
between logic steps.

## Packages with content and images

A code mod may also contain content replacements in `content/` (or a `content_dir` named in
the manifest) and original art in `textures/` or `assets/`. Content follows the same rules
as content-only packages ([mod-manager.md](mod-manager.md)) and is active only when the code
loads. With code mods off, neither part applies.

Images must be your own artwork, never game assets: PNG, at most 2048 × 2048 and 16 MiB
each, at most 32 per package and 16 MiB of decoded pixels in total. Invalid images make the
installation fail.

## Trust

A code mod becomes native code inside the game process. Its memory accesses stay inside the
game's memory, and it reaches the system only through the services above and the game's own
functions, but that is not a sandbox: it can crash the game or damage saves. So enabling a
code mod asks once, like native mods. The confirmation covers the exact ELF (and, for
packages with content or images, every file); a changed package asks again. Rebuilding the
same ELF after a port update doesn't ask.

## Save states

Full save states include the mods' memory and heaps. Both state formats record which mods
(ID and version) were loaded; loading a state made with different mods shows a warning and
continues. Loading a state never installs, enables or disables mods. Files a mod writes
itself are not part of save states.

## Limits

- Desktop only (macOS, Windows, Linux). Android doesn't support code mods yet.
- Changing enabled mods or their options needs a restart.
- Mods live in a 16 MiB region of game memory (`0x7F000000`–`0x80000000`); each mod gets its
  own 64 KiB-aligned part.
- No audio streams, no events, no exports between mods yet.
- Instructions the translator doesn't support are reported when the mod is built.

## How it works

- **Hook checks.** With code mods on, setup builds the game code with a one-byte check at the
  start of every game function. When a mod hooks a function, its flag is set and the call runs
  the mod hooks, the replacement or the original. With code mods off, the checks are not in
  the game code at all.
- **Building a mod.** On the first start after enabling, the port lays out the ELF at its
  assigned address, resolves relocations, translates the PowerPC code to C with the same
  translator as the game, and compiles it with the local compiler into a small library. The
  result is cached; a port update that changes the translator or compiler rebuilds it
  automatically. The module imports nothing from the game executable, so it doesn't depend on
  how the game was built.

## For maintainers and tool authors

**Regenerating the headers** from a clean clone of the public decomp (never a private branch):

```sh
git clone https://github.com/ZeldaWWHDDecomp/wwhd.git build/public-wwhd
git -C build/public-wwhd checkout 47e1dbc3886cfd8233859dffd73efc41a04a9130
python3 tools/guestmod/regenerate_sdk.py --public-clone build/public-wwhd
python3 tools/guestmod/regenerate_sdk.py --public-clone build/public-wwhd --check   # what CI runs
```

**Build tool** (`tools/guestmod/build_guest_mod.py`, used by the mod manager):

- `PACKAGE --out CACHE --base ADDR [--build EU] [--cc-json '[...]'] [--include DIR] --json`
  builds a module; the last line of output is JSON with `ok`, `module`, `cached`, `error`
  plus memory and ELF metadata.
- `--inspect --base ADDR --json` reports the memory size without compiling.
- `--check-cache-json '[{id, package, base, module}, ...]'` checks prepared modules without
  compiling and returns `{ok: true, valid: [ids]}`.

Setup writes `guest-sdk.json` (Python, compiler arguments, translator, headers) into the game
data folder; `WWHD_GUEST_BUILD_CONFIG` selects another one in development.
`WWHD_CODE_MODS=0|1` overrides the player's setting for automated runs (the build must have
been made with the matching option), and setup accepts `--code-mods 0|1`.

**Tests:** `tools/guestmod/test_guestmod.py` (set `WWHD_PPC_CLANG` and `WWHD_PPC_LLD`), the
`guestmods` CI workflow (examples compiled with each platform's host compiler), and the
opt-in real-game driver `tools/guestmod/test_code_mods_e2e.py`.

### Opt-in catalogue and setup lifecycle checks

`tools/guestmod/test_package_lifecycle_e2e.py` exercises a reviewed guest ZIP and
its real catalogue metadata. Supply an installed release/data directory, regional
normal save and extracted HD game, the ZIP, source `catalogue.json`, an explicit
private game-source JSON and a private output folder. The JSON maps declared game
IDs to the player's local paths (for example `gc_wind_waker` to an RVZ); use `{}`
for a mod without a game-source step. Run separately with `--region USA`/`EU` and
`--renderer metal`/`vulkan`; `--mode` accepts `30`, `interp60` and `true60`.

Before launching, the driver invokes the real installer with
`--rebuild-code-mods --code-mods 0 --jobs N` and verifies its completed off cache.
Use the driver’s `--jobs 1..4` to reserve compiler capacity; the default is four.
It records the original generated-C and game-object hash maps, executable SHA,
fingerprint, cache hit/fresh build status and elapsed rebuild time. After the UI
support rebuild it verifies the on selection and cache record, confirms that the
off baseline remains unchanged, and observes each running process's executable
path against the verified selection. Setting an environment flag alone is not
accepted as evidence of compiled hooks being disabled or enabled.

The driver refreshes and installs through the catalogue worker, rebuilds code-mod
support while the package remains disabled, restarts, selects/validates sources,
runs preparation and builds the guest module through normal setup workers, then
enables it for the following restart. It checks setup receipts, declared output
files, regional build allocation and module existence, activation after restart,
disable-until-restart and removal. Normal saves are copied for each process; full
states are never transferred between versions. Only owned game processes are
stopped. The default functional session limit is four and disk floor is 15 GiB;
these checks do not measure performance.

The setup diagnostics require all of `WWHD_NO_HOST_INPUT`, an explicit
`WWHD_MOD_MANAGER_DIR`, `WWHD_TEST_MOD_SETUP` and an absolute private
`WWHD_TEST_GAME_SOURCES` JSON path. They call the same source validators and setup
APIs as the UI, and do not accept native-code trust automatically. The driver uses
the existing explicit isolated-test trust switch; native file-picker interaction
and the trust dialog still require separate UI review. Ordinary runs never select
setup actions from these inputs.

Local verification copies the supplied ZIP beside a private catalogue index and
uses the existing relative-file fixture exception. This verifies catalogue
parsing, hashes, installation and setup, but does not verify remote hosting.
Published catalogues continue to require absolute HTTPS package URLs. Runtime
captures need visual review to establish each mod's visible behavior; successful
loading and receipts alone are not a visual acceptance test.


### Hidden HUD composition diagnostic

Ordinary hidden runs do not present the HUD, so they cannot establish its GPU
composition cost. Internal renderer verification can opt into
`WWHD_TEST_OFFSCREEN_FRAMES=1..10000` together with `WWHD_HIDDEN_WINDOWS=1`,
`WWHD_NO_HOST_INPUT=1` and `WWHD_SIM_SCREEN=1280x720`. This encodes the existing TV
composition path into one reusable offscreen target on Metal or Vulkan. It adds
no image readback, presentation or extra queue wait. The bounded diagnostic
refuses an invalid configuration or exhausted frame allowance. With the flag
absent, it allocates no diagnostic target and encodes no diagnostic composition.

`[headless-compose]` records encoded frames and HUD vertices, indices and command
counts. The `hud-cost` fixture emits 200 rectangles (800 vertices and 1,200 indices)
when enabled, and registers no HUD callback with `draw` false. Both variants must
use the same host, module, compatible full state and warmed cache. Verify actual
state restoration and visible rectangles before measuring. Save-state toasts are
additional overlay geometry and last several wall-clock seconds: exclude their
windows and verify the diagnostic counters throughout the measured interval.
Uncapped game time alone does not establish that a toast has expired.

Measure render-thread CPU time separately from GPU elapsed time. Keep captures
and readback out of measured runs, use a quiet exclusive window, interleave the
variants, and report medians, IQRs and each paired difference. A difference smaller
than the observed variability is inconclusive. This fixture covers rectangles;
it does not measure texture-upload or large-text cost.
