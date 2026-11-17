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
- functions that contain one of the port's instruction-level patches, unless the decompiled source asks the port at
  that point itself (the decompilation's `port_hooks.h`, game build only: `tools/decomp_build/port_sites.py`). So far
  the Classic Wind Waker points in procTactWait and endDemoMode, and Link's player code: quick use, camera-directed
  items, the movement mods, climbing and steering. Such a point runs the hook's own handler
  (`runtime/src/port_points.h`), so the feature behaves the same in both builds;
- functions a code mod hooks or replaces (they stay recompiled while that mod is on, so the mod
  keeps working; see Mods below).

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
- Building it from source: macOS (and Android cross builds). In setup: macOS, Linux (the Steam Deck too) and Windows
  (below); Linux and Windows are untested on real machines so far (CI builds and checks their parts).

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

Option `-DWWHD_DECOMP_THINLTO=ON` (default off) compiles each game unit with ThinLTO, optimised as a whole when
`ld -r` merges it (units stay isolated, so nothing is inlined across units); measured on a Mac it gave no clear
gain (game thread 12.6 to 11.9 core-seconds per 25 s, median, inside the run-to-run noise).

Option `-DWWHD_DECOMP_SITE_LR=OFF` (default on) turns off site-exact LR. With it on, each unit source is compiled
once more as LLVM IR (`tools/decomp_build/site_lr.py`, about one second per source) to find which call expression of
a decompiled function reaches which game function, and the k-th call to a function is given the return address of
the k-th `bl` to it in the original, where both have the same number of calls. Runtime hooks, imports and mods that
read LR then see the caller's exact call site there; elsewhere LR stays as before (the per-unit report is
`decomp/sites/K.txt` in the build directory).

## Fast mode

Fast mode (`-DWWHD_DECOMP_FAST=ON`, cmake/Decomp.cmake) is the decomp build made for speed. It is what setup builds by
default. Compared with the plain decomp build:

- calls between decompiled functions are plain C++ calls, bound by address when the game is linked, and with enough
  memory (12 GB or more) the final link optimises across all game units (whole-image ThinLTO);
- the decompiled draw sites hand the renderer native draw packets (the native renderer). Its gain is on **Vulkan**: on
  Metal every draw takes the full path as before. The decomp version on the Mac therefore starts with Vulkan
  (MoltenVK) by default (below);
- it drops what only verification needs: the per-function isolation of the verifier, site-exact return addresses.

What it means for players:

- **Save states:** portable states (the normal Save, Quick save, the automatic saves) work as in the normal game. Full
  states (the debugging option, Crash Recovery's automatic states) are made and loaded by the same kind of build only: a
  full state made by the normal game, the plain decomp build or fast mode loads only in a build of the same kind, and
  any other says so ("full state of the normal game; it loads only there (use a portable state)").
- **Code mods:** calls between decompiled functions are bound when the game is linked, so the functions installed
  code mods hook are kept enterable at build time (below, "In setup"); built-in mods, content mods and graphics packs
  work as in the normal game.
- **Renderer:** the decomp version on the Mac starts with Vulkan (MoltenVK) unless you chose a renderer in Graphics >
  Renderer (that choice is shared with the normal game and wins), or start it with `--renderer=metal`. If Vulkan cannot
  start, it falls back to Metal. The normal game is unchanged: it still starts with Metal. Both keep their shader
  caches apart (Metal's and Vulkan's are separate files), so switching never discards the other's.

## In setup (macOS, Linux, Windows)

Setup (regular releases and development builds; macOS, Linux including the Steam Deck, and Windows) builds **one**
version of the game, the one you choose:

- **Recompiled version (default)**: the original game code from your copy, translated and compiled; the recommended way
  to play and the fastest setup;
- **Decomp version (experimental)**: the decomp version described here.

Only the chosen version is built and started; nothing of the other one is built or kept.

- in the setup window: choose **Decomp version (experimental)** under "Which version of the game to build" (shown with
  the other build options; Android: below), then install, repair or update;
- in a terminal: `tools/Setup in Terminal.command --decomp` (macOS), `tools/setup-in-terminal.sh --decomp` (Linux),
  `tools\Setup in a console window.bat --decomp` (Windows); `--recomp` (or `--no-decomp`) chooses the recompiled
  version.

The choice is remembered: a repair or an update builds the same version again. To switch, run setup again and choose the
other version (a repair, or `--decomp` / `--recomp` in a terminal): setup builds it, makes it the one that starts and
removes the game binary of the other one (also its copy kept in `data/bin.previous`). The downloaded decomp source, the
declaration table and the compiled objects stay (`data/decomp-src`, `data/decomp-cache`, the object cache), so switching
back is faster; the recompiled version's code-mod builds stay in `data/code-builds` as before. If the decomp version
cannot be built, setup says why and stops; the game installed before is unchanged and starts as before.

An install made by an earlier setup with both versions ("Also build the decomp version") keeps both and starts the one
it started before, until setup is run again with a choice: the setup window then preselects the version that starts and
keeps only the chosen one. Until then **Start the decomp version instead of the normal game** in the setup window (hold
Shift while starting Wind Waker HD) or `--start-decomp 1` / `0` in a terminal still switch between the two.

It builds **fast mode** (above). For testing, `--decomp-plain` builds the plain decomp build instead (remembered;
`--decomp` goes back to fast mode).

The decomp version's step (it translates the game code itself as well) was measured in the real setup flow, then still
after the recompiled version, on a Mac (Apple M3 Max, 16 cores, 128 GB, 8 compile jobs) that was busy with other builds
at the same time (load 17 to 68), so a quiet Mac is faster:

| | fast mode (default) | plain build (`--decomp-plain`) |
|---|---|---|
| time | 41 min (first build; a repair at load 35 to 68 took 84 min) | 31 min (a repair; load about 45) |
| of which: the declaration table | 7 to 15 min (once per decompilation pin and code-mod choice; kept in `data/decomp-cache`) | none |
| memory of setup's processes, peak | 4.0 to 4.4 GB (the whole-image link about 3.5 GB) | 2.6 GB |
| disk space while it builds | about 1 GB (`data/decomp-work`); setup asks for 4 GB free | about 0.7 GB; setup asks for 3 GB free |
| kept afterwards | the game (about 200 MB), the decomp source (120 MB), compiled objects for the next repair | the same |

With less than 12 GB of memory fast mode links each game unit on its own (no whole-image link: less memory, a little
less speed).

It is built with the install's code-mod choice (Settings > Mods, "code-mod support"), and switching code-mod support
in the game rebuilds the decomp version (only it: no recompiled build is made; if that fails, the game built before stays
and Settings > Mods says why). With code mods on, setup installs the Story Notebook with the built decomp version and
then asks it which game functions the installed code mods hook; fast mode keeps those enterable for the mods
(`data/decomp-mod-hooks.txt`, kept across repairs and updates) and builds again when the list grew. Randomized games
are built and played by the recompiled version only: with the decomp version, setup's randomizer says to choose the
recompiled version first.

The version shown in Settings (`F1`) > Language / About ends in `decomp`. The decomp version uses the same saves,
settings and shader caches as the recompiled version.

What setup uses and makes:

- the decompilation's source at the version the release was made with: setup downloads it
  from [github.com/ZeldaWWHDDecomp/wwhd](https://github.com/ZeldaWWHDDecomp/wwhd) (about 8 MB, only
  when the option is on) and checks the unpacked files against the SHA-256 the release names before it
  uses them. It contains no game code or data. It is kept in `data/decomp-src`, so a repair or an
  update with the same version downloads nothing. Fast mode also downloads the disassembler its declaration table needs
  (capstone 5.0.7, BSD-3-Clause, about 1 MB from PyPI, checked by its SHA-256; kept there too). Without an internet
  connection setup says so ("Couldn't download the decomp source"); the normal game is ready as usual, and the next
  repair tries again;
- your own `cking.rpx`: setup prepares the decompiled source with it as described above (the model
  data headers, the source glue), translates the game code with the decomp build's function list, works out which
  calls can be native (`native_rules.py`; fast mode first checks every decompiled call's declaration against your
  game code, `decl_table.py`) and compiles the game units with the same compiler as the normal game (the Command Line
  Tools; fast mode's ThinLTO uses their own libLTO);
- everything it generates stays in the install's data folder (`data/decomp-work`, removed after the
  build); the result is `data/bin/wwhd-decomp` (portable releases) or `data/decomp/bin/wwhd` (`.exe` on Windows).

On Linux and Windows setup uses the compiler it already downloads for the normal game and nothing from the system
(`tools/installer/decomp_platform.py`):

- **Linux** (the Steam Deck too: SteamOS has no compiler, none is needed): the pinned zig. Each game unit is merged with
  `zig ld.lld -r` and its hidden symbols are made local by `tools/decomp_build/elf_localize.py` (what
  `llvm-objcopy --localize-hidden` does; zig has no llvm-objcopy). Fast mode always links each unit on its own (zig has
  no tool for the whole-image link). The declaration table's `clang++` is a small script in the work folder that runs
  `zig c++`.
- **Windows**: the pinned llvm-mingw. Windows object files have no relocatable link, so every game unit source is
  compiled to LLVM bitcode and each unit is linked and isolated in the IR (`tools/decomp_build/unit_ir.py --libllvm`,
  through llvm-mingw's own `libLLVM-23.dll`), then compiled to one object; each unit on its own (no whole-image link
  yet). Its tools read the decompilation with ordinary Windows paths, so the data folder must be at most 164 characters
  long (setup checks this first). **Windows on ARM** builds the plain decomp build: capstone, which fast mode's
  declaration table needs, has no Windows ARM64 package.
- capstone 5.0.7 from PyPI: the manylinux wheel (x86-64, ARM64) on Linux, the win_amd64 wheel on Windows.
- The decomp version starts with Vulkan, the renderer of Linux and Windows anyway.

## In setup (Android)

The Android app's setup offers the same choice: under **Which version of the game to build** on the setup screen (next
to **Code mods**), **Recompiled version (default)** or **Decomp version (experimental)**. Only the chosen version is
built on the phone and started; the decomp version in fast mode, with the Code mods choice (switching Code mods, on the
setup screen or in Settings > Mods, rebuilds only it). Choosing the other version on a finished setup builds it now; the
game built before stays playable meanwhile and, if the build fails, stays the one that starts (the setup screen says why;
Continue tries again). Once the new version is active, the other one's library goes; the decomp source, the
declaration table and the compiled objects stay for a later switch back. Randomized games play in the recompiled version
only: with the decomp version chosen, the randomizer says to choose the recompiled version first.

An app that built both versions before this choice ("Also build the decomp version") keeps both and starts the one it
started (**Start the decomp version instead**, shown only then), until a version is chosen.

How it differs from the Mac (tools/android/decomp_android.py):

- the release carries the decomp parts in the app's runtime SDK (per ABI, plain and fast; no game code), and the
  app's compiler brings `llvm-objcopy` (and `llvm-link` / `llvm-dis` / `llvm-as` for the whole-image link);
- the game units are linked one by one (`ld.lld -r`, ThinLTO inside the unit, then `llvm-objcopy --localize-hidden`).
  The whole-image link (about 5.5 GB at its peak) is not used on phones until it has been measured there;
- the declaration table's disassembly uses the port's capstone stand-in (`tools/decomp_build/capstone_shim`: the same
  text as capstone 5.0.7 for every instruction of the game; nothing is downloaded from PyPI);
- the setup's Python tools run inside the app's setup process, one after the other;
- the built functions code mods hook come from the release's list and from the decomp version's own record (setup
  cannot start the game as a probe on a phone).

## Mods

The decomp version supports the same mods as the normal game:

- built-in mods (Settings > Mods), graphics packs, content mods and Vulkan shader overrides need nothing extra;
- code mods (the catalogue's mods, the Story Notebook and the story system) need code-mod support, as in the normal
  game: the decomp version follows setup's **Build with code-mod support** choice, and switching it in
  Settings > Mods rebuilds the decomp version too, then the game restarts. A function a code mod hooks or replaces
  runs recompiled, with the mod's check in it.
- In fast mode, calls between decompiled functions are fixed when the decomp version is built, so setup builds it
  with the list of functions your code mods hook (`decomp-mod-hooks.txt` in the data folder); only calls to those
  functions change. If you switch on a code mod later that hooks another function, that start runs all game code
  recompiled (the log says `[decomp] code mods: ... not hooked when this decomp version was built`), and the next
  repair or code-mod rebuild includes it.
- Randomized games (the Randomizer page) play in the normal game only: the decomp version is made from the game as
  it is. Starting the decomp version while a randomized game is selected says so and plays the game as it is.

Debug switch: `WWHD_DECOMP_MOD_HOOKS_PROBE=FILE` writes the game functions the enabled code mods hook to FILE and
exits (setup uses it); `-DWWHD_DECOMP_GUEST_HOOKS=FILE` builds a fast decomp version from source with such a list.

## Releases

Every macOS, Linux and Windows release brings the decomp version's parts (release.yml "Decomp version parts"; Windows
on ARM: the plain build only): the runtime built twice
without game units (`-DWWHD_DECOMP_RUNTIME_ONLY=ON`, plain and `-DWWHD_DECOMP_FAST=ON`; tools/release/package.py
`--decomp-build` / `--decomp-fast-build`), the commit and SHA-256 of the decompilation, and the tools above. A release
cannot ship the compiled decomp units: their sources are prepared with data read from your own game, so setup builds
them. The decomp release variant (a separate download whose setup translates the game with `sdk/decomp_hooks.txt`) is
not possible for the same reason.

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
| `WWHD_DECOMP_AUTOFRAME_FAST=0` | the original's stack frame of a decompiled function is looked up on every entry instead of read from a per-function table made at start-up (same frames; a check switch) |
| `WWHD_DECOMP_FRAMES_DUMP=path` | write the stack frame layout of every game function (to compare with the decompilation's `tools/verify/frames_all.py`) |

`tools/gametest/run_scenario.sh` runs one scripted, headless session (muted, own save copy) and
`tools/gametest/compare.py` compares two of them step by step: Link, camera, random numbers, sounds,
a hash of every game object after each update, and the save data.

## Licences

The decompilation (`decomp/`) is released under CC0 1.0 (public domain dedication); the port, including
the decomp build's glue in this repository, is under the MPL-2.0 like the rest of the port. Neither
contains game files: the game is always built from your own copy.
