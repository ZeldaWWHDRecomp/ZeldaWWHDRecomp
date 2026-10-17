# World editor (preview)

**Preview.** The world editor builds and changes mod content inside the running game: place game actors,
your own models and enemies, move and turn them with handles in the picture, import models, write
dialogue and what happens after it, warp around, play your quest as Link without touching your own save,
and export the finished package.

It works with mods that use [actors and scenes](mod-sdk-v3.md) (placed game actors and your own models),
scenes and quests ([SDK v2](mod-sdk-v2.md)). The examples
[sdk-v3-windfall](../examples/guest-mods/sdk-v3-windfall/manifest.json) and
[sdk-v3-creature](../examples/guest-mods/sdk-v3-creature/manifest.json) are good packages to try it on.

## Starting it

Start the game with `--editor`, for example from a terminal (`wwhd --editor`, on Windows `wwhd.exe --editor`)
or from a shortcut with that option added. A normal start has no editor at all: nothing that players see.

The game starts as usual: the title screen, then your file. As soon as Link is in the world, the
**editor view** begins: the camera flies freely, Link stands still, and the editor's own panels are over
the picture. The settings menu still opens with Esc (or F1) and has an **Editor** tab with the same
panels for quick changes.

## What it changes, and what it never changes

- It changes **only the mod you open**, in that mod's own folder: its `manifest.json` (only when you press
  **Save…** and confirm) and, when you import a model, the model file it copies into the mod's `models`
  folder.
- It never changes the game's files, your save, or another mod. An export never includes game files.
- Every change is checked by the same rules the mod manager uses. A change that breaks a rule is refused
  with the reason, and nothing changes.
- Every change can be undone (**Undo** / **Redo**), including a removed actor and a revert to the saved
  package.
- Removing an actor and replacing or reverting files ask for a confirmation first.
- Saving writes the manifest with its fields in alphabetical order and two-space indentation. The content
  is the same; only the order of the fields can differ from a hand-written file.

## The editor view

| | |
| --- | --- |
| **Toolbar** | the open mod, Undo, Redo, Check, Save…, Revert…, **Add**, Move / Turn, grid and angle snapping, **Play**, Play from Link, Restore at Stop, Test mode, and which panels show |
| **Outliner** | the open mod's actors, models, scenes and quests, and the warp destinations |
| **Inspector** | the selected actor: place, properties, dialogue and actions |
| **Problems & export** | the check's problem list, and the export |
| **Camera & test** | reload the room, test mode, test states |
| **Status bar** | the stage and room, the camera's position and speed, Link's position, the last result |

The panels can be moved and resized; their places are remembered.

In the picture, every actor of the open mod has a label. Click a label to select the actor (or pick it in
the Outliner; a double click there flies the camera to it). The selected actor shows its collision
cylinder and handles: drag them to move it, or to turn it with **Turn** chosen in the toolbar. With grid
or angle snapping on, it moves in steps of 10, 50 or 100 units and turns in steps of 15, 45 or 90
degrees. Letting go of a handle is one change (one Undo step).

| Keyboard / mouse | Controller | |
| --- | --- | --- |
| W A S D | left stick | fly forward, back and sideways |
| E / Q | R / L | up / down |
| right mouse button held, arrow keys | right stick | look around |
| Shift / C | ZL / ZR | faster / slower while held |
| mouse wheel | | change the speed |
| click | | select an actor, or place the chosen new one |
| Ctrl+Z / Ctrl+Y (Cmd on a Mac) | | undo / redo |
| Ctrl+D | | duplicate the selected actor |
| F | | fly to the selected actor |
| Tab | View button | Play / Stop |

(These are the default keys; the camera follows your own key and button mapping.) While you type into a
field, the camera does not move.

## Placing new actors

Press **Add** and choose what to place, then click into the picture: the actor stands on the ground where
you clicked (the game's own collision decides where the ground is). Hold Shift while clicking to place
more of the same; **Stop placing** in the toolbar ends it.

- **Game actors:** sign, resident (uo2, um1 or the seated ym1), chest.
- **Props:** pot, barrel, crate.
- **Switches:** switch, door (it opens with one of the mod's switches).
- **Enemies:** ChuChu, Bokoblin, Moblin, Keese.
- **Your models:** one of the mod's models (static or skinned), your own enemy (a skinned model with
  flinch, stun, death and idle clips), a prop Link can carry.

Each new actor gets the flags it needs, named after it (for example `sign-read`, `pot-broken`,
`guard-defeated`). In the Inspector, **To Link**, **To camera** and **Snap to ground** move the selected
actor, **Duplicate** places a copy next to it with flags of its own, and **Remove…** takes it out (after
a confirmation; Undo brings it back).

### What shows at once, and what after a restart

Signs, residents, chests and actors with your models appear, change and disappear in the game at once.
These wait until the game starts again (the change is in the package; save it first):

- pots, barrels and crates, switches and doors, enemies, your own enemies and carried props (their
  progress and drops are set up when the game starts);
- the first actor of a kind the game had none of when it started (the first resident, the first actor
  with one of your models), and actors of a mod that had no actors at start-up;
- switching an actor between a game actor and one of your models.

The status bar says which actors wait for a restart. **Reload room** (Camera & test) enters the room
again; it helps when an actor could not be created, for example a model over the room's memory budget.

## Models

Outliner → **Models** lists the mod's models and the actors that use them. To add one, choose a `.glb`
file (**Choose…**, or type its path). The editor checks it at once and says what it found or which limit
it breaks:

| | static model | skinned model |
| --- | --- | --- |
| file | glTF 2.0 binary (`.glb`), up to 8 MB | the same |
| mesh | one mesh, plain triangles, up to 8192 triangles and 4096 vertices | the same, one skin |
| material | one material with an embedded PNG base colour on UV0, up to 512 × 512 pixels | the same |
| bones | none | 1 to 32, unit scale, one connected hierarchy |
| clips | none | up to 8, each up to 900 frames, distinct lower-case names |

**Import into the package** copies the file into the mod's `models` folder and adds the model. The editor
prepares it for the game while you work; when the status bar says it is ready, actors can use it without
a restart.

## Dialogue and actions

For signs, residents, chests and your own models, the Inspector has:

- **Dialogue:** up to three lines. **Preview in the game** shows them in the game's own message box, as
  players will see them; **Close preview** hides it.
- **Flag:** set when the actor was talked to or opened.
- **After the dialogue:** a list of actions: give or take one of the mod's items, set a quest step, set or
  clear a flag, start a scene. **Apply actions** checks and keeps them.

Pots, barrels, crates, enemies and your own enemies have **Drops** (up to eight of the mod's items) and
their broken or defeated flag.

## Checking and exporting

**Check** (toolbar) checks every actor, model, scene and quest as the mod manager does and lists the
problems in **Problems & export**, each with what it is about. Clicking a problem selects that actor,
flies the camera to it and marks the field in the Inspector.

**Export…** checks the package and writes it into a new or empty folder: the manifest and the mod's own
files (models, textures, assets). Files in the game's own formats are never exported; if the package
holds any, the export says which and stops.

## Play and Stop

**Play** (or Tab, or the controller's View button) hides the editor and you play as Link, from the
camera's place (**Play**) or from where Link stands (**from Link**). Test mode turns on, so saving in the
game writes the test save, never your own. A small **Stop / back to editor** button stays at the top.

**Stop** (or Tab again) returns to the editor view with the camera where it was. With **Restore at Stop**
on (the default), the world goes back to how it was when you pressed Play: Link's place, the progress
and the mod's flags.

## Warp, camera and test mode

- **Warp** (Outliner): the open mod's actors (Link lands in front of them) and its quest steps' places,
  all 49 squares of the Great Sea, and every stage and room of the game, read from your game files.
  **Back to last position** returns to where Link was before the last warp. The editor view comes back
  at the destination.
- **Test mode** (toolbar or Camera & test) protects your progress while you try your quest:
  - while it is on, saving in the game writes a separate test save, never your own;
  - **Reset this mod's quest…** erases the open mod's quest progress and flags from the loaded game, so
    you can play the quest from the start. Other mods and the game's own progress are not touched;
  - **Save test 1–3** / **Load test 1–3** keep test states: the game's progress, Link's place and the
    open mod's flags;
  - turning test mode off takes you back to your progress and place from before the test.
- **Log** shows the last commands the editor ran, with their parameters and results.

## Frame rates

The editor and its camera work at 30 fps, with 60 or 120 fps interpolation and with True 60. The camera
moves at the same speed in every mode.

## Commands

Everything the editor does is a command with named parameters and a JSON result. The same commands will
be available to tools later. For scripted tests, `WWHD_EDITOR=1` starts a run in the editor mode, like
`--editor`, and `WWHD_EDITOR_SCRIPT` runs commands at given frames: `frame {"command": "...", "params": {...}}`
separated by `;`, or `@file` with one such line per row. Results go to the log.

| Command | Parameters | What it does |
| --- | --- | --- |
| `help` | `command`? | the commands and their parameters |
| `list_mods` | | the active mods the editor can open |
| `open_mod` | `mod` | open another mod |
| `list_actors`, `list_models`, `list_scenes`, `list_quests` | `mod`? | the open mod's content |
| `get_actor`, `get_scene` | `id`, `mod`? | one entry as the manifest declares it |
| `list_actor_types` | | what `add_actor` places, and what appears without a restart |
| `add_actor` | `type`, `id`?, `at`? (`link`, `camera`, `position`), `position`?, `stage`?, `room`?, `angle`?, `snap`?, `model`?, `options`?, `mod`? | place a new actor (on the ground unless `snap` is false) |
| `remove_actor` | `id`, `confirm`, `mod`? | remove an actor (needs `"confirm": true`) |
| `duplicate_actor` | `id`, `new_id`?, `offset`?, `mod`? | a copy next to it, with flags of its own |
| `place_actor` | `id`, `at` (`link`, `camera`, `ground`, `position`), `position`?, `snap`?, `mod`? | move an actor to Link, the camera, the ground below it or a position |
| `set_actor` | `id`, `property`, `value`, `mod`? | set one actor property (`null` removes it) |
| `move_actor`, `rotate_actor`, `scale_actor` | `id`, `position` / `rotation` / `scale`, `mod`? | `{x, y, z}` |
| `set_dialogue` | `id`, `lines`, `mod`? | one to three lines |
| `set_then` | `id`, `actions`, `mod`? | what happens after the dialogue (an empty list removes it) |
| `set_drops` | `id`, `drops`, `mod`? | what a prop or enemy drops |
| `preview_dialogue` | `id`?, `lines`?, `close`?, `mod`? | show lines in the game's message box (or hide it) |
| `pick` | `origin`, `direction`, `range`? | the first ground a ray meets |
| `ground` | `position` | the ground height below a point |
| `focus` | `id`, `mod`? | fly the camera to an actor |
| `check_model` | `file` | check a `.glb` file |
| `import_model` | `file`, `id`?, `confirm`?, `mod`? | copy a `.glb` into the mod and add the model |
| `set_scene` | `id`, `property` (`trigger`, `steps`), `value`, `mod`? | |
| `set_quest_step` | `quest`, `step`, `property`, `value`, `mod`? | |
| `undo`, `redo`, `history` | | |
| `validate` | `mod`? | the checks, with the first problem |
| `check_package` | `mod`? | every problem, each with what it is about |
| `save_package` | `confirm`, `mod`? | write `manifest.json` (needs `"confirm": true`) |
| `revert_package` | `confirm`, `mod`? | forget unsaved changes (needs `"confirm": true`) |
| `export_package` | `path`, `confirm`, `mod`? | check, then write the package into a new or empty folder |
| `reload_room` | | enter the current room again |
| `list_locations` | `group`? | warp destinations (`Islands`, `Rooms`, `Quest`) |
| `warp` | `stage`, `room`, `point`?, `position`?, `angle`? | warp to a room's entry point or a position |
| `warp_to` | `location` | warp to a `list_locations` entry |
| `warp_back` | | back to before the last warp |
| `read_state` | | Link's place, the camera, test mode, unsaved changes |
| `fly_camera` | `on`, `place_link`? | the free camera on or off |
| `play` | `from`? (`camera`, `link`), `restore`? | play as Link (test mode on) |
| `stop` | | back to the editor view |
| `test_mode` | `on` | test mode |
| `reset_quest` | `confirm`, `mod`? | test mode: reset the mod's quest progress and flags |
| `save_state`, `load_state` | `slot` (1–3) | test mode: the test states |
| `command_log` | `count`? | the last commands |
