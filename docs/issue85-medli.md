# Medli progression repair for issue 85

The save attached to [issue 85](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/issues/85)
already owns every Master Sword upgrade before the first Dragon Roost visit. The old sword cheat
writes those ownership bits. The game treats them as story milestones and deliberately removes
Medli from her earlier locations. The fix equips the upgraded sword and shield without changing
ownership; an affected saved file also needs its premature sword ownership repaired.

## Evidence

The two ZIP attachments contained `slot2.wwstate` (6,733 bytes) and `cking.sav` (8,966 bytes).
The state is portable, not a full memory state. It identifies runtime `v0.2.8 (d77512f)`, Quest Log 1,
`sea` room 13. The state and disk save agree in every packed progress field except the save
timestamp and save counter. Both contain sword ownership `0x0F`, equipped sword `0x3E`, and shield
ownership/equipment `0x03`/`0x3C`, with no pearls and no Medli sage-awakening flag (`0x2E04`).

Medli's `daNpc_Md_c::create` is at `0x02286084`. At `0x022862F0` it passes the live save base plus
`0xD4` to `dSv_player_collect_c::isCollect(0, 2)` (`0x025B7A2C`). If sword ownership bit 2 is set,
creation fails outside `M_DaiB`, the Earth Temple boss room. The ordinary upper-floor placement is
`Md1`, parameter `0x00FF0001`, in `Atorizk` room 0. Its stage checks also reject Medli after sage
awakening or after passing the Father's Letter. No missing Chieftain event bit explains this
file: the sword check rejects the actor first.

A controlled current-devel probe entered `Atorizk` from copies of the reporter's save. The
unchanged file produced no Medli execute records (`0x02289E28`); restoring early sword/shield
values produced 441 complete records. Applying the corrected sword cheat to that early copy
produced 427 complete records while preserving ownership (`01 01`) and equipping the upgraded
weapons (`3E 3C`). The portable checkpoint verifies the actual stage and room. Counts exclude the
partial final buffered record left when stopping the test process.

In the native dialogue route on the minimally repaired reporter save, Quill finishes before
entering the house: event byte `0x1F` changes from `0x80` to `0xC0` (bit `0x1F40`).
The Chieftain/Quill introduction gives inventory slot 18 the Delivery Bag
(`0x30`) and changes event byte `0x25` from `0xC1` to `0xC3` (bit `0x2502`). It does not change the
sword ownership that selects Medli's story phase. These checkpoints are made by the game's save
functions while the player has control; inventory and story flags are never poked by the route.

The source comparison and live dump also exposed a current-devel regression from `901c8cd`:
`*0x101F84DC` is the save-area base (`0x145AC90C` in the observed session), while the existing cheat
offsets address player data at base plus `0x20` (the old fixed pointer was `0x145AC92C`). The fix
retains the mapped global and adds that player-data offset. This regression is separate from the
v0.2.8 save's premature sword ownership.

Makar's `daNpc_Cb1_c::create` (`0x0221F700`) also selects his phase using Master Sword ownership.
Preserving the ownership bits protects his earlier placements too. The actor code was inspected
read-only and checked against the locally generated game code; no decompiled game code is
included in this change.

## Changes and repair

`runtime/src/mods/cheats.cpp` now equips the full Master Sword and Mirror Shield without recording
ownership. The menus and notice explicitly say **until reload**. The game's own equipment refresh
restores earned equipment when a Quest Log or portable state is loaded. Other cheats retain their
existing behavior.

`tools/savegame/wwsave.py repair-medli` writes a new save plus an exact backup. It validates the
checksums, requires an early first-playthrough file before the first pearl, and refuses unrelated
or later saves. It restores only the selected Quest Log's sword ownership and equipped sword;
shield ownership, items, event flags, other progress, padding and the other Quest Logs remain
byte-identical. On the reporter's file the only changed offsets are `0x0E`, `0xB2`, `0xA8F`,
`0xA92` and `0xA93` (the two sword fields and checksum bytes). Input files are never overwritten.

```sh
python3 tools/savegame/wwsave.py repair-medli /path/to/cking.sav -o medli-repaired --file 1
python3 tools/savegame/hd_save_info.py medli-repaired/cking.sav --file 1
```

With the game closed, keep the original and generated backup, install the repaired `cking.sav`,
and load Quest Log 1 normally. Loading an old portable state would restore the old sword flags;
make a fresh state after the repair. [The save tools README](../tools/savegame/README.md) covers
other Quest Logs and converting a portable state when it holds the latest progress.
