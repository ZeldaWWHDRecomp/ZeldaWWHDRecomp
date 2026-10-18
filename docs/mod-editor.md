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
panels for quick changes. While you place a new actor, drag a handle or connect two quest steps, the first
Esc cancels that instead (as the first Esc frees a captured mouse); Esc with nothing to cancel opens
Settings.

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
| **Toolbar** | the open mod, Undo, Redo, Check, Save…, Revert…, **Add**, Move / Turn, grid and angle snapping, **Play**, Play from Link, Restore at Stop, Test mode, which panels show (also **Quests**, **Scenes**, **Flags** and **Search**), and **Reset layout** |
| **Outliner** | the open mod's actors, models, scenes and quests, and the warp destinations |
| **Inspector** | the selected actor: place, properties, dialogue and actions |
| **Problems & export** | the check's problem list, and the export |
| **Camera & test** | reload the room, test mode, test states |
| **Status bar** | the stage and room, the camera's position and speed, Link's position, the last result, and the quest step the game is at ("Lighthouse quest: step 3 of 6") |
| **Quests** | the open mod's quests as a graph to edit, or as a list, with the step and transition inspector |
| **Scenes** | the open mod's scenes: the steps, the timeline, the step inspector, what starts the scene, its native version, and previews in the game |
| **Flags** | the open mod's flags and their values in the game |
| **Search** | everything of the open mod by name or text |

The panels are docked: the toolbar along the top, the Outliner at the left, the Inspector (with Flags and
Search as tabs) at the right, and Quests, Scenes, Problems & export, Camera & test and Log as tabs along the bottom;
the picture stays free in the middle. Drag a panel by its tab (or title) to another edge, onto another
panel to make them tabs of one place, or into the picture to let it float on its own; drag the borders
between panels to resize them. The layout is remembered. **Reset layout** in the toolbar puts every panel
back in its default place.

To select more than one actor, hold Ctrl (Cmd on a Mac) or Shift while clicking labels in the picture or
names in the Outliner. Dragging the handle then moves them all (one Undo step). **Ctrl+C** copies the
selected actors and **Ctrl+V** pastes them where the camera looks, with ids and flags of their own; that
also works in another room or on another island.

In the picture, every actor of the open mod has a label. Click a label to select the actor (or pick it in
the Outliner; a double click there flies the camera to it). The selected actor shows its collision
cylinder and handles: drag them to move it, or to turn it with **Turn** chosen in the toolbar. With grid
or angle snapping on, it moves in steps of 10, 50 or 100 units and turns in steps of 15, 45 or 90
degrees. While you drag, the actor moves with the handle in the game, smoothly and in place (all
selected actors when you move a group). Letting go is one change (one Undo step, one entry in the Log),
and Esc during the drag puts everything back. When you let go, the editor checks the actor in the game:
it keeps the one that moved when it stands where the package now says; switches, doors and chests (they
have solid ground the game builds when they are created) and an actor that wandered off are created
again at their new place instead. Actors that change only after a restart (see below) stay where they
are while you drag; the status bar says so, and letting go keeps the new place in the package as
before.

| Keyboard / mouse | Controller | |
| --- | --- | --- |
| W A S D | left stick | fly forward, back and sideways |
| E / Q | R / L | up / down |
| right mouse button held, arrow keys | right stick | look around |
| Shift / C | ZL / ZR | faster / slower while held |
| mouse wheel | | change the speed |
| click | | select an actor, or place the chosen new one |
| Esc | | cancel placing, a handle drag or a quest connection; with nothing to cancel: Settings |
| Ctrl+Z / Ctrl+Y (Cmd on a Mac) | | undo / redo |
| Ctrl+D | | duplicate the selected actor |
| Ctrl / Shift + click | | select more than one actor |
| Ctrl+C / Ctrl+V | | copy the selected actors / paste them where the camera looks (in the Quests window: the selected step; in the Scenes window: the selected steps) |
| F | | fly to the selected actor |
| Tab | View button | Play / Stop |

(These are the default keys; the camera follows your own key and button mapping.) While you type into a
field, the camera does not move.

## Placing new actors

Press **Add** and choose what to place, then click into the picture: the actor stands on the ground where
you clicked (the game's own collision decides where the ground is). Hold Shift while clicking to place
more of the same; **Stop placing** in the toolbar or Esc ends it.

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

Signs, residents, chests, pots, barrels and crates, switches and doors, ChuChus, Bokoblins, Moblins and
Keese, and actors with your models appear, change and disappear in the game at once, in the room you are
in. Their drops and broken or defeated flags count from then on, as they do after a start. A change to
one of them (another place, other drops, another flag) creates it again in the game.

These wait until the game starts again (the change is in the package; save it first):

- your own enemies and carried props, and a pot, barrel or crate with a texture of its own;
- the first actor with one of your models when the game had none at start-up, and the actors of a mod
  that had no actors at start-up;
- switching an actor between a game actor and one of your models, or between a pot, switch, door or
  enemy and another kind.

The status bar says which actors wait for a restart. A new kind of enemy loads its models from the game
when it first appears, so it may take a moment to show. **Reload room** (Camera & test) enters the room
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

## Quests

**Quests** (toolbar) opens the quest editor. A quest is a graph: **Start** on the left, one box per step,
**End** on the right, and arrows for what moves the quest on.

- **A step's box** shows its number and place name, its goal (what the notebook says to do next), its
  place on the sea chart, and small marks for its conditions and rewards (T a resident's talk, R a reward,
  E game progress it needs, t a time of day, C a chart to find, S a line of the King of Red Lions). Hover
  over a mark to read it.
- **An arrow** says what moves the quest on: the quest's letter being read, talking to an actor (reading a
  sign, opening a chest), using one of the mod's items, a scene ending, or a resident's talk, which
  finishes the quest (End). Its label shows **[if]** when it has conditions and **[+]** when it hands
  something out. When a step leads to two different steps, a yellow dot marks the branch.
- **Add a step** with the toolbar's **Add step**, or right-click into the graph and choose **Add step here**.
  Right-click a step for **Start at this step**, **Edit text**, **Duplicate**, **Add a step after it**,
  **Copy** and **Remove…**. A double click on a step edits its goal in place (Enter keeps it).
- **Connect two steps:** drag from a step's right-hand dot to another step (or from Start: "any time"),
  then choose the actor that moves the quest on (Esc drops the connection). The actor is then there only while the quest is at the
  first step, and talking to it (reading it, opening it) sets the second. Steps only go forward, so an
  arrow always points to a later step. A quest ends with a step's resident talk (**Talk** in the
  Inspector), or with an actor's or a scene's action that finishes it (`quest_done`, set in the actor's
  actions or talks). An actor with several talks shows one arrow per talk that moves the quest on, each
  with its own conditions (also flags and other quests); change those under **Conditions and talks** in
  the actor's Inspector.
- **Click an arrow** to see and change it: the item Link must carry and the time of day it needs, or
  remove it (also with Delete). Its actor's dialogue and other actions are in the actor's Inspector.
- **Drag a step's box** to arrange the graph; **Arrange all** (right-click) lays it out again. The places
  are kept in the mod's own `editor/quest-layout.json` when you save: the game never reads that file and
  the mod manager ignores it.
- **The Inspector** beside the graph edits the selected step: its goal, its place name, its place on the
  sea chart (from Link's place, the camera's, or a destination of the warp list; the sea chart only knows
  places at sea), its time of day, its transitions, its resident talk, and Earlier / Later / Duplicate /
  Remove. With nothing selected it edits the quest: its title, what the notebook says before step 1, its
  reward text, and whether the notebook shows it.
- **List** (instead of Graph) shows the steps as a compact table; the arrows move a step earlier or later.
  Moving or inserting steps gives them new numbers, and every actor and item that names them follows.
- Every change is one command: Undo and Redo work for all of them, arranging the graph included.

Problems show on the steps themselves: a red mark (hover over it to read them) and a red frame on a step
nothing leads to.

## Testing a quest step by step

- **Start at this step** (right-click a step, or its Inspector) turns test mode on, sets the quest to that
  step, sets the flags and gives the items the earlier steps would have (along the shortest way there,
  through the earlier steps), warps Link in front of the actor that moves the quest on (or to the step's
  place), and plays from there. It only changes this mod's own quest step, flags and items: never the
  game's own progress, events or save, and never another mod's.
- **While playing,** the status bar (and the small bar at the top) says where the quest is, for example
  "Lighthouse quest: step 2 of 3", and changes as you play. With **Show while playing** on in the Quests
  window, the graph stays over the game: the step the game is at has a yellow frame and "now", the steps
  before it say "done".
- **Flags** (toolbar) lists the open mod's flags (signs read, chests opened, switches, flag actions, broken
  props and defeated enemies), who uses each, and their values in the game. In test mode you can switch a
  flag on or off to try a later part of the quest; broken and defeated only show.
- Stop, change the quest, **Save…** and start the game again to try the new version: quest changes take
  effect after a restart.

## Scenes

**Scenes** (toolbar) opens the scene editor: the scene at the top (**New scene…**, **Duplicate**,
**Remove…**), and below it three parts.

- **The steps** of the scene, in order. Steps that only one answer of a choice reaches are indented and
  say which choice and answer lead there. **Add step** adds one after the selected step (before the end):
  camera, camera path, a camera that follows an actor, say, choice, move, face, walk, animation, wait,
  fade, sound, music, particle or end. New steps come with values that fit: a camera takes the fly
  camera's view, a say step the first resident, a walk two points by the actor. The arrows move a step,
  **Duplicate** copies it, **Remove** takes it out (not while another step leads to it).
- **The timeline**: each step as a bar on a time axis (60 frames a second), the way the scene plays when
  every choice takes its default answer; camera keys are small diamonds. Click or drag on the ruler to put
  the cursor at a time: with **Camera follows the cursor** on, the fly camera shows the scene's camera
  there, so you see each moment of a camera path from where it is seen. Drag a bar's end to make the step
  longer or shorter, or a key's diamond to change the time from the key before it. Each drag is one
  change (one Undo step). Ctrl and the mouse wheel zoom; **Fit** shows the whole scene.
- **The inspector** of the selected step: its id and the step it goes on to, its duration, and what the
  step needs:
  - **camera**: **View from the fly camera** takes the camera's view; **Pan to the fly camera's view**
    makes the camera move there while the step lasts; **Add a key** turns it into a camera path;
  - **camera path**: its keys, each with **from camera** and its time to the next key, and the easing
    (linear or smooth). **Add a key from the fly camera** adds the view after the selected key;
  - **say** and **choice**: who speaks and the text (lines separated by " / "); a choice's two answers,
    the step each leads to, and which answer the cursor starts on;
  - **move**, **face** and **particle**: the place, from where the camera looks or from Link;
  - **walk**: the speed and the points. Select a point to move it with the handle in the picture;
    **Add where the camera looks** and **Add at Link** add points;
  - **Fields (JSON)** shows the step as the package writes it, to change any field.

**Camera key** (at the top) is the quickest way to film a scene: fly to a view, press it, fly on, press it
again. The first press adds a camera step; with that step selected, each further press adds a key to its
path.

In the picture, the shown scene's cameras appear as small boxes with a line where they look (the
selected step's in yellow), camera paths as lines from key to key, walking paths as green lines with
their points, and the camera at the timeline's cursor in orange.

**Scene: what starts it, native version** (under the timeline):

- **What starts it**: talking to an actor (reading a sign, opening a chest), a quest step, entering the
  room, entering an area around where Link is (with its radius), or salvaging a chart; none: only an
  actor's or another scene's action starts it.
- **The native version**: a scene of two placed uo2 residents that turn to places (face steps), a camera
  that holds a view and moves to another (camera steps), and waits can also play as the game's own event.
  **Make it from the steps** writes that version from the steps; the panel says whether it would play,
  and why not (for example a say step, which only the steps can play). The steps stay: they play wherever
  the native version can't.

**Play scene** plays the scene in the game, from the editor's view. Test mode turns on, the editor hides
but for a small bar with **Skip** (as Plus / Start does: the end's actions don't run) and **Stop / back to
editor**. When the scene ends, the world goes back to how it was before (Link, the actors, the progress
and the mod's flags), and the editor's view returns. **Play step** plays only the selected step, or the
steps from the selected one to the one selected with Shift, as a short scene of its own. An edited scene
plays its steps; its native version plays once the package is saved and the game starts again.

Scenes take effect for players after a restart, as quest changes do: **Save…**, then start the game
again. Ctrl+C and Ctrl+V in the Scenes window copy the selected steps and paste them after the selected
step, also into another scene; whole scenes are copied with the command `copy`.

## Search

**Search** (toolbar) finds the open mod's actors, quests and steps, scenes (also by their steps' text),
items and flags by their id or text. Clicking a result selects that actor (and flies the camera to it), or
opens that quest step or scene step.

## Checking and exporting

**Check** (toolbar) checks every actor, model, scene and quest as the mod manager does and lists the
problems in **Problems & export**, each with what it is about. Clicking a problem selects that actor,
flies the camera to it and marks the field in the Inspector.

For quests it also warns (in yellow) about steps nothing leads to, steps nothing moves on from, steps
without a place on the sea chart, arrows that point backwards, items a condition needs that nothing in
the package gives, and flag actions on a prop's or enemy's own flag. These warnings don't stop saving or
exporting. Clicking one opens that step in the quest editor.

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

## Using Claude or another MCP client

Claude Code (or any other program that speaks MCP, the Model Context Protocol) can work the editor for you:
warp around, place and move actors, write their dialogue, check the package and look at the result
through screenshots. It uses the same commands as the editor's panels, so everything it does is checked,
can be undone and shows in the editor's **Log**.

### Turning it on

1. Start the game with `--editor` and open **Settings → Editor → Claude / MCP**.
2. Turn on **Claude / MCP access**. It is off at every start.
3. Press **Copy command** and run the copied line in a terminal. For Claude Code it looks like this:

   ```sh
   claude mcp add --transport http wwhd-editor http://127.0.0.1:47615/mcp --header "Authorization: Bearer <token>"
   ```

4. Start Claude Code and ask, for example: "Warp to the lantern sign, put a new sign next to Link and let
   it say hello."

The token is a long random password, made new each time you turn the switch on and never saved. After
you restart the game, run `claude mcp remove wwhd-editor` and add it again with the new command. Other
MCP clients need the address (`http://127.0.0.1:<port>/mcp`, "Streamable HTTP") and the header
`Authorization: Bearer <token>`; both are shown in the same place, each with a **Copy** button.

The editor's status bar shows **MCP: off**, **MCP: on, port N** or **MCP: connected**.

### What it can do

- Every command in the [list below](#commands), with the same parameters.
- `screenshot`: a picture of the game as you see it (`screen`: `tv` or `gamepad`), without the editor's
  panels, at most 1280 pixels wide.
- `read_state`: where Link is, the camera, test mode, unsaved changes, and the open mod's quest steps and
  flags.

### Allow / Deny

Some commands remove or replace things: `remove_actor`, `revert_package`, `save_package`,
`export_package`, `reset_quest`, `load_state`, and `import_model` when it replaces a file. The client
must ask for them with `"confirm": true`, and then the game asks **you**: a window "Claude wants to: …"
with **Allow** and **Deny**. Nothing happens until you press **Allow**; without an answer within two
minutes the request is refused. Undo works for these changes as in the editor.

### Safety rules

- Only programs on your own computer can connect (the game listens on 127.0.0.1 only), and only with
  the token. Web pages cannot use it.
- It exists only when the game was started with `--editor` and you turned it on. It is not available on
  Android.
- Files: the client can read files inside the open mod's package and files you picked yourself in the
  editor (**Choose…** under Import or Export). For any other file the game asks you first and shows the
  full path.
- Exports over MCP go only into the `editor-exports` folder of the game's user data (the panel shows
  where), or into a folder you picked under **Export**. Nothing is ever written anywhere else, and game files are never
  included.
- Every call shows in the editor's **Log**, marked **[MCP]**, with its parameters and its result. The
  token never appears there, in the game's log or in crash reports.

## Frame rates

The editor and its camera work at 30 fps, with 60 or 120 fps interpolation and with True 60. The camera
moves at the same speed in every mode.

## Commands

Everything the editor does is a command with named parameters and a JSON result. Claude and other MCP
clients use exactly these commands (see [Using Claude or another MCP client](#using-claude-or-another-mcp-client)). For scripted tests, `WWHD_EDITOR=1` starts a run in the editor mode, like
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
| `set_actor` | `id`, `property`, `value`, `mod`? | set one actor property (`null` removes it); `when` may name `quest` and `step`, `quests` (`[{quest, step}]`, all must hold), `item`, `time`, `flag` and `not_flag` (a flag id); `talks` is up to eight `{when, lines, then}`, the first that matches is said (one without `when` last; not together with `lines` or `then`) |
| `move_actor`, `rotate_actor`, `scale_actor` | `id`, `position` / `rotation` / `scale`, `mod`? | `{x, y, z}` |
| `set_dialogue` | `id`, `lines`, `mod`? | one to three lines |
| `set_then` | `id`, `actions`, `mod`? | what happens after the dialogue (an empty list removes it): `give`, `take`, `quest_step`, `quest_done` (finishes a quest), `flag`, `scene` |
| `set_drops` | `id`, `drops`, `mod`? | what a prop or enemy drops |
| `preview_dialogue` | `id`?, `lines`?, `close`?, `mod`? | show lines in the game's message box (or hide it) |
| `pick` | `origin`, `direction`, `range`? | the first ground a ray meets |
| `ground` | `position` | the ground height below a point |
| `focus` | `id`, `mod`? | fly the camera to an actor |
| `check_model` | `file` | check a `.glb` file |
| `import_model` | `file`, `id`?, `confirm`?, `mod`? | copy a `.glb` into the mod and add the model |
| `prepare_collision` | `model`, `mod`? | prepare a static model's collision for live world-object placement |
| `place_world_object` | `id`, `model`, `collision`, `behaviour`?, `buoyancy`?, `drift`?, … , `mod`? | place a static, floating or drifting object with collision in one step (see [World objects](world-objects.md)) |
| `set_world_object` | `id`, `collision`?, `behaviour`?, `buoyancy`?, `drift`?, … , `mod`? | change a world object's collision, behaviour and motion together |
| `preview_collision` | `id`, `mod`? | a world object's collision as a wireframe, with placement findings |
| `placement_conflicts` | `id`, `mod`? | placement conflicts and collision budget findings for a world object |
| `set_scene` | `id`, `property` (`trigger`, `steps`, `native`), `value`, `mod`? | (`null` removes a trigger or the native version) |
| `set_quest_step` | `quest`, `step`, `property`, `value`, `mod`? | `place` is `{x, z}` at sea, or inside a building `{stage, room, sea: {x, z}, point}` (or `x`, `y`, `z` instead of `point`); `available` may use `flag` and `not_flag` (a flag id or a list) with its `text` |
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
| `read_state` | | Link's place, the camera, test mode, unsaved changes, the open mod's quest steps and flags |
| `fly_camera` | `on`, `place_link`? | the free camera on or off |
| `play` | `from`? (`camera`, `link`), `restore`? | play as Link (test mode on) |
| `stop` | | back to the editor view |
| `test_mode` | `on` | test mode |
| `reset_quest` | `confirm`, `mod`? | test mode: reset the mod's quest progress and flags |
| `save_state` | `slot` (1–3) | test mode: save a test state |
| `load_state` | `slot` (1–3), `confirm` | test mode: load a test state (replaces the current progress) |
| `command_log` | `count`? | the last commands |
| `get_quest` | `quest`, `mod`? | one quest as the manifest declares it |
| `quest_graph` | `quest`, `mod`? | the quest's nodes and edges, each node's problems, the graph's places, and the step the game is at |
| `add_quest` | `title`, `id`?, `text`?, `mod`? | a new quest with its first step |
| `remove_quest` | `quest`, `confirm`, `mod`? | remove a quest and the actor conditions and actions that name it |
| `set_quest` | `quest`, `property` (`title`, `journal`, `start`, `reward`, `letter`), `value`, `mod`? | |
| `add_quest_step` | `quest`, `after`?, `text`?, `where`?, `place`?, `mod`? | a step at the end, or after a step (later steps move up) |
| `edit_quest_step` | `quest`, `step`, `fields`, `mod`? | several step properties at once (`null` removes one), with the same `place` and `available` forms |
| `remove_quest_step` | `quest`, `step`, `confirm`, `mod`? | remove a step and what names it |
| `duplicate_quest_step` | `quest`, `step`, `mod`? | a copy as the new last step |
| `move_quest_step` | `quest`, `step`, `to`, `mod`? | move a step in the order; the numbers and references follow |
| `set_step_place` | `quest`, `step`, `from` (`link`, `camera`, `location`, `none`), `location`?, `mod`? | the step's place on the sea chart and its name |
| `connect_quest_steps` | `quest`, `from` (0: any time), `to`, `via`, `item`?, `time`?, `mod`? | an actor moves the quest on from one step to another |
| `disconnect_quest_steps` | `quest`, `from`, `to`, `via`, `mod`? | remove that |
| `set_quest_layout` | `quest`, `positions`, `mod`? | the graph's places (`start`, `end`, `step:N`: `{x, y}`) |
| `start_at_step` | `quest`, `step`, `warp`? (`next`, `place`, `none`), `play`?, `mod`? | test mode: start the quest at a step |
| `quest_state` | `mod`? | the step each quest is at in the game |
| `list_flags` | `mod`? | the mod's flags, who uses them, their values |
| `set_flag` | `id`, `value`, `mod`? | test mode: set one of the mod's flags |
| `search` | `text`, `mod`? | find actors, quests and steps, scenes, items and flags |
| `copy` | `ids`, or `quest` and `steps`, or `scenes`, or `scene` and `steps`, `mod`? | copy actors, quest steps, scenes or scene steps |
| `paste` | `at`? (`offset`, `link`, `camera`, `position`), `offset`?, `position`?, `quest`?, `scene`?, `before`?, `mod`? | paste them (actors get ids and flags of their own, scenes ids of their own; scene steps go before the scene's end or before `before`) |
| `move_actors` | `ids`, `offset`, `mod`? | move several actors (one Undo step) |
| `remove_actors` | `ids`, `confirm`, `mod`? | remove several actors (one Undo step) |
| `add_scene` | `id`?, `mod`? | a new scene (with its end step) |
| `remove_scene` | `id`, `confirm`, `mod`? | remove a scene (refused while an actor or another scene starts it) |
| `duplicate_scene` | `id`, `new_id`?, `mod`? | a copy of a scene, without its trigger |
| `set_scene_trigger` | `scene`, `type` (`none`, `talk`, `quest_step`, `room`, `area`, `salvage`), `actor`?, `quest`?, `step`?, `chart`?, `at`? (`link`, `camera`), `stage`?, `room`?, `position`?, `radius`?, `mod`? | what starts the scene |
| `add_scene_step` | `scene`, `type`, `at`?, `fields`?, `mod`? | a step with defaults that fit (a camera takes the fly camera's view; a choice comes with its two branches) |
| `edit_scene_step` | `scene`, `step`, `fields`, `mod`? | several fields of a step at once (`null` removes one); a new id moves the links to it along |
| `remove_scene_step` | `scene`, `step`, `mod`? | remove a step (refused while another step leads to it) |
| `move_scene_step` | `scene`, `step`, `to`, `mod`? | move a step in the list |
| `duplicate_scene_step` | `scene`, `step`, `mod`? | a copy right after it |
| `add_camera_key` | `scene`, `step`?, `at`?, `frames`?, `eye`?, `target`?, `mod`? | the fly camera's view as a camera key: a new camera step, a camera step that becomes a path, or a new key of a path |
| `set_camera_key` | `scene`, `step`, `key`, `from`? (`camera`, `none`), `eye`?, `target`?, `frames`?, `mod`? | a key takes the fly camera's view, and its time to the next key |
| `remove_camera_key` | `scene`, `step`, `key`, `mod`? | remove a key (a path of two becomes a camera step) |
| `retime_scene_step` | `scene`, `step`, `frames`, `key`?, `mod`? | a step's duration, a path key's time to the next, or a whole path (scaled) |
| `scene_timeline` | `scene`, `mod`? | each step's start and duration on the default path, the branches, the camera keys |
| `scrub_scene` | `scene`, `time`, `move`?, `mod`? | the scene's camera at a time; the fly camera shows it |
| `set_scene_choice` | `scene`, `step`, `option`?, `label`?, `next`?, `default`?, `mod`? | a choice's labels, where each option leads, the default answer |
| `add_walk_point`, `set_walk_point`, `remove_walk_point` | `scene`, `step`, `at`? / `point`, `from`? (`camera`, `link`, `position`), `position`?, `mod`? | the points of a walk |
| `native_scene` | `scene`, `from_steps`?, `value`?, `mod`? | the scene's native version: made from its steps, set or removed; whether it would play |
| `preview_scene` | `scene`, `from`?, `to`?, `restore`?, `mod`? | test mode: play the scene (or some of its steps) in the game |
| `skip_scene`, `stop_scene` | | skip the scene that plays; stop the preview and go back to the editor |
| `scene_state` | | whether a preview plays, which scene and step |
