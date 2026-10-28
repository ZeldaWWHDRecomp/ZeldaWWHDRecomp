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
the menu) and select **Mods**. It has three sub-tabs (L / R on a controller move between
them, then on to the neighbouring settings tabs). The sub-tab last chosen stays selected until
the game is closed. Both mod lists use the same rows: an on/off switch, the name (it opens the
mod's page), the *Experimental* tag where it applies and a one-word status (*On*, *Off*,
*Needs setup*, *Installing… 45%* while it is downloaded, *Setting up… 45%* after that,
*Restart to apply*, *Update available*, *Error*).

- **Built-in** ("Included with the game. Switch on or off any time."): the mods compiled
  into the game: direct camera, mouse camera, camera-directed items, first-person shortcut, extended enemy lock-on, wall climbing,
  run/swim speed, faster ladders, faster sidling, faster ivy climbing, faster ledge hanging, sailing speed, automatic wind, jump anywhere,
  quick doors, fast forward and fast scene changes; all off by default.
- **Community** ("Made by players. Downloaded from the mod catalogue."): every installed
  package (from the catalogue, from a file or folder, graphics packs and content packs),
  then **Available to download**: the catalogue's mods that aren't downloaded yet (see
  Catalogue). Code mods such as Call of the Sky and the GameCube sea minimap are catalogue
  mods and are listed here. With nothing installed and the list not loaded yet, it shows
  "Loading the mod list…" (or why it couldn't be loaded, with Retry).
- **More**: what a player rarely needs, in collapsed sections: Switch all mods
  off, Profiles, Install from a file or folder, GameCube game for mods, Code-mod
  support, Save backups, Mod list (its source and the automatic check), Cheats
  and the Mods folder. Notices from the manager (for example a graphics pack that
  was switched off) appear at the top of the tab until OK.

**Switching a mod on is the one way to get it running.** A built-in mod or a mod
that needs nothing switches on at once. Otherwise one review opens with
everything at once: what the mod does, the experimental warning (saves are backed
up first), the GameCube game (picked right there, checked immediately, shown by
file name with the full path on hover), setup choices, that it contains code
that runs on the computer, that code-mod support is added (one rebuild of about
5 minutes, the game restarts by itself) or that it starts working after a
restart, and the download size and expected time. One button (**Install** for a
mod that isn't downloaded yet, **Switch on** otherwise) and Cancel, which is the
default for keyboard and controller (B and Esc cancel). Install stays unavailable,
with the reason next to it, until a needed GameCube game is chosen.

After that, one progress view on the mod's page lists the steps with check marks
(Download, Check, Back up saves, Prepare…, Add code-mod support, Build code mod,
Switch on), an overall bar, the current step's bar, the time so far and an
estimate. It keeps running when the menu is closed: a small line over the game
shows the progress and then "… is on", "… is ready. Restart the game to switch it
on." or the failure. At the end the mod is on; if it needs a restart, **Restart
now** / **Later** (on Android: close and reopen the app). A failure shows one
plain sentence, **Show details** (the technical reason and tool output, with
Copy details) and **Try again**, which keeps the completed steps; Cancel also
keeps them.

A mod's page shows its switch and status, description, the setup state ("Set up
✓", or "Needs setup — about 1 minute. Switch it on to set it up."), the options, **Update** and **Remove**. Setup choices and setup
actions are not offered while the mod is on or still loaded in this session;
one line says why instead: "Switch the mod off first to change its setup.", or,
when it is off but its files stay loaded until exit, "Off — takes effect after a
restart." with **Restart now** right there. Options that can change are always
editable ("Changes apply after a restart." for mods loaded at startup).
**Details** (collapsed) lists version, author, licence and the kind of mod in
plain words; **Troubleshooting** (collapsed) lists each setup step as Done / Not
done yet / Working… / Failed with **Run again** while the mod is off and not
loaded, the GameCube game's file name, the manager's status and the content
hashes. **Remove** asks once and says how much space it frees: it deletes the
package and the files its setup prepared (`Data/<id>`); saves are not touched.

A mod that applies at the next start shows *Restart to apply* in its row after it is
switched on or off, and the top of Mods says "Restart to apply: … ." with **Restart
now** (on Android: close and reopen the app). There is one restart offer at a time:
the end of an install and the page of a mod that is off but still loaded offer
their own instead, and switching mods does not open a popup.

**Letterbox while targeting** is on by default in **Mods > Built-in > Camera**; switch it off to remove bars during normal targeting while keeping cutscenes and special cameras unchanged.

### Camera-directed items (prototype)

**Camera-directed items (prototype)** is off by default. Enable it in
**Mods > Built-in** to turn Link toward the free camera once when starting a
supported grounded item action: bows, boomerang, hookshot, grappling hook,
qualifying inventory-bomb throws, hammer strikes and empty-bottle swings.
Grounded Deku Leaf gusts also follow the camera's up/down direction.
Enemy lock-on and existing first-person aiming keep priority; other projectile
pitch and ongoing aim stay native. This is partial, experimental coverage, not
continuous camera-facing movement or full camera aiming for every item.
Water, bait and other unsupported or contextual actions stay unchanged.

### Extended enemy lock-on

**Extended enemy lock-on** is off by default. Switch it on in **Mods > Built-in**
to adjust **Enemy lock-on range** from **1x to 3x** (default **1x**). Only enemy
horizontal targeting distances change; the game's eligibility rules and NPC
interactions stay unchanged. **Cycle enemy targets with R3** is on by default:
press R3 while locked onto an enemy to select the next eligible enemy. With no
alternative target, the lock stays unchanged. Switching the mod off restores
original targeting behavior.

### Quick-use locked items

In **Mods > Built-in**, switch on **Quick-use locked items** (off by default).
While locked onto an enemy, one item-button press equips and uses a bow (including
fire/ice/light arrows), boomerang, hookshot or grappling rope; inventory bombs
are readied and thrown, and equipping the hammer completes one native strike.
Already-equipped hammer attacks remain native. Without an enemy lock, aiming
and item use are unchanged; native item restrictions and resource costs still apply.
Choose **Quick-use animation speed**: **1x (vanilla)**, **1.25x** or **1.5x**.
It speeds up supported equip animations and boomerang wind-up, not projectile
flight or grapple countdowns. Choosing a speed alone does not enable quick use.

Run/swim speed offers three animation choices. **Native** keeps the game's cycle with the boosted
cadence. **Dash instead of walk** replaces the low-speed walk portion; the normal full-speed run
already uses dash. **Sprint pose** adds a forward torso lean, compensating head tilt and stronger
arm drive to the current run cycle, blended with the boost. It applies during ordinary forward
running, leaving lock-on, item actions and special walks to their own poses. In water the same
choice redraws the boosted surface swim as a crawl: a flatter torso with a much wider
phase-locked arm sweep instead of the stock paddle. The choice
is saved in the active mod profile. Older profiles without an animation key retain the saved
settings choice at startup. `WWHD_MOD_MOVE_ANIM=native|dash|sprint` overrides saved choices
at startup. For in-game verification, `WWHD_MODS_TRACE=<path>` records applied sprint and swim
poses with their blend weight and cycle phase, plus `swim-swirl` and `swim-trail` splash lines.

The sprint pose leans up to 30 degrees through the torso, with a compensating head tilt and wider
arm swings. Starting a grounded run boost emits one short fan of three larger, more opaque puffs
of the game's land dust behind Link's feet. While the run boost remains active on the ground, a
small dim puff follows every few steps as a dust trail. A forward roll keeps the current boost cycle and
continues using stamina, but plays at its native speed and
pose. Running resumes automatically after the roll while the cycle still has stamina; cancelling,
exhausting the bar or genuinely stopping ends it. Resuming the same cycle after a roll does not
emit a second starting burst. While the swim boost is active, each arm pull splashes the game's
own sea-coloured water element at that side, and a light foam trail follows behind every few
steps. Both use the game's own water particles, so the native swim wake is untouched.

The boost bar is the ring that shows how much boost is left. It appears while you boost and
while it refills, then fades out. Set **Boost time** to "no limit" and there is nothing to run
out, so no bar appears at all. To keep a limit but hide the bar, turn off **Show boost bar**
(on by default): the boost, its time limit and its cooldown work exactly as before, you just
don't see the ring. The option is saved with your other run/swim settings and in your mod
profiles; `WWHD_MOD_MOVE_BAR=0` or `1` overrides it at startup. The boost bar and the
stamina wheel of "Climb any wall" both fade out while the pause menu, an item or collection
screen, the map, a cutscene or a conversation is on screen, and come back when you return
to play.

The boost button can be L3, R3, L, R, ZL or ZR, or **Roll, then hold A**. With that choice,
press A while running: Link rolls exactly as usual. Keep A held as he comes out of the roll and
the boost starts; it lasts as long as you hold A and ends when you let go or stop running.
Boost time, cooldown and the boost bar work as with the other buttons. Only a roll you start
while running counts: rolling from a standstill, letting go of A during the roll, or holding A
from before (after talking to someone or picking something up) doesn't start a boost, so A
keeps all its usual jobs. This choice is always held, even with **Press to toggle** selected,
and it works on land only: you can't roll in water, so swimming isn't boosted with it (pick
one of the other buttons if you want a swimming boost). The choice is saved like the other
buttons and in your mod profiles; `WWHD_MOD_MOVE_BUTTON=L3`, `R3`, `L`, `R`, `ZL`, `ZR` or
`roll-a` overrides it at startup.

Faster ladders and faster sidling each have one speed slider on their mod page (1x to 3x,
1.5x when first switched on). Both speed up the game's own animation, so hands and feet stay in step with the
movement: a ladder clip moves Link by its own motion, and sidling scales the animation and the
sidling speed together. Climbing on and off a ladder speeds up the same way, and the next rung
starts right after the footstep instead of after a short pause, so the climb speeds up by about
the chosen factor; the footstep sounds stay on their frames. Faster ivy climbing (up, down and
sideways on ivy and vines, also on walls climbed with Climb any wall) and faster ledge hanging
(moving along a ledge while hanging from it) work the same way, each with its own slider. Sailing speed multiplies the speed
the wind gives the boat, with separate sliders on its mod page for the normal sail and the Swift Sail; the
boat's jump keeps its normal height. Automatic wind makes the normal sail behave like the Swift
Sail: while you sail, the wind turns to blow the way the boat is heading (in eight directions,
as with the Swift Sail). It changes only the wind; the normal sail keeps its own speed, look and
sounds. On the macOS menu bar the same switches and a few speed presets are under Gameplay.
`WWHD_MOD_LADDER_SPEED`, `WWHD_MOD_SIDLE_SPEED`, `WWHD_MOD_IVY_SPEED`, `WWHD_MOD_HANG_SPEED`,
`WWHD_MOD_SAIL_SPEED` and `WWHD_MOD_AUTO_WIND` (`=1`) switch them on at startup, with the factors
`WWHD_MOD_LADDER_FACTOR`, `WWHD_MOD_SIDLE_FACTOR`, `WWHD_MOD_IVY_FACTOR`, `WWHD_MOD_HANG_FACTOR`,
`WWHD_MOD_SAIL_FACTOR` and `WWHD_MOD_SWIFT_SAIL_FACTOR`. With a mod off,
the game runs its original code. These mods work the same at 30 fps, with 60/120/240 fps
interpolation and with true 60: they change animation rates and speed targets inside the game's
own steps, never amounts added per frame.

Jump anywhere makes Link jump from standing or running with an input of its own (L1 and Q by
default; any controller button, key or, on a touch screen, a Jump button: see
[controller-input.md](controller-input.md#jump-anywhere-and-the-moon-jump-on-inputs-of-their-own)),
using the game's own jump off a ledge, a little higher. It never presses the game's L. Options on
its mod page: **Double jump** allows one more jump in the air, and **No automatic jumps off
ledges** makes Link drop off an edge instead of jumping by himself; press the button just after
leaving the edge to jump. It does not work in cutscenes or dialogue or while swimming, climbing or
on a ladder; with the moon jump cheat on the same input, the moon jump wins. `WWHD_MOD_JUMP=1`
switches it on at startup (`WWHD_MOD_JUMP_DOUBLE`, `WWHD_MOD_JUMP_NO_AUTO`; `WWHD_MOD_JUMP_BUTTON`
only for a controls file from before the Jump input).

**More > Install from a file or folder** accepts a local folder or `.wwhdmod` ZIP
from the file/folder picker; the review for switching it on opens right away
(Cancel leaves it installed and off). Nothing from a package is loaded until it
is switched on. Enabling
resolves required dependencies; missing versions, cycles, declared conflicts and
overlapping settings presets produce an error. The details show metadata, status
and bool/number/string/enum options. String edits commit with Enter. Disable a
package and wait for its next game update before updating or removing it.
Reinstall the same ID while disabled to update; configuration is preserved by
ID. Refresh discovers manual folder changes when all packages are disabled.

### Native code confirmation

Switching on a package with native code (`kind: native`, guest code or a setup
tool) asks in the review: "Contains code that runs on your computer with the
game's permissions. Install only mods from people you trust." **Install** /
**Switch on** confirms; **Cancel** (the default button, also B) leaves it off. The
review covers native dependencies that would be switched on as well. For a
catalogue install the confirmation is for that catalogue version: the download is
checked against the catalogue's size and SHA-256 and manifest before the trust
record (below) is written for the installed files. It works with the mouse, the
keyboard (Tab/arrows, Enter or Space) and a controller (D-pad, A, B).

The confirmation is asked once per package version and fingerprint. It is stored
in `profiles.json` as `native_trust`, mapping the package ID to the SHA-256 of a
sorted inventory of every package file, including the manifest and imported
helpers. It applies to every profile. Changing any file asks again; removing a
package forgets its confirmation. Built-in mods, settings presets, content mods
and Cemu graphics packs without preparation tools need no confirmation.

Manager 1.3 also recognizes declarative `setup` steps. Packages with a `run_tool`
preparation step and guest packages use the same trust record. Guest compilation
keeps its separate ELF fingerprint for address allocation and module-cache validation.
Settings/content packages without tools still need no native-code confirmation.
Preparation options must match the manifest's boolean or enum option schema.

Preparation tools run from the mod's `Data/<id>` folder with a declared argument
vector, without a shell. `{data}`, `{package}` and `{game:gc_usa}` (or another
supported game-source ID) substitute within one argument without word splitting.
`gc_wind_waker` accepts the supported USA, European or Japanese GameCube disc
identifiers, for tools that use the same input format across regions. Region-specific
IDs still require their exact region. Disc headers are checked for both plain ISO
files, RVZ containers and extracted folders containing `sys/boot.bin`. RVZ selection
checks its uncompressed embedded disc identifier and GameCube magic; the setup tool
must separately validate container checksums, compression and resource formats.
Other compressed disc formats are not accepted.
Game-source paths are saved locally under shared `game_sources` settings and
validated again when used. The tool's bounded final output is available on
failure, with saved game-source paths redacted. A successful tool must produce
all declared data-relative `outputs`; its setup receipt is bound to the package
fingerprint and resolved argument list, including selected game-source paths.
Updating a helper invalidates that receipt as well as native trust. Selecting a
different source or moving the required source makes the step unsatisfied.
Starting a rerun clears its earlier receipt before launching the tool, so a
failed rerun cannot appear ready because old output files remain on disk.

Switching a mod on (in the list or on its page) opens the review described under
Player workflow. It asks for the GameCube game and choices of the remaining
required steps; accepting records trust for the exact package/version (a changed
package asks again), then the steps run in order and the mod is switched on. If
code-mod support is missing, the review says so and the setup adds it: the game
code is rebuilt and the game restarts. Setup resumes after that restart in the
same profile for the same package fingerprint, before switching the mod on, and
the progress view continues where it was. A guest mod needs one final **Restart
now** to become active. Individual steps can be run again under the collapsed
**Troubleshooting** section of the mod's page while it is off and not loaded.
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
If code-mod support is off, the setup adds it as one of its steps.

Native code is never loaded without a matching confirmation, also when a
profile switch, an older `profiles.json` or an updated library would enable it.
Such a package stays unloaded and is switched off in that profile, and the tab
shows "Not loaded: it contains native code you have not confirmed. Enable it
again to review." Switching it on again shows the review. Packages that depend
on it report that a dependency failed to load, as for any failed load. The
manager API enforces this as well (`enable()` refuses unconfirmed native code;
`unconfirmed_native()` and `confirm_native()` serve the dialog).

### Experimental mods and save backups

Some mods are marked experimental: they are untested and can cause game-breaking
problems, and saving the game while one is active can damage the save. The
list hides those not downloaded yet unless **Show experimental mods** (under
Available to download, off by default) is on. An installed experimental mod is
always listed, with an orange **Experimental** label, so it can be turned off or
removed.

Switching on an experimental mod (or a mod that requires one) explains the risk in
the review, and its first step backs up the whole save folder before anything is
turned on; **Cancel** (the default, also B) leaves it off. If the backup fails,
for example because the disk is full, the mod stays off and the progress view
says why. Each switch-on asks again; the confirmation is not remembered. A profile switch to a profile that already has the mod enabled does
not ask.

**Mods > More > Save backups** lists every backup with its date, reason and size.
**Back up now** makes a manual backup. Backups live in `save-backups` next to the
save folder (on Android in the app's files folder, next to `save`; with a save folder chosen in
the Saves tab, for example a cloud folder, they stay in the usual local `save-backups`), one folder
per backup named after its date and reason (for example
`2026-10-11_14-03-12-mod-dragon` before a mod, with its id cut to 7 characters,
`…-restore` or `…-manual`), with the copied files under `save/` and
the details in `backup.json`. A backup is written to a temporary folder and
renamed when complete, and an existing backup is never overwritten. The newest
10 automatic backups (before a mod, before a restore) are kept; manual backups
are never deleted automatically, and folders you put there yourself are left
alone. **Delete** removes one backup, manual or automatic, after a confirmation
("This can't be undone"); the backup staged for a restore cannot be deleted until
that restore is cancelled.

**Restore** asks for confirmation and then stages the restore: the game keeps
its save files open while it runs, so the files are replaced at the next start,
before the game loads any save. The Restart required notice offers a restart
(on Android, restart the app yourself); **Cancel restore** undoes the choice.
At that start the current save is backed up first ("Before a restore"), so a
restore can itself be undone. If the restore fails, the save folder stays as it
was and the Mods tab says why. Backups and restores are logged with the
`[save-backup]` prefix.

Package manifests and catalogue entries mark a mod with `"experimental": true`
(optional, default `false`; older catalogues without the field keep working). A
package installed from an experimental catalogue entry counts as experimental
even if its own manifest predates the field.

Profiles (More > Profiles) save package switches/configuration and built-in
choices. **Copy current** creates another profile from the name typed next to it;
choose the active one in the Profile list. Other profiles can be deleted; the
active one can't. **Switch all mods off** (More) covers both built-ins and
external packages. Explicit
startup environment values, including zero, override saved choices at startup.

Storage is `<host config directory>/ModManager`: `Mods/<id>/manifest.json` plus
package files, and `profiles.json`. `WWHD_MOD_MANAGER_DIR` selects isolated
storage. `WWHD_NO_HOST_INPUT` skips user preferences and package storage unless
an explicit manager directory is supplied for a test. Content mods are copied locally into manager storage; the original game files
are preserved. Game assets and saves are never committed, uploaded or redistributed.

Test aids (only with `WWHD_NO_HOST_INPUT`): `WWHD_TEST_TRUST_NATIVE_MODS=id[,id…]`
treats those native packages as confirmed without writing `native_trust`; it
also needs an explicit `WWHD_MOD_MANAGER_DIR`, so it never applies to a player's
storage. With an explicit `WWHD_MOD_MANAGER_DIR` and `WWHD_TEST_OVERLAY=open:mods`:
`WWHD_TEST_MOD_ENABLE=<id>` flips that mod's switch once (with
`WWHD_TEST_GAME_SOURCES=<json>` it first sets the GameCube game the review asks
for); `WWHD_TEST_CATALOGUE_INSTALL=<id>` switches on that catalogue mod once the
list is loaded; `WWHD_TEST_MOD_SETUP=<id>` accepts that mod's review;
`WWHD_TEST_MOD_DISABLE`, `WWHD_TEST_MOD_REMOVE` (accepts the confirmation) and
`WWHD_TEST_MOD_INSTALL=<path>` (More > Install from a file) do the same for those
actions; `WWHD_TEST_MODS_VIEW=more|page:<id>` opens that area or page. The log
says `[catalogue] installed <id>`, `[mods] <id> is on` and `[setup test] ready <id>`.

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

Manager versions: 1.1 content mods, 1.2 Cemu graphics packs, 1.3 setup steps, 1.4 the
`experimental` flag, 1.5 the region-neutral `game_id` `"wwhd"` (imports written by 1.5 use it and
require 1.5.0, so an older build shows "Requires mod manager 1.5.0" instead of "targets another game").
Manifests with `"wwhd-usa"` keep loading.

Package manifest fields:

| Field | Meaning |
| --- | --- |
| `format_version` | `1` |
| `id`, `name`, `version` | Stable lowercase ASCII identifier, display name, three-part version |
| `game_id` | `wwhd`: this game, any region (manager 1.5; give `"minimum_manager_version": "1.5.0"` with it). `wwhd-usa` from older manifests is still accepted. Region checks stay where they apply: the title ID of Cemu and SDCafiine packs and the game build of code mods; the runtime also verifies its exact RPX entry |
| `author`, `description` | Optional display metadata |
| `minimum_manager_version` | Optional three-part minimum |
| `experimental` | Optional `true` for untested mods: hidden by default, enabling backs up the saves first (manager 1.4) |
| `kind` | `native`, `settings`, `content` or `cemu` |
| `abi_version`, `binaries` | Native ABI `1`; platform-to-relative-library map |
| `content_dir` | Content package: relative folder holding game-relative replacement files. A content package that only declares story content (letters, charts, items, quests, models, actors, scenes) can leave it out |
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

`mods_model` checks the Mods tab's state model without UI: every row status and
its enabled/loaded combinations, which actions a mod's page offers and which
explanation it shows instead, the install flow (review, progress, done or failed,
Try again keeping completed steps, Cancel, restart needed), the progress and time
estimates, and the catalogue list: cache shown at once, background check, offline
fallback and the once-per-session and hourly limits. `mod_oneclick_flow` drives the
real tab offscreen with mouse clicks on synthetic packages: switch on, the one
review (GameCube game picker, choices), progress, failure and Try again, Cancel,
the code-mod rebuild with its restart and resume, a catalogue download, and experimental mods (installed
from the catalogue, switched on, updated while on: exactly one save backup before it is switched on;
a backup that can't be made stops with its reason). Its `tabs` scenario checks that every built-in mod is listed under Built-in only, and the installed and
the downloadable catalogue mod under Community only, and that the chosen sub-tab is kept. Its `ids`
scenario opens every view of the tab (both lists, More with every section
open, every mod page, the reviews and the progress states), hovers it all over and fails when
Dear ImGui reports two items with one ID. Release builds turn Dear ImGui's red "conflicting ID" box
off (`io.ConfigDebugHighlightIdConflicts`); debug builds and the tests keep it.

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

Use **Mods → More → Install from a file or folder**.
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
  "game_id": "wwhd",
  "minimum_manager_version": "1.5.0",
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
choosing a load order. Content packages can declare setup options, but cannot declare dependencies, with one
exception: a content package with story content (letters, charts, items, quests, models, actors or scenes)
depends on the Story Notebook, the story system's own package; it gets that dependency even when it doesn't
declare it, and it can't depend on anything else. Native packages can depend on content packages, but wait until
the required startup content is active.

Installed payloads should not be edited externally while the game runs.
Savestates must be used with the same active content set; savestate metadata
does not record an asset fingerprint yet. Import/staging limits are 4096
entries, 128 MiB per file and 512 MiB per package. Symlinks and path traversal
are rejected. Content mods are copied into manager storage; the original game
files are not changed, and game assets stay outside Git.

## Cemu graphics packs (manager 1.2)

Import one Cemu pack folder or ZIP with Mods → More → Install from a file or
folder. The adapter reads `rules.txt` versions 4/5, checks the USA WWHD title
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
limits are 1–16384 and aspect ratios 1–4. Packs that change the same shader
variant, overlap graphics rules or both set the aspect ratio cannot be enabled
together. The mod's page shows “Can't be used with …” under Details. Switching on a conflicting
pack opens a **Switch / Cancel** dialog; Switch saves the new choice and disables
its conflicting packs in one change. Cancel leaves the profile alone. The dialog
supports mouse, keyboard and controller, with Cancel focused initially.

A pack switched on or off shows *Restart to apply* in the list. A
"Restart to apply: …" line with **Restart now** appears at the top of Mods. Switching packs keeps
the current pack active until restart, then applies the chosen pack; details
continue to show applied/rejected shader counts as shaders are encountered.

Old profiles with conflicting packs are repaired and saved before activation:
the currently active pack wins, otherwise the earliest recorded enable wins.
Legacy profiles without enable history use package ID as a deterministic tie
breaker. A one-time, dismissible notice at the top of Mods names each disabled pack. Profile
switches and preset changes use the same repair rule. Independent packs still
work together. There is no pack order or partial mixing: **a combined pack is
needed to get both effects** from conflicting packs.

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

The catalogue fills **Available to download** in Mods. Opening the Mods tab
shows the last successfully loaded list from the local cache at once and checks
for new mods in the background, without blocking the menu: once per session, again
when the last check is more than an hour old while the game runs, and when the
small Refresh (↻) next to "Show experimental mods" is pressed. While it checks,
"Checking for new mods…" is shown; when it fails, the cached list stays with one
quiet line, "Couldn't check for new mods — showing the last list." and **Retry**.
On first use without a cache and without a network: "Couldn't load the mod list.
Check your internet connection." and Retry.

A check fetches only the public `index.json` (no IDs, no personal or game data);
packages are downloaded only by Install or Update. **More > Mod list** says so and
has **Check for new mods automatically** (on by default; off, the list comes from
the cache until Refresh), the list's address with **Use this list** and **Reset
to default**, and the last check's technical error. The address is saved in local
settings; `WWHD_MOD_CATALOGUE` overrides it and can name a local index for
fixtures. A search field appears when the list has more than eight entries.

A mod that isn't downloaded yet shows *Not downloaded* with its download size
(and "code mod" where that applies); its page has the description and, under
Details, version, authors, licences and the kind of mod. Incompatible entries say
"Not available for this version of the game." next to a switch that can't be
used. Update is offered on the mod's page for a newer three-part version and uses the
same review and progress view. An update ends in the mod's state before it: a mod
that is off is downloaded, checked and prepared (its tools run; its code mod is
built when code-mod support is there, otherwise when it is switched on) and stays
off ("… is updated. It stays off."); a mod that is on is offered the update only
where it can be switched off and on while the game runs (native and settings
mods): it is switched off for the update and on again at the end. A mod loaded at
the next start (code mods, content and graphics packs) is switched off first, and
updated after the restart that unloads it.
Install verifies the downloaded size and SHA-256 plus package/index metadata,
then uses the manager's atomic installer, then continues with the setup and
switches the mod on.
Successful checks save validated metadata in the manager’s Catalogue folder.
Cache keys are bound to the selected URL or fixture path. Failed or invalid
checks keep the previous cache; installed packages remain available without a
network connection.
