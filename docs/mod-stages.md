# Mod stages

A mod stage is a new place for the game: a cave or a dungeon of its own, with its own rooms, that Link
walks into from a room of the game and leaves through an exit. Your package describes the stage and ships
the rooms' ground models; the port builds the stage on the player's machine from the player's own game
files when the game starts, and the game loads it like any of its own stages.

Mod stages are experimental. A stage has rooms built from your models or taken from the game's rooms,
spawn points, exits, doors (plain, small-key, boss and shutter doors), chests with small keys, the map, the
compass and the boss key, enemies and pots, return warps, ways in from the game's rooms, save flags of its
own and a dungeon map.

A larger example, a three-room dungeon with a locked door, a key in a chest and an enemy, is
`examples/guest-mods/mod-stage-dungeon` in the repository.

## A first stage

This package adds a chamber behind a hidden passage of the lava cave on the Great Sea's square 34. Its exit
leads back to the sea outside the cave (`examples/guest-mods/mod-stage-cave` in the repository):

```json
{
  "format_version": 1,
  "id": "mod-stage-cave",
  "name": "A room behind the lava cave",
  "version": "0.1.0",
  "game_id": "wwhd",
  "kind": "content",
  "content_dir": "content",
  "experimental": true,
  "stages": [
    {
      "id": "chamber",
      "template": "Cave01",
      "kind": "cave",
      "music_like": "Cave01",
      "entrance": {"stage": "sea", "room": 34, "point": 1},
      "rooms": [
        {
          "room": 0,
          "kit": 0,
          "ground": "assets/room.glb",
          "material": "lambert2_v",
          "materials": {"wall": "E3mR02roomKB_v", "pillar": "E3mR02YK_v"},
          "spawns": [{"point": 0, "position": {"x": -1000, "y": 0, "z": 0}, "angle": 90}],
          "exits": [
            {
              "id": "to-sea",
              "box": {"min": {"x": 900, "y": -100, "z": -800}, "max": {"x": 1200, "y": 100, "z": 800}},
              "to": {"stage": "sea", "room": 34, "point": 1}
            }
          ]
        }
      ],
      "entries": [
        {
          "id": "hidden-passage",
          "stage": "Cave01",
          "room": 0,
          "position": {"x": -1300, "y": -200, "z": -900},
          "size": {"x": 200, "y": 200, "z": 200},
          "to": {"room": 0, "point": 0}
        }
      ]
    }
  ]
}
```

Link walks into the entry's box in the lava cave and arrives at spawn point 0 of the chamber. Walking onto
the floor at the chamber's far end takes him back to the sea.

A package with stages needs no other package: stages are not story content.

## The stage

| Field | |
|---|---|
| `id` | The package's name for the stage (lowercase letters, digits, `-`, `_`, `.`; up to 32). The game gets a name of its own for it from the port; you never need it. |
| `template` | A stage of the game the new stage takes its lighting, sky, camera, door models and room kits from. Its rooms are not part of the new stage. See [Choosing a template](#choosing-a-template). |
| `kind` | `cave` (the default) or `dungeon`. |
| `save_group` | Optional: stages with the same `save_group` share their save flags, keys and dungeon items (a dungeon and its boss stage, say). Default: the stage's own id. |
| `light` | Optional: `{"stage": "sea", "room": 44}` lights the stage as that place of the game is lit: its colours through the day, its sky colours and its lights, following the game's clock (see [Light](#light)). Without it the stage has the template's light. |
| `music_like` | Optional: a stage of the game whose music and ambient sounds the stage plays. The boss rooms' music follows the story and can't be borrowed. Without it the stage is silent apart from battle music. |
| `entrance` | A place of the game: `stage`, `room` and spawn `point`. A save made in the stage continues here when the package is not installed (see [Saves](#saves)). Usually the spot in front of the way in. |
| `rooms` | 1 to 16 rooms, numbered 0, 1, 2, ... in order. |
| `doors` | Optional, up to 16: the game's doors between two rooms (see [Doors](#doors)). |
| `joins` | Optional: pairs of rooms joined by an open passage without a door, `[[2, 3]]`. Both rooms are kept loaded and drawn while Link is in either (a room behind a door is loaded but not drawn until the door opens). |
| `entries` | Optional, up to 4: boxes in rooms of the game that take Link into the stage. |
| `warps` | Optional, up to 4: return warps (see [Return warps](#return-warps)). |
| `shaders` | Optional: a shader template (`.wwht`) in the package, so the stage's shaders are prepared before play (see [Shaders](#shaders)). |
| `map` | Optional: `{"title": "Hidden Keep"}` gives the stage a dungeon map (see [The dungeon map](#the-dungeon-map)); the title (up to 40 characters) shows above its floors. Without it the map screen shows the sea chart, as in the game's caves. |

Every room must be reachable: from where Link comes in (the entries' rooms, room 0 without entries) through
doors, joins and exits. Check names a room that can't be reached.

A package declares up to 4 stages.

### Choosing a template

The editor's `list_stage_templates` command (and the template list of **Stages > New stage**) lists the stages
of your own game files: their rooms, whether they have a **sky**, the kinds of door they have models of
(`knob`, `boss`, `small_key`, `shutter`) and whether their music can be borrowed. `"sky": true` lists the
open-air ones; `"door": "boss"` the ones with a boss door.

- **A stage in the open** (an island in the sky, a mountain top) needs a template with a sky. Its rooms show
  the sky wherever the ground model leaves it open. For the light of a place of the game (the sea's, through
  the day), add `"light"` (see [Light](#light)). The Tower of the Gods' outside stage (`GTower`) has a day
  sky and the knob and boss doors; Dragon Roost's mountain path (`Adanmae`) follows the time of day; the
  Forsaken Fortress tower (`M2tower`) is always night. Twenty-one stages have both a sky and a boss door.
- **A cave or dungeon** needs a template with the doors it uses: Cave01, M_NewD2, kindan and Siren have all
  four.
- A room's `kit` can be a room of any stage; the template's own rooms keep the template's lighting best.

### Light

A stage is lit as its template is: the template's colour sets for each time of day, its palettes and its sky
colours. Some templates have a light of their own whatever the hour (the Tower of the Gods' outside stage is
always a dusky blue). `"light"` takes the light from another place of the game instead: the stage's colour sets,
palettes, sky colours and lights are that stage's, and every room is lit as that stage's room `room` (a square
of the Great Sea: `{"stage": "sea", "room": 44}` is Outset Island's). The stage then follows the game's clock as
that place does: bright at noon, golden in the evening, dark at night. The template still gives the sky's models,
the camera and the doors.

A stage in the open reached from the sea (a hall under an island, a mountain top) can take `"light": {"stage":
"sea", "room": 44}`, so it has the same light as the sea round Outset that Link came from.

### Rooms

| Field | |
|---|---|
| `room` | The room's number: 0 for the first, then 1, 2, ... |
| `kit` | A room of the game whose materials, textures and light the room uses: a room number of the template, or `{"stage": "M_NewD2", "room": 3}` for a room of any stage. Default: the template's room of the room's own number. |
| `ground` | The room's model: a `.glb` file of the package (see [The ground model](#the-ground-model)), or `"kit"`: the kit room itself, its models and its collision, as the game has it. |
| `material` | Optional: the kit room's material the model is drawn with. Default: the kit room's main material, the one most of its triangles use. |
| `materials` | Optional: the model's material names mapped to kit materials, for a model with several materials. A model material that is also a kit material's name is drawn with that one. |
| `spawns` | 1 to 8 spawn points: `point` (0 to 99, unique in the stage), `position`, `angle` in degrees (0 faces +z, 90 faces +x) and `start`: `stand` (the default) or `fall` (Link drops in from above). |
| `exits` | Up to 8 exits: an `id`, a `box` (`min` and `max` corners) and the place it leads `to`. |
| `actors` | Up to 16 of the game's actors (see [Actors](#actors)). A chest or darknut can have `then` (see [A quest through the stage](#a-quest-through-the-stage)). |
| `floor` | Optional: the dungeon map's floor the room is on: 1 (1F, the default) to 5, or -1 (B1) to -5. |

An exit's box marks floor: the ground's floor triangles whose middle lies in the box take Link to the exit's
place when he steps on them, the way the game's own caves are left. Walls and ceilings in the box don't
count, and an exit whose box covers no floor is an error. Make the box a little taller than the floor so
uneven ground stays inside.

A place `to` is:

- a place of the game: `{"stage": "sea", "room": 34, "point": 1}`;
- a spawn of the same stage: `{"room": 1, "point": 3}` (no `stage`);
- a spawn of another of the package's stages: `{"stage": "stage:<id>", "room": 0, "point": 0}`.

A kit room used as it is (`"ground": "kit"`) keeps its floors, walls and its surfaces (lava burns, water
is swimmable); its own ways out of its stage are removed, and the room's `exits` boxes work on its floor as
on a model's. Its actors are not taken over: place the ones you want with `actors`. A kit room of another
stage than the template is lit by the template's first room's light.

### Doors

```json
{"position": {"x": -1000, "y": 0, "z": 0}, "angle": 90, "rooms": [0, 1], "lock": "small_key"}
```

A door stands at `position`, turned by `angle`, between the two `rooms` it joins. Leave an opening in both
rooms' ground models where the door stands (the door is 3 m wide and 3 m high). The port lists each room's
door neighbours in the stage's room table, which the door needs to open. `lock`:

- `none` (the default): the door opens when Link walks up to it and presses A.
- `small_key`: the door is locked; a small key opens it for good (the key is used up).
- `boss`: the boss door; the stage's boss key opens it.
- `shutter`: bars close the door until switch `switch` (0 to 63) of the stage is on.

The template's stage must have the door's model: Cave01, M_NewD2, kindan and Siren have all four; a stage with a door its template lacks is left out with a message.
A small-key door needs a small key in a chest of the stage's save group, and a boss door the boss key; Check
says so when one is missing. The key counter shows in a stage with small keys.

### Actors

```json
{"id": "key", "actor": "chest", "item": "small_key", "position": {"x": -300, "y": -200, "z": 0}, "angle": 270}
```

A room's `actors` are the game's own actors, created by the game when the room loads:

| `actor` | |
|---|---|
| `chest` | `item`: `small_key` (in a small chest), `map` or `compass` (in a larger one), `boss_key` or `heart_container` (in the big chest). Link opens it from its front (the side `angle` faces). Each chest opens once per save. |
| `chuchu` | `variant` `green` (default), `red`, `blue`, `yellow` or `black`. |
| `bokoblin` | `variant` `stick` (default), `shield` or `machete`. |
| `keese` | `variant` `normal` (default) or `fire`. |
| `darknut` | `variant` `darknut` (default), `shield`, `helmet` or `mighty`. |
| `pot`, `big_pot` | A pot Link can lift and throw. |
| `tree` | The game's own trees, with their own collision, sway and cutting: `variant` `small` (default; one small tree), `small_3` or `small_5` (a group), `bushy`, `large` (the big leafy tree; `"scale"` 0.5 to 1.5 sizes it) or `palm`. |
| `lava` | The game's lava: its glowing, moving surface, drawn at the actor's height over an area `size` (`{"x": ..., "z": ...}`, game units, 100 to 25500 each, centred on its position, turned by `angle`). It is only the picture: give the room's model a `col_lava` surface at the same height (see [Lava](#lava)). |
| `heat_shimmer` | The heat haze over the game's lava (Dragon Roost's): the picture shimmers while Link is in the room. One per room is enough. |

A heart container from a mod's chest raises Link's maximum hearts in the player's real save file, as the
game's own do, and turning the mod off does not take it back. Give one deliberately: one per boss room at
most, and say so in the package's description.

`id` names the actor in the manifest (unique in the room), `position` and `angle` (degrees about the vertical
axis) place it. The chests are numbered in the order they appear in the manifest, per save group, and a save
remembers them by number: when you change a released package, add new chests after the old ones.

### Return warps

```json
"warps": [{"id": "home", "room": 2, "position": {"x": -2450, "y": -200, "z": 1500}, "size": {"x": 200, "y": 200, "z": 200}}]
```

A warp is a box in a room of the stage: when Link walks into it, the warp light takes him to `to` (a place,
as for an exit; by default the stage's `entrance`). Like an entry, it works once Link has been outside it.

`"after": "heart"` makes the warp wait for a chest of the stage (by its id): until that chest is open, walking
into the box does nothing. The game's log says when a warp waits. This is the boss room's way out (see
[Boss rooms](#boss-rooms)). The warp itself is invisible: give its spot a mark in the ground model (a
raised pad, a different material).

### Entries

An entry is a box in a room of the game: `stage`, `room`, `position` (the middle of the box's bottom) and
`size` (10 to 5000 units each way). When Link walks into it, the game takes him to the stage's spawn `to`
(`room` and `point`). An entry only works once Link has been outside its box since he came into the room, so
arriving inside one never sends him away again at once.

`"style": "updraft"` makes the entry a column of wind: the wind lifts Link `height` units (200 to 5000,
default 1500) before the stage takes over, with a fade. Use a tall box (`{"x": 400, "y": 800, "z": 400}`) at
a spot Link can walk or swim into: off an island's shore, at the foot of a waterfall, on your own island
(an entry can be in any room of the game, including a sea room your package builds on).

```json
{"id": "updraft", "stage": "sea", "room": 44, "style": "updraft", "height": 1500,
 "position": {"x": -194845, "y": 0, "z": 315400}, "size": {"x": 400, "y": 800, "z": 400}, "to": {"room": 0, "point": 0}}
```

`"style": "hole"` makes the entry a hole Link falls into, as into the game's holes into caves: its box is the
shaft under an opening in the ground (an opening in a world object's model and its collision, say), below the
ground Link walks on, so only a fall reaches it. The screen fades as he falls, and he arrives at a spawn with
`"start": "fall"`, dropping in from above (Check refuses a hole that leads to another spawn). Leave the shaft open
at the bottom: a fall that doesn't take the hole (the game busy with a scene) goes on through the dark and out
of the shaft rather than leaving Link stuck in it.

```json
{"id": "hole", "stage": "sea", "room": 44, "style": "hole", "position": {"x": -200300, "y": 6750, "z": 301900},
 "size": {"x": 150, "y": 220, "z": 150}, "to": {"room": 0, "point": 0}}
```

### Getting there by a scene

A talk's or a scene's `then` can take Link into one of the package's own stages:
`{"action": "stage", "id": "sky", "room": 0, "point": 0}` (`room` and `point` default to 0). With a scene's
demo step that carries Link (`attach`, [mod SDK v3](mod-sdk-v3.md)), the Helmaroc King can fly him there:
the scene's `end.then` holds the stage action.

A scene without words needs nothing else: a package of stages with a scene of camera, demo, fade and
wait steps, an `area` or `room` trigger and only `stage` and `scene` actions stands on its own. Text
(`say`, `choice`, a demo's `messages`), sounds, music, talks, quests and the other actions are story
content and need the Story Notebook.

For example, a scene can start when Link walks along a pier: the Helmaroc King swoops down, closes its
talons around him with the game's own grab animation (the attach's `blend` lifts him off the planks without a
jump) and climbs with him; the scene's end takes him into a stage spawn with `"start": "fall"`, so he drops
into it from above. Use `{"actor": "Dk", "spawn": "always", "params": 2}` for such a bird: the demo's own
Helmaroc King, without ground collision. Outset has a Helmaroc King of its own, waiting hidden for its story
event; without `"spawn": "always"` the demo would move that one, which doesn't show.

A far-off view of a place, such as an island in the sky, can be a world object of the package
([world objects](world-objects.md)) shown only until a flag is set (`"when": {"not_flag": "…"}`). World
objects are story content, so such a package needs the Story Notebook.

### Boss rooms

A boss room is a room behind the boss door, with something to beat, a reward and a way out:

1. **The boss door**: a door with `"lock": "boss"` into the room, and a chest with `boss_key` somewhere
   before it (Check says when the key comes after its door).
2. **The fight**: the game's enemies (a darknut is a strong one). There is no boss actor yet.
3. **The reward**: a chest with `heart_container` (see the note under [Actors](#actors)).
4. **The way out**: a return warp with `"after"` naming that chest, so it opens once the reward is taken.

Rooms can take materials from different kit rooms: a hall drawn with Hyrule Castle's stone and an open-air
arena with the Tower of the Gods' paving, say. Two rooms drawn as one place (a ground and the ruins on it, each
with its own kit) can be joined with `joins`, so both are drawn together; one of them can leave the collision
to the other (a small floor far below) so Link never leaves the first room there.

### A quest through the stage

A stage's chests, darknuts, entries and warps can move a quest of the package on, with `then`: the same
story actions as a talk's or a scene's (`quest_step`, `quest_done`, `flag`, `scene`):

| On | `then` runs when |
|---|---|
| a chest (`"actor": "chest"`) | Link opens it |
| a darknut (`"actor": "darknut"`) | it is defeated (by the sword, or falling off the stage) |
| an entry | Link takes it (an updraft: when the wind starts to lift him) |
| a return warp | Link steps into it, as the warp light takes him |

```json
{"id": "heart", "actor": "chest", "item": "heart_container", "position": {"x": 0, "y": 200, "z": 4400},
 "then": [{"action": "quest_step", "id": "sky", "step": 5}]}
```

Quest steps only go forward, so a chest opened before the step it belongs to (the heart container before
the guardian falls) moves the quest past the steps in between, and a step reached twice changes nothing.
A defeated darknut stays defeated: the port gives it one of the save group's switches, which the game sets
when it falls; the same numbers as the locked doors are used, up to 64 of both in a save group.

A quest step's `place` can be inside one of the package's own stages: `{"stage": "stage:sky", "room": 1,
"sea": {"x": -194790, "z": 314530}, "x": 0, "y": 200, "z": 3600}`. The sea chart's ring goes to the `sea`
place (the way in), and the editor's Quest warp list takes Link into the stage there.

**The Story Notebook as an optional dependency.** A package of stages can bring its quest without needing
the Story Notebook: `"dependencies": [{"id": "story-notebook", "minimum_version": "0.0.0", "optional": true}]`
(the editor's `set_package_info` with `"field": "story_notebook", "value": "optional"`). With the
notebook installed, the quest, its letter and every story action work as in any story package. Without it,
the package's stages and its wordless scenes play just the same, its quest and letters stay out, and its
story actions are skipped (a line in the game's log says so); `stage` and `scene` actions still run. Only
the Story Notebook can be an optional dependency.

A package whose scene or quest uses world objects or story actions needs the Story Notebook (a plain
dependency, `set_package_info` `"value": "required"`). A quest through a stage might go: a letter sends Link
to a pier, a scene or a hole takes him into the stage, the boss key's chest moves the quest to the guardian,
the darknut's defeat to the heart container, the big chest to the way home, and the return warp finishes it
with its reward, given once.

## The ground model

A room's ground model is one `.glb` file (glTF 2.0, up to 16 MiB). As for [world objects](world-objects.md),
one metre in the file is 100 game units. The room's coordinates are the stage's: put the room where you
want it around the origin, and make the spawns, exits and doors fit it.

- **Visible meshes** are drawn with the kit room's materials and textures; your model's texture coordinates
  (and vertex colours, when it has them) are used, its own textures are not. Up to 20000 visible triangles per
  room, with materials from up to four of the kit room's models.
- **Collision**: meshes or nodes whose name ends in `_col` are the room's collision and are not drawn. Their
  material names choose the surface, as for world objects: `col_stone`, `col_grass`, `col_wood`, `col_sand`,
  `col_metal`, `col_wall`, `col_objects`, `col_climb`, `col_hookshot`, `col_water` (swimmable), `col_shallow`,
  `col_waterfall`, `col_ice`, `col_void`, `col_lava` (see [Lava](#lava)). Without a `_col` mesh, the visible triangles are the collision, on
  stone (or on the surface a `col_` material name gives). Up to 20000 collision triangles per room.
- Faces are seen from the side their normal points to; for a room seen from inside, the walls and ceiling
  face inwards.
- **Give each kind of surface its own named material** (`floor`, `wall`, `ceiling`, ...) and map the names to
  kit materials with `materials`. With one material, or materials without names, every surface is drawn
  the same.
- **Unwrap every visible mesh** (UVs; in Blender **UV → Smart UV Project** or **Cube Projection**). A surface
  without texture coordinates shows a single colour of its texture. The editor's `check_stage` reports both.

### Lava

A `col_lava` surface in the room's collision is lava, as in the game's lava rooms: Link burns, jumps out, loses
half a heart and starts again where he came into the room; a bomb that falls in explodes, a pot breaks, other
things are gone. Model it as upward-facing faces at the lava's level, lower than the floor around it, so that
Link walks off the edge into it. The surface is no ground: the lava gets a bottom 1 metre (100 units) below it,
where things sink to. Keep that space free, and count lava's triangles twice towards the room's 20000.

The game draws its lava with an actor of its own, not with the room's model. Place a `lava` actor at the
surface's height, as large as the pool (it may be larger: the room's floors and walls hide what reaches under
them), and add `heat_shimmer` for the heat haze:

```json
"actors": [
  {"id": "lava", "actor": "lava", "position": {"x": 0, "y": -50, "z": 400}, "size": {"x": 2000, "z": 1200}},
  {"id": "heat", "actor": "heat_shimmer", "position": {"x": 0, "y": 0, "z": 0}}
]
```

The room's lava faces can also be drawn with a lava material of a kit room (under the `lava` actor's surface, or
on their own for an even glow). These are the kit materials of the game's lava (the material name, and kit rooms
that have it):

| Material | Kit rooms | |
|---|---|---|
| `E3mYOGAN_v` | `Cave01` rooms 0 and 1 (the lava cave) | the lava cave's lava: an even orange glow |
| `R00_1015_3_lambert6_v` | `MiniKaz` room 0 (inside Fire Mountain) | Fire Mountain's lava: an even orange glow |
| `E3mYOGAN_v` | `M_NewD2` rooms 2, 7, 8, 10, 11, 12, 13 and 15 (Dragon Roost Cavern), `Adanmae` room 0 (Dragon Roost's mountain path) | the moving glow along the lava's rocks: the game lights it by where it stands, so on a model of your own it glows only in parts |

Map them with `materials`, for example `"materials": {"col_lava": "E3mYOGAN_v"}` for faces whose material is
`col_lava` (they are then drawn and are the lava's collision). A kit room used as it is (`"ground": "kit"`) keeps
its own lava; place `lava` and `heat_shimmer` actors in it as you would in a room of your own (its own are not
taken over).

### Kit materials

The materials are the kit room's own: their names are those of the kit room's model. The editor's
`list_kit_materials` command lists them for any room of the game (`{"stage": "sea", "room": 44}` is
Outset Island): each one's textures, how many of the room's triangles use it and the texture coordinates
the room gives it. When a name is wrong, the game's log also says which ones the kit room has (`the kit
room has no material "..." (it has: ...)`).

Pick materials by what their textures show, not by their names: a name like `test_lambert8_v` says nothing,
and a kit room has backdrop materials (distant mountains, clouds) that look smeared and cut out on the
ground. Without `material` and `materials`, the model is drawn with the kit room's main material, the one
most of its triangles use.

**Atlases.** Many of the game's textures hold several surfaces in bands: Outset's ground texture has grass
at the top, a round clearing, an earth path strip and sand, one under the other. The room's own models
give such a material texture coordinates that repeat along one axis (`uv_min` and `uv_max` beyond 0..1)
and stay inside one band along the other (inside 0..1). A ground model that tiles the whole texture shows
all the bands at once. Keep each surface's coordinates inside its band on that axis, and repeat only along
the other; where a surface must repeat both ways, walk the band forwards and back (0 to 1 to 0) with
vertices closer together than one band.

A kit material keeps the game's drawing of it: an opaque one stays opaque, a cut-out stays a cut-out and a blended
one stays see-through (`list_kit_materials` does not show which is which; the export tool's GLB marks it in each
material's `alphaMode`). The game's falling water (Forest Haven's, the castle's, the sea's islands') is a cut-out,
drawn white with moving streaks; for water that shows the sky through it, use a blended water material of the kit,
such as Forest Haven's spray (`SC_00_m_2_`) or coast foam (`SC_00_m`). A waterfall can be a thin stream of the falling water
with a curtain of the spray over it.

Give the model **vertex colours** (glTF `COLOR_0`) to shade it as the game's rooms are: the kit materials
multiply their texture by them, and the game's own rooms darken corners, the foot of cliffs and the
undersides this way. Without vertex colours every surface is drawn at full brightness.

## The dungeon map

A stage with `map` has a dungeon map on the map screen (the GamePad's MAP, or the menu's MAP tab with the
Pro Controller). The port draws it from the rooms' floors, seen from above, on the game's dungeon map page:

- the rooms Link has been in, and the room he is in, with his place and the way he faces;
- with the stage's dungeon map (a chest with `"item": "map"`): every room, the ones he hasn't been in lighter;
- with the compass: the chests (gold while closed, grey once open) and the boss door (a red mark);
- the title, and each floor's name on the floor buttons, the floor Link is on marked.

The map shows the floor Link is on; the floor buttons don't switch floors yet. Higher ground shows lighter
and pits and basins darker, and a room's edge is drawn where its floor ends.

## Saves

The game's save file never names a mod stage. When the player saves in the stage, the save file stores the
stage's `entrance`, so the file always loads, with or without the package. Where in the stage the save was
made is kept in the port's save area for mods (with story progress, after the game's own bytes): loading
that Quest Log while the package is installed continues in the stage, at the spawn Link came in by.
Without the package, or when the stage has changed so that spawn is gone, it continues at the entrance.
Continuing inside the stage needs the game built with code-mod support (the same as story progress);
otherwise the Quest Log continues at the entrance.

Each save group keeps its own flags, as each of the game's dungeons does: which chests are open and which
doors unlocked, the switches, the rooms Link has seen, the small keys and the map, compass and boss key. They
are kept in the port's save area for mods and saved with the game's saves; the save file's own table of
stage flags is never written for a mod stage. Without code-mod support they are kept until the game is
closed. A save state made in the stage carries them; a portable save state (the editor's) keeps the stage
flags the game has when it is loaded.

A **Game Over** in the stage works as in the game's dungeons. **Continue** starts Link again at the
stage's start (the spawn he came in by). **Save** on the Game Over screen writes the save file with the
stage's `entrance` and keeps the stage's flags, and loading that Quest Log later continues in the stage, as
any save made there. **To Title Screen** without saving leaves the save file as it was.

## Save states

Save states made in a mod stage load only while the package that makes it is installed; otherwise the
state is refused with a message and nothing changes. The slot list names the stage by its id and package.

## Building

The port builds each stage when the game starts, from the player's game files (the template's stage
archive and the kit rooms) and your models, and keeps the result in the mod manager's data folder. A stage
is built again only when the package, the player's game files or the port's builder change; otherwise the
kept one is used. A stage the player's game can't make (another game version without the template, a
wrong kit material, an exit box that covers no floor) is left out with the reason in the game's log; the
rest of the package works.

Nothing from the game goes into a package: ship your models, the manifest and your own files only.

## Editor

With `--editor`, the warp list (`list_locations`) has a group **Mod stages** with every room of the
installed packages' stages, named by the stage's id and package. The `warp` command takes a package's stage
as `"stage": "stage:<package id>/<stage id>"`, for example
`{"command": "warp", "params": {"stage": "stage:mod-stage-cave/chamber", "room": 0, "point": 0}}`.

The editor's **Stages** tab (in the Outliner and under **Settings → Editor**) works on the open mod's stages:

- **New stage**: a room model (`.glb`), a template and an optional map title; **Create, build and warp in**
  copies the model into the mod, adds the stage, builds it and takes Link to its first spawn.
- Each stage's rooms, **Add room** (another model), **Rebuild**, **Warp in** and **Check** (the problems, as
  a list).
- **Lava**: **Lava floor here** puts the game's lava floor at Link's feet (its size in the fields next to the
  button) and **Heat shimmer here** the heat haze. In the picture a lava floor shows as an orange outline with
  two round marks: the middle one moves it, the corner one sizes it (letting go is one undo step). Selecting it
  shows its size in the Stages tab, and **Remove this lava floor**. Give the room's model a `col_lava` surface at
  the same height and **Rebuild**.
- **Where Link stands**: inside the stage, a spawn, a return warp, an exit (to the sea outside the lava cave
  or to a spawn of the stage), a door to another room with its lock, a chest with its item, an enemy or a
  pot. In a room of the game, the stage's entry.
- In the editor view, the stage's spawns, exits, doors, chests and warps show as boxes with their names. A
  click on an item's round mark shows its move handle; letting go of the handle moves it (one undo step).
  **Rebuild** shows the change in the game.

The same steps are commands, for scripts and MCP clients: `list_stages`, `import_ground`, `add_stage`,
`add_room`, `set_stage_field`, `place_in_stage`, `remove_stage`, `check_stage`, `rebuild_stage` and
`reset_stage_save` ([the list](mod-editor.md#commands)). `rebuild_stage` while Link is in the stage loads it
again where he stands. [Building a dungeon with an AI assistant](mod-stages-ai.md) goes through a whole
stage built this way, with Blender.

## Shaders

Your stage's rooms draw the kit's materials with your models, which the game draws with shader variants of
their own. The game compiles each the first time it draws it, so the first visit to a stage stutters a
little, unless the stage ships a **shader template** that setup prepares with the game's own shaders:

1. Play through the stage once (the editor's warp is enough), so every room has been drawn.
2. Close the game and run `python3 tools/shaderprep.py stage-template assets/stage.wwht` in the game's
   folder: it reads your shader caches (as `export` does; `--from DIR` names their folder) and keeps only what
   they have beyond the release's template. Play with a fresh cache, or with a cache from vanilla play only,
   so the file holds the stage's shaders and little else.
3. Name it in the stage: `"shaders": "assets/stage.wwht"`.

The file holds only hashes and render settings: no shader code and nothing of the game's files, so it may be
part of a package. Setup's shader preparation includes the templates of the enabled packages. For a package
installed after setup, run setup and choose **Repair** (the game's **Rebuild shader cache** asks for the same);
until then the stage's shaders compile during play. The game's log says whether a stage's template is
prepared yet.

## Limits

- The dungeon map shows only the floor Link is on, and the map can't be zoomed or moved by touch.
- Only the actors listed under [Actors](#actors); no boss actor yet, and no actors of a kit room's own.
- The stage's rooms are lit by the template (or by the stage `light` names); a sky only comes with a template that has one.
- A return warp has no light of its own; mark its spot in the ground model.
- Ballad of Gales and the blue warp don't lead to or from a mod stage.

Corner-map guest mods can reuse a mod dungeon's current-room image, world bounds
and compass markers through `wwhd_dungeon_room_get`; see
[the SDK guide](mod-sdk-v2.md#current-room-maps-for-mod-dungeons). It uses the map
already supplied by the stage, so stage authors need no additional artwork.
