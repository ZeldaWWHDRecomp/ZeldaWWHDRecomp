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

### Native code confirmation

Enabling a package with native code (`kind: native`) first shows a confirmation:
"<Mod name> contains native code. It runs with the game's full permissions and
can do anything a program on your computer can. Only enable mods from sources
you trust." **Enable** confirms and enables it; **Cancel** (the default button,
also B on a controller) leaves it disabled. If enabling a package would also
enable native dependencies that are not confirmed yet, the one dialog names all
of them. The dialog works with the mouse, the keyboard (Tab/arrows, Enter or
Space) and a controller (D-pad, A, B), on the AppKit and the SDL host.

The confirmation is asked once per package and native library: it is stored in
`profiles.json` as `native_trust`, mapping the package ID to the SHA-256 of the
package's library for this platform (the file named in `binaries`). It applies
to every profile. Installing an update whose library differs asks again;
removing a package forgets its confirmation. Only that one library is
fingerprinted; anything the library itself loads from its folder is not.
Built-in mods and settings presets, content mods and Cemu graphics packs without
preparation tools need no confirmation. Guest packages confirm their ELF through
the same dialog before their translated module can load.

Manager 1.3 also recognizes declarative `setup` steps. A package of any kind which
ships a `run_tool` preparation step uses this same confirmation, and the dialog
names its package-relative tools. For those packages, the stored SHA-256 covers a
sorted inventory of every package file, including the manifest and imported
helpers. Changing any file requires confirmation again. Guest compilation keeps
its separate ELF fingerprint for address allocation and module-cache validation.
Settings/content packages without tools still need no native-code confirmation.
Preparation options must match the manifest's boolean or enum option schema.

Preparation tools run from the mod's `Data/<id>` folder with a declared argument
vector, without a shell. `{data}`, `{package}` and `{game:gc_usa}` (or another
supported game-source ID) substitute within one argument without word splitting.
Game-source paths are saved locally under shared `game_sources` settings and
validated again when used. The tool's bounded final output is available on
failure, with saved game-source paths redacted. A successful tool must produce
all declared data-relative `outputs`; its setup receipt is bound to the package
fingerprint and resolved argument list, including selected game-source paths.
Updating a helper invalidates that receipt as well as native trust. Selecting a
different source or moving the required source makes the step unsatisfied.
Starting a rerun clears its earlier receipt before launching the tool, so a
failed rerun cannot appear ready because old output files remain on disk.

Installed-package details show each declared setup step, including shared game
source selection, choices, confirmations, tool execution and guest preparation.
Tools and guest builds run in a background worker. Options and profiles cannot
change during preparation. Guest preparation uses the same persisted address
allocator and build bridge as startup, including the installed game's region
mapping. A failed build retains its allocation for a stable retry. A successful
build leaves the package inactive; enabling it takes effect after restart.
Readiness checks the ELF, region, allocation and cached module, then validates
the full build cache key in one startup batch outside the manager lock. The key
includes the selected compiler and version, flags, translator, ABI and source
inputs. Stale or unavailable entries stay unsatisfied; a successful preparation
marks the package ready immediately. The UI reads this cached readiness without
running tools each frame. Startup also validates and rebuilds stale modules.
If code-mod support is off, preparation offers the existing rebuild dialog.

Native code is never loaded without a matching confirmation, also when a
profile switch, an older `profiles.json` or an updated library would enable it.
Such a package stays unloaded and is switched off in that profile, and the tab
shows "Not loaded: it contains native code you have not confirmed. Enable it
again to review." Ticking it again shows the confirmation. Packages that depend
on it report that a dependency failed to load, as for any failed load. The
manager API enforces this as well (`enable()` refuses unconfirmed native code;
`unconfirmed_native()` and `confirm_native()` serve the dialog).

Profiles save package toggles/configuration and built-in choices. Clone current
creates another profile; select it in Active profile. Switch away before deleting
a profile. Disable all covers both built-ins and external packages. Explicit
startup environment values, including zero, override saved choices at startup.

Storage is `<host config directory>/ModManager`: `Mods/<id>/manifest.json` plus
package files, and `profiles.json`. `WWHD_MOD_MANAGER_DIR` selects isolated
storage. `WWHD_NO_HOST_INPUT` skips user preferences and package storage unless
an explicit manager directory is supplied for a test. Content mods are copied locally into manager storage; the original game files
are preserved. Game assets and saves are never committed, uploaded or redistributed.

Test aids (only with `WWHD_NO_HOST_INPUT`): `WWHD_TEST_TRUST_NATIVE_MODS=id[,id…]`
treats those native packages as confirmed without writing `native_trust`; it
also needs an explicit `WWHD_MOD_MANAGER_DIR`, so it never applies to a player's
storage. `WWHD_TEST_MOD_ENABLE=<id>` ticks that package's checkbox once when the
Mods tab is drawn (with `WWHD_TEST_OVERLAY=open:mods`), which shows the
confirmation for an unconfirmed native package.

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
host code with the same permissions as the game; the player confirms each
native library once before it loads (see Native code confirmation). Unload callbacks run when a
mod is disabled/profile-switched, before its library closes; process termination
is not a guaranteed cleanup callback.

This ABI supports frame-driven native mods. (Function hooks and replacements are the subject
of the PowerPC guest mod prototype, [mod-sdk-v2.md](mod-sdk-v2.md).) It does **not** provide arbitrary
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
| `kind` | `native`, `settings`, `content` or `cemu` |
| `abi_version`, `binaries` | Native ABI `1`; platform-to-relative-library map |
| `content_dir` | Content package: relative folder holding game-relative replacement files |
| `cemu_dir` | Cemu package: relative folder holding `rules.txt` and shader files (empty for the package root) |
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
named in `binaries`. Packages you publish must not contain game files; content and Cemu packs are
imported locally by the player (see below). Native packages run
with the same permissions as the game: install only mods you trust.

## Validation

The standalone `mod_manager` CTest checks defaults, saved settings, invalid
values, explicit environment precedence, persistence and test isolation.
`mod_packages` loads an independently compiled fixture library and exercises
install, profiles, missing dependencies, live configuration, disable/unload and
removal. It also checks the native confirmation: an unconfirmed library is not
enabled or loaded, a confirmed one loads in a second process on the same storage
without asking, a changed library asks again (also through a profile switch),
removal forgets the confirmation, settings presets never ask, and the test aid
applies without writing to `profiles.json`. The same test installs synthetic
Cemu and content packs: legacy imports, loose `.pack` files, preset validation,
replacement conflicts, the rejection of code, shader, rule and region mismatches,
and the restart-only lifecycle; such packages never ask for a native confirmation.

`mod_content_startup`, `mod_cemu_startup` and `mod_cemu_backend` start the
manager on prepared storage and check startup activation, read-only routing,
volume boundaries, case-insensitive lookup and that content stays active when a
shader pack cannot run on the current renderer. `mod_content_fs_off` and
`mod_content_fs_on` call the guest filesystem HLE handlers (open/read/stat,
directory listing, writes and savestate reopens) against synthetic files, with
content replacement off and on. `mod_cemu` covers `rules.txt` parsing, preset
expressions, dimension rules and shader-hash names. All fixtures are synthetic;
these are host tests, not proof that a given community mod looks right in game.

## Existing model, texture and UI mods (manager 1.1)

Use **Mods → Installed packages → Choose folder… / Choose package… → Install package**.
Select a single local pack with a `content/` directory, or its ZIP. Installation
starts disabled. Enable it and restart the game. Disable it and restart to restore
original reads; then it can be updated or removed. Profiles choose the next
launch's content set. Active content is deliberately immutable for the session.
Content payloads need no native-code confirmation. A content package with a
preparation tool asks for confirmation before that tool runs.

The importer accepts a simple `MyMod/content/...` tree, a single-pack SDCafiine
layout, and file-only Cemu packs with Definition metadata. Explicit SDCafiine
and Cemu title IDs must include WWHD USA `0005000010143500`. ZIP wrappers are
accepted if they contain exactly one content directory. Multiple packs require
selecting or extracting one pack first. Known loose pack files (including
`permanent_3d.pack` and all nine `permanent_2d_<Us|Eu|Jp><Language>.pack`
language packs) can also be selected directly, or imported from a folder/ZIP
with their original filenames. They map to `Common/Pack/`. Other loose files
are placed where the installed game has a file of the same name, when it has
exactly one (for example `Title_00.szs` goes to `Common/Layout/`); files the
game doesn't have (read-me texts, pictures) are not used, and the package
description lists what was placed and what was not. A loose file whose name
the game has several times, or an unknown `.pack`, requires an explicit
content tree.

The source filename supplies a stable `content.<name>` ID, so same-named imports
count as updates; supply an explicit manifest for a different ID, descriptive
metadata or version.

### Fan translations

A fan translation is usually a replaced 2D language pack
(`permanent_2d_<Region><Language>.pack`: every message, the fonts and the 2D
layouts), often with a replaced title logo (`Common/Layout/Title_00.szs`).
Install it like any content mod: choose its folder or ZIP (loose files or a
`content/` tree) and Install package, enable it and restart. The language pack
applies to the language of the same name whatever region its file name has: a
translation shipped as `permanent_2d_EuEnglish.pack` (made for the European
game) replaces English in the USA game, and a `permanent_2d_UsEnglish.pack`
replaces English when English comes from a European language source
(docs/language-packs.md). A pack with the exact name the game asks for always
comes first. Choose the language the translation replaces (usually English) in
Settings > Language. No renaming is needed, and the game folder is never
changed. Arabic and Hebrew translations are shaped and laid out right to left
by the port itself (docs/rtl-text.md); a Cemu code patch that such a
translation ships for that purpose is not needed. Install only its `content`
folder: a package with a code patch is refused as a whole (see below).

These conventions follow the upstream [SDCafiine documentation](https://github.com/wiiu-env/sdcafiine_plugin)
and [Cemu graphic-pack format](https://github.com/cemu-project/cemu_graphic_packs/wiki/How-to-create-Graphic-Packs).
Support here covers **content file replacement only**. The importer rejects
code/meta/DLC folders, PPC patches (`patches.txt` and Cemu `.asm` code patches),
shader files and non-Definition Cemu rule sections. A mod that ships content
files together with a Cemu code patch is refused as a whole, so part of it is
never dropped silently; to use only its content files, select its `content`
folder. Randomizers and mixed code/data mods are not supported by this adapter.

An explicit package uses, for example:

```json
{
  "format_version": 1,
  "id": "my-model",
  "name": "My local model replacement",
  "version": "1.0.0",
  "game_id": "wwhd-usa",
  "minimum_manager_version": "1.1.0",
  "kind": "content",
  "content_dir": "content"
}
```

Keep the original game-relative filenames and directory structure under content.
The game must already be able to load the replacement format: archive/model
sizes, joints, animations and resource names must match the mod's target. Raw
PNG/DDS texture packs that expect Cemu's renderer interception are not equivalent
to replacements of game archives and need a separate adapter. A manifest marked
compatible only means the host can load the package, not that every modified
model or UI archive has been visually tested.

`runtime/src/mods/content.cpp` validates and indexes files at startup. Read-only
FS opens, path stats and read-only savestate handle reopens consult the immutable
case-insensitive map. Writes, saves, code and meta paths use the original
resolver. Missing paths fall through unchanged. Directory enumeration retains
original names but reports replacement sizes for replaced entries: this adapter
targets replacement of existing resources, not discovery of new files or
deletion/hiding. Conflicting enabled packages are rejected, rather than silently
choosing a load order. Content packages can declare setup options, but cannot declare dependencies. Native packages can depend on content packages, but wait until
the required startup content is active.

Installed payloads should not be edited externally while the game runs.
Savestates must be used with the same active content set; savestate metadata
does not record an asset fingerprint yet. Import/staging limits are 4096
entries, 128 MiB per file and 512 MiB per package. Symlinks and path traversal
are rejected. Content mods are copied into manager storage; the original game
files are not changed, and game assets stay outside Git.

## Cemu graphics packs (manager 1.2)

Import one Cemu pack folder or ZIP with Mods → Installed packages → Install
package. The adapter reads `rules.txt` versions 4/5, checks the USA WWHD title
ID, and exposes each preset category as a dropdown. Enable it and restart.
Changing a preset, disabling or switching profiles also requires a restart. The
active snapshot stays fixed until exit; active pack files cannot be replaced or
removed by the manager. Cemu packs contain no native code and never ask for a
native confirmation.

Supported graphics rules are width/height/depth, formats/tileModes filters and
`overwriteWidth`/`overwriteHeight`, applied to physical render-target dimensions.
Guest memory, resource formats and the original game files remain unchanged.
Matching rules replace the native resolution scale rather than multiplying it.
Pure dimension packs work in Metal and Vulkan. Packs containing GLSL require a
build configured with `-DWWHD_RENDERER=BOTH` or `VULKAN`, and Vulkan selected at
runtime: choose it in Graphics, restart, then enable the pack. Metal shader
translation is not implemented. If Vulkan falls back to Metal, the pack's shader
and dimension changes are suppressed and the UI reports the backend requirement.

Custom `16hex_16hex_vs.txt` and `_ps.txt` shaders use Cemu-compatible base and
auxiliary hashes (`runtime/third_party/cemu/graphic_pack_hash.h`). Preset
variables expand before GLSL→SPIR-V compilation. The adapter restores the legacy
pixel support block only for matching shaders that expect `uf_fragCoordScale`,
then validates descriptors, buffer member offsets/strides/array lengths and
location types against the original shader. Failed compilation or layout
mismatch retains the original shader, with a log diagnostic and applied/rejected
counts in Mods. Matching a filename is not a guarantee of compatibility with
this backend.

The official WWHD Resolution pack's exact EUR/JAP/USA aspect constant patch table
is recognized and mapped to the recomp's native projection adapter. No arbitrary
instruction patch is applied. Other `patches.txt` contents, geometry shaders,
unsupported rules/conditions, format replacement, DLC/code/meta payloads and
mixed content-plus-shader packs are rejected. Select a single pack rather than
an entire Cemu pack collection. Models/UI in `content` use the separate content
replacement adapter documented above.

Preset expressions support finite arithmetic, parentheses, variables,
min/max/floor/ceil/round; missing variables and cycles are rejected. Dimension
limits are 1–16384 and aspect ratios 1–4. Overlapping rules from different
enabled packs and duplicate shader variants are rejected. Both imported presets
and changes to an enabled pack are checked before saving the profile.

Primary format reference: [Cemu graphics pack documentation](https://github.com/cemu-project/cemu_graphic_packs/wiki/How-to-create-Graphic-Packs).
Compatibility was checked against the public [WWHD Resolution pack](https://github.com/cemu-project/cemu_graphic_packs/tree/master/Resolutions/WindWakerHD_Resolution)
and [WWHD Contrasty pack](https://github.com/cemu-project/cemu_graphic_packs/tree/master/Enhancements/WindWakerHD_Contrasty);
their sources are not part of the repository, and the host tests use synthetic
fixtures only. This covers the tested adapter paths, not universal Cemu
graphics-pack compatibility or the visual accuracy of every preset.

## Catalogue transport dependencies

Catalogue downloads use the operating system's certificate validation and accept
HTTPS URLs only, including redirects. Downloads have a byte limit, five-redirect
limit and a two-minute transfer deadline; failed transfers remove partial files.
macOS uses Foundation, Windows uses WinHTTP and Android uses
HttpsURLConnection. Android requires the INTERNET permission for explicit
catalogue requests. Linux uses the system `libcurl.so.4`, loaded when a download
is requested. Install the distribution's `libcurl4` package to enable downloads;
local package installation and offline startup do not load it. Linux developers
need libcurl headers (`libcurl4-openssl-dev` on Ubuntu). The build does not link
libcurl into the executable.

The Mods panel's Browse catalogue section loads metadata only when Refresh is
selected. The URL is saved in local settings; `WWHD_MOD_CATALOGUE` overrides it
and can name a local index for fixtures. Search matches mod names, IDs and
descriptions. Details show authors, licences, dependencies and setup steps.
Incompatible entries cannot be installed. Update is offered only for a newer
three-part version, and an enabled or active package must be disabled first.
Install verifies the downloaded size and SHA-256 plus package/index metadata,
then uses the manager's atomic installer. Packages start disabled and their setup
details open in Installed packages. Nothing is enabled or downloaded automatically.
Successful refreshes save validated metadata in the manager’s Catalogue folder.
Load offline catalogue reads this cache without a network request, including
after a restart, and labels its versions as potentially out of date. Cache keys
are bound to the selected URL or fixture path. Failed or invalid refreshes keep
the previous cache; installed packages remain available without a network connection.
