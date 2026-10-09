# Wind Waker save tools (GameCube → Wii U HD)

## How to use (players)

Bring your GameCube Wind Waker progress to Wind Waker HD:

1. Export the save from your memory card as a `.gci` file (Dolphin: Tools › Memory Card
   Manager › Export; a real card: GCMM, Swiss or similar). USA (GZLE01) and Japanese (GZLJ01)
   saves are supported; European (GZLP01) saves are rejected.
2. Convert it (Python 3, nothing else needed):

   ```
   python3 tools/savegame/gc2hd.py "My Save.gci" -o converted
   python3 tools/savegame/hd_save_info.py converted      # check: hearts, items, songs, ...
   ```
3. Put `converted/cking.sav` in place. **It replaces all three Quest Logs: back up the old
   `cking.sav` first.**
   - this port: `save/user/cking.sav` (or the folder you pass with `--save`, plus `/user/`);
   - Cemu: `mlc01/usr/save/00050000/10143500/user/80000001/cking.sav` for the USA game
     (title ID 0005000010143500; `80000001` is the first Wii U user). For other regions, use the
     folder of your own copy: Cemu shows the title ID in the game list (right-click → properties).
   Leave the other files there (`cking_pic*.sav` are the Picto Box album, `cking_playlog.sav`
   the play log); HD creates them if they are missing.
4. Start the game: the three GameCube files appear as Quest Logs 1–3 with their names, places and
   progress. Saving in HD then works as usual.

**Japanese saves:** names in Japanese characters cannot be shown by the USA HD font; full-width
Latin letters are converted to normal ones, any other name becomes "Link" (`--name NAME` sets a
name of your choice for all three files).

**Not converted:** GameCube Picto Box pictures (HD has its own album format), the memory card
banner/icon. The **Tingle Tuner** becomes HD's **Tingle Bottle** (same item number and the same
Tingle Island event; `--drop-tingle-tuner` removes it instead). HD-only data (for example its
play statistics) starts as in a new HD file. Files that were empty on the GameCube stay
"New Game".

## Tools

Plain Python 3, no dependencies, no game data. Layouts come from the zeldaret tww decompilation
(GameCube) and this project's verified WWHD decompilation (HD); `wwsave.py` names the exact
functions.

| Tool | Use |
|---|---|
| `gc2hd.py SAVE.gci -o OUTDIR` | GameCube memory-card save (`.gci`, USA GZLE01 or Japan GZLJ01) → `OUTDIR/cking.sav` for HD. `--batch SRC_DIR -o ROOT` converts every `.gci` below `SRC_DIR` into `ROOT/<path>/user/cking.sav`. Options: `--drop-tingle-tuner`, `--name NAME`. |
| `hd_save_info.py PATH` | progress of each file: hearts, rupees, magic, items, sword/shield, arrows/bombs, songs, Triforce shards, pearls, sail (Sail / Swift Sail), islands visited and charted, charts owned, dungeon items and bosses, return stage, time of day, deaths, New Game+ count, save counter. `PATH` = `cking.sav`, a `save/` or `save/user/` folder, or a `.gci`. `--short` (one line per file), `--json`, `--file N`. |
| `hd2gc.py cking.sav -o OUT.gci` | the reverse direction (shared fields only), mainly for the round-trip check. `--set-gc-name` writes the HD name into the GameCube name field, `--template X.gci` copies the banner/icon block. |
| `roundtrip_test.py cking.sav ...` | HD → GameCube layout → HD; every shared field must come back bit-identical (GameCube and HD checksums are verified on the way). |
| `wwstate.py info STATE.wwstate` / `wwstate.py to-sav STATE.wwstate -o OUTDIR [--into cking.sav] [--file N]` | a portable save state (`slotN.wwstate`, [docs/portable-save-states.md](../../docs/portable-save-states.md), e.g. from a bug report): check it and show its place and progress, or write its save data as Quest Log N of a `cking.sav`. |
| `wwsave.py repair-medli cking.sav -o OUTDIR [--file N]` | repair an early save whose premature Master Sword ownership hides Medli (#85). Writes a repaired `OUTDIR/cking.sav` and an exact `OUTDIR/cking.sav.before-medli.bak`; never changes the input or overwrites existing output files. Quest Log N defaults to 1. |

Work on copies: none of the tools writes to its input. Use a converted save like any test save:
`cp -R <folder>/user <test>/save/` and start the game with `--save <test>/save`.

## Medli missing on the first Dragon Roost visit (#85)

The old **Master Sword + Mirror Shield** cheat also recorded ownership of every sword upgrade.
The game uses those ownership bits as story milestones: the Earth Temple upgrade removes Medli
from her earlier locations. Turning mods off or restarting does not undo changes already saved.
The corrected cheat equips the weapons without changing ownership and is labelled **until reload**;
loading a Quest Log or portable state restores the equipment earned in that playthrough.

For an affected early save, close the game and run:

```sh
python3 tools/savegame/wwsave.py repair-medli /path/to/cking.sav -o medli-repaired --file 1
python3 tools/savegame/hd_save_info.py medli-repaired/cking.sav --file 1
```

Use the actual Quest Log number (1–3). Keep the source and the generated `.bak`, then replace the
game's `cking.sav` with `medli-repaired/cking.sav` and load that Quest Log normally. The tool restores
the Hero's Sword and its ownership in that Quest Log; it preserves the shield, inventory, event
flags, other progress, and the other Quest Logs. It verifies the save checksums and refuses saves
past the first pearl, New Game+ saves, or saves without the premature sword flags. It does not
guess earned sword progress in later saves.

An old portable state still contains the old flags: do not load it over the repaired save. Make
a fresh portable state after loading the repair. If your only current progress is in a portable
state, first use `wwstate.py to-sav STATE.wwstate -o state-copy --into /path/to/cking.sav`, then repair
`state-copy/cking.sav` as above, choosing the state's Quest Log. Neither step changes its input.

## File formats

**GameCube `.gci`** (`m_Do_MemCardRWmng`): 0x40-byte directory entry, then 12 card blocks of
0x2000. Block 0: banner, icon, comment. Blocks 1 and 2: two copies of `card_savedata`
(save count, data version, three `card_gamedata` of 0x770 = 0x768 bytes of save data + u64
checksum (byte sum, complement sum), u32 checksum of big-endian u16 sums at 0x1FFC). Blocks 3..11:
Picto Box pictures. Read like `mDoMemCdRWm_Restore`: per file the first copy, the second if the
first fails its checksum.

**HD `cking.sav`** (8966 bytes, SaveMgr in `wwhd_src/d/d_menu_save_*`):

| Offset | Size | Content |
|---|---|---|
| 0x0000 | 3 × 0xA94 | per file: 0x768 bytes of save data, zero up to 0xA8C, then u32 byte sum and u32 complement sum over the first 0xA8C bytes |
| 0x1FBC | 3 × 16 | HD per-file "player" data (fresh: byte 5 = 1) |
| 0x1FEC | 3 × 4 | HD per-file "status" (fresh: 0) |
| 0x1FF8 | 3 × 20 | HD per-file "event" (fresh: 0) |
| 0x2034 | 3 × 18 | player name, UTF-16BE, 8 characters + terminator (fresh: "Link") |
| 0x206A | 3 × 220 | HD per-file "map" (fresh: 0) |
| 0x22FE | 4 | format word, always 4 (checked on load) |
| 0x2302 | 4 | CRC-32 of the five HD sections (0x342 bytes); quirk: each name contributes only its first (characters + 1) **bytes**, zero padded to 18 |

The picture album (`cking_pic*.sav`) and `cking_playlog.sav` are separate files; HD creates them
when missing (checked: the game loads a folder holding only `cking.sav`).

## Field mapping GameCube → HD

The 0x768-byte save data has the same packed layout on both systems: HD's
`dSv_info_c::card_to_memory` (025BA7B0) copies the same blocks with the same sizes in the same
order as the GameCube one, and the structures inside have the same sizes. `wwsave.py` writes the
two layouts down independently (442 named fields), checks that they agree, and `gc2hd.py` copies
field by field through them. Group by group:

| Group | Mapping |
|---|---|
| status A (life, rupees, item buttons, equipment, wallet, magic) | copied |
| status B (date, time of day, Wind Waker wind direction) | copied. The GameCube save date (OSTime) is kept; HD overwrites it with its own clock on the next save |
| return place (stage, room, spawn point) | copied |
| items, get-item flags, item record/max (arrows, bombs, bottles), bags (spoils, bait, delivery) | copied, except the Tingle Tuner (below) |
| collect (Triforce, songs, pearls, …), map (charts, sea squares), priest, status C (the four recollection records) | copied |
| info (player names, deaths, puzzle, New Game+ count, salvage point) | copied; **save counter** (+0x10): HD shows a file whose counter is 0 as "New Game" (GameCube leaves the field 0 and uses the name), so a used GameCube file gets 1. Japanese: Shift-JIS bytes in the three name fields are cleared (HD's own files leave them empty) |
| config (furigana, sound, Z-targeting, vibration) | copied (HD sets the sound mode itself on load) |
| dungeon memory (16 stages: chests, switches, items, rooms, keys, map/compass/boss key/boss), ocean, event flags, reserve | copied |
| Tingle Tuner (item 0x21 in slot 7) | HD uses the same item number and slot for the **Tingle Bottle**, given by the same Tingle Island event, so the item is kept and becomes the Tingle Bottle. `--drop-tingle-tuner` clears the slot, its get-item flag and any item button on it instead |
| GameCube pictures | dropped (different format; HD has its own album) |
| HD-only per-file data (player, status, event, map) | the values of a fresh HD file (HD's init 0271FF4C) |
| HD player name (UTF-16) | the GameCube name; Japanese names are folded to ASCII when possible (full-width letters), otherwise "Link" (the USA HD font is Latin only) |

Unused GameCube files (no player name) are copied as they are; HD shows them as "New Game"
(save counter 0) and starting a game re-initialises them. GameCube USA and Japan saves have the
same layout (tww: `d_save.h` only differs for the demo build, event flags have no version
conditionals); PAL saves are rejected (the save counter holds the language there; not checked).

## Validation (2026-10-05)

- Round trip HD → GameCube layout → HD (`roundtrip_test.py`) on the three HD saves of the local
  collection: all 442 shared fields bit-identical in every file; only HD-only data resets to
  fresh values.
- 167 GameCube saves (CryZe/WindWakerSaves: 5 USA, 162 Japan, nine routes) convert without
  errors.
- Game test in the original build (headless, copies of the saves): early, middle and late save
  of every route (26 saves). Every converted save shows its Quest Logs with name and place in
  the HD file select and loads into gameplay. Hearts, rupees, magic, items and place match
  `hd_save_info.py`. No crash. A New Game+ file shows the Triforce mark. The first test found
  the save counter rule (files with counter 0 showed "New Game"); fixed in `gc2hd.py`.
