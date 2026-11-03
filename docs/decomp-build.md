# Decomp build (experimental)

The decomp build is a second way to build the port. In the normal build, every function of the game
is translated from your own copy of the game (`cking.rpx`) into C by the static recompiler. In the
decomp build, the game functions that have been **decompiled** run as compiled C++ source instead:
the [verified decompilation](https://github.com/ZeldaWWHDDecomp/wwhd) of *The Wind Waker HD*, which
this repository includes as the git submodule `decomp/`.

Everything else is the same in both builds:

- the libraries the decompilation does not cover (parts of JSystem, sead/agl, nw4f, the C runtime) and
  every game function that is not decompiled are still recompiled from your own copy of the game when
  setup runs, as in the normal build;
- the runtime, the renderers, setup, mods, save states and the settings are shared.

The decompiled code does not contain any of the game's code or data, but it is written against the
game's memory layout and calls the functions it uses by their address in the game. It therefore only
works together with the recompiled game, which is still built from your own copy. The few pieces of
game data some decompiled units need (the model data of the ship's sail, the grid sail, the finish-line
flag and the buoy flags), and the guest addresses of their string literals and tables, are read from
your own copy of the game when the build is configured, as the decompilation's own tools do; they stay
in the build directory. The decomp build
needs the **USA** game (version 0), the build the decompilation was made from.

The decomp build is **opt-in and experimental**: the normal (recompiled) build is the default and
recommended way to play. The decomp build plays the same game; it exists to run, test and improve the
decompiled source in the real game, and to make the game code readable and changeable as source.

## What runs decompiled

Every function the decompilation has verified (22,705 functions in 862 game units at the pinned
version), except:

- functions the port's runtime replaces itself (its hooks for 60 fps, the camera, the controls and so
  on: `tools/recomp/hooks*.txt`) - the port's version stays in charge;
- functions that contain one of the port's instruction-level patches;
- functions a guest code mod hooks or replaces (they stay recompiled while that mod is on, so the mod
  keeps working).

The game log says how many run decompiled (`[decomp] decomp build: ...`), and the version shown in
Settings (`F1`) > Language / About and in the performance report ends in `decomp`.

## Status

- Tested on macOS (Apple M3 Max, Metal) with the decompilation at 2c1deae (and before at 47e1dbc): a
  scripted session from the title screen through the file select into the game and 30 s of walking and
  jumping matched the normal build step by step (Link, camera, random numbers, sound starts, every game
  object after every update, the save data). That session itself does not always play out the same from
  run to run (in either build, from about 18 s on); each decomp run matched a normal run.
- Speed: the game runs at full speed, but the decompiled code needs more processor time than the
  recompiled code: about 1.3 times the CPU time of the whole game process in that session (21.6 s
  instead of 16.8 s for 30 s of play, on a busy machine; 1.6 times at 47e1dbc). Making it faster is
  future work.
- Not yet available on Windows, Linux and Android. In setup: macOS development builds (below).

## Building it from source

macOS only for now (Metal; other platforms follow). You need the same tools as for the normal build,
plus the submodule:

```sh
git submodule update --init decomp
cmake -S . -B build/decomp -G Ninja -DWWHD_DECOMP=ON -DWWHD_RENDERER=METAL -DGEN_DIR=$PWD/build/gen-decomp
```

The configure step reads your game from `game/` (another folder: `-DWWHD_DECOMP_GAME=/path/to/game`, the
folder that holds `code/cking.rpx`); it takes about two minutes, mostly for the source glue.

The first configure writes the list of decompiled functions to `build/decomp/decomp/decomp_hooks.txt`
and stops with the command that translates your game code with that list, into its own directory
(keep the normal build's `build/gen` as it is):

```sh
python3 tools/recomp/recomp.py game/code/cking.rpx build/gen-decomp --decomp-hooks build/decomp/decomp/decomp_hooks.txt
cmake -S . -B build/decomp      # configure again: now it finds matching code
cmake --build build/decomp --target wwhd -j 4
```

`WWHD_DECOMP=OFF` (the default) builds exactly the normal game; it refuses a `GEN_DIR` that was made
for the decomp build, and the decomp build refuses one made for the normal build or with another
function list (after a submodule update: translate again with the new list).

How it works: `tools/decomp_build/gen.py` reads the decompilation's units, prepares their sources as
the decompilation's verification harness does (its `tools/verify/autoglue.py` turns the GameCube-style
sources into compilable glue: function entries, `VERIFY` marks, guest views of static tables, guest
copies of string literals; its `tools/verify/assets.py` writes the model data headers `assets/*.h`),
all into the build directory (`build/decomp/decomp/{glued,include,verify}`), and writes the function
list and the glue. Each game unit (the decompilation's verification units
that share decompiled sources) is compiled with hidden visibility and merged into one object with
`ld -r`, so units never see each other's classes or helpers (as in the decompilation's verification
harness, where each unit is its own program). The recompiler (`--decomp-hooks`) routes every listed
function through the runtime (`runtime/src/decomp`), which runs the decompiled function in its place.
The runtime also rebuilds, from the game's code in memory, the stack frame layout of each decompiled
function's original, so the decompiled code leaves the game's stack as the original does.

## Host runtime (decomp build only)

The decomp build also replaces parts of the game's own runtime library (the compiler's C/C++ runtime and
the nn static wrappers, which belong to the port rather than to the decompilation) with host code:
`runtime/src/hostrt`, routed by `tools/recomp/decomp_hostrt_hooks.txt`, which `recomp.py` reads only with
`--decomp-hooks`. Each replacement was measured bit-exact against the recompiled original
(`tools/host_runtime/difftest`). The normal build never uses them: its game code is exactly the recompiled
originals.

Switches (decomp build only; the normal build ignores them):

- `WWHD_HOSTRT=0` runs the recompiled originals instead of the replacements (A/B comparisons);
- `WWHD_HOSTRT_CHECK=1` runs both for every call of a replacement that only computes registers and logs the
  first differences (`[hostrt] CHECK`).

A function that a guest mod hooks keeps its original in any case.

## In setup (development builds, macOS)

The development builds for macOS (the rolling "Development build" pre-release) can build the decomp
version during setup, next to the normal game. It is off by default:

- in the setup window: tick **Also build the decomp version (experimental)** (shown with the other
  build options; not shown in regular releases or on Windows, Linux and Android), then install,
  repair or update;
- in a terminal: `tools/Setup in Terminal.command --decomp` (`--no-decomp` removes it again).

Setup always builds the normal game first; the decomp version is an extra step at the end (10 to
30 minutes more, depending on the Mac; about 100 MB on disk, and about 500 MB more while it builds). If it fails, setup says why and the normal game is ready as
usual. The choice is remembered: a repair or an update builds the decomp version again.

To play it, tick **Start the decomp version instead of the normal game** in the setup window (hold
Shift while starting Wind Waker HD), or `--start-decomp 1` in a terminal; or start Wind Waker HD with
`--decomp` once. The version shown in Settings (`F1`) > Language / About ends in `decomp`. The
decomp version shares the saves, settings and shader caches of the normal game.

What setup uses and makes:

- the decompilation's source at the version the development build was made with: setup downloads it
  from [github.com/ZeldaWWHDDecomp/wwhd](https://github.com/ZeldaWWHDDecomp/wwhd) (about 8 MB, only
  when the option is on) and checks the unpacked files against the SHA-256 the release names before it
  uses them. It contains no game code or data. It is kept in `data/decomp-src`, so a repair or an
  update with the same version downloads nothing. Without an internet connection setup says so
  ("Couldn't download the decomp source"); the normal game is ready as usual, and the next repair
  tries again;
- your own `cking.rpx`: setup prepares the decompiled source with it as described above (the model
  data headers, the source glue), translates the game code with the decomp build's function list and
  compiles the 862 game units with the same compiler as the normal game;
- everything it generates stays in the install's data folder (`data/decomp-work`, removed after the
  build); the result is `data/bin/wwhd-decomp`.

## Releases

Regular releases do not offer the decomp version. The decomp release variant (a separate download
whose setup translates the game with `sdk/decomp_hooks.txt`) is not possible with the decompilation
at 2c1deae: its sources are prepared with data read from your own game, so a release cannot ship the
compiled decomp units. Setup builds them instead (above).

## Debug switches

None of these is needed to play; all are off by default.

| Environment variable | Meaning |
|---|---|
| `WWHD_DECOMP=all\|none\|unit,...` | game units that run decompiled code (default `all`; `none` runs the recompiled code everywhere, in the same executable) |
| `WWHD_DECOMP_FUNCS=ADDR,...\|@file` | only these functions (hex addresses) run decompiled |
| `WWHD_DECOMP_EXCLUDE=ADDR\|unit,...\|@file` | these stay recompiled (for bisecting a difference) |
| `WWHD_DECOMP_STATS=path` | per-function call counts (decompiled / recompiled), rewritten every 2 s |
| `WWHD_DECOMP_GUARD=log\|restore` | report decompiled functions that write into their caller's stack frame (`restore` also undoes it) |
| `WWHD_DECOMP_FRAMES=0` | no native stack frames |
| `WWHD_DECOMP_FRAMES_DUMP=path` | write the stack frame layout of every game function (to compare with the decompilation's `tools/verify/frames_all.py`) |

`tools/gametest/run_scenario.sh` runs one scripted, headless session (muted, own save copy) and
`tools/gametest/compare.py` compares two of them step by step: Link, camera, random numbers, sounds,
a hash of every game object after each update, and the save data.

## Licences

The decompilation (`decomp/`) is released under CC0 1.0 (public domain dedication); the port, including
the decomp build's glue in this repository, is under the MPL-2.0 like the rest of the port. Neither
contains game files: the game is always built from your own copy.
