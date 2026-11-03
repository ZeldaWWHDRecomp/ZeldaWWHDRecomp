# Experimental randomized-RPX builds

This developer workflow tests route 2 for [#117](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/issues/117):
compile the player's randomized USA RPX together with that seed's content. It is
not an installer feature or a claim that every seed is compatible. Ordinary setup
still requires a recognized, unmodified RPX.

Use separate checkouts and private build, save, user and cache folders. Keep the
original game and saves unchanged. Generated C, randomized game files, spoiler
logs and saves remain private; do not attach them to a PR.

## Audit a seed

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

Run the generated executable against the matching randomized folder, with a
copied save and explicit private `--save` and `--user-dir`. For muted hidden
smoke tests use `WWHD_HIDDEN_WINDOWS=1 WWHD_NO_AUDIO=1 WWHD_NO_GAMEPAD=1
WWHD_NO_HOST_INPUT=1`; put shader caches and states in the experiment directory
with `WWHD_SHADER_CACHE`, `WWHD_VK_SHADER_CACHE` and `WWHD_STATE_DIR`. Verify a
fresh file through name entry and new-save initialization, not only the title
screen or an existing vanilla save. Never launch the game from the main checkout.

## Evidence and limits

The initial test used TWWHD Randomizer **1.2.0**, revision
`f8d95359e97798ed4ae43536cdaffc04bfc8e44f`, default options and seed
`CodexRoute2Experiment117`, with port base `451ffe698` on macOS Metal.
Its output changed **238 original function bodies**, appended **11,784 text
bytes**, and overlapped **six hook points in three functions**. None of those
hook instructions changed. The reviewed overlaps were Game Over's health-cap
change, a later hammer guard, and grapple animation speed constants; the port
retains the randomized original code in those cases.

With register preservation it passed fresh randomized initialization and reached
playable Outset. Its saved progress showed the randomizer's starting equipment,
songs and all 49 visited islands. This is a boot smoke test of one seed, not a
full playthrough, multi-seed guarantee or a Windows test. Generated appended
text also includes data that discovery can report as unhandled instructions;
those reports need inspection rather than blanket suppression.

The upstream root [LICENSE.txt](https://github.com/SuperDude88/TWWHD-Randomizer/blob/f8d95359e97798ed4ae43536cdaffc04bfc8e44f/LICENSE.txt)
is MIT. This change copies no upstream randomizer code or game data. A later
source port must retain the applicable notices and review dependency licences.

## SDK direction

Route 2 gives a working experiment without translating hundreds of patches by
hand. Keep its patch catalogue as a requirements map for general SDK facilities:
item-location tables and item-get interception, save/event flags, and controlled
startup changes. Existing `WWHD_HOOK` hooks remain available, but these higher
level APIs are future work. A core randomizer mod can eventually use those APIs
instead of depending on a particular patched RPX. This experiment does not yet
implement that mod or make a compatibility promise for it.
