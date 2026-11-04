# Choosing code-mod support during setup

Before it builds the game, setup offers **Build with code-mod support**. It is off for a new
installation. The terminal setup (Setup in Terminal, `setup-in-terminal.sh`) asks the same question.

> For mods in the catalogue or Mods tab marked as code mods. Built-in mods, graphics packs and content mods do not need this. The game code gets a small check in every function, which can cost some performance, and the build takes a bit longer. You can switch this later in Settings > Mods; that requires another rebuild.

- **Updates and repairs** start with the choice that is installed now, including a change made later
  in Settings → Mods. You can change it before building.
- **After setup**, Settings → Mods shows the chosen mode. With support built in, you can enable a code
  mod right away, without another rebuild. Code mods still ask for your approval and run their own
  preparation steps.
- **Switching later** in Settings → Mods rebuilds the game code and needs a restart, as before.
- **Scripts** can skip the question with `--code-mods 0` / `--code-mods 1` or `WWHD_CODE_MODS=0` / `1`
  (the command-line option wins). Without a terminal, setup keeps the current choice, or uses off
  for a new installation.

## When the mod compiler is missing

Code mods are built with the compiler that setup downloads into the data folder (`toolchain/`). If it
was removed (for example by answering yes to setup's "Remove the downloaded compiler?"), the code mods you
switched on can't start. The game then says so once at start, for example "Story Notebook needs the mod
compiler, which is missing. Run setup and choose Repair to install it again.", and Settings → Mods shows
"The mod compiler is missing" next to each of these mods. Run setup and choose **Repair** to download it
again. Content mods (stages, models) don't need the compiler.

## Android

The setup app has a **Code mods** switch under the setup buttons. It is off for a new installation.

- Turned on before the first setup, the game is built with code-mod support right away.
- Turned on (or off) after setup, the app builds the game code again on your Android device. That takes
  about as long as the first setup; you can keep playing the current build meanwhile, and the new one is
  used the next time you open the game.
- **Settings → Mods → Use code mods** in the game does the same: setup builds in the background, then
  close and reopen the app.
- Both builds stay on your device, so switching back later is quick.

The first time a code mod is switched on, the app prepares its compiler for mods (a few seconds, once
per app version). Each code mod is then built on your device when the game starts, like on a computer.
A game built on a computer and installed over USB uses the choice made when it was built there.

## Experimental randomizer core build worker

On desktop, open **Settings → Mods → More → Import randomized game**. Choose
an unmodified USA game folder and the complete official randomized output folder.
A compatible PowerPC-capable Clang must already be available; this flow does
not download one or convert a platform compiler command. The setup compiler is
used only when its explicit executable passes the backend check. Commands such
as macOS `xcrun clang` require choosing a PowerPC-capable Clang executable instead. **Build and import** works in the
background. Cancel finishes the current step and discards the import. Compiler
checks time out after a minute and compilation after five minutes; cancellation
does not forcibly interrupt filesystem operations or compiler-wrapper children. The build
is experimental and has not been verified by a full playthrough.

A completed package enters the normal mod import/review flow. It stays off until
the existing code-mod and experimental confirmations are accepted; enabling it
requires a restart to select the seed's saves. Folder-based building is currently
available on desktop; Android's file chooser cannot select these input folders.
The game-free worker is also available for scripts:

Run the release's Python with `tools/core_randomizer.py`, passing `--resources`
(the release's `core-randomizer-resources.json`), `--original`, `--randomized`,
`--output` (a new private directory), and `--compiler-json` (an argv JSON array
whose first entry is an absolute PowerPC-capable compiler executable). The
worker checks the compiler backend without running its output, validates the
seed, and builds using the release's pinned public source and guest SDK headers.
It needs neither Git nor a compiler lookup on PATH.

The returned package is for the existing mod manager's Import flow. It does not
install, enable or launch anything. Keep the experimental/code-mod confirmation
and restart steps: the package selects its save profile at startup. Never share
the generated package, which contains assets from the player's own game.
