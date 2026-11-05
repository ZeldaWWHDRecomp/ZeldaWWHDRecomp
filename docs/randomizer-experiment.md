# Randomized games

The port includes TWWHD Randomizer 1.2.0 by SuperDude88 and its contributors (MIT). Setup makes
randomized games from your installed game and builds them; you don't need the PC randomizer or a
full copy of the game per seed. A seed made with the official TWWHD Randomizer 1.2.0 can be played
too (see [A seed from the official randomizer](#a-seed-from-the-official-randomizer)).

Install the game as it is (the USA or the European release) first. The Japanese release can't be
randomized: the port has no Japanese build.

## Make a randomized game

In the setup window, choose **Randomized games (TWWHD Randomizer)**:
- **New game** has the presets (beginner, standard, everything), the seed name, the permalink
  (copy, or paste one), and every setting on six pages. **Make this game** makes and builds it;
  the game plays it from its next start.
- **Games** lists the game as it is and every randomized game made or imported, each with
  **Play this**, **Copy permalink**, **Spoiler log...** (it asks first) and **Delete...** (asks
  first; the saves stay). **Import seed...** adds a seed made with the official randomizer (see
  below).

Making a seed takes about 10 s. The first seed's build takes about as long as the game's first
build; later seeds build much faster (on a Mac: 94 s for the first seed, 24 s for the next).
Switching between games that were built before needs no new build. Each seed takes about 500 MB.

## In the game: Settings > Randomizer

- this game's seed hash words and permalink;
- the games already built, to play from the next start;
- the spoiler log, after a question: the way to the end, every location, entrances, hints,
  charts, and a map of the sea chart's squares with the places of the locations. Setup reads
  those places from your own game files.

## Android

The setup screen has **Randomized game...** once the game is set up:
- choose a preset and a seed name, or paste a permalink;
- **Make it**: setup makes the randomized game from the installed one, checks it and builds it,
  and the game plays it from its next start;
- **Import a seed...** in the same dialog adds a seed made with the official randomizer (see
  below);
- **Play the game as it is** in the same dialog goes back to the normal game.

## Saves

Every randomized game has its own saves, backups and save states, kept beside the normal game's
(`save-randomizer-<the game code's SHA-256>`). Settings and controls are shared. Deleting a seed keeps its saves.
Saves don't move between seeds, and copying a save or a state by hand between seeds is not
supported. A seed made in setup and the same seed made with the official randomizer have
different game files, so each has its own saves.

If you choose a cloud save folder, the seed's saves stay inside it. A `--save DIR` start uses
exactly that folder (except the install's own `save` folder, which Play and the shortcuts pass).

If an older experimental build left randomized saves inside the normal save folder, the
randomized game stops before touching saves and shows the old and new paths. Move that data
while the game is closed; never merge or overwrite an existing folder.

## A seed from the official randomizer

Make a USA game with the official TWWHD Randomizer 1.2.0 (the official randomizer supports the
USA release only), then:
- in the setup window: **Randomized games (TWWHD Randomizer)**, **Games**, **Import seed...**,
  and choose its output folder (the one with `code`, `content` and `meta`);
- on Android: **Randomized game...**, **Import a seed...**, and choose that folder.

Setup runs the same check as for every randomized game, keeps only the seed's files that differ
from your installed game (the installed game must be the USA one, as it is) and builds it. The
game plays it from its next start, with its own saves. You can delete the output folder
afterwards. Imported seeds have no permalink, and their spoiler log stays in the official
randomizer's `logs` folder.

Setup no longer installs a randomized output folder as the game: choosing one as the game (in
the setup window, on Android, or with `setup --game-dir`) is refused with a pointer to Import
seed (`setup --import-seed PATH` in a terminal).

If an earlier setup installed a randomized output folder as the game, it still plays, updates
and repairs as before, with its saves. To use the Randomizer page, install the game as it is
(**Change the game**); importing that same output folder again then plays it with the same
saves.

Use the official randomizer for race seeds ("do not generate spoiler log"); the included one
refuses those settings.

## Permalinks and limits

Permalinks match the PC randomizer 1.2.0, with these exceptions:
- Race-mode settings are refused: the official randomizer adds a secret key to those seeds, so
  the same permalink would make a different game.
- Plandomizer settings are refused.
- The randomizer's title logo, icons and key-chest texture are left out: they are edited game
  art, which the port doesn't ship.

While a randomized game is played, a language source from another region is not used: the
randomizer changes only the installed game's own texts.

Before anything is built, setup checks the randomized game code against the port: every changed
function the port hooks must be a reviewed change. A seed that fails the check is refused. The
check covers TWWHD Randomizer 1.2.0 with every option that changes game code; older releases
(1.1.1 and earlier) are refused. Passing it is not a full playthrough guarantee: so far seeds
were played up to their starting island on macOS. Windows gameplay is untested.

European randomized games use GreenNaugahyde's port of the randomizer's patches to the European
release (ZeldaWWHDRecompAndroid, MPL-2.0).

## In a terminal

```sh
python3 tools/installer/setup.py --randomize --rando-preset standard   # or beginner, everything
python3 tools/installer/setup.py --randomize --rando-permalink PERMALINK
python3 tools/installer/setup.py --randomize --rando-new-seed          # same settings, new seed
python3 tools/installer/setup.py --list-seeds
python3 tools/installer/setup.py --play-seed 2                         # or vanilla
python3 tools/installer/setup.py --delete-seed 2 --yes
python3 tools/installer/setup.py --import-seed /path/to/official-output  # a seed from the official randomizer
```

Seeds are kept in `data/randomizer/seeds/<id>/` with their spoiler logs.

Maintainers: the compatibility profiles and how a new randomizer variant is reviewed are in
[randomizer-maintainers.md](randomizer-maintainers.md).
