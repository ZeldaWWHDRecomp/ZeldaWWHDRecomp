# Randomized games: compatibility profiles (maintainers)

Player instructions are in [randomizer-experiment.md](randomizer-experiment.md). This page is for
maintainers reviewing a new randomizer variant for the compatibility check.

Setup checks the section/import layout, every port hook instruction and the complete containing
functions of a randomized RPX against a shipped profile (`tools/recomp/randomizer-usa.json`,
`randomizer-eu.json`; hashes and addresses only). It checks the code as the system loader leaves it,
with the RPX's relocations to its own symbols applied: the randomizer redirects some pointers by
changing only relocation entries (an item-function table, float constants), and the port loads and
translates the relocated code and data too (`runtime/src/rpx_relocs.h`, `tools/rpx.py`). Unreviewed changes are rejected; a matching
randomizer title alone never bypasses these checks. Randomized builds preserve assembly
scratch-register returns and use translated randomizer code throughout; decompiled replacements
are disabled. An executable checks its exact RPX hash before accessing saves.

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

A hooked function below 64 bytes has no digest of its own. All of them, with every hook word, form
one aggregate. When a reviewed function of that kind changes (for 1.2.0: `02417FF4`, through a
relocation), the profile accepts this input's whole aggregate as a variant. Hook words never change.

After that review, maintainers can regenerate the shipped hash-only profile:

```sh
python3 tools/recomp/mk_randomizer_profile.py /private/original-usa/code/cking.rpx \
  tools/recomp/randomizer-usa.json \
  --reviewed-rpx /private/randomized/code/cking.rpx \
  --reviewed-functions REVIEWED_COMMA_SEPARATED_FUNCTIONS
```

Repeat `--reviewed-rpx` for independently reviewed variants. The profile is rebuilt
from scratch, so pass outputs that together cover every reviewed variant (for 1.2.0:
one seed with Quiet Swift Sail, any entrance shuffle and Classic mode on, or one seed for each),
or earlier variants are dropped. The tool refuses a
changed hook instruction or a changed function missing from the explicit review
list. Adding a port hook also invalidates the profile until it is regenerated
and reviewed; `test_randomizer_compat.py` checks that it matches the current hooks.

## Generate and build

After reviewing the reported overlaps, pass their exact function entries. For
default-option 1.2.0 seeds these are `0216F7FC,023EFF20,025499A0`, plus `02417FF4,0242A218`
(float constants redirected through relocations; the port's hooks there wrap the translated original);
Quiet Swift Sail adds `0247E854`, any entrance shuffle adds `025217F8`, and Classic mode adds `0243A094,023F2048`
(procTactWait and endDemoMode: the port's Classic Wind Waker sites `@0243A294` and `@023F22B4` stand next to the two
words Classic mode patches, never on them, and do the same thing, so both together change nothing). Do not copy that list for
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
turn the randomized RPX into a mod or resolve conflicts with other mods.

Run the generated executable against the matching randomized folder. Test a fresh
file through name entry and new-save initialization as well as existing saves.

## European profile

The European patches (GreenNaugahyde's ZeldaWWHDRecompAndroid, MPL-2.0,
`tools/rando/twwhd_rando/asm_eur`) were checked independently of the fork's own mapper:
- Every one of its 3514 patched words matches the USA patch at the address this port's own
  USA-to-EU map gives (`tools/recomp/builds/eu.json`).
- Branches were compared by their mapped targets.
- The randomizer's own code moves as one block (+0x8C0).
- Data addresses were mapped through the EU map.

`randomizer-eu.json` was made from the 22 option sets of the 1.2.0 sweep, generated from a
European game:
- Each changed a hooked function exactly as the USA seed with the same settings changed the
  mapped USA function. These are the five reviewed USA functions: 0216F7FC, 023EFF20->023EFF24,
  0247E854->0247E858, 025217F8->025217FC, 025499A0->025499A4.
- The same seed and settings give the same hash words on both releases.

Both profiles were regenerated for the relocated check (format 3) from one seed each with Quiet
Swift Sail and dungeon entrance shuffle, made by the in-app randomizer. With the earlier raw-byte
rule, those seeds give exactly the earlier profiles. The relocations add the same changes on both
releases: 0242A218->0242A21C and the tiny 02417FF4->02417FF8.

On macOS, European seeds with the standard and the everything preset reached the Outset pier
from a fresh file.

## Japanese profile

The Japanese patches (the same fork, `tools/rando/twwhd_rando/asm_jpn`) were checked the same way by
`tools/rando/test_ported_patches.py`, which runs on repository files only:
- Every one of its 3514 patched words and 290 relocations matches the USA patch through the USA-to-JP
  map (`tools/recomp/builds/jp.json`). Branches compare by mapped target, `lis`/low-half pairs and
  address words by mapped value.
- The randomizer's own code follows the longer Japanese `.text` (+0xAE0).
- Three words address `g_dComIfG_gameInfo` members the Japanese build moved (0x5B60, 0x5B64: −4),
  and match the map's member layout.
- Six sites are in functions the Japanese build changed, which the map does not map instruction by
  instruction: `make_game_nonlinear` at 021B0BC0 (JP 021B0BC4), and `swordless.asm`'s three hooks
  into `dSv_player_status_a_c` with their branches back (025B2030, 025B26F4, 025B26F8 → JP 025B2018,
  025B26DC, 025B26E0). Each was compared by hand: the instructions around them are the same in both
  games. They are listed in the test.

`randomizer-jp.json` was made from two seeds generated from a Japanese game by the in-app randomizer:
one with Quiet Swift Sail, one with Classic Mode and dungeon entrance shuffle. The reviewed functions
are the USA ones mapped to the Japanese build (`--reviewed-functions 0216F800,023EFF28,025499A4,
02417FFC,0242A220,0247E85C,025217FC,0243A09C,023F2050`); the tool requires the changed functions to
equal that list exactly.

On Windows, the Quiet Swift Sail seed (JPTest1) reached the Outset pier from a fresh file with the
randomizer's starting items (sword, shield, magic, Wind Waker, Swift Sail). Later play is untested, as
is the dungeon-entrance seed in play.
