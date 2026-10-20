# World editor (preview)

**Preview.** The world editor builds and changes mod content inside the running game: place game actors,
your own models and enemies, move and turn them with handles in the picture, import models, write
dialogue and what happens after it, warp around, play your quest as Link without touching your own save,
and export the finished package.

It works with mods that use [actors and scenes](mod-sdk-v3.md) (placed game actors and your own models),
scenes and quests ([SDK v2](mod-sdk-v2.md)). The examples
[sdk-v3-windfall](../examples/guest-mods/sdk-v3-windfall/manifest.json) and
[sdk-v3-creature](../examples/guest-mods/sdk-v3-creature/manifest.json) are good packages to try it on, or
[start a new mod](#starting-a-new-mod) of your own in the editor.

## Starting it

Start the game with `--editor`, for example from a terminal (`wwhd --editor`, on Windows `wwhd.exe --editor`)
or from a shortcut with that option added. A normal start has no editor at all: nothing that players see.

The game starts as usual: the title screen, then your file. As soon as Link is in the world, the
**editor view** begins: the camera flies freely, Link stands still, and the editor's own panels are over
the picture. The settings menu still opens with Esc (or F1) and has an **Editor** tab with the same
panels for quick changes. While you place a new actor, drag a handle or connect two quest steps, the first
Esc cancels that instead (as the first Esc frees a captured mouse); Esc with nothing to cancel opens
Settings.

## Starting a new mod

Press **New mod…** next to the mod list in the toolbar (or in the Editor tab of Settings). Fill in:

- **Id**: the mod's short name for the game and the mod manager, for example `harbor-quest`. Lower-case
  letters, digits, `-`, `_` and `.`; it can't be changed later and no other mod may use it.
- **Name**: what players see in the mod manager, for example "Harbor quest".
- **Author** (optional): you.

Press **Create**. If something is wrong (the id is taken, or has a character that isn't allowed), the
form says so under the fields and nothing is created. Otherwise the editor:

- makes a new, empty package for the mod in the mod manager's mods folder (only its `manifest.json`),
- turns it on in your current mod profile, so it also loads the next time you start the game,
- and opens it: everything you add (actors, models, quests, scenes, letters, charts and items) goes into it.

Nothing else changes: no other mod, no game file and no save. The new mod uses the Story Notebook, like
every mod with actors or quests; the game installs it with the story system.

Your work is in the package once you press **Save…**. After that it is a package like any other: export it
under **Export** to share it. Creating a mod is not an Undo step. To get rid of a mod you don't want, turn
it off in **Settings > Mods** and press **Remove** there (a mod the game has loaded since needs a restart
first).

## What it changes, and what it never changes

- It changes **only the mod you open**, in that mod's own folder: its `manifest.json` (only when you press
  **Save…** and confirm) and, when you import a model or an item's icon, the file it copies into the
  mod's `models` or `icons` folder.
- **New mod…** adds a new package folder of its own and turns that mod on; it never replaces a package.
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
| **Toolbar** | the open mod, **New mod…**, Undo, Redo, Check, Save…, Revert…, **Add**, Move / Turn, grid and angle snapping, **Play**, Play from Link, Restore at Stop, Test mode, which panels show (also **Quests**, **Scenes**, **Flags** and **Search**), and **Reset layout** |
| **Outliner** | the open mod's actors, models, scenes and quests, its letters, charts and items (**Story**), and the warp destinations |
| **Inspector** | the selected actor: place, properties, its route, dialogue and actions |
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
you clicked (the game's own collision decides where the ground is; a placed island ground or another world
object is ground too). On the sea, a click on open water places at the sea's surface. Hold Shift while clicking to place
more of the same; **Stop placing** in the toolbar or Esc ends it.

- **Game actors:** sign, resident, chest. **Resident** opens a list of who it is: one of the game's
  villagers (the man from the bar, the windmill keeper, the lookout sailor, a seated man and more; the
  list is in [Actors and scenes](mod-sdk-v3.md)). A placed resident's **Type** in the Inspector
  changes it; the resident is then created again in its room.
- **Props:** pot, barrel, crate.
- **Switches:** switch, door (it opens with one of the mod's switches).
- **Enemies:** ChuChu (green, red, blue, yellow or black), Bokoblin (with a stick, a shield or a machete),
  Moblin, Keese (or a Fire Keese), Miniblin, Peahat (or a Seahat), Darknut (with a shield, a helmet, or the Mighty Darknut), Rat hole (rats,
  bomb rats or both), Boko Baba, Bubble (red or blue), Octorok, Stalfos, Redead,
  Mothula (on the ground or winged), Morth, Magtail, Poe, Armos, Armos Knight, Kargaroc, Wizzrobe, Warship, Gyorgs (one to five), Floormaster, Big Octo (a target only). The Inspector's **Variant** changes a placed one, and **Sets switch when defeated**
  picks one of the mod's switches to set once every enemy naming it is defeated.
- **Your models:** one of the mod's models (static or skinned), your own enemy (a skinned model with
  flinch, stun, death and idle clips), a prop Link can carry.
- **Game objects:** the game's own palm trees, trees, tables and more, and island buildings (see below).

Each new actor gets the flags it needs, named after it (for example `sign-read`, `pot-broken`,
`guard-defeated`). In the Inspector, **To Link**, **To camera** and **Snap to ground** move the selected
actor, **Duplicate** places a copy next to it with flags of its own, and **Remove…** takes it out (after
a confirmation; Undo brings it back).

### Game objects

**Add > Game objects** lists the game's own objects you can place: palm trees and other trees, tables, a shelf,
lamps, plants, a nest, flags, a sea lookout tower, grass and flowers (docs/mod-sdk-v3.md, "The game's own objects"). Type in the search field or pick a
category, then click an object and click into the picture. An object your game files don't have is
greyed out with "not available in your game files". The first game object adds
`"requires": {"game_objects": 1}` to the manifest for you.

Grass, flowers and small trees appear at once too, but when you remove or move them they go only
when the room loads again: the status bar says so; **Reload room** (Camera & test) enters the room
again. A placed game object appears in the game at once. In the Inspector you choose its **Variant** (for
the table: table, stool, or a stool Link walks through), its **Turn** and, where the object allows
it, its **Scale**. Each change makes the object again in its place (one undo step). A game object
has no flags, drops or dialogue.

**Buildings** in the same list are the islands' own houses, towers and shelters
(docs/mod-sdk-v3.md, "Island buildings"); **Island parts** are smaller pieces of other islands made the
same way (Dragon Roost's shrine, torii, walkways and sea rocks, Windfall's windmill cap, shipwrecks); **Islands** are
the ground of small islands, bare, always at the sea's level: click the open sea where it goes, and it stays at
sea level when you move it (docs/mod-sdk-v3.md, "Island grounds"). Houses, villagers, enemies and other actors can be
placed on an island ground like on any island. The first time a mod places one, the editor makes it from
your game files: the list says "(preparing...)" and the building appears in the picture a moment
later, when it is ready. A building turns about the vertical axis only and keeps its size; it has no variants. While
a building (or any world object) is selected or dragged, its footprint shows in the picture: green where it may stand, red
with what is in the way (a villager, a villager route, a chest, a door, a cutscene area...) where it
may not, with a grid on its base. The label names the first thing ("In the way: a door of the island (and 2
more)"); hover it to see them all, with the game's own names, and when a single small thing is in the way it
says how far to move. What is in the way is drawn in the picture: a ring around a door, chest, villager or
start point, a line along a villager's route. You can place it anywhere: on a red spot it is kept (and saved)
as a see-through outline but isn't in the game; drag it until the grid turns green and it appears. The status
bar, Check and export say which placements aren't in the game yet. If your game files can't make it, the list
shows the reason.

### Routes: actors that walk

A resident that walks (the old man on the bench `uo1`, the man from the bar `uo2`, the well-dressed man
`uo3`, the windmill keeper `um1` or the young man `um2`) and an actor with one of your skinned models can
walk a route. Select it; the Inspector's **Route** section has:

- **Add point at Link** and **Add point at camera**: a new point on the ground there (after the selected
  point, or at the end). A resident starts where it stands, so its position is the route's start; to move
  the start, move the resident.
- The points, numbered as the picture shows them. Click one to select it: its handle in the picture moves
  it (letting go is one change). **Up**, **Down** and **Remove point** change the order or take it out;
  every change is one Undo step.
- **Speed** in game units per second. A resident walks at its own pace unless you set one (from half to
  twice its own pace). **Mode**: once (it stops at the end), loop (back to the start and round again) or
  back and forth. **Strolling pauses**: now and then it stands for a moment, as the game's own walkers do.
- For your models: the **Walk** and **Idle** clips and **Look at Link** (it turns its head bone to Link).
  A resident walks with its own walk and stands with its own idle; it stops and looks at Link when he is
  close in front of it, turns on the spot at corners and talks as usual, so its **Look at Link** is
  always on.

In the picture the selected actor's route shows as a green line with numbered points. The editor checks
the ground under every segment with the game's collision: a segment over no ground, a drop or a step too
high (a wall) is drawn red, and the Inspector says where. A walking resident's state in the game shows
under its route: walking, skipped a blocked point, stopped (blocked, or it left the ground of its route)
or finished. Route changes go to the game at once: the actor keeps walking, on the new route. Adding the
first point or removing the route creates a resident again in its room.

Residents that have no walking animation of their own (the women on the steps and the pier, the young
women, the sailors) and the seated villager can't walk a route; the Inspector says so.

### Paths: lines and areas in a room

A wall cannon is placed with a new firing area in front of it (a square of 1600 units, its id the
cannon's with `-area`), or on an area the mod has (`add_actor` `options.path`). A Wizzrobe summoner is placed with its path and that path's next path (its summon points, `<id>-route`
and `<id>-route-spots`); `set_path` `next` changes which path follows another. An actor that uses one of the mod's room paths (see "Room paths" in the SDK) has a **Path** section in
the Inspector: the mod's paths in its room to choose from, **New area in front** (a square area in front
of it, which it then uses), **Area (closed)**, the points (click one to select it, then drag its handle
in the picture), **Remove point** and **Add point at Link** / **at camera**. In the picture every path
of the mod on this stage shows as a thin blue line with its id; the selected actor's path is drawn
thicker with its numbered points. Path changes go to the game at once: an actor on the path uses the
changed points straight away. A path an actor uses can't be removed; the refusal names the actor.

### What shows at once, and what after a restart

Signs, residents, chests, pots, barrels and crates, switches and doors, ChuChus, Bokoblins, Moblins,
Keese, Miniblins, Peahats, Darknuts, Boko Babas, Bubbles and Octoroks, and actors with your models appear, change and disappear in the game at once, in the room you are
in. Their drops and broken or defeated flags count from then on, as they do after a start. A change to
one of them (another place, other drops, another flag) creates it again in the game.

These wait until the game starts again (the change is in the package; save it first):

- your own enemies and carried props, and a pot, barrel or crate with a texture of its own;
- the actors of a mod that had no actors at start-up;
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
| mesh | one mesh, plain triangles in up to 32 primitives, up to 8192 triangles and 4096 vertices | the same, every primitive skinned by one skin |
| material | up to 16 materials, each with an embedded PNG base colour on UV0 (opaque or `MASK` cut-out), up to 512 × 512 pixels each and 1 MiB for all textures | the same |
| bones | none | 1 to 32, unit scale, one connected hierarchy |
| clips | none | up to 8, each up to 900 frames, distinct lower-case names |

**Import into the package** copies the file into the mod's `models` folder and adds the model. The editor
prepares it for the game while you work; when the status bar says it is ready, actors can use it without
a restart.

## Water: ponds, rivers and waterfalls

**Add → Water → Pond, River or Waterfall** (or the toolbar's **Water**) opens the Water window and starts
drawing: each click on the ground in the picture adds a point (Esc stops drawing; **Add points** goes on).
The points show in the picture, numbered; click one to select it and drag its handle to move it.

- **Pond:** click the shore's points in order round the pond (3 to 48). The water shows as a light fill at
  its level with a line down to each picked point; the blue handle in the middle moves the level up and
  down (or type it under **Level**; until you choose one it is the pond's **Depth** above the lowest ground along
  its shore, so Link can swim in it and its banks rise as a low mound round it). Set its
  **Depth** (the floor below the water), **Banks above the water** (how high its rim rises above the water
  before it slopes down to the ground), **Foam at the shore** and the **Floor colour** (used where your game
  files don't give the island's own ground texture). The outline's corners are rounded off.
- **River:** click its middle from where it starts to where it flows (2 to 32 points): the order is the flow,
  and white arrows along the band show it (**Reverse flow** turns it round). It follows the ground and steps
  down where the ground drops; set its **Width**, its **Depth** (the water above its bed where Link swims)
  and, per section, **shallow** (water to wade through, shown in a lighter colour).
- **Waterfall:** click the two ends of the edge the water falls over; it falls to the lower side (or choose a
  side) down to the ground below. A river with a step has **Waterfall here** next to each step when you edit
  it: the waterfall then fills the step across the river's width.

Until it is made, the water shows in the picture as a light fill (a river's band in two colours: deep and
shallow water); once it is made the game's own water shows, and **Apply** after a change shows the new
water as soon as the game has prepared it (a few seconds).

The window checks the drawing as it changes and lists what is wrong in red (warnings in yellow): an
outline that crosses itself, too few or too many points, a pond whose water is above the highest ground of
its sea square (the game doesn't find water there; see [Water](world-objects.md#water)), too many
triangles for the water surface or the collision, a mod that would have more than 8 models (a body of water
takes two), and what it would be in the way of: the island's people and their routes, doors, chests, event
areas and other mods' objects (its floor and banks may meet the island's ground, and the ground of your mod's own
objects it lies on, such as an island ground or a terrace: of those only what rises through the water counts). The outline turns red in
the picture with the first error next to it. **Create** (one Undo step) is possible once nothing is red.

What it makes: two models in the package's `models/water/` folder (the water's ground, banks and
collision; the water surface, drawn with the game's own water from your game files: an island's calm
shallows with wave lines and coast foam for a pond, Forest Haven's streams and falls for a river and a
waterfall), and a static world object that uses them, placed like any other: move, turn, copy and remove it as
usual (removing it takes its two models along). The game prepares the models while you work; the water
appears when the status bar says they are ready. Selecting it shows **Water** in the Inspector: **Edit
shape…** loads its points and settings into the Water window, where **Apply** builds it again where it
stands (one Undo step; Undo brings the earlier models back). The generated files are the same on every
computer for the same drawing.

The banks are natural ones: a rim a little above the water, then a slope down to where it meets the island's
ground, painted with the island's own grass, sand or rock there (read from your game files when the models
are prepared). The island's ground can't be lowered, so water deep enough to swim in sits on top of it: a
pond on a slope gets a grassy mound round it on the downhill side. See
[Bodies of water made with the editor](world-objects.md#bodies-of-water-made-with-the-editor).

Bodies of water are made on the sea's islands (the `sea` stage). The same works through the commands
`check_water`, `add_pond`, `add_river`, `add_waterfall`, `edit_water` and `get_water` (below).

## Dialogue and actions

For signs, residents, chests and your own models, the Inspector has:

- **Dialogue:** up to three lines. **Preview in the game** shows them in the game's own message box, as
  players will see them; **Close preview** hides it.
- **Flag:** set when the actor was talked to or opened.
- **Chest holds** (chests only): a green, blue, yellow, red, purple, orange or silver rupee, a piece of
  heart, a heart container, or one of your items (an orange rupee when nothing is chosen). Link gets it
  with the game's item-get pose when he opens the chest; an item with **hold_up** shows your model.
  `set_actor` with `parameters` (`{"item": "rupee_red"}`, `{"item": "item:crystal"}`) does the same.
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

### A quest talk for one of the game's villagers

The game's own townspeople that quests can name (the NPC catalogue, for example the lighthouse keeper on
Windfall) have a teal name label in the picture while they are near. They are not part of your mod, so
you can't move or change them, but you can give them a line for your quest: hover over the label to see
who it is, then click it. **Add a quest talk for this villager…** offers:

- **New quest…**: a new quest (its title) whose first step is this villager's talk;
- **New step of a quest**: a new last step of one of your quests, with the talk;
- **A step of a quest**: the talk for a step you already have (one it had is replaced).

Write what the villager says (one message box) and press **Add the talk**. It is one change (one Undo
step), and the quest editor opens at that step, where you can change the line, add conditions or what the
talk hands out. While that step is the quest's current one, talking to the villager gives your line once
and finishes the quest (see Talking to residents in the [SDK v2 guide](mod-sdk-v2.md)); at any other time
the villager talks as in the game.

**The villager itself is never changed:** its own dialogue, its quests and the game's save flags stay as
they are. Your mod only adds a talk that the port plays while your step is current.

The talk works in the game at once, like every quest change (see [Story changes while the game
runs](#story-changes-while-the-game-runs)); change its line and the villager says the new one in the next
talk, remove it (or Undo) and the villager talks as in the game again. Save the package to keep it.
Something has to bring the quest to that step: its letter, an actor's action, an item, or, while testing,
**Start at this step**. The villager may first say a short greeting of its own; your line follows in
the same talk.

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
- Change the quest while you test: the game takes each change at the next calm moment (see [Story
  changes while the game runs](#story-changes-while-the-game-runs)), so there is no need to restart.

## Letters, charts and items

The Outliner's **Story** tab (also **Story** in the settings' Editor tab) holds the mod's letters on their
own, its treasure charts and its items, one tab each. Each tab lists them; click one to edit it, and the
form below adds a new one. Every change is one command, so Undo and Redo work for all of them.

- **Letters** are letters on their own: no quest and no notebook entry. Any Rito postbox hands them out
  while your mod is on (once Link has the Delivery Bag); once Link has read one, he gets what it encloses,
  once. Give it a sender (one line, the signature) and a text (about ten lines of 40 characters), and tick
  the charts and items it encloses. A letter can also wait: for a quest step, a done quest or one of
  your flags (`when`), and then some days of the game's calendar (`after_days`), for example a thank-you
  letter two days after the quest is done (set them in **JSON** or with `set_letter`; `advance_days`
  moves the calendar on in test mode). A letter that starts a quest is the quest's own letter (the quest's
  Inspector), not one of these. Up to 8 per mod.
- **Charts** are treasure charts. While Link owns one, the MAP screen's chart list shows it with an X at
  its place; salvaging the treasure there opens its chest. **Add with the X at Link** (or **at the
  camera**) puts the X where Link or the fly camera is on the Great Sea; **X at Link** / **X at the camera**
  moves it later. Choose what the chest holds: one of your items, a game item (purple, orange or silver
  rupee, a piece of heart, a heart container) or, by default, an orange rupee. A letter, a resident's talk,
  an actor's dialogue or a scene's end (**After the dialogue**: give `chart:<id>`) or a quest's reward
  gives the chart; a quest step's **find** or a scene's **salvage** trigger uses it. Up to 8 per mod.
- **Items** are kept in the Delivery Bag's extra pages. When Link gets one, its name and description show
  with the game's small get jingle. An item needs an icon: **Choose...** picks a `.png` (best 128 x 128
  with transparency, at most 2048 x 2048), which is copied into the package's `icons` folder. **How many
  Link can carry** is 1 to 254. **Use** says what using it on X, Y or R does: show its Info box, open the
  story notebook, move one of your quests to a step, or tell your mod's code; without a use it stays in
  the bag. With a model of your own as its held model and **hold_up** on (in **JSON**, or `set_item`), Link
  holds the model up over his head with the game's get message when he gets it, as he does with the
  game's items. Actors give or take items in their actions (**After the dialogue**), enemies and props drop
  them, and letters and charts can hold them. Up to 32 per mod.

Under each entry, **Used by** lists everything that names it. A chart or item that is still used can't be
removed: change or remove those first (**Remove…** says so). **JSON** shows the whole entry to edit by hand
(a field you take out is removed). Letters and charts change in the game at once, as quests do; of an item,
its name, description, how many and use do, while a new item, its icon, held model and **hold_up** wait
for a restart (see [Story changes while the game runs](#story-changes-while-the-game-runs)).

## Story changes while the game runs

Quests (their steps, places, conditions and texts), quest talks (also those for the game's villagers),
the quests' letters, letters on their own and charts change in the running game. Every change, and every
Undo and Redo, hands the open mod's new story to the game, which takes it at the next calm moment: at once
in the editor view, and while playing as soon as no talk, letter, message, menu, scene or stage change is
running (a talk that is running ends as it began). The status bar says when the game took it ("Story of
... updated in the game") or what it waits for. Only the open mod's story changes: other mods and the
game's own villagers, dialogue and quests stay as they are. A villager whose talk you remove talks as in
the game again. Save the package to keep your changes: the next start reads the package.

These wait for a restart (save, then start the game again); the status bar names them after the change:

- a new item and a removed item, and an item's icon, held model and **hold_up** (the game builds them
  into its menus when it starts); its name, description, how many and use change at once;
- a removed chart: the chart list keeps it until a restart. A new or changed chart shows in the chart
  list at once; its treasure's salvage point is set up when Link enters the sea room, so it follows
  there after **Reload room** or the next visit;
- what the game did not run at start-up: the King of Red Lions' lines when no quest had one, items when
  no mod had any, a chart's treasure when no mod had a chart, and all of the story when the Story
  Notebook package was not on;
- scenes (see [Scenes](#scenes)).

**Progress stays as it is.** A change never writes to the Quest Log: the step each quest is at, done
quests, quest flags, the letters Link got and the charts and items he has stay. A quest's progress is its
step number: editing a step keeps its number; adding, moving or removing steps gives the later steps new
numbers (every actor, item and letter that names them follows), and a Quest Log is then at the step
that has its number now, exactly as after a restart. When that is not the step it was at, the status bar
says so ("quest lamp is at step 3, which is step 4 now", or "which was removed"). A step number that no
step has any more shows no next step and no talk until something moves the quest on (in test mode,
**Start at this step**) or the step comes back (Undo). A removed quest or letter keeps its records in the
save, so Undo brings it back where it was.

**Save files and save states hold progress, never the story.** A save file, a full save state and the
editor's test states play with the story as the editor has it now, whenever they were made: a state taken
before an edit comes back with its old progress under the new quests, as a save of an earlier version of
a mod does after an update. Nothing of an older story comes back with it.

Without `--editor` none of this runs.

## Scenes

**Scenes** (toolbar) opens the scene editor: the scene at the top (**New scene…**, **Duplicate scene**,
**Remove scene…**; **Undo** brings a removed scene back), and below it three parts.

- **The steps** of the scene, in order. Steps that only one answer of a choice reaches are indented and
  say which choice and answer lead there. **Add step** adds one after the selected step (before the end):
  camera, camera path, a camera that follows an actor, say, choice, move, face, walk, animation, wait,
  fade, sound, music, particle, demo or end. New steps come with values that fit: a camera takes the fly
  camera's view, a say step the first resident, a walk two points by the actor. The arrows move a step,
  **Duplicate step** copies it, **Remove step** takes it out (not while another step leads to it); the
  buttons wrap onto a second line when the panel is narrow. Right-click a step for the same, or press
  **Delete** to remove the selected step.
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

### Demo steps on the timeline

A **demo** step (Add step, then **demo**) plays keyframe tracks through the game's own cutscene system
(see "Demo steps" in [the SDK reference](mod-sdk-v3.md)). With a demo step selected, the timeline shows
its lanes under the step bars, on the demo's own frames (30 a second; the ruler stays in scene time):

- **Camera**: `eye`, `target`, `fovy` and `roll`, each key a diamond.
- **Each actor**: its `position`, `rotation` and `scale` keys, then a row of its **clips** as blocks
  (from their frame to the next clip), and for Link his **modes** the same way.
- **Events**: messages as points with their text, sounds, particles, attach and detach markers, and the
  time and weather keys.

Click a key to select it; drag it to another frame; **Delete** removes it. **Add key** on a lane adds a key
at the cursor: for the camera from the fly camera's view, for an actor where the selected key's handle is
or at Link. The inspector shows the selected key's value, the lane's curve (linear, smooth, bspline,
step) and the selected event's fields. Every change is one Undo step.

**Actors** of the demo: under **Add an actor**, **What it is** picks the kind (the **(?)** says what each
one is):

- **game actor**: search the list by name (for example "helmaroc", "pig" or "pot"): characters, the mod
  catalogue's residents and game objects with readable names, then every other actor of the game by its
  own short name;
- **Link**: nothing more to choose;
- **model**: one of the game's object archives, then one of its models;
- **mod character**: one of the mod's placed actors.

**Name in this demo** is the id that keys and events use; the editor suggests one from the choice
("helmaroc", "link", ...), and you can change it. After **Add actor**, put the cursor on a time, select
the actor's position lane and press **Add key**.

**Add an event at the cursor**: choose the kind, then fill it in with its own controls. **sounds**: search
the game's sound effects by name and listen with **Play**, and pick the actor it comes from;
**particles**: search the particles, pick the actor and one of its joints (or the demo's origin);
**messages**: the text; **time**: the hour; **weather**: how much rain; **attach** / **detach**: the
carried actor, the one that carries it and its joint; **clips**: an animation of the actor's archive (or
another archive), for a mod character one of its model's clips; **modes**: one of Link's demo modes
(**clip**: one of his demo animations). **Advanced** shows the event as JSON, for fields the controls
don't cover. Names of sounds and particles come from the decompilation's source code; a sound or particle
without a name can still be entered by its number in Advanced.

With a demo step selected, the cursor reads as its time and the demo's own frame (for example "cursor 4.00s · demo frame 120"); new keys and events go to that frame. Moving the cursor previews the demo at that frame without starting it: with **Camera follows the cursor**
on, the fly camera shows the demo camera, and in the picture each actor appears as a marker at its place,
its position track as a curve with its keys as points. The selected position key has a handle to drag in
the picture.

What the game would refuse (a key past the end, rain above 1, an actor an attachment needs) is refused
when you make the change, with the reason in the status line; a step that is wrong already (for example
after editing its JSON) shows the reason in red above its lanes.

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

Scenes take effect for players after a restart: **Save…**, then start the game again. Ctrl+C and Ctrl+V in the Scenes window copy the selected steps and paste them after the selected
step, also into another scene; whole scenes are copied with the command `copy`.

## Search

**Search** (toolbar) finds the open mod's actors, quests and steps, scenes (also by their steps' text),
letters, charts, items and flags by their id or text. Searching for a chart or an item (`old-map`,
`item:lamp`) also finds what uses it: the actors that give, take, need or drop it, the quests and steps
that name it, the scenes its salvage starts, the letters and charts that hold it. Clicking a result selects that actor (and flies the camera to it), or
opens that quest step or scene step.

## Checking and exporting

**Check** (toolbar) checks every actor, model, scene, quest, letter, chart and item as the mod manager
does (and that each item's icon is a PNG in the package) and lists the problems in **Problems & export**,
each with what it is about. Clicking a problem selects that actor, flies the camera to it and marks the
field in the Inspector; for a letter, chart or item it opens it in the **Story** tab.

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
  - **Reset this mod's quest…** erases the open mod's quest progress and flags from the loaded game, its
    letters, and the charts and items Link got from it, so you can play the quest from the start. Other
    mods and the game's own progress and items are not touched;
  - **Save test 1–3** / **Load test 1–3** keep test states: the game's progress, Link's place and the
    open mod's flags;
  - turning test mode off takes you back to your progress and place from before the test.

Preview and Play report a completed restore only after the load finishes. If it is refused, the
status line and log show **The world could not be restored: …**, followed by the reason.
Test-state commands use portable states by default. Use `"kind":"full"` with `save_state` and
`load_state` for a full snapshot in the editor's test folder; it stays separate from regular player
state slots and the portable file for that test slot. A queued `load_state` returns a `state_load`
request ID with `pending:true`. Poll `read_state.state_load` for `pending:false`, `ok` and `reason`.

- **Log** shows the last commands the editor ran, with their parameters and results.

## Stages

The **Stages** tab builds new caves and dungeons from your own room models: a new stage from a `.glb`,
Rebuild, Warp in, Check, and placing spawns, exits, doors, chests and enemies where Link stands. See
[Mod stages](mod-stages.md#editor), and [Building a dungeon with an AI assistant](mod-stages-ai.md) for the
same with Claude and Blender.

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

Some commands remove or replace things, or write outside the open mod: `new_mod`, `remove_actor`, `remove_letter`, `remove_chart`, `remove_item`,
`revert_package`, `save_package`, `export_package`, `reset_quest`, `load_state`, and `import_model` when it
replaces a file. The client
must ask for them with `"confirm": true`, and then the game asks **you**: a window "Claude wants to: …"
with **Allow** and **Deny**. Nothing happens until you press **Allow**; without an answer within two
minutes the request is refused. Undo works for these changes as in the editor.

### Safety rules

- Only programs on your own computer can connect (the game listens on 127.0.0.1 only), and only with
  the token. Web pages cannot use it.
- It exists only when the game was started with `--editor` and you turned it on. It is not available on
  Android.
- Files: the client can read files inside the open mod's package and files you picked yourself in the
  editor (**Choose…** under Import or Export, or for an item's icon). For any other file (a model to
  import, an icon to copy in) the game asks you first and shows the full path.
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
| `new_mod` | `id`, `name`, `author`?, `description`?, `stages`?, `confirm` | create a new, empty mod, turn it on in the current mod profile and open it (needs `"confirm": true`; refused when the id is taken); `"stages": true` makes a mod of stages only, which needs no Story Notebook |
| `list_actors`, `list_models`, `list_scenes`, `list_quests` | `mod`? | the open mod's content |
| `get_actor`, `get_scene` | `id`, `mod`? | one entry as the manifest declares it |
| `list_actor_types` | | what `add_actor` places, and what appears without a restart |
| `list_game_objects` | `category`?, `query`? | the game's own objects `add_actor` places as `game:<id>`, with variants, limits and whether your game files have them, and island buildings it places as `piece:<id>` (`state`: preparing, ready or the reason it failed) |
| `add_actor` | `type`, `id`?, `at`? (`link`, `camera`, `position`), `position`?, `stage`?, `room`?, `angle`?, `snap`?, `model`?, `options`?, `mod`? | place a new actor (on the ground unless `snap` is false); `type` `game:<id>` places a game object (`options.variant`), `piece:<id>` an island building (made from your game files first: `state`, `note`) |
| `remove_actor` | `id`, `confirm`, `mod`? | remove an actor (needs `"confirm": true`) |
| `duplicate_actor` | `id`, `new_id`?, `offset`?, `mod`? | a copy next to it, with flags of its own |
| `place_actor` | `id`, `at` (`link`, `camera`, `ground`, `position`), `position`?, `snap`?, `mod`? | move an actor to Link, the camera, the ground below it or a position |
| `set_actor` | `id`, `property`, `value`, `mod`? | set one actor property (`null` removes it); `when` may name `quest` and `step`, `quests` (`[{quest, step}]`, all must hold), `item`, `time`, `flag` and `not_flag` (a flag id); `talks` is up to eight `{when, lines, then}`, the first that matches is said (one without `when` last; not together with `lines` or `then`) |
| `move_actor`, `rotate_actor`, `scale_actor` | `id`, `position` / `rotation` / `scale`, `mod`? | `{x, y, z}` |
| `set_dialogue` | `id`, `lines`, `mod`? | one to three lines |
| `add_route_point` | `id`, `at`? (`link`, `camera`, `position`), `position`?, `after`?, `snap`?, `mod`? | add a point to an actor's route (the first starts the route); `after` is the point it follows (0: first) |
| `move_route_point` | `id`, `point`, `position` or `at` (`link`, `camera`), `snap`?, `mod`? | move one route point (numbered from 1) |
| `remove_route_point` | `id`, `point`, `mod`? | take one point out (the route goes when too few are left) |
| `reorder_route_point` | `id`, `point`, `to`, `mod`? | give a route point another place in the order |
| `set_route` | `id`, `speed`?, `mode`? (`once`, `loop`, `back_and_forth`), `pauses`?, `walk`?, `idle`?, `mod`? | route settings; a resident's `speed` `null` is its own pace; `walk` and `idle` are a model's clips |
| `remove_route` | `id`, `mod`? | the actor stands where it is placed |
| `check_route` | `id`, `mod`? | the ground under each segment: no ground, a drop or a step too high marks it |
| `route_status` | `id`, `mod`? | a walking resident in the game: walking, skipped a blocked point, stopped or finished |
| `list_paths` | `mod`? | the mod's room paths, the actors that use each, and each one's state in the game (its number, whether its record is written) |
| `add_path` | `id`?, `closed`? (default `true`), `at`? (`link`, `camera`, `position`), `position`?, `stage`?, `room`?, `size`?, `points`?, `snap`?, `mod`? | a new room path: an area (a square of `size` units around the place, default 800) or a line (from the place ahead), or the `points` given |
| `add_path_point` | `id`, `at`?, `position`?, `stage`?, `room`?, `after`?, `snap`?, `mod`? | add a point to a path (after point `after`; 0: first; default: at the end) |
| `move_path_point` | `id`, `point`, `position` or `at`, `snap`?, `mod`? | move one point (numbered from 1) |
| `remove_path_point` | `id`, `point`, `mod`? | take one point out (an area keeps 3, a line 2) |
| `set_path_point` | `id`, `point`, `release`, `mod`? | mark a point: `release` (a Kargaroc lets its rider go there) |
| `set_path` | `id`, `closed`?, `new_id`?, `mod`? | an area or a line; a new id (the actors that use it follow) |
| `remove_path` | `id`, `mod`? | remove a path; refused while an actor uses it |
| `check_path` | `id`, `for`? (`warship`, `floormaster`), `mod`? | a warning check of a route: a warship's segments over land, a Floormaster's off the floor (never a refusal) |
| `path_inside` | `id`, `at`?, `position`?, `stage`?, `room`?, `mod`? | whether Link, the camera or a position is inside an area, by the game's own test |
| `model_clips` | `model`, `mod`? | a model's animation clips |
| `set_then` | `id`, `actions`, `mod`? | what happens after the dialogue (an empty list removes it): `give`, `take`, `quest_step`, `quest_done` (finishes a quest), `flag`, `scene`; `{"action": "give", "id": "chart:<id>"}` gives one of the mod's charts |
| `set_drops` | `id`, `drops`, `mod`? | what a prop or enemy drops |
| `preview_dialogue` | `id`?, `lines`?, `close`?, `mod`? | show lines in the game's message box (or hide it) |
| `pick` | `origin`, `direction`, `range`? | the first ground a ray meets |
| `ground` | `position` | the ground height below a point |
| `focus` | `id` or `position`, `distance`?, `yaw`?, `mod`? | fly the camera to an actor (a world object: its whole extent in view) or a place; `distance` back, looking along `yaw` degrees |
| `check_model` | `file` | check a `.glb` file |
| `import_model` | `file`, `id`?, `confirm`?, `mod`? | copy a `.glb` into the mod and add the model |
| `place_world_object` | `id`, `model`, `collision`, `stage`, `room`, `position`, `behaviour`?, `buoyancy`?, `drift`?, `anchor`?, `grapple_points`?, `distant`?, `shoreline`?, `chart_marker`?, `arrival`?, `rotation`?, `scale`?, `mod`? | place a static, floating or drifting object of the mod with collision (one Undo step) |
| `set_world_object` | `id`, `collision`?, `behaviour`?, `buoyancy`?, `drift`?, `anchor`?, `grapple_points`?, `distant`?, `shoreline`?, `chart_marker`?, `arrival`?, `mod`? | change a world object's collision, behaviour and motion together (`null` removes one) |
| `preview_collision`, `placement_conflicts` | `id`, `mod`? | a world object's collision as a wireframe, and what its place conflicts with |
| `prepare_collision` | `model`, `mod`? | prepare a static model's collision for placing world objects while the game runs |
| `check_water` | `kind` (`pond`, `river`, `waterfall`), `points`?, `level`?, `depth`?, `rim`?, `foam`?, `floor`?, `width`?, `shallow`?, `top`?, `bottom`?, `side`?, `river`?, `step`?, `id`?, `mod`? | what a body of water would be and every problem with it (errors and warnings), without changing anything: its place, triangles, file size, a river's levels and steps; `id` a body being changed |
| `add_pond` | `points`, `level`?, `depth`?, `rim`?, `foam`?, `floor`?, `id`?, `mod`? | a pond: its outline (world `{x, z}` points), its water's height (default: `depth` above the lowest ground along its shore, deep enough to swim in), the floor `depth` below (20–480, default 200), its rim `rim` above the water (0–300), coast foam at the shore, the floor colour `"#rrggbb"` (one Undo step) |
| `add_river` | `points`, `width`?, `depth`?, `shallow`?, `floor`?, `id`?, `mod`? | a river: its centre line in the flow's order, `width` 100–2000 (default 400), deep water `depth` above the bed, `shallow` per section (or one for all) |
| `add_waterfall` | `points` or `river` and `step`?, `top`?, `bottom`?, `side`?, `floor`?, `id`?, `mod`? | a waterfall: its top edge (two points; or a river's step), the top's and the ground's height (default: the ground there), `side` 1 or -1 (default: the lower side) |
| `edit_water` | `id`, and what changes of the above, `mod`? | build a body of water made with the tool again with those changes (one Undo step) |
| `get_water` | `id`, `mod`? | a body of water's parameters in world units, a river's steps and levels |
| `set_scene` | `id`, `property` (`trigger`, `steps`, `native`), `value`, `mod`? | (`null` removes a trigger or the native version) |
| `set_quest_step` | `quest`, `step`, `property`, `value`, `mod`? | `place` is `{x, z}` at sea, or inside a building `{stage, room, sea: {x, z}, point}` (or `x`, `y`, `z` instead of `point`); `available` may use `flag` and `not_flag` (a flag id or a list) with its `text` |
| `undo`, `redo`, `history` | | |
| `validate` | `mod`? | the checks, with the first problem |
| `check_package` | `mod`? | every problem, each with what it is about |
| `save_package` | `confirm`, `mod`? | write `manifest.json` (needs `"confirm": true`) |
| `revert_package` | `confirm`, `mod`? | forget unsaved changes (needs `"confirm": true`) |
| `export_package` | `path`, `confirm`, `mod`? | check, then write the package into a new or empty folder |
| `reload_room` | | enter the current room again |
| `list_locations` | `group`? | warp destinations (`Islands`, `Rooms`, `Mod stages`, `Quest`) |
| `warp` | `stage`, `room`, `point`?, `position`?, `angle`? | warp to a room's entry point or a position; a package's stage is `"stage:<mod>/<id>"` |
| `import_ground` | `file`, `as`?, `confirm`?, `mod`? | copy a room's `.glb` into the package (`assets/<name>.glb`), with its parts, the parts without UVs and its material names |
| `place_in_stage` | `stage`, `what`, `at`?, `position`?, `room`?, `id`?, `point`?, `to`?, `to_room`?, `to_point`?, `lock`?, `switch`?, `item`?, `actor`?, `variant`?, `size`?, `style`?, `height`?, `after`?, `angle`?, `mod`? | put a spawn, an exit box, a door, a return warp, a chest, an enemy or a pot of a stage where Link stands; standing in a game room, its entry |
| `list_stage_templates` | `sky`?, `door`? | the stages of your game files a stage can take as its template: rooms, sky, door kinds, whether their music can be borrowed |
| `list_stages` | `mod`? | the mod's stages ([mod stages](mod-stages.md)): rooms, grounds, kits, floors, spawn points, doors, entries, whether the game has it built |
| `add_stage` | `id`, `ground`, `template`?, `kind`?, `kit`?, `materials`?, `material`?, `spawn`?, `angle`?, `title`?, `music_like`?, `entrance`?, `entry`?, `mod`? | a new stage with one room whose ground is a `.glb` in the package |
| `add_room` | `stage`, `ground` (a `.glb` or `kit`), `kit`?, `floor`?, `materials`?, `material`?, `spawn`?, `angle`?, `mod`? | one more room |
| `set_stage_field` | `stage`, `field`, `value`, `mod`? | one field of a stage by its path (`map`, `entrance`, `doors`, `joins`, `entries`, `warps`, `rooms/1/exits`, `rooms/0/actors`, ...); an index one past a list's end adds, `null` removes |
| `remove_stage` | `stage`, `confirm`, `mod`? | remove a stage |
| `check_stage` | `stage`?, `mod`? | the stages' problems as a list (`severity`, `stage`, `field`, `message`): rules, rooms that can't be reached with the keys given before each locked door, missing ground files, ground models without UVs or named materials |
| `rebuild_stage` | `stage`?, `mod`? | build the stages again from the open manifest and the package's files; the stage Link is in loads again where he stands |
| `reset_stage_save` | `stage`?, `confirm`, `mod`? | test mode: forget the stage flags of the loaded Quest Log (chests, doors, keys, map, compass) |
| `warp_to` | `location` | warp to a `list_locations` entry |
| `warp_back` | | back to before the last warp |
| `read_state` | | Link's place, the camera, test mode, unsaved changes, the open mod's quest steps and flags |
| `fly_camera` | `on`, `place_link`? | the free camera on or off |
| `play` | `from`? (`camera`, `link`), `restore`? | play as Link (test mode on) |
| `stop` | | back to the editor view |
| `test_mode` | `on` | test mode |
| `reset_quest` | `confirm`, `mod`? | test mode: reset the mod's quest progress and flags, its letters, and the charts and items Link got from it |
| `advance_days` | `days` (1–255) | test mode: move the game's calendar on, to try letters that wait some days |
| `save_state` | `slot` (1–3), `kind`? (`portable`, `full`) | test mode: save a test state |
| `load_state` | `slot` (1–3), `kind`? (`portable`, `full`), `confirm` | test mode: load a test state (replaces the current progress) |
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
| `list_villagers` | | the game's own villagers loaded now that quest talks can name: catalogue id, name, place, position |
| `pick_villager` | `origin`, `direction`, `range`? | the villager a ray meets first (clicking into the picture) |
| `add_villager_talk` | `who`, `lines`, `quest`?, `step`?, `title`?, `id`?, `text`?, `mod`? | a villager's quest talk: a new quest with `title`, a new last step of `quest`, or the talk of `step` (one undo step; the villager itself is not changed) |
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
| `demo_lanes` | `scene`, `step`, `mod`? | a demo step's lanes and their key frames, its events, its actors, and why the game would refuse it (`problem`) |
| `demo_pose` | `scene`, `step`, `frame`, `move`?, `mod`? | the demo at a frame (30 a second) without playing it: the camera and each actor's position, rotation and scale in the world; the fly camera shows the demo camera |
| `set_demo_key` | `scene`, `step`, `lane`, `frame`, `value`? or `from`? (`camera`, `link`), `curve`?, `mod`? | a key of a lane (`camera.eye`, `camera.target`, `camera.fovy`, `camera.roll`, `<actor>.position`, `<actor>.rotation`, `<actor>.scale`); added when none is at that frame |
| `move_demo_key` | `scene`, `step`, `lane`, `frame`, `to`, `mod`? | move a key to another frame |
| `remove_demo_key` | `scene`, `step`, `lane`, `frame`, `mod`? | remove a key (the lane goes with its last key) |
| `set_demo_curve` | `scene`, `step`, `lane`, `curve`, `mod`? | a lane's curve: `linear`, `smooth`, `bspline` or `step` |
| `add_demo_event` | `scene`, `step`, `list`, `actor`?, `event`, `mod`? | an event in frame order: `clips`, `modes`, `data` (an actor's), `messages`, `sounds`, `particles`, `attach`, `detach`, `time`, `weather` |
| `edit_demo_event` | `scene`, `step`, `list`, `actor`?, `index`, `fields`, `mod`? | fields of an event (`null` removes one) |
| `remove_demo_event` | `scene`, `step`, `list`, `actor`?, `index`, `mod`? | remove an event |
| `add_demo_actor` | `scene`, `step`, `actor`, `mod`? | an actor of the demo: `{"id", and "actor" (a game actor or Link), "model" {archive, file} or "character" (one of the mod's placed actors)}` |
| `remove_demo_actor` | `scene`, `step`, `id`, `mod`? | remove an actor (refused while a sound, particle or attachment names it) |
| `demo_choices` | `what` (`sounds`, `particles`, `actors`, `link_modes`, `archives`, `files`, `joints`), `query`?, `archive`? | names for a demo's pickers: sound effects and particles (id and a readable name), actors (the game's name, a readable name, its archive, a suggested id), Link's modes, the game's object archives, an archive's files or its models' joints |
| `play_sound` | `id` | play a sound effect with the game's own sound playback |
| `set_scene_choice` | `scene`, `step`, `option`?, `label`?, `next`?, `default`?, `mod`? | a choice's labels, where each option leads, the default answer |
| `add_walk_point`, `set_walk_point`, `remove_walk_point` | `scene`, `step`, `at`? / `point`, `from`? (`camera`, `link`, `position`), `position`?, `mod`? | the points of a walk |
| `native_scene` | `scene`, `from_steps`?, `value`?, `mod`? | the scene's native version: made from its steps, set or removed; whether it would play |
| `preview_scene` | `scene`, `from`?, `to`?, `restore`?, `mod`? | test mode: play the scene (or some of its steps) in the game |
| `skip_scene`, `stop_scene` | | skip the scene that plays; stop the preview and go back to the editor |
| `scene_state` | | whether a preview plays, which scene and step |
| `list_letters`, `list_charts`, `list_items` | `mod`? | the mod's letters on their own, charts or items, each with what uses it (`used_by`) |
| `add_letter` | `sender`, `text`, `id`?, `encloses`?, `when`?, `after_days`?, `mod`? | a letter on its own; `encloses` lists `"chart:<id>"` and `"item:<id>"`; `when` waits for `{quest, step}`, `{quest, done: true}`, `quests`, `flag`, `not_flag`; `after_days` waits that many game days more |
| `set_letter` | `id`, `fields`, `mod`? | its `sender`, `text`, `encloses`, `when` or `after_days` (`null` removes `encloses`, `when` or `after_days`) |
| `add_chart` | `name`, `place`? or `at`? (`link`, `camera`), `id`?, `description`?, `item`?, `mod`? | a treasure chart; `place` is `{x, z}` at sea; `item` is `"item:<id>"` or `game:purple_rupee`, `game:orange_rupee`, `game:silver_rupee`, `game:heart_piece`, `game:heart_container` |
| `set_chart` | `id`, `fields`?, `at`?, `mod`? | its `name`, `description`, `place`, `item` (`null` removes `description` or `item`); `at` moves the X to Link or the camera |
| `add_item` | `name`, `description`, `icon`? or `icon_file`?, `id`?, `stack`?, `use`?, `held_model`?, `held_frames`?, `hold_up`?, `mod`? | an item; `icon` is a PNG in the package, `icon_file` a PNG copied into its `icons` folder; `use` is `{action: show}`, `{action: open_notebook}`, `{action: quest_step, quest, step}` or `{action: event}`; `hold_up: true` shows `held_model` over Link's head when he gets it |
| `set_item` | `id`, `fields`?, `icon_file`?, `mod`? | its fields (`null` removes the optional ones); `icon_file` copies a new icon in |
| `remove_letter`, `remove_chart`, `remove_item` | `id`, `confirm`, `mod`? | remove one (needs `"confirm": true`; a chart or item still in use is refused, with the list of uses) |
