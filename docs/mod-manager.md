# Recomp mod manager

The mod manager installs, enables and configures mods from inside the game. No game
content is part of the repository or of mod packages; mods that need game data read it
from the player's own game files.

## Public project references (checked 2026-10-06)

- [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp): its built-in
  manager installs mod files through a button or drag-and-drop, lists mods,
  allows enable/disable, and exposes configurable options. This is the user
  experience reference. Its packages target a different recomp runtime.
- [Its mod template](https://github.com/Zelda64Recomp/MMRecompModTemplate/blob/main/mod.toml):
  stable IDs, display metadata, versions, game targeting, dependency versions,
  and typed configuration belong in a manifest rather than scattered UI code.
- [BlueWake mod documentation](https://github.com/chrissotraidis/bluewake/blob/main/docs/MODS.md):
  BlueWake does have a Mods menu. It manages compiled options and texture
  replacement. Its translated code variants are prepared during the build;
  selecting them does not provide arbitrary runtime package installation.
  Its option-site and chunk-table system is useful architectural research,
  but differs from this Wii U recomp's generated functions and hook dispatch.
- [ModernGekko](https://github.com/ExpansionPak/ModernGekko): a GameCube/Wii
  runtime with native code-mod packages, a versioned ABI, dependency resolution
  and hook/patch registration. This is the loader reference. Its existing
  packages cannot be assumed ABI-compatible with this runtime.

No source from those projects has been copied into this mod manager. References
guide the design; any future reuse requires a separate license review.

## Player workflow

Open the in-game settings overlay (F1, Fn+F1 on many Macs, Cmd+, or Settings in
the menu) and select **Mods**. The searchable built-in catalogue manages the
mods that are part of this build: direct camera, mouse camera, first-person
shortcut, wall climbing, quick doors and fast scenes. Descriptions and options
appear beside the selected entry. All defaults are off.

The Installed packages section accepts a local folder or `.wwhdmod` ZIP. Choose
it with the file/folder picker, then press Install package. Installed packages
start disabled; nothing from a package is loaded until you enable it. Enabling
resolves required dependencies; missing versions, cycles, declared conflicts and
overlapping settings presets produce an error. The details show metadata, status
and bool/number/string/enum options. String edits commit with Enter. Disable a
package and wait for its next game update before updating or removing it.
Reinstall the same ID while disabled to update; configuration is preserved by
ID. Refresh discovers manual folder changes when all packages are disabled.

Profiles save package toggles/configuration and built-in choices. Clone current
creates another profile; select it in Active profile. Switch away before deleting
a profile. Disable all covers both built-ins and external packages. Explicit
startup environment values, including zero, override saved choices at startup.

Storage is `<host config directory>/ModManager`: `Mods/<id>/manifest.json` plus
package files, and `profiles.json`. `WWHD_MOD_MANAGER_DIR` selects isolated
storage. `WWHD_NO_HOST_INPUT` skips user preferences and package storage unless
an explicit manager directory is supplied for a test. Game assets and saves
are never installed or redistributed by this manager.

## Native mod SDK v1

`runtime/include/wwhd_mod.h` defines a plain C ABI. Export
`wwhd_mod_init_v1`, validate host size/ABI, and return initialized `WWHDModV1`.
Initialization, configuration callbacks, game-update callbacks and unloading run
on the game thread. The frame callback runs once per original logic step after
actor execution; interpolated draws do not invoke it. Host/context pointers and
configuration strings remain valid until configuration changes or unload. Copy
strings if retaining them across either boundary. Callbacks must not throw or
start asynchronous guest-memory work; stop any owned workers before unloading.

Host services provide typed option access, a status line, logging and bounded
reads/writes of guest data RAM (MEM2, MEM1 and foreground bucket, maximum 1 MiB
per request). Bytes use guest big-endian order. Native packages execute trusted
host code with the same permissions as the game. Unload callbacks run when a
mod is disabled/profile-switched, before its library closes; process termination
is not a guaranteed cleanup callback.

This ABI supports frame-driven native mods. It does **not** provide arbitrary
translated-function interception, PPC instruction patch execution, texture
providers, or compatibility with Zelda64Recomp/BlueWake packages.

Package manifest fields:

| Field | Meaning |
| --- | --- |
| `format_version` | `1` |
| `id`, `name`, `version` | Stable lowercase ASCII identifier, display name, three-part version |
| `game_id` | `wwhd-usa` (the runtime also verifies its exact RPX entry) |
| `author`, `description` | Optional display metadata |
| `minimum_manager_version` | Optional three-part minimum |
| `kind` | `native` or `settings` |
| `abi_version`, `binaries` | Native ABI `1`; platform-to-relative-library map |
| `settings` | Settings preset: built-in IDs to booleans |
| `dependencies` | Objects with `id` and optional `minimum_version`; `builtin:<id>` allowed |
| `conflicts` | Package or `builtin:<id>` IDs |
| `options` | Typed defaults and names; numeric min/max/step or enum choices |

Platform keys include `macos-arm64`, `macos-x86_64`, `windows-x86_64`,
`windows-arm64`, `linux-x86_64`, `linux-arm64` and `android-arm64`.
Unsupported platform binaries remain visible as incompatible. The desktop
folder/file install workflow is the current supported UI; Android document URIs
need a separate import bridge. Online downloads/catalogues are outside v1.

## Packaging a mod

A package is a folder, or a ZIP archive of that folder's contents renamed to
`.wwhdmod`, with `manifest.json` at its root and, for a native mod, the library
named in `binaries`. Packages must not contain game files. Native packages run
with the same permissions as the game: install only mods you trust.

## Validation

The standalone `mod_manager` CTest checks defaults, saved settings, invalid
values, explicit environment precedence, persistence and test isolation.
`mod_packages` loads an independently compiled fixture library and exercises
install, profiles, missing dependencies, live configuration, disable/unload and
removal.
