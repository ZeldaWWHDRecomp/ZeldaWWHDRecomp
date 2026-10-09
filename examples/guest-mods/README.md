# Example guest mods (Mod SDK v2 prototype)

Two small mods written in C and compiled for the game's CPU (32-bit big-endian PowerPC). See
[docs/mod-sdk-v2.md](../../docs/mod-sdk-v2.md) for the design. They contain no game code or data:
game functions and variables are referenced by address only.

| Mod | What it shows |
| --- | --- |
| `heart-ticker` | entry and return hooks on Link's per-step function (`0240EBB0`), a call of a game function (`cLib_addCalc2`), an option (`every`). Link's hearts tick down a quarter heart every `every` logic steps to half, then refill. |
| `addcalc-replace` | a full replacement of a small game function (`cLib_addCalc2`, `0200ED84`) with an equivalent implementation; every other call goes to the game's own code (`WWHD_GAME_ORIGINAL`). No visible change; the log counts the calls. |

Build (needs clang with the PowerPC target and ld.lld; on macOS `brew install llvm lld`):

```sh
make CLANG=/opt/homebrew/opt/llvm/bin/clang LLD=/opt/homebrew/opt/lld/bin/ld.lld
```

Each folder is then a package (`manifest.json` + `mod.elf`). The source uses generated
public HD function names and layouts from `runtime/guest/include/game`.

Install each folder through **Mods → Installed packages → Choose folder → Install package**,
enable it, accept the ELF trust confirmation, and restart. Set heart-ticker's `every` option
in the Mods tab. The manager builds and caches the translated modules on startup.
Game code must have been translated with `--mod-hooks`; this remains opt-in until the
phase 1 performance gate passes. Android guest modules are currently unsupported.
