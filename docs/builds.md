# Building the port from another regional build of the game

The port is written against the **USA** build of the game: every game address in this repository
(`tools/recomp/hooks*.txt`, `runtime/src`, the notes in `docs/`) is an address of that executable.
Those addresses are the *canonical ids* of the game's functions and globals.

Another regional build of The Wind Waker HD is the same program, compiled with that region's
language branches: the same functions in the same order, a few hundred bytes apart. A **build map**
(`tools/recomp/builds/*.json`) turns a canonical address into that build's address, so the one set
of hooks and the one runtime serve every build.

| Build | Title | `code/cking.rpx` SHA-256 | Map |
|---|---|---|---|
| USA (canonical) | `00050000-10143500` | `c4f0ab30…f16153` | the identity |
| Europe | `00050000-10143600` | `f9f46173…6bbf0b` | `tools/recomp/builds/eu.json` |
| Japan | `00050000-10143400` | `91183a3d…59de8f` | `tools/recomp/builds/jp.json` |

Only version 0 of each (the disc or eShop release, without the update). The Japanese build is
newer here and differs more than the European one; see [The Japanese build](#the-japanese-build).

## What the map is

Two things, both piecewise constant shifts:

- **code**: canonical `.text` address → this build's. For Europe: 15 runs, from `+0` to `+0x8C0`.
- **data**: canonical `.rodata`/`.data`/`.bss` address → this build's. For Europe: 18 runs, from
  `-8` to `+0x100`.

Plus the list of functions whose **body** differs, which is what the build map cannot paper over.

Between the USA and the European build, 18 of 37,766 functions differ, and they are the region's
own language code: the reader of the console language, the player name in a message tag with the
German genitive, `putPlayerName`, parts of the message window and the on-screen keyboard. Every
other function is the same code at a shifted address, with shifted references to the globals.

## How it is derived and checked

`tools/recomp/mkbuildmap.py` takes the two executables and writes the map:

```sh
python3 tools/recomp/mkbuildmap.py usa/code/cking.rpx eur/code/cking.rpx \
    --name EU --title 0005000010143600 --out tools/recomp/builds/eu.json
python3 tools/recomp/mkbuildmap.py --check tools/recomp/builds/eu.json \
    usa/code/cking.rpx eur/code/cking.rpx       # re-derive and compare
```

It refuses to write a map unless the two executables really are the same program (for the
Japanese build, the relaxed form in [The Japanese build](#the-japanese-build)):

- the same number of functions, in the same order;
- every function whose body is unique within both builds is at the same index in both, which is
  what ties the i-th function of one to the i-th of the other (USA ↔ Europe: 22,844 such functions,
  none at a different index);
- every direct call out of a function with an identical body lands on the mapped address
  (USA ↔ Europe: 163,166 calls, no exception);
- every global referenced from such a function maps to exactly one address (54,760 addresses, none
  ambiguous).

The map itself holds numbers only — address runs and shifts — and never any of the game's code.
A build's `cking.rpx` is identified by its SHA-256, like the USA one always was.

The map also records the canonical text and static-data bounds. Python and runtime
mapping reject addresses outside those bounds and interior addresses in changed
functions. Allocated objects must be found through mapped globals, not fixed heap
addresses. Special AO shaders are identified by their program hash rather than the
address where a regional build happens to allocate them.

## How the port uses it

- **The recompiler** (`tools/recomp/recomp.py`) identifies the executable by its SHA-256, translates
  the hooks' canonical addresses into that build's, and names every generated function by its
  **canonical** address: the runtime's `f_025F172C` is the same symbol whichever build it was
  translated from, while the dispatch table maps the build's real addresses to it. For the USA build
  the generated code is byte for byte what it always was.
- **The runtime** gets the map as data in the generated `table.c` and translates the addresses it
  uses as values: `GC(…)` for code, `GD(…)` for data
  ([runtime/include/guest_addr.h](../runtime/include/guest_addr.h)). An offset inside an object does
  not change with the build, so only the base of each object is translated, with one exception:
  the members of `g_dComIfG_gameInfo` the Japanese build moved, which the runtime names by their
  canonical offset through `GI(off)` (`ld32(play + GI(0x5B34))`: Link). Linked without the game
  code (unit tests, `stubgen.py`) the map falls back to the identity.
- **A hooks file** may say which builds it is for:

  ```
  # builds: USA
  ```

  `tools/recomp/hooks_language.txt` does, because it adds to the USA code what the European build
  already has of its own (its language reader, the German genitive of the player's name). The
  runtime side of such a hook declares the original function weak and does nothing when it is
  absent (`runtime/src/language_region.cpp`).
- **Everything else is checked**: an instruction-level hook (`@ADDR`) inside a function the build
  compiled differently is refused by the recompiler, because it would patch different code. A hook
  on the *entry* of such a function wraps it whole, which usually still holds; the recompiler warns
  and notes it in `build/gen/report.txt`.

## The Japanese build

The Japanese executable has 37,772 functions to the USA build's 37,766, so the strict check above
(same count, same order) cannot hold. `mkbuildmap.py` aligns the two by body instead and checks the
pairing the same way (calls and global references), then reads the code pointers in the data
sections (vtables, static initializer and callback tables): a function only one build seems to have,
held by a slot whose counterpart holds a function only the other build seems to have, is the same
function rewritten and is paired; a slot holding another function than the map gives is a regional
table entry, and more than 1% of those is refused. A map may then also list:

- `removed_functions`: canonical functions the build does not have (6). `code()` refuses them, and the
  runtime's `guest_code_valid()` / `GC_OPTIONAL()` treat them as absent (`g_guest_removed_code`).
- `inserted_functions`: functions only the build has (12). The recompiler names them
  `f_8XXXXXXX` (`builds.BUILD_ONLY` | their own address), which no canonical name can be; so is an
  entry the fixpoint finds inside a function the build compiled differently.
- `layouts`: objects some of whose members moved. The Japanese `dComIfG_play_c` (in
  `g_dComIfG_gameInfo` at `+0x12A0`) is 4 bytes shorter from gameInfo `+0x5AB0` on (5 at `+0x5BEF`),
  so thousands of instructions reach those members with another displacement. A function whose
  only difference is such a displacement counts as the same code (2,653 of them), and the member
  shifts are part of the data map, so `GD(0x1046F0B0 + off)` and `GI(off)` give the right member.

```sh
python3 tools/recomp/mkbuildmap.py usa/code/cking.rpx jpn/code/cking.rpx --name JP \
    --title 0005000010143400 --out tools/recomp/builds/jp.json \
    --layout "g_dComIfG_gameInfo:1046F0B0:62F8:0+12A0:5AB0=-4+5BEF=-5+5BF0=-4"
python3 tools/recomp/mkbuildmap.py --check tools/recomp/builds/jp.json \
    usa/code/cking.rpx jpn/code/cking.rpx       # re-derive and compare (the layouts come from the map)
```

USA ↔ Japan: 35,069 functions with the same body, 2,653 that differ only by the moved members,
38 that differ (mostly the message, text and language code and the Options window), 6 removed and 12
inserted; 163,450 direct calls, 54,526 global addresses and 34,246 code pointers checked. One function
(`025FD550`) calls a Japanese-only helper and is counted as differing. Two static initializers
(`0202EABC`, `0264E754`) were rewritten too much to pair by their code and are paired by the
initializer table. 38 pointer slots hold a regional function (the message code's tag handlers and an
Options window slot). Nine differing functions are tied to their counterpart only by position and
similar code, with no call or pointer to confirm it; the map lists them (`checked.differing_unconfirmed`).

Tested on Windows (MSYS2 CLANG64, translated with `--mod-hooks`): a new game from the title screen, file
select and name entry in kana (`WWHD_SWKBD_TEXT`), the prologue and the opening on Outset Island, with
the play context (`WWHD_PLAYCTX_LOG=1`) reading Link, events and message windows through `GI()`. Later
parts of the game are untested.

What that means for the port on the Japanese build:

- The Options window is Japanese code: One screen mode's "Options without Controller" hooks are for
  the USA and European builds only (`tools/recomp/hooks_options_order.txt`), so on the Japanese
  build the Controller item stays.
- The game has its own (Japanese) text; a language source is refused, as on the European build.
- Story mods' demo windows (`story_demo.cpp`) hook message-window code the Japanese build changed;
  the Japanese function takes the same members and arguments, so the hook is the same, though only
  demo steps without messages were played there. Scene choices (`story_scene_choice.cpp`) check
  `guest_code_unchanged` on the message-control constructors (`025F65F4`, `025F7424`, `025F7500`)
  and stay disabled on the Japanese build until tested there. Randomized games: see
  [Randomized games](#randomized-games). The map/compass bridge for code mods
  ([mod-sdk-map-hud.md](mod-sdk-map-hud.md)) is: every function it uses is the same code. The
  catalogue's minimap mods do not support the Japanese build yet: the HD minimap enables itself on
  USA and EU only, and both read members of the play object that moved (see the next point).
- Guest mods are translated for the build, so their calls and globals map, and the SDK's
  `WWHD_PLAY_*_OFFSET` values follow the moved play object members: the translator resolves them
  (`WWHD_GAME_INFO_OFFSET`, [mod-sdk-v2.md](mod-sdk-v2.md)). On the Japanese build, a mod whose code
  adds a raw unrelocated offset of a moved member (`0x5AB0` to `0x62F7`, wherever its pointer came
  from) is refused unless resolved through `WWHD_GAME_INFO_OFFSET`, and a mod built with an older SDK
  is also refused if it calls or hooks `dComIfGp_get` or addresses `g_dComIfG_gameInfo`. Mods
  rebuilt with this SDK (`__wwhd_ginfo_*`, `guestmod-0.4`) won't load on older ports, so their
  manifests need `"minimum_manager_version": "1.13.0"` and their catalogue entries need a matching
  minimum port version (`0.5.2`). In a shared catalogue `index.json`, entries that support all three
  builds should omit `"builds"` (or leave it empty) rather than listing `"JP"`, because ports `0.5.1`
  and earlier reject `index.json` at parse time if any entry's `"builds"` names `"JP"`.

## Languages

The European build has English, French, German, Italian and Spanish itself and chooses between them
by the console language, which the port's settings decide (`F1` → Language, or `WWHD_LANGUAGE`).
Its five `permanent_2d_Eu*.pack` files are part of that game, so a **language source**
([language-packs.md](language-packs.md)) is neither needed nor accepted on that build: it exists to
lend a European or Japanese game's text to the USA code.

## Adding a build

1. Derive and check the map with `mkbuildmap.py` from your own dumps of both games, and commit the
   JSON (and only the JSON).
2. Run the recompiler on the new executable. It reports the hooks it skipped and every hook that
   sits on a function the build compiled differently; look at each one.
3. Check the features that lean on a particular piece of the game's code: frame interpolation
   (`tools/recomp/hooks.txt`), the save states
   (`runtime/src/savestate.cpp`), the mods, and the Cemu aspect-ratio pack
   (`runtime/src/mods/cemu_pack.cpp` picks the section for the build).
4. Check that no member of an object moved: `mkbuildmap.py` reports functions that differ only by
   a displacement; if there are many, find the object and pass `--layout`
   ([The Japanese build](#the-japanese-build)). `tools/recomp/test_coverage.py` then flags any
   `GD(object) + offset` in the runtime whose member moves.

## Regression checks

`python3 -m unittest discover -s tools/recomp -p 'test*.py'` checks every active EUR
hook, including the pause-menu site `02715310` and stars site `02574144`, and audits
runtime game-address literals for GC/GD mapping. This source audit complements the
executable comparison; it is not a C++ parser or a proof of object layouts. CTest
also exercises the generated EUR map and the USA identity fallback without game data.

On devel `54761fd3`, all 247 active EUR hook addresses map: 95 instruction sites and
152 function wrappers. Every instruction site is in an unchanged body. Of the wrappers,
151 have unchanged bodies; the HUD pane wrapper `02593B10` wraps a changed function at
its mapped entry. The four USA language hooks are intentionally skipped because EUR
supplies that behavior natively. There are 37,748 unchanged function bodies and 18
regional differences out of 37,766 paired functions.

With an extracted game and a disposable copy of an existing save, the scripts in
`runtime/tools/` exercise the real game:

- `portable_state_scenario.py`: stage/position/inventory restoration across processes,
  Quest Log changes, boat restoration, cutscene refusal, and full-state regression.
- `interp_menu_scenario.py`: pause/open/close at 120 and 240 interpolation passes on
  both renderers, plus F10 TV/GamePad screenshots through the normal key handler.
- `regional_game_scenario.py`: native German/Italian (or USA English), Outset gameplay,
  interpolation mode switches (including a saved True 60 choice falling back to 60 fps
  interpolation), returning to 30 fps and saving a portable state.

These scripts use private caches, states and screenshot folders and never modify the
input save. Keep extracted games and generated recompilation output outside Git
checkouts. High interpolation rates check behavior, not physical 240 Hz display
delivery or a performance guarantee.

## Combined PR #77 validation (2026-10-08)

ElFDA's two commits were rebased onto GitHub devel `54761fd3`, preserving their
authorship. The desktop prototype's hardening uses PR #77's GC/GD tables throughout;
there is one mapping pipeline. Builds used `-j 4`, external generated-code/build
folders, extracted games outside Git, and copies of the input saves.

Verified locally on Apple Silicon/macOS with the real version-0 executables:

| Check | Result |
|---|---|
| Fresh EUR and USA builds, Metal + Vulkan, setup GUI | Pass |
| CTest, each build (including map fixtures and coverage audit) | 33/33 pass |
| Build-map/source-audit Python tests | 21/21 pass |
| Installer tests, each real dump | 43 pass; Windows-only test skipped on macOS |
| Re-derive EUR map from USA/EUR executables | Unchanged |
| German portable house/Outset states across cold processes | Pass; position, angle and save data match |
| Quest Log 1 state loaded into Quest Log 2 | Pass; notice and destination slot verified |
| Sailing boat state across cold processes | Pass; aboard, heading and horizontal position verified |
| Portable cutscene refusal, then saving after dialogue | Pass; no refused-state file written |
| Full snapshot save/load and keeping it beside a portable state | Pass |
| German pause/open/close, Metal/Vulkan, 120/240 interpolation passes | All four cases pass |
| F10 TV + GamePad capture in those four cases | 16 valid PNGs; dimensions and chunk CRCs verified |
| Italian/Metal and German/Vulkan Outset, interpolation/true60/30 fps | Pass; portable state written after switching |
| USA/Metal Outset portable state across cold processes | Pass; position, angle and save data match |
| USA/English/Vulkan Outset, interpolation/true60/30 fps | Pass; portable state written after switching |

German pause text and Italian gameplay HUD text were also checked visually. This
is local runtime validation; Android, Linux and Windows execution remains covered
by their own CI/build and platform testing, not by these macOS game runs.

## Randomized games

Setup makes randomized games itself from the USA, European or Japanese game (the
in-app randomizer, TWWHD Randomizer 1.2.0) and also plays USA seeds made with the
official TWWHD Randomizer. It checks every randomized executable against a compatibility
profile (`tools/recomp/randomizer-usa.json`, `randomizer-eu.json`, `randomizer-jp.json`)
and pins the build to that exact game file. Arbitrary modified RPX files remain
unsupported. See [randomized games](randomizer-experiment.md) for players and
[randomizer-maintainers.md](randomizer-maintainers.md) for the profile review.
