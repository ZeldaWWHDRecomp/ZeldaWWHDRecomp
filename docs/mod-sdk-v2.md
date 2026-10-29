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
- [Live options, input filters and touches](#live-options-input-filters-and-touches)
- [Port settings](#port-settings)
- [Drawing on screen (HUD API v2)](#drawing-on-screen-hud-api-v2)
- [Letters](#letters)
- [Charts and markers](#charts-and-markers)
- [Items](#items)
- [The story notebook](#the-story-notebook)
- [Packages with content and images](#packages-with-content-and-images)
- [Trust](#trust)
- [Save states](#save-states)
- [Limits](#limits)
- [How it works](#how-it-works)
- [For maintainers and tool authors](#for-maintainers-and-tool-authors)

## For players: enabling code mods

Code-mod support is **off** by default. Content replacements, settings presets, Cemu
graphics packs and Native SDK v1 mods don't need it.

The simplest way: switch the code mod on in **Settings (F1) → Mods**. Its review says that
code-mod support is added; after **Install** (or **Switch on**) the game code is rebuilt once
(a few minutes; the progress view shows it), the game restarts by itself and the setup
continues until the mod is on. A failed step shows one sentence, **Show details** and **Try
again**; Cancel keeps the current build.

To switch support on or off by hand: **Mods → More → Code-mod support → Use code mods** (on macOS the
Gameplay menu has the same option), confirm the rebuild, then **Restart now** (or later).
Support is active from the next start. Both variants are cached, so switching back to one
you built before is quick. Keep the complete release folder and the compiler setup
downloaded: building code mods uses those local tools. If they are missing, the Mods tab asks
you to run setup again.

Accepting the review records your trust in this package version (see [Trust](#trust)); a
changed package asks again. Individual steps can be run again under **Troubleshooting** on
the mod's page while it is off. Switching code mods on or off and changing their options take
effect after a restart.

### GameCube files used by mods

The review of a mod that needs it asks for your GameCube Wind Waker disc image (`.iso`,
`.gcm` or `.rvz`) or extracted game folder; **Mods → More → GameCube game for mods**
changes it later. The setting shows the recognised
region; some mods require a specific region. These files stay on your computer.

Mods use this shared copy automatically. A mod with a missing or incompatible copy shows
a warning and a **Go to GameCube game for mods** button, and cannot be enabled until the
copy passes validation. Then choose **Set up** to complete its remaining preparation.
Changing or clearing the copy turns dependent mods off in every profile and marks their
preparation for rerunning. Prepared files are kept; restart to unload any active mod,
then rerun its setup before enabling it again.

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

Put a `manifest.json` next to `mod.elf` (`game_id` is `"wwhd"` for both regions, from mod manager 1.5;
`"wwhd-usa"` from older manifests is still accepted; the game build is checked when the code mod is built):

```json
{
  "format_version": 1,
  "game_id": "wwhd",
  "minimum_manager_version": "1.5.0",
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

**Options on the item menu's Settings page.** An option with `"menu": true` also shows in the
**Mods** tab of the Settings page in the game's item menu (press + and go left of the items), so
players can switch it without leaving the game. Switches (`bool`), choices (`enum`) and numbers
(`number`, with its `min`, `max` and `step`) are shown; other types stay in the Mods tab of the
Settings window. Optional fields:

- `"section"`: a group name shown with the option (`"Songs"`); options are listed by section.
- `"live": true`: your mod follows a change while the game runs. Without it the page says that the
  change applies at the next start, because `wwhd_config_*` values are read at startup.

The page and the Settings window change the same value, saved in the player's mod profile.

```json
"options": [
  {"id": "replay", "type": "enum", "name": "Replay conducted songs", "choices": ["Off", "Once", "Always"],
   "default": "Off", "menu": true, "section": "Songs"}
]
```
Distribute only your manifest, your ELF and resources you are allowed to share, never game
files.

The hooks example [`heart-ticker`](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDMods/tree/main/heart-ticker) is in the
mod repository: entry and return hooks on Link's per-step function, calling a game function.

The examples in [`examples/guest-mods`](../examples/guest-mods) are complete mods:

| Example | Shows |
| --- | --- |
| `addcalc-replace` | replacing a game function (`cLib_addCalc2`) and calling the original |
| `hud-demo` | HUD drawing with package artwork and text, texture reload after a state load |
| `button-icons` | reading port settings, following the game's button panes |

Story mods live in the mod repository ([ZeldaWWHDMods](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDMods))
with their content:

| Package | Shows |
| --- | --- |
| `story-notebook` | the story system's required package: the letter that brings the story notebook, the notebook's look, a first quest as data (see [The story notebook](#the-story-notebook)) |
| `rito-letter` | a story mod's letter, delivered once per save file by the Rito postbox |
| `story-chart` | a story mod's chart, like a treasure chart: the red circle on Windfall's lighthouse once Link stands there |

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
| `wwhd_config_int`, `_bool`, `_float`, `_string` | Your mod's options (values from startup, or as they are now for `"live"` options; fallback on missing or wrong type; strings copy into your buffer) |
| `wwhd_config_changed` | A live option's revision, below |
| `wwhd_malloc`, `wwhd_free` | Per-mod heap, 16-byte aligned; size from `guest.heap_size` (default 256 KiB, max 8 MiB); null when full |
| `wwhd_input_read` | Buttons, sticks and touch (read-only) |
| `wwhd_input_filter_register` | Change the controller state the game reads, below |
| `wwhd_touch_register`, `wwhd_touch_claim` | Touches (or mouse clicks) on parts of the TV picture your mod claims, and pinches there, below |
| `wwhd_screen_point` | Where the drawn camera shows a point of the world, in the touches' layout units, below |
| `wwhd_mic_open`, `_close`, `_state`, `_pitch` | The microphone's pitch, for an option the player turned on (asks for permission then), below |
| `wwhd_file_read`, `wwhd_file_write` | Flat file names in your mod's own data folder, at most 1 MiB per call; write replaces the file; return bytes or -1 |
| `wwhd_logic_dt`, `wwhd_logic_step` | Length of the current logic step in seconds (60 fps modes included) and the step counter |
| `wwhd_setting_get`, `wwhd_setting_changed` | Read-only port settings, below |
| `wwhd_hud_*` | Drawing on screen, below |
| `wwhd_audio_*` | Bounded PCM streams, below |
| `wwhd_mail_send`, `wwhd_mail_state` | A letter from your mod, delivered by the Rito postbox, below |
| `wwhd_chart_*` | A chart from your mod that marks places on Link's sea chart, below |
| `wwhd_quest_*` | Your mod's quests in the story notebook, below |
| `memcpy`, `memmove`, `memset` | As usual |

Your mod can also call any game function. Original synthesized audio can use the PCM stream service below.

## Live options, input filters and touches

These let one package hold several small features that the player switches while playing (the QoL
package in the mod repository is built this way).

**Live options.** Mark an option `"live": true` in the manifest. `wwhd_config_*` then return its value as it is
now, and `wwhd_config_changed(id)` returns its revision: 1 at start, one more each time the player changes it
in Settings or on the item menu's page (0 for an option your mod doesn't have). Compare it with the last
revision you saw, once per logic step, and read the value again when it changed. Keep your hooks installed and
return early while a feature is off. Options without `"live"` keep their start value.

```c
static unsigned long long seen;
void step(void) {
    unsigned long long r = wwhd_config_changed("clothes");
    if (r != seen) { seen = r; clothes_on = wwhd_config_bool("clothes", 0); }
}
```

**Input filters.** `wwhd_input_filter_register(fn)` calls `fn(state)` on each of the game's controller reads
(full logic steps, also at 60 fps), after the port's own rules (the touch controls, talks by touch) and before
the game sees it. Change `buttons` and the sticks; the touch fields are read only. Filters of several mods run
in their load order. One filter per mod; register 0 to remove it. The function must be your mod's own.

**Touches on the TV picture.** Register a callback with `wwhd_touch_register(fn)`, then claim rectangles with
`wwhd_touch_claim(slot, x0, y0, x1, y1)`: layout units, 1280 x 720 around the picture's centre with +y up (a
wider picture extends x), up to 8 slots. A finger (on Android with the on-screen controls shown or hidden) or a
mouse click on the desktop that lands in a claim goes
to your callback as `WWHD_TOUCH_DOWN`, `_MOVE`, `_UP` or `_CANCEL` with its position, and to nothing else: no
on-screen stick, no talk by touch. It stays yours until it lifts, even outside the rectangle. Where claims
overlap the newest wins. Claim only while your feature shows something to touch, and remove the claim (an
empty rectangle, x1 <= x0) when it doesn't, so the player's thumbs work the game again. Events arrive at the
next controller read, before the input filters, so a touch can become a button in the same read.

**Pinches.** A second finger next to your mod's finger (Android, with the on-screen controls shown or hidden), or the
mouse wheel over one of your claims (desktop), comes as `WWHD_TOUCH_PINCH` with `x` = the spread now divided by the
spread when the pinch began (above 1: the fingers moved apart, or the wheel turned away from the player; each notch is
15 % more or less), then `WWHD_TOUCH_PINCH_END` (on the desktop 0.3 s after the last notch). The finger itself keeps
its own events: its moves pause while the pinch lasts, and its `WWHD_TOUCH_UP` comes when it lifts. Hosts from before
pinches never send them.

**Points of the world on the picture.** `wwhd_screen_point(world, out)` takes a point of the current room (`world`:
x, y, z) and writes where the drawn camera shows it into `out[0]`, `out[1]`, in the same layout units as touches,
with the picture's shape and field of view as drawn (wider screens, the field-of-view setting). It returns 1, or 0
when the point is behind the camera or no camera is drawn. Compare touches with it on the logic step you use it:
the camera moves.

```c
f32 square[3] = {x, y, z}, at[2];
if (wwhd_screen_point(square, at) && (e->x - at[0]) * (e->x - at[0]) + (e->y - at[1]) * (e->y - at[1]) < 40 * 40) {
    /* the finger is on the square */
}
```

**The microphone.** For features like singing the songs. A mod may open the microphone only through an option
the player turns on: mark a bool option `"permission": "microphone"` in the manifest, and call `wwhd_mic_open()`
while it is on (the port refuses it otherwise). Nothing asks for the microphone before that: the first open asks
the system (Android's runtime permission, the macOS prompt; Windows and Linux open the default recording device).
If the player declines, the state becomes `WWHD_MIC_DENIED` and the port turns the option off again with a notice
over the game; the player can allow the microphone in the system settings and turn the option on again. Call
`wwhd_mic_close()` when you no longer listen (the port also closes it when the option goes off). Keep it closed
when nothing listens: the QoL package opens it once when the option is turned on (for the question), then only
while Link conducts.

```json
{"id": "sing", "type": "bool", "name": "Sing the songs", "default": false, "live": true, "permission": "microphone"}
```

The port analyses the sound for its pitch and drops it: a mod never gets samples, and nothing keeps, writes, logs
or sends the sound. `wwhd_mic_pitch(out)` writes the newest analysed window's (about 21 ms, every 1024 samples at
48 kHz) pitch in Hz to `out[0]` (0 when there is none: silence, noise), its loudness (RMS, 0..1) to `out[1]` and its
clarity (0..1, how clean a tone it is) to `out[2]`, and returns how many windows were analysed so far; a new value
means a new window. `wwhd_mic_state()` is one of `WWHD_MIC_CLOSED`, `_ASKING`, `_OPEN`, `_DENIED`, `_UNAVAILABLE`.
Test aids for scripted runs: `WWHD_TEST_MIC=from-to:hz,...` (TV frames) feeds tones instead of a device without
asking; `WWHD_TEST_MIC_DENY=1` answers the question with "declined".

Hosts with these calls (and `wwhd_screen_point`, pinches and `"permission"`) have mod manager version 1.6.0: set
`"minimum_manager_version": "1.6.0"` in a manifest that uses them, so older ports refuse the package up front ("needs
a newer version of the game"). A module built for services the port doesn't have says which one when it fails to load.

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
| `settings.<id>` | BOOL, U32 or F64 | A setting of the item menu's Settings page by its ID: switches are BOOL, choices U32 (the choice's index), numbers F64. For example `settings.camera.invert-y`, `settings.direct-camera.speed`, `settings.screen.fov` |

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

### Current-room maps for mod dungeons

`wwhd_dungeon_room_get(&room, cached_revision)` returns the current mod-stage
room's existing map pixels as an owned HUD image and its world X/Z bounds. Call it
from a HUD callback. It returns 0 when the stage has no map, the room has no image,
or the host predates this service. Stock dungeon images remain the mod's responsibility.

`wwhd_dungeon_room` is an 808-byte guest structure: version (1), image handle,
revision, signed floor, four float bounds (min X/Z, max X/Z), marker count, reserved
zero, then up to 64 `(float x, float z, u32 kind)` markers. Kinds are 1 (unopened
chest) and 2 (boss door). The port filters markers by current room and compass
ownership, using the same save record and map pixels as its map page. This does
not reveal neighboring rooms or change save flags.

Pass cached revision 0 on first use and after `wwhd_hud_epoch()` changes. A new
revision supplies a new image; release the preceding handle. An unchanged revision
returns image 0 and the caller retains its existing handle. Geometry and markers
are refreshed every call, including chest opening; rebuilding a room changes its
revision. Never pass a nonzero revision after releasing its image.

The helper uses the existing `wwhd_setting_get` query transport with key
`mod-stage.current-room-map` and structured type 5. Older hosts return 0 for this
unknown key/type, so the guest keeps working with no new import or translator
requirement. Querying a null buffer with zero capacity reports 808 on hosts that
support the query. The guest module ABI remains unchanged.

### The game's layout art

A mod can draw with the game's own 2D art without shipping it. The port reads that art from the
player's installed 2D pack, in the language the game uses, while the game runs:

- `wwhd_hud_texture(WWHD_HUD_GAME, "<Layout>/<texture>")` loads a texture of one of the game's layout
  archives as an owned HUD image (for example `"Map_00/MapShip_00^t"`). Add `#<material>`
  (`"BtnMapBalloon_00/r2c_b^t#P_DummyMapBig_00"`) to colour it with that material's black and white
  colours, the way the game draws it. End the name with `@2`, `@3` or `@4`
  (`"BtnMapBalloon_00/r2c_b^t#P_DummyMapBig_00@3"`) to get the picture enlarged that many times with
  Lanczos-3 resampling (premultiplied alpha) and sharpened (an alpha-weighted unsharp mask on the colour,
  a steeper alpha ramp at outlines), crisper than the HUD's own scaling when small soft art is drawn
  large. Images count towards the usual per-mod limits; release them as usual.
- `wwhd_game_pane_get("layout-pane:<Layout>/<pane>", &pane)` reads a pane of such a layout. It gives
  the centre and size in the layout's root coordinates, with +y up and the parents' translation and
  scale applied. It also gives the pane's own rotation and alpha, whether it and its parents are
  visible, and, for a picture pane, its texture's name. The result is a 64-byte `wwhd_game_pane`.
  The key `layout-texture:<Layout>/<texture>` checks that a texture exists, without loading it. It
  sets flag 4 and returns the texture's size in `w` and `h`.

Names may contain letters, digits and `_ ^ + - .` only. A missing layout, texture or pane returns 0,
with the reason in the Mods tab. Older hosts return 0 for both: the texture source and the query key
are unknown to them, and nothing new is imported. Only the names belong in your mod; never copy the
pictures or layout data into a package. The example is `hd-minimap` in the mod repository.

## Letters

A story mod (a mod that adds a quest) can announce itself the way Wind Waker does: with a
letter. The Rito postbox hands it out like the game's own mail, and the game shows it in its
letter window.

```c
#include "wwhd_guest.h"
#include "wwhd/functions.h"
#include "wwhd/save.h"

WWHD_HOOK(WWHD_ADDR_daPy_Execute, send_letter, (void* link)) {
    (void)link;
    u16 max_life = dComIfGs_player_status_a.max_life;
    if (max_life < 12 || max_life > 80) return;  /* no save file loaded */
    wwhd_mail_send("hello", "A wandering storyteller",
                   "Dear Link,\na new tale has reached the Great Sea.\nFair winds!");
}
```

`wwhd_mail_send(letter_id, sender, text)` puts the letter in the postbox of the loaded save file,
unless that file already got it. Call it from a gameplay hook; calling it again (even every logic
step) never sends the letter twice. It returns the letter's state:

| Result | Meaning |
| --- | --- |
| `WWHD_MAIL_NONE` (0) | never sent to this save file (only from `wwhd_mail_state`) |
| `WWHD_MAIL_WAITING` (1) | waiting in the postbox (handed out once Link has the Delivery Bag) |
| `WWHD_MAIL_READ` (2) | handed out and read |
| `WWHD_MAIL_INVALID` (-1) | invalid ID, sender or text |
| `WWHD_MAIL_NO_ROOM` (-2) | no save file loaded, or its story record is full (255 entries per Quest Log, shared with charts and the notebook) |

`wwhd_mail_state(letter_id)` returns the state without sending; use it, for example, to start
your quest once the letter was read.

- **ID:** 1 to 32 characters `a`-`z`, `0`-`9`, `-`, `_`, `.`; unique within your mod.
- **Sender:** one line, up to 48 bytes. It is shown below the text as the signature.
- **Text:** plain UTF-8, up to 1200 bytes; `\n` breaks a line. Other control characters are
  refused (the game uses them for its own text codes). Keep lines short (about 40 characters)
  and the letter to about ten lines. Characters the game's font lacks are not shown.
- The text of the first call in a session is used; the letter can only be shown while your mod is
  on.

**Delivery.** Any Rito postbox hands out waiting letters, after the game's own, once Link has the
**Delivery Bag** (from Quill on Dragon Roost Island; the port asks the game, as its own characters
do). Until then a letter stays waiting in the save file: nothing is lost, and it comes with the
first postbox visit after Link gets the bag. The game opens its postboxes only after the first
pearl; letters from mods are handed out before that too, as soon as Link has the bag (the game's
own letters still wait for the pearl). A letter from a mod needs no postage. Like the game's
letters it comes with an enclosure, which Link takes after reading: the Story Notebook package's
letter encloses the **story notebook**, every other one a green rupee. Several waiting letters are
handed out one after another.

**Letters in the manifest.** A letter needs no code: declare it in `manifest.json` and the port
sends it while your mod is on. Once Link has read it, he has what it encloses: your charts
(`"chart:<id>"`, see [Charts and markers](#charts-and-markers)) and items (`"item:<id>"`, see
[Items](#items)), each given once. Such a letter makes no entry in the story notebook; for a letter
that starts a quest, use the quest's own `"letter"` (see [Quests in the manifest](#quests-in-the-manifest)).

```json
"letters": [
  {"id": "map-letter", "sender": "A sailor",
   "text": "Dear Link,\nI found this old map in a bottle.\nMaybe you can find what it marks.",
   "encloses": ["chart:old-map"]}
]
```

Up to 8 letters per mod; the same rules for ID, sender and text as above. Every chart and item a
letter encloses must be declared in the manifest, and a letter's ID must not be a quest letter's.

**A letter that waits.** A letter can wait for your story to get somewhere (`"when"`) and then for
some days of the game's calendar (`"after_days"`), for example a thank-you letter that arrives two
days after a quest is done:

```json
"letters": [
  {"id": "thanks", "sender": "The lighthouse keeper",
   "text": "Dear Link,\nsince you brought the light back,\nthe ships come safely home.",
   "when": {"quest": "lighthouse", "done": true}, "after_days": 2}
]
```

- **when** (optional): every condition in it must hold before the letter is sent.
  - `"quest"` with `"step": 2`: that quest of your manifest has reached step 2 or gone past it.
  - `"quest"` with `"done": true`: that quest is done.
  - `"quests"`: a list of up to 8 such conditions, `[{"quest": "a", "step": 2}, {"quest": "b", "done": true}]`.
  - `"flag"` / `"not_flag"`: one of your mod's flags is set / is not set (the same flags actors
    and scenes use, see [Actors and scenes](mod-sdk-v3.md)).
- **after_days** (optional): 1 to 255 days. The game's calendar moves on one day each time its
  clock passes midnight (also with the Song of Passing). The days count from the day `when` first
  held (without `when`, from the first day your mod is on). Once the count has started, it goes on
  even if the condition stops holding.
- Without `when` and `after_days` the letter is sent as before, as soon as your mod is on.

Once due, the letter waits in the postbox like any other. The day the count started is kept in the
save file's story record, so it survives saving and loading and save states; it is freed when the
letter is sent. The quests, steps and flags `when` names must be your manifest's own.

**The story notebook** is the port's journal for story mods (see [The story notebook](#the-story-notebook)).
Link keeps it in the Delivery Bag. Receiving it writes nothing to the inventory: it is kept in the
story record (below).

**Saving.** Whether a letter was sent and read, and whether the notebook was received, is kept with
the save file (see [Where story progress is saved](#where-story-progress-is-saved)). A letter
received but not saved arrives again after loading the older save. The text is not stored; turning
your mod off leaves its letters waiting in the record, and nothing changes for the postbox.

**Players without letter mods.** The postbox, message and item functions are only hooked while a loaded
mod uses `wwhd_mail_*`. If another mod replaces one of them, letters from mods are not delivered
in that session and the log says so.

## Charts and markers

A story mod can give Link a chart the way Wind Waker gives him a treasure chart, and the sea
chart treats it the same way. In the game, an owned treasure chart puts nothing on the sea chart:
its X is only on the chart itself. Once the treasure is salvaged, the game's red hand-drawn circle
marks the spot on the sea chart. Your mod's chart behaves the same way: while Link only owns it,
the sea chart shows nothing for it; when your mod finishes it, the red circle marks each of its
markers' spots. (The guiding marks of your quest are the story notebook's job: the quest shown on
the chart gets the game's white story ring, see [The story notebook](#the-story-notebook).)

```c
#include "wwhd_guest.h"
#include "wwhd/functions.h"
#include "wwhd/save.h"

WWHD_HOOK(WWHD_ADDR_daPy_Execute, quest_step, (void* link)) {
    (void)link;
    u16 max_life = dComIfGs_player_status_a.max_life;
    if (max_life < 12 || max_life > 80) return;  /* no save file loaded */
    if (wwhd_mail_state("hello") == WWHD_MAIL_READ) wwhd_chart_give("tale");
    wwhd_chart_mark("tale", "start", WWHD_CHART_COLUMN_X('D'), WWHD_CHART_ROW_Z(2),
                    WWHD_CHART_ICON_STORY, "Windfall Island: the storyteller");
}
```

| Service | What it does |
| --- | --- |
| `wwhd_chart_give(chart)` | Link gets the chart in the loaded save file (once; later calls change nothing) |
| `wwhd_chart_finish(chart)` | the chart is done: the red circle marks its markers' spots |
| `wwhd_chart_state(chart)` | the chart's state, without changing it |
| `wwhd_chart_mark(chart, marker, x, z, icon, label)` | adds the marker, or moves it if it exists |
| `wwhd_chart_unmark(chart, marker)` | removes the marker; `NULL` removes all markers of the chart |

All of them return the chart's state in the loaded save file, or an error:

| Result | Meaning |
| --- | --- |
| `WWHD_CHART_NONE` (0) | not given to this save file |
| `WWHD_CHART_OWNED` (1) | Link has it (nothing on the sea chart yet, like an owned treasure chart) |
| `WWHD_CHART_DONE` (2) | finished: its markers have the red circle |
| `WWHD_CHART_INVALID` (-1) | invalid ID, place, icon or label |
| `WWHD_CHART_NO_ROOM` (-2) | no save file loaded, or its record is full; or too many markers |

- **IDs:** chart and marker IDs are 1 to 32 characters `a`-`z`, `0`-`9`, `-`, `_`, `.`; chart IDs
  are unique within your mod, marker IDs within the chart.
- **Place:** `x` and `z` in the Great Sea's world coordinates, the ones Link's position uses. The
  sea is 7 × 7 squares of 100000 units; the center of square D4 is (0, 0), `x` grows to the east
  and `z` to the south. `WWHD_CHART_COLUMN_X('A'..'G')` and `WWHD_CHART_ROW_Z(1..7)` give the
  center of a square, so Windfall Island (D2) is `WWHD_CHART_COLUMN_X('D'), WWHD_CHART_ROW_Z(2)`.
  The game's own marks sit on the island squares too.
- **Icon:** `WWHD_CHART_ICON_STORY` is the only one for now (the sea chart draws the game's red
  circle for a finished chart's marker, whatever the icon).
- **Label:** one line of UTF-8, up to 64 bytes, or `NULL`. The sea chart shows the circle only,
  like the game's own marks; the label names the marker for other screens (a quest notebook, a
  minimap).
- **Markers** are placed by your mod in every session, like the letter text: call
  `wwhd_chart_mark` from a gameplay hook (every logic step is fine) with the place for the
  current step of your quest, and move or remove it as the quest goes on. Up to 8 markers per
  mod, 32 in all.

**What the player sees.** While Link owns a chart, nothing of it is on the sea chart. Once the
chart is finished, each of its markers has the game's red hand-drawn circle (the mark of a
salvaged treasure) at its exact spot: on the sea chart of the MAP screen, on the GamePad, on the
TV (Pro Controller) and on the warp map, and in the zoomed view of the marker's square. The circle
is drawn and animated by the game: it is the circle of the marker's own square, or of another
square, as long as the game does not show that square's circle itself. The game's own circles
always come first: when Link salvages the treasure of a square whose circle a marker uses, the
game gets it back and the marker takes another square's circle (with all 49 in the game's use, it
waits). In the zoomed view of a square there is one circle: the notebook's shown quest comes first,
then finished charts' markers, and the game's own circle wins over both.

**Charts as data: the chart list.** Declare your charts in the manifest, and they are charts like
the game's: while Link owns one, the MAP screen's chart list shows it after the game's charts, with
its name, and opening it shows its square's map with the X at its place (the game's own chart view;
once the chart is finished, its red circle).

```json
"charts": [{"id": "old-map", "name": "Old map",
            "description": "An old chart. An X marks the top of the lighthouse.",
            "place": {"x": 1759, "z": -202572}}],
"quests": [{"id": "hunt", "title": "The old map",
  "letter": {"id": "hunt", "sender": "A sailor", "text": "...", "encloses": ["chart:old-map"]},
  "steps": [{"n": 1, "text": "Find the treasure marked on the Old map.",
             "where": "On your Old map (chart list)", "find": "chart:old-map"}]}]
```

- **Declaring:** `id`, `name` (up to 40 bytes, the list's caption), `description` (up to 200 bytes)
  and `place` (the X). Up to 8 per mod.
- **Getting it:** a letter that encloses `"chart:<id>"` gives it when it is read; a resident's talk
  with `"then": {"give": "chart:<id>"}`; a placed actor's dialogue, a talk or a scene's end with the
  action `{"action": "give", "id": "chart:<id>"}` (see [Dialogue and actions](mod-sdk-v3.md#dialogue-and-actions));
  the quest's reward (`"reward": {"chart": "chart:<id>"}`, when the quest is done); or your code
  (`wwhd_chart_give`). Each way gives it once. A chart the manifest names must be declared.
- **Finishing it:** a step with `"find": "chart:<id>"` finishes the chart once the quest is past that
  step (your code reports the step, a condition or a talk moves the quest on); or your code calls
  `wwhd_chart_finish`. Finished: the red circle on the sea chart and in the chart view.
- **"Open chart":** while the current step's chart (`find`) is Link's, the notebook's step page has
  an Open chart plate (X): the notebook closes and the MAP screen opens with that chart selected.
- **In the list,** a mod chart stands in for a treasure chart the save file does not own: the
  game's chart bits are neither read nor written for it, and opening it in the list is kept in
  the chart's record. With every treasure chart owned there is no room in the list; the chart
  still works (the notebook, the red circle).

**A treasure chart on its own.** A chart that is just a treasure to find needs no quest and no
notebook entry. Declare it and send it in a letter; that is all. The Old map example:

```json
"charts": [
  {"id": "old-map", "name": "Old map", "description": "An old map. An X marks the top of the lighthouse.",
   "place": {"x": 1759, "z": -202572}}
],
"letters": [
  {"id": "map-letter", "sender": "A sailor",
   "text": "Dear Link,\nI found this old map in a bottle.\nMaybe you can find what it marks.",
   "encloses": ["chart:old-map"]}
]
```

Link gets the letter at the next Rito postbox; after reading it he owns the Old map, and the MAP
screen's chart list shows it with its X. Such a mod needs no code and no game files: a content
package (`"kind": "content"`) with only `charts` and `letters` (or items, or quests) is enough, and
it needs no `content_dir` (see [the mod manager](mod-manager.md)). A chart's `"item"` says what its treasure chest holds: one of your items
(`"item:<id>"`, see [Items](#items)) or a game item (`"game:purple_rupee"`, `"game:orange_rupee"`,
`"game:silver_rupee"`, `"game:heart_piece"`, `"game:heart_container"`); without it, an orange rupee. A quest whose only step is to find a chart gets a warning in
the log: make it a chart with a letter instead. A chart to find as one step of a longer quest is
fine (`"find": "chart:<id>"`, see [Quests in the manifest](#quests-in-the-manifest)).

**Saving.** Whether Link owns a chart, and whether it is finished, is stored in the save file in
the same record as the letters (it stays unused by the game itself; letters and charts of all mods
share its 19 places per save file). It is saved and loaded with
the game and kept by full and portable save states. Full save states hold the sea chart as the
game left it, without the markers' circles; they appear again on the next frame. The places of the
markers are not saved: your mod places them every session. Turning your mod off leaves its charts
in the record, and the sea chart shows only the game's own marks.

**Players without chart mods.** The sea chart functions are only hooked while a loaded mod uses
`wwhd_chart_*`. If another mod replaces one of them, markers are not shown in that session and the
log says so.

**For map mods (native).** `story_chart::shown_markers()` (`runtime/src/mods/story_chart.h`) lists
the markers the sea chart shows now, with their world place, icon and label, so a minimap can draw
them in its own style.

## Items

A story mod can give Link its own items. They are kept in the Delivery Bag like the game's
delivery items, but they never take one of the bag's eight places unless the player puts one
there: the bag gets extra pages for them. An item is declared in the manifest; Link gets it from
the mod's quests (a letter, a talk, a reward) or from the mod's code.

```json
"items": [
  {"id": "old-key", "name": "Old key", "description": "A rusty key. It smells of the sea.",
   "icon": "assets/old_key.png"},
  {"id": "lamp", "name": "Storm lamp", "description": "Shows the way in the dark.",
   "icon": "assets/lamp.png", "stack": 3, "use": {"action": "show"}}
]
```

- **id:** 1..32 of `a-z 0-9 - _ .`, unique within the mod.
- **name** (up to 40 bytes) and **description** (up to 200 bytes, line breaks allowed): what the
  item's Info box shows.
- **icon:** a PNG in your package, best 128 × 128 with transparency.
- **stack:** how many Link can carry, 1..254 (default 1). A give beyond it keeps the maximum.
- **use** (optional): what using the item does. Only an item with a use can go on X, Y or R.
  - `{"action": "show"}`: shows its Info box.
  - `{"action": "open_notebook"}`: opens the story notebook.
  - `{"action": "quest_step", "quest": "<quest id>", "step": 2}`: that quest of your manifest
    reaches the step.
  - `{"action": "event"}`: your code is told (`wwhd_item_used`).

**Giving items from the manifest.** A quest's letter can enclose an item
(`"encloses": ["item:old-key"]`), a resident's talk can give one (`"then": {"give": "item:old-key"}`)
and a quest's reward can be one (`"reward": {"item": "item:old-key"}`, given once when the quest is
done). Every item a quest names must be declared in `"items"`.

**Holding it up.** An item with a model of your own (`"held_model"`) and `"hold_up": true` is shown
like the game's own items when Link gets it: he holds the model up over his head with the game's get
message, instead of the small caption. See
[Original item models in Link's hand](mod-sdk-v3.md#original-item-models-in-links-hand-preview).

**From code:**

```c
#include "wwhd_guest.h"
s32 n = wwhd_item_give("lamp", 1);      /* the count now, or WWHD_ITEM_INVALID / WWHD_ITEM_NO_ROOM */
if (wwhd_item_count("old-key") > 0) wwhd_item_take("old-key", 1);  /* WWHD_ITEM_NOT_ENOUGH if fewer */
if (wwhd_item_used("token") > 0) { /* the player used it since the last call */ }
```

When Link gets an item, its name and description show with the game's small get jingle. If the
bag's pages are full (4 pages of 8 by default), the item is kept and gets a place as soon as one is
free; the caption says so.

**In the bag.** In the Delivery Bag window, R (or a tap on the page dots above the window) turns to
the extra pages and L back; B returns to the game's eight places. A on an item offers Info, Use and
a move into an empty place of the game's eight; A on an empty place there brings a mod item back.
X or Y puts an item with a use on that button. New items fill 4 pages by default (`WWHD_BAG_PAGES=1..16`);
when they are full, new items wait and the page says how many.

**Saving.** How many of each item Link has, where it sits in the bag and which button it is on are
stored in the save file's story area (see [Where story progress is saved](#where-story-progress-is-saved)),
never in the game's own item data. A save file without your mod is a normal save: the bag shows the
game's items only, and the buttons keep the game's items. Turning your mod on again brings its items
back. To draw an item in the game's own places and buttons, the port lends it one of the game's
unused item numbers for that save file; the game's data never refers to these numbers.

## The story notebook

With several story mods on, the player needs to know what each of them asks for. Wind Waker has
no quest log; the port adds one in the game's own way: a notebook that arrives by Rito mail.

**The story system and its required package.** Letters, quests, the notebook and mod charts are the
port's story system (the "Story system" built-in, on by default). It comes with one package from
the mod repository, **`story-notebook`**: its letter brings the notebook (its manifest's
`"encloses": ["notebook"]`; only this package's letter can), its manifest's `notebook` block gives
the notebook's look (`"icon"`: a PNG, `"model"`: a binary glTF the port turns into the game model on
the player's machine), and it holds a first small quest. The mod manager downloads it from the mod
catalogue by itself, confirms it like the port's own code and keeps it enabled while the story
system is on; it can't be switched off or removed on its own. Every story mod depends on it (a
manifest with `quests`, `charts`, `letters`, `items`, `models`, `actors` or `scenes` gets the
dependency even if it doesn't declare it; a package of [mod stages](mod-stages.md#a-quest-through-the-stage)
can declare it `"optional": true` instead and plays without its story content then). While it
isn't installed (offline, download failed), the story system is inactive: no letters, takeovers,
chart marks or notebook; quest mods wait with "Needs the Story Notebook quest", the game is untouched
and no save is changed; the next start tries again.

Your mod fills the notebook with its quests: what to do next, where, and how far the player got.
The player can show one quest on the sea chart.

```c
WWHD_HOOK(WWHD_ADDR_daPy_Execute, quest_step, (void* link)) {
    (void)link;
    u16 max_life = dComIfGs_player_status_a.max_life;
    if (max_life < 12 || max_life > 80) return;  /* no save file loaded */
    s32 letter = wwhd_mail_send("hello", "A wandering storyteller", "Dear Link, ...");
    s32 quest = wwhd_quest_define("tale", "The storyteller's tale", "A piece of heart", 0);
    if (letter == WWHD_MAIL_READ && quest == WWHD_QUEST_NEW) quest = wwhd_quest_step("tale", 1, 0, 0);
    if (quest == 1) {
        wwhd_quest_step("tale", 1, "Ask the storyteller about his lantern.", "Windfall Island");
        wwhd_quest_place("tale", WWHD_CHART_COLUMN_X('D'), WWHD_CHART_ROW_Z(2));
    }
}
```

| Service | What it does |
| --- | --- |
| `wwhd_quest_define(quest, title, reward, condition)` | registers the quest for this session (call it first, in every session) |
| `wwhd_quest_step(quest, step, next, where)` | step N reached (1..254), with what to do next and where; step 0 sets the texts only |
| `wwhd_quest_place(quest, x, z)` | the next step's place on the sea chart (the coordinates of `wwhd_chart_mark`) |
| `wwhd_quest_no_place(quest)` | the next step has no place on the chart; the notebook says so |
| `wwhd_quest_known(quest, text)` | what the player knows so far (several lines) |
| `wwhd_quest_portrait(quest, png)` | the quest's portrait, a PNG of your package (`assets/` or `textures/`, up to 256 × 256) |
| `wwhd_quest_done(quest)` | the quest is done |
| `wwhd_quest_state(quest)` | the quest's state, without changing it |
| `wwhd_quest_set_flag(quest, n, on)` / `wwhd_quest_flag(quest, n)` | the quest's flags 1..8, your own markers for progress inside a step (returns the flag, 0 or 1) |

All of them return the quest's state in the loaded save file, or an error:

| Result | Meaning |
| --- | --- |
| `WWHD_QUEST_NEW` (0) | not started in this save file |
| 1 to 254 | the step reached ("in progress") |
| `WWHD_QUEST_DONE` (255) | done |
| `WWHD_QUEST_INVALID` (-1) | invalid ID or text, or the quest was not defined in this session |
| `WWHD_QUEST_NO_ROOM` (-2) | no save file loaded, or its story record is full; or too many quests (8 per mod, 32 in all) |

- **IDs:** 1 to 32 characters `a`-`z`, `0`-`9`, `-`, `_`, `.`; unique within your mod.
- **Texts** are plain UTF-8, one line each, `NULL` for none: title (1 to 48 bytes), reward (96),
  condition (96), next step (120: one sentence), where (48: the island or place). What is known may
  have line breaks (600 bytes). Like letters, texts are registered in every session and not saved.
- **Steps** only go forward: a lower step than the saved one changes nothing, and a done quest stays
  done. Call the services from a gameplay hook; calling them every logic step is fine.
- **Condition:** a quest that can't be started yet names its condition ("after the Tower of the
  Gods rises"); the notebook shows "Not yet available: ..." until your mod reaches step 1 or
  defines the quest again without a condition.

**What the player sees.** The notebook comes with the Story Notebook package's letter, so only once
Link has the Delivery Bag (see [Letters](#letters)); a story mod's quests are in it from then on. It is kept in
the Delivery Bag (the first empty place of the bag;
if all eight places hold the game's items, it waits for one to be free), with its own icon: the port
draws it over the bag place and it follows the menu's animations. It can't be put on X, Y
or R. Touching it on the GamePad (or clicking it on the GamePad picture), or choosing it with A in
the item menu, opens its page over the game:

- one row per quest: your portrait, or an emblem with the initial of your mod's name; the title and
  your mod's name; the next step; where; and the status: **New**, **In progress**, **Not yet
  available**: the condition, or **Done** with a stamp;
- the order: the quest shown on the chart, quests in progress, new ones, ones not yet available.
  Done quests move to the **Archive** tab (Left/Right or a tap on the tab switches);
- choosing a row shows what is known so far and the reward, and **Show on chart**: that quest's
  next step gets the game's story ring on the sea chart (the first of the game's eight story
  rings the game is not showing; the game's own marks come first). One quest at a time;
  choosing it again stops showing it. The ring moves when you place the next step elsewhere and
  disappears when the quest is done or the step has no place (the page then says the step has no
  place on the chart). In the zoomed view of that square, the game's red hand-drawn circle (the one
  it uses for a salvaged treasure) marks the step's exact place; where the game shows its own circle
  in that square, the game's wins;
- Up/Down (or the stick) choose, A opens or shows on the chart, B goes back and closes. Taps (and
  the mouse on the GamePad picture) work on the rows and buttons. The game gets no input while the
  page is open.

The page looks like one of the item menu's own windows: its text is in the game's menu font, its
window and plates are made from the item menu's torn-paper window and button plate, the A and B
prompts are the game's button pictures (in the Xbox colours or as PlayStation shapes when the
**Button prompts** setting restyles the game's own prompts), and the game's cursor brackets
mark what A acts on. It opens and closes like the menu's mini windows, turns its page when you
switch tabs or open a quest, and plays the menu's own sounds (cursor, decide, back, tab, show on
chart). All of it is read from your copy of the game while you play; nothing of the game is stored
in the port. If a piece can't be read (another mod changed the menu), the page falls back to plain
boxes and the overlay font. The sealed letter of a quest is not the notebook: A or a touch on it in
the Delivery Bag shows a small window with what it is (B, A or a tap closes it).

**Saving.** The save file keeps the step reached, whether a quest is done, its flags, which quest is
shown on the chart and the letters Link carries (see [Where story progress is saved](#where-story-progress-is-saved)).
Quests that were never started take no room. Nothing is written to the inventory or the Delivery
Bag: with all mods off, the game's save is a plain game save, and the notebook, its records and the
chart ring are simply not shown.

### Where story progress is saved

Everything the port keeps for story mods (letters, charts, the notebook, quests, their flags, the
tracked quest, carried letters) is stored per Quest Log in a **story block appended to `cking.sav`**,
after the game's own bytes:

- the game never sees it: its reads end at its own part and the file size it gets is its own. A
  `cking.sav` with the block loads in the game (and on Cemu or a Wii U, where a save there drops the
  block: the game save stays intact, the story progress is lost);
- it is written with every game save (the block follows the game's write, through a temporary file
  that replaces the save in one step), so story progress always matches the game save: progress not
  saved is undone by loading, like the game's own. Full save states carry it; portable save states
  don't;
- without any story progress nothing is appended: a save stays byte for byte the game's;
- the block ends with a footer (magic, length, format version, checksum). A block whose checksum
  doesn't match is not used: the log says so and the file is copied once to
  `cking.sav.story-damaged`; the next game save writes a new block;
- up to 255 records per Quest Log. Saves from earlier test builds, which kept a smaller record in
  the game's own save data, are moved into the block the first time that Quest Log is loaded.

### Quests in the manifest

The preferred way: describe the quest as data in your `manifest.json`. The port reads it when the
player enables your mod, without running your code, so the quest is in the notebook at once. Your
code then only reports progress the port can't see (`wwhd_quest_step`, `wwhd_quest_done`); the
`story-notebook` package's quest needs no code at all. (The example is that package's own manifest:
only it may enclose the notebook; your letters enclose `letter:...` or `chart:...`.)

```json
"npc_catalogue": 3,
"quests_format": 1,
"quests": [{
  "id": "lantern",
  "title": "The storyteller's lantern",
  "journal": "on_enable",
  "start": {"text": "Check a Rito postbox for a letter."},
  "letter": {"id": "lantern", "sender": "The storyteller", "text": "Dear Link, ...",
             "encloses": ["notebook", "letter:friend"]},
  "reward": {"text": "100 rupees", "item": "rupee_orange"},
  "steps": [{
    "n": 1,
    "text": "Bring the storyteller's letter to his old friend who keeps Windfall's light.",
    "npc": {"who": "windfall-um1",
            "when": {"carries": "letter:friend"},
            "lines": ["A letter from my old friend? Thank you!", "...", "..."],
            "then": {"give": "rupee_orange", "take": "letter:friend", "complete": true}}
  }]
}]
```

- `journal`: `on_enable` (in the notebook as soon as the mod is on, the default), `on_trigger` or
  `hidden` (not in the notebook; revealing comes later).
- `letter`: the port sends it (the postbox hands it out once Link has the Delivery Bag). When it
  is read the quest reaches step 1, and with `"letter:..."` in `encloses` Link carries a sealed
  letter for the quest: it shows in the Delivery Bag next to the notebook (never in the game's
  inventory) until it is handed over. A or a touch on it shows what it is; the enclosure may be
  written as an object to say that in its own words: `{"id": "letter:friend", "name": "Sealed
  Letter", "description": "A sealed letter from the storyteller for his old friend."}` (both
  optional; `name` up to 40 bytes, `description` up to 200 bytes and three lines). Without them the
  bag says "Sealed Letter" and "A sealed letter from <sender>. It isn't addressed to you."
- `steps`: the notebook shows each step's `text`, `where` and `place` (world `x`/`z`, the
  coordinates of `wwhd_chart_mark`; the tracked quest's ring goes there). A step with an `npc`
  takes `where` and `place` from the NPC catalogue unless it gives its own. While that character is
  loaded (Link is in its room), the tracked step's marks on the sea chart follow where it actually
  stands, read from the game without changing anything; otherwise they are at the place. A step's
  `"npc": {"follow": false, ...}` keeps them at the place. Before step 1 it shows `start.text`.
- `available` (optional, per step): the step's prerequisites, checked read-only from the save:
  `"event"` / `"not_event"`: the game's event flag numbers (`"0x2D01"`, or a list) that must be set
  / not set; `"time"`: `"day"` (6:00–18:00), `"night"` or `"any"`; and `"text"` for the player.
  While they don't hold, the notebook says "Not yet possible: <text>", the step has no ring on the
  chart and its resident talk is not live. The port never changes any of them.
- `sail_line` (optional, per step): what the King of Red Lions says once about this step, the next
  time Link boards him or walks up to him: a string or a list of up to three lines (one message box,
  240 bytes). Below.
- `npc`: a talk to a game resident, below. Up to 8 quests per mod; texts have the limits of
  `wwhd_quest_*`.
- A declaration the port doesn't accept (an invalid field, an NPC its catalogue doesn't know, or
  an `npc_catalogue` version newer than the port's) keeps the mod from being enabled; the Mods tab
  shows the reason ("Story quests: ...").

### Mod flags and places inside buildings

A step's `available` can also use `flag` and `not_flag`. Each accepts a string
or a list of up to eight flag IDs, and every condition must hold:

```json
"available": {"flag":["notice-read","door-open"], "not_flag":"reward-taken",
              "text":"Read the notice and open the door first."}
```

These are the mod's own actor flags, not game event bits. Each flag must have a
producer in the same manifest: an actor's read/open, switch, break or defeat
flag, or a `flag` action, including a talk or scene end. Naming a flag in a
condition does not declare it. The existing `event` / `not_event` conditions
remain read-only checks of game events. The notebook and resident talks
re-evaluate mod-flag conditions as quest availability changes.

A sea place keeps its existing `{"x":100,"z":-201500}` form. For a building,
cave or dungeon, give its stage and room together with the sea entrance hint:

```json
"place": {"stage":"Pnezumi", "room":0,
          "sea":{"x":0,"z":-200000}, "point":0}
```

`room` is 0–63 and `point` is a spawn number from 0–255. Omit `point` to use the room's normal entrance,
or replace it with all three room coordinates: `"x":10,"y":0,"z":20`.
For one of the package's own [mod stages](mod-stages.md#a-quest-through-the-stage), `stage` is
`"stage:<its id>"`. Coordinates must be finite and within ±350000. Do not combine `point` and
coordinates. The explicit `sea` hint is required:
a stage can have several entrances, so a stage name does not uniquely identify
the island to mark. The notebook shows the step's `where` text, and the sea
chart's ring stays at the hint even if the step also names a resident. There is
no interior minimap or world-space quest ring. The editor's Quest warp list
uses the stage/room and spawn or coordinates to enter the destination.

Placed actors can finish a declared quest with
`{"action":"quest_done","id":"lantern"}` in their dialogue actions or a
scene's final `end.then`. The reward is given once; a repeated completion does
nothing. See the SDK v3 dialogue section for flag-gated actors, multiple quest
conditions and ordered talks.

### Talking to residents

A step can have a game resident speak your lines instead of their own, once: the friend reads
the letter, thanks Link, gives the reward through the game's own get-item scene, and the quest is
done. The port arbitrates, not your mod:

- the talk takes over while your quest is at that step and its conditions hold: Link carries its
  letter (`when.carries`) and the step's `available` holds. Tracking is not needed; it only decides
  the sea chart's ring and circle. Otherwise the resident talks exactly as in the game;
- several quests (from any mods) may want the same resident at once. Then the talk starts with the
  game's own choice box: two of the quests (the one shown on the chart first, else the ones started
  first, by their titles, shortened to fit) and "Never mind". The chosen quest's lines follow in the
  same talk. "Never mind" ends the talk, the resident's next talk is the game's own line, and the one
  after that offers the quests again. One quest per talk;
- a step that takes over a resident with no condition at all (no letter, no `available`) speaks for
  your quest whenever the step is current; the log warns about it, so give such steps a condition;
- the port never sets or clears a game flag. For that one talk the resident's own choice of line
  does not run, so a first-meeting line the game would have shown (and the flag it would have
  set) is not lost: it comes with a later talk;
- the lines are one message box: up to three lines, 240 bytes. `then.give` is a rupee item
  (`rupee_green` ... `rupee_orange`, 100);
- `"who"` names a resident from the NPC catalogue, below.

From code, the same is `wwhd_quest_talk(id, step, "windfall-um1", lines, reward_item)` and
`wwhd_quest_carry(id, 1)` gives the sealed letter.

### The King of Red Lions

A step's `sail_line` is said once by the King of Red Lions, the way he gives his own news: the next
time Link boards him or walks up to him in front, he turns to Link and says it in one message box.

```json
{"n": 1, "text": "Bring the storyteller's letter to his old friend who keeps Windfall's light.",
 "sail_line": ["That sealed letter you carry is for the man",
               "who keeps the light up on Windfall's windmill.",
               "Let us not keep an old friend waiting, Link!"]}
```

The port arbitrates, not your mod:

- only for the quest **shown on the chart** (tracked), at its current step. Untracked (or with your
  mod off) the boat talks exactly as in the game;
- **the game comes first.** His own lines that he says unasked (news after a pearl, a story
  moment) always go first; the quest's line waits while one of them is pending, while any event or
  cutscene runs, and for a short moment after the boat's last talk. Talking to him yourself always
  gives his own hint: the quest's line never replaces it;
- said once per step: the save's story record remembers it (see
  [Where story progress is saved](#where-story-progress-is-saved)), never a flag of the game. A
  later step with its own `sail_line` is said once again. A line that was cut short before its box
  showed (a save state loaded during the talk) comes again;
- the port never sets or clears a game flag for it, and the boat's state after the talk is the
  state before it.

### The NPC catalogue

The characters a quest can name, by a neutral id: `<place>-<actor code>` (`windfall-um1`). The port
keeps a **recipe** per place (`runtime/assets/npc_catalogue/<place>.recipe.json`): the ids, our own
description of each character, the place in our words, when they are out, and references into the
game (actor class and code, stage, room, layers, the figurine's message number). No game text and no
coordinates are in the port: the character's name (its Nintendo Gallery figurine name) and where it
stands outside come from the player's own game files. The port runs
`tools/story_mods/npc_catalogue.py` the first time a quest mod is loaded and keeps the result in its
data folder (`story/` in the port's configuration folder); without it, the notebook and the log show
the description instead of the name, and residents have no place on the chart unless the step gives
one.

**Which id is which character.** Run the tool against your own game:

```sh
python3 tools/story_mods/npc_catalogue.py --game <your game folder> --list
```

It prints each id with the name your game gives the character, where it is and its spot; the list of
names exists only on your machine.

A new place or a new character adds rows to a recipe, never a change to the manifest format. A mod
that names a character added in a later catalogue version declares the version it needs,
`"npc_catalogue": 3`, so an older port refuses it with a clear message instead of ignoring the talk.

Each entry lists the actors that are this character (actor class plus what identifies it: the
townsperson code for the `people` class, the stage actor name for others) and the rooms and room
layers they stand in. The name is the game's figurine name for exactly that actor: the decompilation
ties each actor's pictograph type to its figurine (`dSnap_RegistFig`, figure = type − 0x49, name
message 0x37DD + figure). A figurine that shows a pair or a group doesn't say which actor is which;
those entries show their description.

A talk is matched where the recipe says the actor stands (stage and room). Only the townspeople
(class `people`) can speak a quest's lines so far; naming another character is refused with the
reason. When the characters are out: "by day" is 6:00–18:00, and during the Endless Night (between
the Wind Waker and Nayru's pearl) it is always night. A step whose `available` time never meets its
character's is accepted with a warning in the log. Some characters move with the story (the recipe's
`gate`): a couple of the town square follows their own story, two families swap houses, the pirates
are in the bomb shop only for that scene.

Catalogue version 3, Windfall Island ("unconfirmed": for characters other than the townspeople, the
actor's own conditions for appearing are not decoded yet; only the room layers say when they are out).

| Id | Who (our words) | Where | Out | Actor | Quest lines |
|---|---|---|---|---|---|
| `windfall-uo1` | an old man resting on the sea-view bench at the cliff | on the sea-view bench at the cliff | day | people Uo1 | yes |
| `windfall-uo2` | a man who sits at the bar all day | in the bar | day | people Uo2 | yes |
| `windfall-uo3` | a well-dressed man walking through town | walking through town | day | people Uo3 | yes |
| `windfall-ub1` | one of the two women chatting up the steps by the brick house (the first) | up the steps by the brick house | day | people Ub1 | yes |
| `windfall-ub2` | one of the two women chatting up the steps by the brick house (the second) | up the steps by the brick house | day | people Ub2 | yes |
| `windfall-ub3` | a woman on the harbour pier | on the harbour pier | day | people Ub3 | yes |
| `windfall-ub4` | a woman by the town gate | by the town gate | day and night | people Ub4 | yes |
| `windfall-uw1` | the young woman working in the bar | in the bar | day and night | people Uw1 | yes |
| `windfall-uw2` | a young woman in the town square or in the bar | the town square or the bar | day | people Uw2 | yes |
| `windfall-um1` | the man up on the windmill by the light | up on the windmill by the light | day and night | people Um1 | yes |
| `windfall-um2` | a young man in the town square or in the bar | the town square or the bar | day | people Um2 | yes |
| `windfall-um3` | a young man on the steps by the red door (up high at night) | on the steps by the red door (up high at night) | day and night | people Um3 | yes |
| `windfall-sa1` | a sailor who drinks in the bar at night | in the bar at night | night | people Sa1 | yes |
| `windfall-sa2` | a sailor on the high lookout | on the high lookout | day | people Sa2 | yes |
| `windfall-sa3` | a sailor on the stone dock | on the stone dock | day and night | people Sa3 | yes |
| `windfall-sa4` | a sailor at the end of the wooden pier | at the end of the wooden pier | day and night | people Sa4 | yes |
| `windfall-sa5` | a sailor on the grass by the stone wall | on the grass by the stone wall | day and night | people Sa5 | yes |
| `windfall-ug1` | one of the two by the flowers at the cliff (the first) | by the flowers at the cliff | day and night | people Ug1 | yes |
| `windfall-ug2` | one of the two by the flowers at the cliff (the second) | by the flowers at the cliff | day and night | people Ug2 | yes |
| `windfall-rsh1` | the travelling merchant at his stall | the travelling merchant's stall | day (unconfirmed) | npc_rsh1 Rsh1 | not yet |
| `windfall-tt` | the dancer on the hill | dancing on the hill | day and night (unconfirmed) | npc_tt Tt | not yet |
| `windfall-kk1` | the rich man's daughter | in town | day and night (unconfirmed) | npc_kk1 Kk1 | not yet |
| `windfall-gk1` | the rich man, the daughter's father | in town | day and night (unconfirmed) | npc_gk1 Gk1, Kf1 | not yet |
| `windfall-kp1` | the poor family's daughter | the auction house | day and night (unconfirmed) | npc_kp1 Kp1 | not yet |
| `windfall-pf1` | the poor family's father | in town | day and night (unconfirmed) | npc_pf1 Pf1, Gp1 | not yet |
| `windfall-bms1` | the bomb shop keeper | the bomb shop | day and night (unconfirmed) | npc_bms1 Bms1, Bms2 | not yet |
| `windfall-ds1` | the potion maker in his shop | the potion shop | day and night (unconfirmed) | npc_ds1 Ds1 | not yet |
| `windfall-po` | the photographer in his studio | the photographer's studio | day and night (unconfirmed) | npc_photo Po | not yet |
| `windfall-ho` | the teacher of the school house | the school house | day and night (unconfirmed) | npc_ho Ho | not yet |
| `windfall-kg1` | the host of the battleship game | the battleship game | day and night (unconfirmed) | npc_kg1 Kg1 | not yet |
| `windfall-npcso` | the talking fish in the sea by the island | the sea by the island | day and night (unconfirmed) | npc_so NpcSo | not yet |
| `windfall-tc` | the man locked in the jail | the jail | day and night (unconfirmed) | npc_tc Tc | not yet |
| `windfall-bm4` | a man in the bar who also comes to the auctions | the bar and the auction house | day and night (unconfirmed) | npc_bm4 Bm4 | not yet |
| `windfall-zl1` | the pirate captain in the bomb shop | the bomb shop | day and night (unconfirmed) | npc_zl1 Zl1 | not yet |
| `windfall-p1a` | a pirate in the bomb shop (the first of three) | the bomb shop | day and night (unconfirmed) | npc_p1 P1a | not yet |
| `windfall-p1b` | a pirate in the bomb shop (the second of three) | the bomb shop | day and night (unconfirmed) | npc_p1 P1b | not yet |
| `windfall-p1c` | a pirate in the bomb shop (the third of three) | the bomb shop | day and night (unconfirmed) | npc_p1 P1c | not yet |
| `windfall-mk` | one of the four children around town (the leader) | around town | day (unconfirmed) | npc_mk Mk | not yet |
| `windfall-ukb` | one of the four children around town (the second) | around town | day (unconfirmed) | npc_uk UkB | not yet |
| `windfall-ukc` | one of the four children around town (the third) | around town | day (unconfirmed) | npc_uk UkC | not yet |
| `windfall-ukd` | one of the four children around town (the fourth) | around town | day (unconfirmed) | npc_uk UkD | not yet |

Not in the catalogue: the auction's bidders (their faces are chosen at random each auction), animals
and objects, and characters who only appear in cutscenes.

**Testing.** `tools/story_mods/notebook_e2e.sh` runs the `story-notebook` package headless from a
save on Windfall: letter, notebook, page, tracking and the sea chart, and checks the log.

**Players without story mods.** The item menu functions are only hooked while a loaded mod uses
`wwhd_quest_*`. If another mod replaces one of them, the notebook is not in the item menu that
session and the log says so.

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
code mod asks once, like native mods. The confirmation covers the package version and a
fingerprint of every package file; a changed package asks again. Rebuilding the same package after a port update
does not ask for trust again.

## Save states

Full save states include the mods' memory and heaps. Both state formats record which mods
(ID and version) were loaded; loading a state made with different mods shows a warning and
continues. Loading a state never installs, enables or disables mods. Files a mod writes
itself are not part of save states.

Mod features reserve guest memory only when a loaded mod uses them (native particles, held item
models, world actors and scenes, story progress). A start without mods reserves none, so its full
save states keep the layout of the game without mods: full states made without mods by builds
from before 2026-10-17 load again.

## Limits

- macOS, Windows, Linux and Android. On Android the setup app builds the game code with code-mod
  support (its **Code mods** switch, see [setup-code-mods.md](setup-code-mods.md)), and each mod is built
  on the device with the app's own compiler. Mod preparation tools written in Python run there too;
  other programs in a mod package don't run on Android.
- Changing enabled mods or their options needs a restart, except options the manifest marks `"live": true`.
- Mods live in a 16 MiB region of game memory (`0x7F000000`–`0x80000000`); each mod gets its
  own 64 KiB-aligned part.
- No events or exports between mods yet. Audio streams support 48 kHz mono/stereo PCM.
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

In one game run, as a player does it, the driver switches the catalogue mod on
(the test aid accepts its review, with the GameCube game from the private JSON): it
is downloaded, its setup adds code-mod support, the game relaunches itself into
the hook-enabled build (the relaunch keeps the support just built, also when the
run started with `WWHD_CODE_MODS=0`), the setup resumes there, runs preparation,
builds the guest module through the normal setup workers and switches it on for
the following restart. It checks setup receipts, declared output
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


## PCM audio streams v1

Guest mods may synthesize original audio with `wwhd_audio_open(48000, channels)`
(one or two channels), `wwhd_audio_submit(handle, samples, frames, channels)`,
`wwhd_audio_available(handle)` and `wwhd_audio_close(handle)`. Samples are signed
16-bit, interleaved and guest-endian. The host copies them before returning;
it retains no guest pointers. Buffers must fit in the calling mod's region.
Only 48000 Hz is supported; callers resample other content themselves.

Each stream holds 8192 frames, each submission is at most 2048 frames, and limits
are four streams per mod and 32 globally (1 MiB fixed host sample storage).
Open returns a positive opaque handle. Available returns free queue frames;
submit returns accepted frames, including zero when full. Close returns zero.
Errors are `WWHD_AUDIO_INVALID` (-1), `WWHD_AUDIO_BUSY` (-2; retry later without
advancing a synthesis cursor), and `WWHD_AUDIO_QUOTA` (-3). Handles are checked
against the calling mod and never reused during the process lifetime. Numeric
handle exhaustion fails explicitly. Submission/query/close use try-locks;
there are no waits for a device. Open can allocate a bounded owner string.

The AX producer mixes copied samples into the common stereo output before its
master gain and dump path. Mixing sums streams in 32-bit precision and saturates
once to S16. Mono duplicates to both channels. Underruns contribute silence;
there is no automatic replay. The device callback performs no mod work. With no
streams the producer bypasses the mixer. `WWHD_AUDIO_VOLUME` applies equally to
game and guest PCM; zero mutes while draining. `WWHD_NO_AUDIO=1` opens no device
but drains on the existing AX producer clock, including optional diagnostic dumps.
The service is independent of Metal/Vulkan and does not expose callbacks,
networking or host filesystem access.

Compare `wwhd_audio_epoch()` before using handles. Full-state loading and output
flush/reset clear all streams and increment this host-only epoch. CoreAudio
notifies default-output changes; SDL output topology/format events invalidate
streams too. Queues and handles are not serialized. Recreate streams on a change
and resume from restored guest musical state; previously played sound is not
rewound. Disabling takes effect at restart, so no streams survive a changed mod
set. This service does not add backend device-reconnection facilities beyond
those of the existing output backend.

For bounded service-call diagnostics, `WWHD_AUDIO_STREAM_TRACE=1` logs open/submit/close
results and epochs, never sample payloads. Leave it unset for normal operation.
