# Randomized USA games (experimental)

Setup compiles the player's randomized USA RPX together with that seed's content. It is
an experimental installer feature, not a claim that every seed is compatible.

## Randomized games made in setup

Setup can now make randomized games itself. It includes TWWHD Randomizer 1.2.0 by SuperDude88
and its contributors (MIT), so you don't need the PC randomizer or a full copy of the game per
seed. Install the game as it is (USA release) first.

In the setup window, choose **Randomized games (TWWHD Randomizer)**:
- **New game** has the presets (beginner, standard, everything), the seed name, the permalink
  (copy, or paste one), and every setting on six pages. **Make this game** makes and builds it;
  the game plays it from its next start.
- **Games** lists the game as it is and every randomized game made, each with **Play this**,
  **Copy permalink**, **Spoiler log...** (it asks first) and **Delete...** (asks first; the saves
  stay).

In the game, **Settings > Randomizer** shows:
- this game's seed hash words and permalink;
- the games already built, to play from the next start;
- the spoiler log, after a question: the way to the end, every location, entrances, hints,
  charts, and a map of the sea chart's squares with the places of the locations.

The same in a terminal:

```sh
python3 tools/installer/setup.py --randomize --rando-preset standard   # or beginner, everything
python3 tools/installer/setup.py --randomize --rando-permalink PERMALINK
python3 tools/installer/setup.py --randomize --rando-new-seed          # same settings, new seed
python3 tools/installer/setup.py --list-seeds
python3 tools/installer/setup.py --play-seed 2                         # or vanilla
python3 tools/installer/setup.py --delete-seed 2 --yes
```

What each run does:
- Each seed keeps only the files the randomizer changed (about 500 MB), in
  `data/randomizer/seeds/<id>/`, together with its spoiler log.
- The same compatibility check as below runs before anything is built, then that seed's game
  code is built. Making a seed takes about 10 s. The first seed's build takes about as long as
  the game's first build. Later seeds compile only the few source files that differ (about a
  sixth of them); setup keeps compiled files in `data/code-objects` (up to 1 GB). On a Mac with
  4 compile jobs that took 94 s for the first seed and 24 s for the next.
- The game plays the seed from its next start. `--play-seed vanilla` switches back. A
  build that was made before is reused, so switching takes no new build.
- Saves, backups and save states stay separate per seed.
- Deleting a seed removes its files and keeps its saves.

Permalinks match the PC randomizer 1.2.0, with these exceptions:
- Race-mode settings ("do not generate spoiler log") are refused. The official randomizer adds
  a secret key to those seeds, so the same permalink would make a different game. Use the
  official randomizer for race seeds and import its output folder as described below.
- Plandomizer settings are refused.
- The randomizer's replacement title logo, its icons and the key-chest texture are left out:
  they are edited game art, which the port doesn't ship.

Setup also reads the places of the randomizer's locations from your own game's stage files for
the spoiler log's map (`data/randomizer/places.tsv`).

A language source from another region is not used while a randomized game is played. The
randomizer changes only the installed game's own texts.

## Player setup

Generate a USA game with the official TWWHD Randomizer, then choose its complete
`code/content/meta` output folder in setup. Selecting that folder's
`code/cking.rpx` also works; the surrounding content and metadata are required.
Keep the seed output for as long as that installation uses it.

Setup automatically checks the section/import layout, every port hook instruction
and the complete containing functions against a shipped compatibility profile.
The profile contains hashes and addresses only. Players do not need to supply a
second original dump or manually review hooks. Unreviewed changes are rejected;
a matching randomizer title alone never bypasses these checks. The normal
supported-build registry still rejects arbitrary modified RPX files.

Randomized builds automatically preserve assembly scratch-register returns and
use translated randomizer code throughout; decompiled replacements are disabled.
An executable checks its exact RPX hash before accessing saves. If you regenerate
the output in place, run setup again for the new seed.

Each randomized executable uses the full RPX SHA-256 to isolate its saves,
backups, cloud-folder preferences and save states from other seeds. Settings and
controls remain shared. Importing a save in setup targets the currently installed
seed. Default save folders are siblings, such as `save-randomizer-<hash>/user`
and `save-backups-randomizer-<hash>/`. When you choose an empty cloud folder,
its seed-specific save folder stays inside that folder so it is synced. When
selecting an existing vanilla save folder, the randomized save uses a sibling
instead. An explicit `--save DIR` uses exactly that directory, except a folder named `save` (the install's
own, which setup's Play button, launchers and shortcuts pass): the seed's folder is its sibling then. Save-state slots
retain their per-seed subfolder.
Copying a save or a state manually between seeds is unsupported.

If an earlier experimental build left randomized data inside a vanilla save or
backup folder, randomized builds stop before restoring or moving saves. Ordinary
vanilla startup does not run this experimental-layout check. Follow the shown
old and new paths to move that data out of the vanilla folder while the game is
closed. Never overwrite or merge an existing destination; keep both copies until
you have verified which one to use. Update any old cloud-folder choice as well.

The automatic compatibility check covers TWWHD Randomizer **1.2.0** (the newest
release) with every option that patches game code, each alone over the defaults:
instant text boxes and full sea chart (on and off), Quiet Swift Sail, fix RNG,
performance, classic mode, inverted sea compass, swordless, remove music, fewer
than two starting hearts, random starting island, and dungeon, boss, miniboss,
cave, door and misc entrance shuffle. Combinations checked: all entrances mixed
and decoupled, everything code-related on at once, a race-style seed, and the
cosmetic and data-only options. Older releases (1.1.1 and earlier) are not covered; the check refuses them.
Windows and a full playthrough still need validation; passing the hook check is
not a guarantee of complete gameplay compatibility.

## Maintainer audit of a new compatibility variant

You need an original supported USA dump and a randomized `code/content/meta`
folder. The experimental tool accepts the randomizer title `0005000010143599`
and the USA address layout. It requires the exact randomized RPX SHA-256 and
checks the original dump against the supported-build registry.

```sh
shasum -a 256 /private/randomized/code/cking.rpx
python3 tools/recomp/randomizer_experiment.py \
  --base-game /private/original-usa \
  --game /private/randomized \
  --sha256 FULL_RANDOMIZED_RPX_SHA256 \
  --report /private/experiment/audit.json \
  --audit-only
```

The JSON report contains addresses and counts, without instruction bytes. It
separates changed original function bodies, appended text, hooks in changed
functions and changed hook instructions. It compares the actual output, not just
an upstream patch catalogue: options and seeds can produce different changes.

Review every overlapping function against the port's hook implementation. An
unchanged hook instruction does not prove compatible behavior: a replacement
that skips the original function could skip the randomizer patch too. The tool
refuses to generate if a hook instruction changed or text lies outside the
reference function map. Such cases need a port change and another audit.

After that review, maintainers can regenerate the shipped hash-only profile:

```sh
python3 tools/recomp/mk_randomizer_profile.py /private/original-usa/code/cking.rpx \
  tools/recomp/randomizer-usa.json \
  --reviewed-rpx /private/randomized/code/cking.rpx \
  --reviewed-functions REVIEWED_COMMA_SEPARATED_FUNCTIONS
```

Repeat `--reviewed-rpx` for independently reviewed variants. The profile is rebuilt
from scratch, so pass outputs that together cover every reviewed variant (for 1.2.0:
one seed with Quiet Swift Sail and any entrance shuffle on, or one seed for each),
or earlier variants are dropped. The tool refuses a
changed hook instruction or a changed function missing from the explicit review
list. Adding a port hook also invalidates the profile until it is regenerated
and reviewed; `test_randomizer_compat.py` checks that it matches the current hooks.

## Generate and build

After reviewing the reported overlaps, pass their exact function entries. For
default-option 1.2.0 seeds these are `0216F7FC,023EFF20,025499A0`; Quiet Swift Sail adds
`0247E854` and any entrance shuffle adds `025217F8`. Do not copy that list for
another output without reading its report.

```sh
python3 tools/recomp/randomizer_experiment.py \
  --base-game /private/original-usa \
  --game /private/randomized \
  --sha256 FULL_RANDOMIZED_RPX_SHA256 \
  --report /private/experiment/audit.json \
  --reviewed-hook-functions REVIEWED_COMMA_SEPARATED_FUNCTIONS \
  --outdir /private/experiment/gen
cmake -S . -B /private/experiment/build -G Ninja \
  -DGEN_DIR=/private/experiment/gen -DCMAKE_BUILD_TYPE=Release \
  -DWWHD_DECOMP=OFF
cmake --build /private/experiment/build --target wwhd -j4
```

Select the normal renderer for the platform if needed (`WWHD_RENDERER=METAL` on
macOS). The tool preserves the randomizer title in the generated build map. Do
not use decomp function replacements: those would restore vanilla behavior over
randomized code.

The tool enables `Recompiler(..., preserve_registers=True)`. The equivalent
low-level compiler switch is `--preserve-registers`; it does not bypass RPX
identification. It disables CR liveness and leaf/nonleaf ABI register-local
optimizations, preserving scratch register writes across guest calls. The
randomizer's new-save helper returns a value in `r12`, outside the usual EABI
return registers. Default code generation discarded it and crashed after name
entry. Register preservation fixes that case without enabling mod-hook dispatch.
Vanilla compiler defaults stay unchanged. Add `--mod-hooks` to the experimental
tool when testing existing mod-SDK hooks alongside the randomized RPX. This
instruments the generated functions with the normal SDK dispatch; it does not
convert the randomized RPX into a core mod or resolve conflicts with other mods.

Run the generated executable against the matching randomized folder. Test a fresh
file through name entry and new-save initialization as well as existing saves.

## Evidence and limits

Compatibility has been checked with TWWHD Randomizer **1.2.0**. Through normal
setup on macOS Metal, default-option, swordless, Quiet Swift Sail and door-entrance
seeds reached playable Outset, and a seed with every code-patching option on
(random starting island) reached its starting island. Fresh saves contained the expected starting equipment, songs and
visited islands. These are initialization and boot checks, not full playthroughs.
Windows builds pass CI; Windows gameplay remains untested.

Passing the compatibility check verifies the audited code boundaries, not every
randomizer option or gameplay path. Other mod combinations also need testing.
Generated appended text can include data reported as unhandled instructions;
maintainers should inspect those reports before approving another variant.

The upstream [licence](https://github.com/SuperDude88/TWWHD-Randomizer/blob/main/LICENSE.txt)
is MIT. This feature distributes no randomized game files or upstream randomizer
implementation. Players generate their own output using the official randomizer.
