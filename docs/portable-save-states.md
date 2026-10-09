# Portable save states

Save states come in two kinds that share the five slots:

| | Portable (default) | Full (debugging) |
|---|---|---|
| File | `slotN.wwstate` | `slotN.bin` |
| Size | a few KB (refused above 64 KB) | about 270–300 MB |
| Holds | the save data of the current Quest Log, Link's place, a header | all guest memory (game code, decompressed assets), threads, HLE state |
| Shareable | yes, attach it to bug reports | **never**: it contains game code and data |
| Loads into | any session of the same game, any build | only the same build, exactly where it was saved |
| Exact | no (see below) | yes |

Both live in the states folder (its path is shown in the Saves tab; releases keep it in `data/user`, builds from source
use `~/Library/Application Support/wwhd/states/` on macOS and the configuration folder elsewhere; `WWHD_STATE_DIR` overrides it). Loading a slot loads whichever kind it holds (the newer file if it
holds both). Crash Recovery's automatic states are always full states.

## Choosing the kind

- **Save** (Saves tab of the settings overlay, Shift+F1–F5, the macOS Save States menu) makes a
  portable state, **only while you control Link** (see below).
- *Full save states (large, contain game data, don't share) – for debugging* in the Saves tab (or
  the Save States menu on macOS) switches Save to full states; the choice is kept in
  `states/full_save_states.cfg`.
- `WWHD_FULL_SAVE_STATES=1` / `=0` decides for one start. The scripted-test variables
  `WWHD_STATE_SAVE_AT`, `WWHD_STATE_LOAD_AT`, `WWHD_TEST_SAVE` and `WWHD_TEST_LOAD` keep using full
  states (unless `WWHD_FULL_SAVE_STATES=0`).

A slot can hold both kinds: saving one kind never deletes the other. The slot loads (and shows) the
newer one; when a portable state is the newer one and an older full state (`slotN.bin`) is still
there, the Saves tab says so under the slot ("also holds an older full state (slotN.bin, 273 MB)")
and the log notes it when saving, so the large file is not forgotten (delete it in the states
folder if you no longer need it).

## When a portable state can be saved

Only while the player controls Link: the conditions under which the game itself opens its pause
menu (`dMw_c` in `d_menu_window.cpp`): no event or cutscene running (`dComIfGp_event_runCheck`,
and not for 5 frames after one, like the game), no message or dialogue box (`dComIfGp_getMesgStatus`,
the telescope's message status), no game menu open (`dMenu_flag`), no stage change or wipe in
progress (`dComIfGp_isEnableNextStage`, `fopOvlpM_IsDoingReq`), Link is the controlled actor
(not, e.g., a possessed Moblin or the Seagull), and Link is not on a rope: the play state's player
status 0 (HD play+0x5CD8) bit 0x00800000, tww `daPyStts0_UNK800000_e`, which only Link's rope
procedures set (`procRopeReady/Swing/HangWait/Up/Down_init`): on a rope or swinging from the
Grappling Hook (he would restart in mid-air). The menu's Telescope / Picto Box aiming checks
(`dCamAttnStts_TELESCOPE_LOOK_e`, `dCamAttnStts_PICTO_BOX_AIM_e`) are left out: Link restarts
standing at the same spot, which is harmless. Otherwise nothing is written; the screen shows
"can't save during a cutscene or dialogue - try again when you have control of Link" and the log
names the reason (`[savestate] slot N: portable state refused: ...`). This covers the Save button
and Shift+F1–F5. Full save states and Crash Recovery's automatic states can be made at any time.
Loading is not affected.

## Bug reports

The Saves tab's **Copy save for bug report** copies the paths of the newest portable state and of
`cking.sav`. The issue template asks for both. A developer loads a received state by copying it
into the states folder as `slotN.wwstate` and loading slot N, or with
`WWHD_PORTABLE_LOAD=<file>` (applied as soon as a Quest Log is being played). `tools/savegame/wwstate.py info <file>`
shows what a state holds (place, hearts, items, songs, ...) and `wwstate.py to-sav <file> -o <dir>`
turns it into a `cking.sav`.

## What a portable state holds

A UTF-8 text file of `key = value` lines (`runtime/src/portable_state.h`):

- header: `format` (1), `title_id` and `title_version` (from `meta/meta.xml`), `game_hash` (a hash
  of `cking.rpx`, to tell executables apart; not its contents), `runtime` (version and commit),
  `created`, `file_slot` (Quest Log 0–2), `player_name`;
- place: `stage`, `start_point`, `start_room`, `layer` (how the stage was entered), `room` (Link's
  room), `link_pos`, `link_angle_y` (shape angle), `link_proc`, `on_ship` (Link rides the boat),
  `has_ship`, `ship_pos`, `ship_angle_y` (the boat, when it is in the stage), `time_of_day` and
  `date` (day counter; day of week = date % 7);
- `savedata`: the Quest Log's block of `cking.sav` (0xA94 bytes: 0x768 bytes of save data, zeros, the
  game's byte sum and complement sum), made by the game's own save functions: `dSv_info_c::putSave`
  for the current stage and `dComIfGs_setGameStartStage` as the in-game save does before writing
  (both undone afterwards, so making a state changes nothing in the game), then
  `dSv_info_c::memory_to_card` (025BA9FC). Inventory, flags, progress, dungeon memory, time of day;
- `hd_player`, `hd_status`, `hd_event`, `hd_map`: the HD per-file sections of `cking.sav` (16, 4, 20
  and 220 bytes), the stored ones with the live data copied over by the SaveMgr's own copy functions;
- `checksum`: CRC-32 of everything before it.

Weather is not recorded: it follows from the progress, the place and the time of day.

**Guard**: the writer and the reader accept only these fields, the binary ones at exactly these
sizes, text values up to 128 characters and files up to 64 KB; a file that breaks any of this is
not written, and not read. A state can therefore never carry a memory dump.
`runtime/tools/portable_state_test.cpp` tests the format (round trip, version mismatch, both
checksums, the size guard, unknown or repeated fields).

## Loading

At the frame boundary on the game's main thread, once a Quest Log is being played (the title
screen and file select wait):

1. `dSv_info_c::card_to_memory` (025BA7B0) puts the save data into the game;
   `dSv_info_c::getSave` of the current stage makes the stage memory the loaded one (the stage
   change puts it back), the dungeon bits (`dSv_danBit_c::init(-1)`) and temporary flags start
   fresh, the HD sections are copied in, and the SaveMgr's refresh (02721880) sets the item buttons
   and equipment from the loaded data;
2. Link's room, position and angle go into `dSv_restart_c` (as when the game restarts a room after a
   fall), and the next stage is the recorded one with spawn point -1, the recorded room and layer:
   `dStage_playerInit` creates Link at that position.
   With the boat in the stage (on it, or beside it on an island), the restart the Song of Passing
   uses instead (`daPy_lk_c` tact, `dStage_turnRestart`): `dSv_turnRestart_c::set` (025B998C) gets
   Link's and the boat's position and heading, spawn point -3; `dStage_setShipPos` puts the boat
   back, and Link starts on it (start mode 2, as `getDayNightParamData` sets it when he rides) or
   standing beside it. The sail is down after the load. As with the Song of Passing, the game puts the
   boat where Link sat (`daPy_lk_c` create: `initStartPos` with Link's position), a few tens of units
   from where the boat itself was (35 in the test); the heading is the saved one.

The log then says `[savestate] portable load: arrived in <stage> room <n> at x y z (distance d from
the saved position)` (with the boat also where the boat is and whether Link is on it).

The state goes into the Quest Log being played. When it was made in another Quest Log, the screen
shows "This state is from Quest Log N; it is loaded into Quest Log M (saving in game will write it
there)" for a few seconds (and the log notes it); loading goes ahead.

Save-state notices (saved, refused, loaded, this one) show at the bottom of the picture for a few
seconds and in the macOS window title.

**Not restored** (it is not a snapshot): enemies, items lying around, moving platforms and other
actor state start as the stage starts them; a running cutscene, dialogue or minigame is not resumed;
Link starts standing, or sitting in the boat with the sail down; the camera starts behind Link;
the file is loaded into the current Quest Log of the session (saving in game afterwards writes it
there; `file_slot` only drives the notice above).

## Scenario test

`runtime/tools/portable_state_scenario.py <wwhd> <game> <save> <workdir> [--boat-save <dir>]
[--event-save <dir>]` (headless, copies of the saves only): from a copy of a save, warps into
Link's house (and, second case, stays on Outset), walks, saves a portable state; cold boot, changes
the rupees, loads the state, saves again once Link has arrived, and compares stage, room, position,
angle, the save data field by field (`tools/savegame/wwsave.py`) and the HD sections. Further cases:
the house state loaded while Quest Log 2 is played (notice, data in Quest Log 2); with a save that
has the boat (gametest `ghost`): swim to the boat, climb aboard, set sail, save at sea, cold boot,
load, Link is on the boat and the boat at its place and heading; with gametest `helm`: a portable
save while the King of Red Lions talks is refused and writes nothing, a full save at the same
moment works, a portable save after the dialogue works. Last, full save states still save and load.

Controller metadata: format 1 accepts optional `controller = 1` (GamePad) or `controller = 2` (Pro Controller). Loading restores that host input mode. Missing metadata or `0` leaves the current mode unchanged. Unknown values are refused; the existing field and total-size guards still apply.
