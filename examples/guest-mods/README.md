# Example guest mods (Mod SDK v2)

Small mods written in C and compiled for the game's CPU (32-bit big-endian PowerPC). See
[docs/mod-sdk-v2.md](../../docs/mod-sdk-v2.md) for the design. They contain no game code or data:
game functions and variables are referenced by address only.

| Mod | What it shows |
| --- | --- |
| `heart-ticker` | entry and return hooks on Link's per-step function (`0240EBB0`), a call of a game function (`cLib_addCalc2`), an option (`every`). Link's hearts tick down a quarter heart every `every` logic steps to half, then refill. |
| `hud-demo` | rectangle, original PNG and heart-count text on TV and GamePad, with anchors and state-load handle renewal. |
| `button-icons` | combined code/art package and face-layout setting reads; currently an overdraw candidate awaiting game-frame calibration. |
| `hud-cost` | performance fixture: exactly 200 rectangles versus no HUD callback, selected by a restart-only option. |
| `addcalc-replace` | a full replacement of a small game function (`cLib_addCalc2`, `0200ED84`) with an equivalent implementation; every other call goes to the game's own code (`WWHD_GAME_ORIGINAL`). No visible change; the log counts the calls. |

Build (needs clang with the PowerPC target and ld.lld; on macOS `brew install llvm lld`):

```sh
make CLANG=/opt/homebrew/opt/llvm/bin/clang LLD=/opt/homebrew/opt/lld/bin/ld.lld
```

Each folder is then a package (`manifest.json` + `mod.elf`, plus any `assets/`).
The HUD example art is original CC0 geometry and lettering; regenerate it with
`python3 examples/guest-mods/generate_art.py` from the repository root. The source uses generated
public HD function names and layouts from `runtime/guest/include/wwhd`.

Install each folder through **Mods → Installed packages → Choose folder → Install package**,
enable it, accept its code/package trust confirmation, and restart. Packages with artwork
include the entire folder in one trust decision; they apply only with code mods on. Set heart-ticker's `every` option
in the Mods tab. The manager builds and caches the translated modules on startup.
Game code must have been translated with `--mod-hooks`. Android guest modules are
currently unsupported.

## Local HUD frame checks

After building the examples and a real-game runtime, use
`python3 tools/guestmod/test_hud_e2e.py --help` from the repository root.
The opt-in driver takes explicit game, save and compatible full-state paths,
runs one renderer/package/frame-rate/preset case, and checks original-art colour
presence across 300 consecutive composed frames. It copies saves and the state,
uses private settings and shader caches, disables audio, and refuses to start
while another game or benchmark is running. Pillow is required for image checks.

Run both renderers, both game builds and both frame rates separately. Use
`--wide` for the 21:9 anchor check, and `--layout position` or `--layout custom`
for the button-icon absence checks. These preset cases use `controls.json` at
startup; they do not prove a live preset change during one process. Colour
presence checks detect missing frames but still require visual inspection for
placement, contextual prompts, fades and menu visibility. Use `--keep-frames`
for that inspection; otherwise images are deleted after the result is recorded.
Outputs include private game images and paths and must never be uploaded,
committed or included in packages or CI artifacts.

Full states are tied to the game's region. If no compatible state is available,
use `--boot --keep-state` to boot the copied normal save and create a private
regional state before capture. Boot cases default to frame 3000; `--first-frame`
can adjust that for a save's menu/loading sequence. Review that the captured
scene is gameplay, then use its `states/slot1.bin` as `--state` in later cases.
The driver verifies that a full-state file with a recognized header was created
and checks HUD colour presence. Review the log and scene before reusing it;
boot automation does not cover every menu sequence.
