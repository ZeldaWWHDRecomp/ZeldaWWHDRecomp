# Local catalogue pilots

Generate two fully authored synthetic packages and a 32-byte GameCube ID fixture:

```sh
python3 tools/catalogue/make_pilots.py --out build/catalogue-pilots
```

Set `WWHD_MOD_CATALOGUE` to `build/catalogue-pilots/index.json` (use an absolute
path when launching from another directory). Open Mods, Refresh catalogue, and
install Synthetic content pilot or Synthetic setup pilot. Installation stays
disabled. The setup pilot has a colour choice, a confirmation, a source picker
and a packaged Python tool. Choose `synthetic-gc-usa.iso` as the source; it is an
ID-validation fixture, not a playable disc. The tool writes only authored text
to the mod's data folder. It never copies input bytes. Its execution requires
the same native-code confirmation as executable mods and the player-installed
Python command from `guest-sdk.json`.

To include the real heart-ticker guest example, first compile it with a PowerPC
capable clang and lld:

```sh
make -C examples/guest-mods heart-ticker/mod.elf CLANG=clang LLD=ld.lld
python3 tools/catalogue/make_pilots.py --out build/catalogue-pilots \
  --heart-elf examples/guest-mods/heart-ticker/mod.elf
```

The guest package contains the ELF, its authored source and the repository's MPL
licence. Catalogue metadata declares USA and EU support; the existing guest
builder maps USA hook addresses for the installed region. Guest mods remain
unsupported on Android. Actual game activation must still be tested separately;
fixture generation does not prove region mapping or renderer behavior.

`ctest --test-dir build/catalogue-check -R catalogue_pilots --output-on-failure`
exercises verified ZIP staging, manager installation, disabled state, native
trust, shared source selection, options, actual packaged Python execution,
output receipts, moved-source invalidation and removal. It uses temporary
fixtures and no game files. Generated archives and disc headers belong under
`build/` and must not be committed.

For an installed test release, `tools/guestmod/test_code_mods_e2e.py --catalogue`
generates a local catalogue with the compiled heart pilot, refreshes and installs
through the overlay's normal asynchronous catalogue actions, then checks the code
mods offer, rebuild, restart, active hook, hooks-off byte identity and removal.
Pass the release, regional game, copied-save source and compatible regional state
paths as documented by `--help`. Run separately for USA and EU; use `--boot`
to start from copied normal saves without loading a full state from another mod
set. Boot runs retain private Browse/setup screenshots. `--max-game-sessions 4`
allows concurrent functional checks when the operator authorizes them; it does
not relax benchmark gates. The headless
`WWHD_TEST_CATALOGUE_INSTALL` aid selects an entry; it retains compatibility, hash,
manifest, manager trust and restart checks. These are opt-in real-game runs, not
unit tests. Keep the default one-session limit unless the operator explicitly
authorizes concurrent functional checks; performance measurements remain exclusive.
