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

Android: the choice isn't available there yet.
