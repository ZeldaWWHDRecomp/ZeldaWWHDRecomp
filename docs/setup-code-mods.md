# Choosing code-mod support during setup

Before building, desktop setup offers **Build with code-mod support**, off for a new installation.
The same choice appears in terminal setup, including Setup in Terminal and `setup-in-terminal.sh`.

> For mods in the catalogue or Mods tab marked as code mods. Built-in mods, graphics packs and content mods do not need this. The game code gets a small check in every function, which can cost some performance, and the build takes a bit longer. You can switch this later in Settings → Mods; that requires another rebuild.

Updates and repairs start with the current installed choice, including changes made later in
Settings → Mods. You can change it before building. Terminal scripts can pass `--code-mods 0` or
`--code-mods 1`, or set `WWHD_CODE_MODS=0` or `WWHD_CODE_MODS=1`, to skip the question; the command-line
flag takes precedence. Noninteractive setup keeps the current choice, or uses off for a new install.

After setup, Settings → Mods shows the chosen mode. With support built and enabled, a code mod can
be enabled without first rebuilding the game. Code mods still require their normal trust approval
and any package preparation steps. Switching support later in Settings → Mods rebuilds and requires
a restart as before.

The desktop GUI is shared by macOS, Windows and Linux. macOS writes the choice to `display.plist`;
the SDL host on Windows and Linux uses `settings.ini`. Portable installations keep these in
`data/user`; explicit host settings path overrides are respected.

Android currently builds the native game code before packaging the APK (`android/build_native.sh`);
it has no on-device setup flow using the desktop installer. There is no setup checkbox or terminal
question on Android yet. Supporting that choice on-device requires an installer, a compiler and
runtime relinking/packaging support on the device.

For GUI automation, use `"set": {"code_mods": true}` (or `false`) on the `source` or `menu` screen.
The setup protocol's `hello.code_mods` reports the initial selection, and an `install` request accepts
`code_mods` as a JSON boolean.
