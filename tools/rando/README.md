# tools/rando: the in-app randomizer

`wwhd-rando` is [TWWHD Randomizer](https://github.com/SuperDude88/TWWHD-Randomizer) 1.2.0 by SuperDude88 and
its contributors (MIT, `twwhd_rando/LICENSE.txt`), packaged as a command for the port's setup. The randomizer and
its region support come from GreenNaugahyde's
[ZeldaWWHDRecompAndroid](https://github.com/GreenNaugahyde/ZeldaWWHDRecompAndroid) (MPL-2.0, commit `db65d60`).
`twwhd_rando/WWHD.md` lists every change made to the randomizer.

The game build (the top-level `CMakeLists.txt`) does not include this folder.

```sh
cmake -S tools/rando -B build/rando -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/rando
build/rando/wwhd-rando --data build/rando/data --work WORK settings
python3 tools/rando/test_rando_cli.py build/rando
```

## Commands

Each command prints one JSON object (`{"ok": true, ...}` or `{"ok": false, "error": "..."}`) and runs
in a process of its own. The randomizer keeps global state, and a run needs about 1 GB of memory.
`WORK` holds the randomizer's own `config.yaml` and `preferences.yaml`, plus its `logs/`, `progress`
and `last.json`.

| Command | What it does |
|---|---|
| `version` | `1.2.0` |
| `settings` | every plain setting by its randomizer name, as a value index; the seed and the permalink |
| `get NAME`, `set NAME INDEX` | one setting (names as in `options.cpp`, e.g. `Progress Dungeons`) |
| `seed`, `set-seed TEXT`, `new-seed` | the seed name |
| `defaults`, `preset beginner\|standard\|everything` | the randomizer's defaults, optionally with a few changes; the seed name stays |
| `permalink`, `load-permalink TEXT` | the same permalink as the PC randomizer 1.2.0 |
| `generate --game GAME --out OUT` | one randomized game; only the changed files go to `OUT` |

Refused:
- race-mode settings (no spoiler log). The official builds add a secret key to those seeds, so the same permalink would make a different game here.
- plandomizer settings.
- the Japanese release (the port has no Japanese build).
- the European release, until its compatibility check exists (`--allow-eur` is for that work only).

## Spoiler map places

`places.py --game GAME --out places.tsv` reads the player's own stage files and writes where each
randomizer location is: its sea square, stage and position. Setup runs it on the player's machine.
Nothing game-derived ships with the port.

## Checks against the official randomizer

These need a USA game and run outside CI:
- The `code/cking.rpx` must have the same SHA-256 as the official 1.2.0 build makes from the same settings.
- With `-DWWHD_RANDO_UPSTREAM_ART=ON` and upstream's `assets/` in the data folder, every changed file must be the same. This build is for that test only and is never released.
