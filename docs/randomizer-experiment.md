# Randomized USA games (experimental)

Setup compiles the player's randomized USA RPX together with that seed's content. It is
an experimental installer feature, not a claim that every seed is compatible.

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
instead. An explicit `--save DIR` uses exactly that directory. Save-state slots
retain their per-seed subfolder.
Copying a save or a state manually between seeds is unsupported.

If an earlier experimental build left randomized data inside a vanilla save or
backup folder, randomized builds stop before restoring or moving saves. Ordinary
vanilla startup does not run this experimental-layout check. Follow the shown
old and new paths to move that data out of the vanilla folder while the game is
closed. Never overwrite or merge an existing destination; keep both copies until
you have verified which one to use. Update any old cloud-folder choice as well.

Two default-option 1.2.0 seeds and one swordless seed pass the automatic compatibility check. Windows,
other option combinations and a full playthrough still need validation; passing
the hook check is not a guarantee of complete gameplay compatibility.

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

Repeat `--reviewed-rpx` for independently reviewed variants. The tool refuses a
changed hook instruction or a changed function missing from the explicit review
list. Adding a port hook also invalidates the profile until it is regenerated
and reviewed; `test_randomizer_compat.py` checks that it matches the current hooks.

## Generate and build

After reviewing the reported overlaps, pass their exact function entries. For
the tested 1.2.0 seed these were `0216F7FC,023EFF20,025499A0`; do not copy that list
for another output without reading its report.

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

Compatibility has been checked with TWWHD Randomizer **1.2.0**. Two default-option
seeds and one swordless seed have been built and reached playable Outset on
macOS Metal. Fresh saves contained the expected starting equipment, songs and
visited islands. These are initialization and boot checks, not full playthroughs.
Windows builds pass CI; Windows gameplay remains untested.

Passing the compatibility check verifies the audited code boundaries, not every
randomizer option or gameplay path. Other mod combinations also need testing.
Generated appended text can include data reported as unhandled instructions;
maintainers should inspect those reports before approving another variant.

The upstream [licence](https://github.com/SuperDude88/TWWHD-Randomizer/blob/main/LICENSE.txt)
is MIT. This feature distributes no randomized game files or upstream randomizer
implementation. Players generate their own output using the official randomizer.
