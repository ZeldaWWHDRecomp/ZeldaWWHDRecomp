# World editor (preview)

**Preview.** The world editor lets you look at the mods you have turned on, change their placed actors
while you play, warp around the world and test your quest without touching your own save. This is its
first stage: changing existing content. Placing new actors, importing models, writing dialogue in a form
and exporting a finished package come later.

It works with mods that use [actors and scenes](mod-sdk-v3.md) (placed game actors and your own models),
scenes and quests ([SDK v2](mod-sdk-v2.md)). The examples
[sdk-v3-windfall](../examples/guest-mods/sdk-v3-windfall/manifest.json) and
[sdk-v3-creature](../examples/guest-mods/sdk-v3-creature/manifest.json) are good packages to try it on.

## Starting it

Start the game with `--editor`, for example from a terminal (`wwhd --editor`, on Windows `wwhd.exe --editor`)
or from a shortcut with that option added. An **Editor** tab then appears in the settings menu. A normal
start has no editor at all: no Editor tab and nothing that players see.

## What it changes, and what it never changes

- It changes **only the mod you open**, and only its `manifest.json` in that mod's own folder. Nothing is
  written until you press **Save package…** and confirm.
- It never changes the game's files, your save, or another mod.
- Every change is checked by the same rules the mod manager uses. A change that breaks a rule is refused
  with the reason, and nothing changes.
- Every change can be undone (**Undo** / **Redo**), including a revert to the saved package.
- Saving writes the manifest with its fields in alphabetical order and two-space indentation. The content
  is the same; only the order of the fields can differ from a hand-written file.

## The Editor tab

At the top: the open mod, **Undo**, **Redo**, **Check** (runs the checks and shows the first problem),
**Save package…** and **Revert…** (forget unsaved changes). Below that, the result of the last action.

### Actors

The open mod's placed actors: game actors (sign, resident, chest, pots, barrels and crates, switches,
doors and enemies) and your own models (including mod enemies and carried props). Select one to see and
change its position, rotation, scale, model, collision, animation, dialogue lines and flag, and for the
newer kinds their defeated, broken or switch flag, the switch a door listens to, and a prop's texture.
Press Enter (or leave the field) to apply a change.

Changes show in the game right away: the actor is removed and created again with its new properties.
**Move to Link** puts the actor where Link stands; **Warp here** takes Link to the actor.

Some changes need a restart, and the editor says so:

- a new actor, or a removed one;
- a model that no actor used when the game started (models are prepared at start-up);
- switching an actor between a game actor and one of your models;
- any change to an enemy, a pot, barrel or crate, a switch or door, a mod enemy or a carried prop (their
  progress and drops are set up when the game starts).

Save the package and start the game again to see those.

### Models, scenes and quests

**Models** lists your models and which actors use them. **Scenes** shows each scene; its steps can be
edited as text and applied with **Apply steps**. **Quests** lists each quest's steps; select a step to
change its text, where-text and its place on the sea chart. Scene and quest changes are checked and can be
undone at once, but take effect in the game after you save and start it again.

### Warp

- **Quest**: the open mod's actors (Link lands in front of them, facing them) and its quest steps' places.
- **Islands**: all 49 squares of the Great Sea.
- **Rooms**: every stage and room of the game, read from your game files.
- **Back to last position**: where Link was before the last warp. Pressing it again goes back and forth.

Warps use the game's own room change: Link arrives at the room's own entry point, like after a door. They
are only possible while you control Link: not during a cutscene, a dialogue, a menu or a room change.

### Camera & test

**Fly camera** detaches the camera from Link. Link stays where he is and does nothing. While it flies:

| Keyboard / mouse | Controller | |
| --- | --- | --- |
| W A S D | left stick | fly forward, back and sideways |
| E / Q | R / L | up / down |
| arrow keys, mouse | right stick | look around (the mouse when the mouse camera has captured it) |
| Shift / C | ZL / ZR | faster / slower while held |
| mouse wheel | | change the speed |
| J | B | back to Link |
| K or Space | A | end it with Link moved to the camera's position |

(These are the default keys; the fly camera follows your own key and button mapping.) The bottom-left
corner shows the camera's position. The tab also has **Stop** and **Stop and place Link here**.

**Reload room** leaves the room and enters it again where Link stands, so every actor in it is created
again. Use it when an actor could not be created, for example a model over the room's memory budget.

**Test mode** protects your progress while you try your quest:

- while it is on, saving in the game writes a separate test save, never your own;
- **Reset this mod's quest…** erases the open mod's quest progress and flags from the loaded game, so you
  can play the quest from the start. Other mods and the game's own progress are not touched;
- **Save test 1–3** / **Load test 1–3** keep test states: the game's progress, Link's place and the open
  mod's flags;
- turning test mode off takes you back to your progress and place from before the test.

### Log

The last commands the editor ran, with their parameters and results.

## Frame rates

The editor and the fly camera work at 30 fps, with 60 or 120 fps interpolation and with True 60. The fly
camera moves at the same speed in every mode.

## Commands

Everything the Editor tab does is a command with named parameters and a JSON result. The same commands
will be available to tools later. For scripted tests, `WWHD_EDITOR=1` starts a run in the editor mode, like `--editor`,
and `WWHD_EDITOR_SCRIPT` runs commands at given frames: `frame {"command": "...", "params": {...}}`
separated by `;`, or `@file` with one such line per row. Results go to the log.

| Command | Parameters | What it does |
| --- | --- | --- |
| `help` | `command`? | the commands and their parameters |
| `list_mods` | | the active mods the editor can open |
| `open_mod` | `mod` | open another mod |
| `list_actors`, `list_models`, `list_scenes`, `list_quests` | `mod`? | the open mod's content |
| `get_actor`, `get_scene` | `id`, `mod`? | one entry as the manifest declares it |
| `set_actor` | `id`, `property`, `value`, `mod`? | set one actor property (`null` removes it) |
| `move_actor`, `rotate_actor`, `scale_actor` | `id`, `position` / `rotation` / `scale`, `mod`? | `{x, y, z}` |
| `set_dialogue` | `id`, `lines`, `mod`? | one to three lines |
| `set_scene` | `id`, `property` (`trigger`, `steps`), `value`, `mod`? | |
| `set_quest_step` | `quest`, `step`, `property`, `value`, `mod`? | |
| `undo`, `redo`, `history` | | |
| `validate` | `mod`? | the checks, with the first problem |
| `save_package` | `confirm`, `mod`? | write `manifest.json` (needs `"confirm": true`) |
| `revert_package` | `confirm`, `mod`? | forget unsaved changes (needs `"confirm": true`) |
| `reload_room` | | enter the current room again |
| `list_locations` | `group`? | warp destinations (`Islands`, `Rooms`, `Quest`) |
| `warp` | `stage`, `room`, `point`?, `position`?, `angle`? | warp to a room's entry point or a position |
| `warp_to` | `location` | warp to a `list_locations` entry |
| `warp_back` | | back to before the last warp |
| `read_state` | | Link's place, the fly camera, test mode, unsaved changes |
| `fly_camera` | `on`, `place_link`? | the fly camera |
| `test_mode` | `on` | test mode |
| `reset_quest` | `confirm`, `mod`? | test mode: reset the mod's quest progress and flags |
| `save_state`, `load_state` | `slot` (1–3) | test mode: the test states |
| `command_log` | `count`? | the last commands |
