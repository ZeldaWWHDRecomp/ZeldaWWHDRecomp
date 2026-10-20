# World objects

World objects let a mod add walkable ground, solid obstacles and floating bodies using its
own models. Link and the ship use the game's background collision. Ground
materials select the game's footstep sounds and surface effects.

Static objects stay in place. Floating bodies follow the sea, and drifting bodies
also move with wind or a current. Collision materials can make walls climbable
and surfaces accept the hookshot. Objects can also declare grapple points.
Static islands can add distant models, shore foam, sea-chart markers and arrival
notices.

## Place a model with collision

Declare an original static `.glb` model, then refer to it from an actor:

```json
{
  "models": [
    {"id": "island", "source": "assets/island.glb"}
  ],
  "actors": [
    {
      "id": "island-a",
      "kind": "mod",
      "model": "island",
      "behaviour": "static",
      "stage": "sea",
      "room": 12,
      "position": {"x": 75000, "y": 0, "z": -201500},
      "collision": {"mesh": "island"}
    }
  ]
}
```

`model` selects the visible model. `collision.mesh` names a declared static model
whose geometry supplies collision; it can name the same model. `behaviour` is
`static`, `floating` or `drifting`. A world object uses a rigid model and has no
carry, combat, path, animation or dialogue behavior. Floating and drifting
objects currently require the `sea` stage, with no base pitch or roll; yaw is
allowed.

GLB coordinates are metres. Conversion uses 100 game units per metre and keeps
the authored origin. Manifest positions and shape dimensions use game units;
rotations use degrees. Optional `rotation` and `scale` apply to both appearance
and collision. Keep object origins near their geometry rather than modelling
at sea-map coordinates in Blender.

## Author collision in the GLB

Keep one visible mesh. It can have several materials, each with its own
embedded PNG texture (see [Several materials](#several-materials)). Collision
meshes may have multiple triangle primitives. Name each collision mesh or its
scene node with an `_col` suffix, for example `island_col` or `pier_col`. These
meshes are omitted from rendering.

A collision primitive needs indexed triangles and `POSITION`. Normals and UVs
are optional; they do not control collision. Assign one of these material names:

| Material | Surface |
| --- | --- |
| `col_stone` | Stone |
| `col_grass` | Grass |
| `col_wood` | Wood |
| `col_sand` | Sand |
| `col_metal` | Metal |
| `col_wall` | Ordinary wall surface |
| `col_objects` | Object collision, including the ship; Link and camera pass through |
| `col_climb` | Native vine-style climbable wall |
| `col_hookshot` | Hookshot attachment surface with wooden ground sounds |
| `col_water` | Water to swim in: Link swims below its surface (see "Water") |
| `col_shallow` | Shallow water: Link wades through it |
| `col_waterfall` | The ground of a waterfall, as the game's own waterfalls |
| `col_ice` | Ice: Link slides on it |
| `col_void` | A drop: landing on it sends Link back to the room's restart point, as a bottomless pit does |

Use separate material slots for different ground types. Triangle direction
matters: orient ground faces upward and obstacle faces outward. The converter
preserves node transforms and corrects winding for mirrored transforms. It
rejects zero-area faces, invalid indices, nonfinite coordinates and unsupported
material names.

When no `_col` mesh exists, collision can be derived from the visible triangles.
Equivalent positions are welded, but the converter does not reduce a detailed
model to an arbitrary triangle budget. Each visible material’s `col_*` name
selects the fallback surface of its triangles; other visible material names use
stone. Use a separate, simpler `_col` mesh when the render mesh exceeds the
collision limits, or when cut-out cards such as bushes should not be solid.

Names such as `col_noship` and `col_shiponly` are not available. The native collision filter groups the ship with other objects; it
has no separate ship-only category.

Use `col_objects` on vertical collision-envelope faces when the visible hull is
too thin for reliable ship collision. It blocks all object-class queries,
including the ship. Link, the camera, arrows, bombs, boomerangs and rope pass
through those faces. Keep the ordinary wood or metal deck faces for Link. Do
not cap an object envelope with additional ground faces. This material is
available on GLB collision geometry, not as a simple shape's `surface`.

Ship collision samples several heights rather than treating every thin floating
surface as a solid hull. Give static coasts and obstacles sufficient vertical
sides. A shallow horizontal sheet alone is not a reliable obstacle to the ship.
The original island example extends its coast below sea level.

## Several materials

A visible model can use up to 16 materials, so an island can have sand, grass,
rock and wood that each repeat their own texture, like the game's own islands.
Give each material one embedded PNG base colour texture. The mesh can have up to
32 triangle primitives; each primitive uses one material, and several primitives
may share a material. Blender exports one primitive per material slot.

Two material kinds are available:

| glTF material | Draws as |
| --- | --- |
| `alphaMode` `OPAQUE` (the default) | Opaque |
| `alphaMode` `MASK` | Cut-out: texels with alpha below `alphaCutoff` (default 0.5) are left out. Use it for leaves, fences, grass and bush cards. |

`doubleSided: true` draws both faces of either kind, each lit from its own side;
use it for cut-out cards. The converter adds a mirrored copy of those faces, so
they count twice toward the vertex and triangle limits.
Blended transparency (`alphaMode` `BLEND`) isn't supported: the model is refused
with a message. Paint a cut-out's soft edges hard, or use `MASK`.

Every material is lit the same way, by the game's own lighting for objects, so
the materials of one model match at every time of day. The converter takes the
materials' shading from a get-item model with an opaque and a cut-out material
in the player's own game; a mod contains only its own model and textures.

| Limit | Value |
| --- | --- |
| Materials per model | 16, each used by at least one primitive |
| Triangle primitives in the visible mesh | 32 |
| Vertices and triangles | 4,096 and 8,192 for all primitives together; double-sided faces count twice |
| One texture | at most 512 × 512 pixels; width a multiple of 64, height a multiple of 8 |
| All textures of one model | 1 MiB as decoded RGBA: one 512 × 512, four 256 × 256 or sixteen 128 × 128 textures |

Several materials sharing one image use one texture. The game's memory budgets
for mod models still apply to the converted model: 2 MiB per model, 8 MiB per
mod and 32 MiB per room. Each material adds one draw, so merge materials that
share a texture and keep small repeating textures where they suffice.

A model with one opaque, single-sided material converts exactly as before.

Animated (skinned) models can use several materials too, with the same kinds and
limits. Every primitive is bound to the model's one skin, so all parts of the
model follow the same bones. Where two materials meet, the border vertices
belong to both primitives; give them the same joints and weights in both (Blender
does this when one mesh has several material slots) and the parts stay joined in
every pose. An animated model with one opaque, single-sided material also converts
exactly as before. See [Skinned models and motion](mod-sdk-v3.md#skinned-models-and-motion).

`examples/guest-mods/world-objects-w5` contains an island with six materials:
sand, grass, rock and wood, and cut-out fences and bushes. Its
[`assets/make_fixtures.py`](../examples/guest-mods/world-objects-w5/assets/make_fixtures.py)
reproduces the model and textures. It occupies the same sea area as the other
island examples, so enable it on its own.

`examples/guest-mods/sdk-v3-skinned-materials` contains an animated creature with
three materials: cloth and bark for its body and head, and cut-out, double-sided
leaves on its head and arms. Its body is cut into the cloth and bark materials at
the waist, where its idle clip bends it. Its
[`assets/make_reed_walker.py`](../examples/guest-mods/sdk-v3-skinned-materials/assets/make_reed_walker.py)
reproduces the model, its textures and its idle and walk clips.

## Simple collision shapes

Use a shape instead of a collision mesh for a small obstacle. All shapes are
centered at the actor origin, including their vertical dimension. They follow
the actor's rotation and scale. Their optional `surface` defaults to `stone` and
accepts `stone`, `grass`, `wood`, `sand`, `metal` or `wall`.

```json
{"shape": "box", "size": {"x": 200, "y": 100, "z": 300}, "surface": "wood"}
```

Box `size` gives full width, height and depth. A box with height 100 spans local
Y=-50 to Y=50. To rest it on ground height 0, place its origin at Y=50.

```json
{"shape": "cylinder", "radius": 100, "height": 200, "surface": "stone"}
```

```json
{"shape": "sphere", "radius": 100, "surface": "metal"}
```

Put one of these objects inside the actor's `collision` field, alongside
`behaviour: "static"`. Cylinder and sphere collision use bounded triangle
approximations. A shape cannot also specify `mesh`.

## Floating and drifting

A floating object's target height is the native wave height plus its manifest
`position.y`, minus `buoyancy.draft`. The object's local origin matters: a raft
with its deck at local Y=0 and `position.y: 45`, `draft: 0` aims to keep the deck
45 units above the wave. A positive draft lowers the body.

```json
{
  "id": "floating-raft",
  "kind": "mod",
  "model": "raft",
  "behaviour": "floating",
  "stage": "sea",
  "room": 12,
  "position": {"x": 75000, "y": 45, "z": -204500},
  "collision": {"mesh": "raft"},
  "buoyancy": {"draft": 0, "damping": 0.3, "max_tilt": 10}
}
```

| Buoyancy field | Units and range | Default |
| --- | --- | --- |
| `draft` | Game units, −1,000 to 1,000 | 0 |
| `damping` | Seconds, 0 to 10 | 0.25 |
| `max_tilt` | Degrees, 0 to 30 | 10 |

The body samples the game's sea height and nearby wave slope. Damping controls
how quickly it follows the height and tilt targets; a larger value responds
more slowly. Link rides the moving collision through the game's riding system.
The model and collision move together.

For drifting, change `behaviour` to `drifting` and add `drift`:

```json
{
  "drift": {
    "wind_speed": 8,
    "current": {"x": -12, "z": 8},
    "max_speed": 30
  },
  "anchor": {
    "point": {"x": 75000, "y": 0, "z": -204500},
    "radius": 600
  }
}
```

Drift speeds use game units per second. `wind_speed` follows the native wind
direction and accepts 0 to 100. The optional current gives world-space X/Z
components, each from −100 to 100. `max_speed` caps the combined horizontal
speed, from above zero to 100; its default is 30. A drifting body needs nonzero
wind or current movement. Drifting collision respects islands, protected places,
other mod objects and the ship; a blocked move does not move through them.

An optional anchor confines the object's origin to a horizontal circle. `point`
is a world-space `{x,y,z}`; its Y value does not change the floating height.
`radius` is above zero and at most 10,000 game units. The initial position must
lie inside the circle. The anchor is a movement limit, with no rendered rope.

Every scaled collision vertex on a floating or drifting object must lie within
2,000 units of its origin. This bounds its footprint through all allowed tilts.
Native wave heights outside the supported ±2,048-unit range are refused;
an unavailable wave or placement query does not authorize unverified movement.
Keep floating objects small and leave enough space for their whole motion.

Full save states preserve the object's current motion state. Portable states,
Last session and automatic portable saves reconstruct actors from their
manifest placements: drift starts again from the authored position. They do not
promise an exact snapshot of each drifting object's location.

The original
[`world-objects-w2` example](../examples/guest-mods/world-objects-w2) contains a
floating raft, a tall metal buoy, an anchored drifting raft and anchored
flotsam. Its
[`assets/make_fixtures.py`](../examples/guest-mods/world-objects-w2/assets/make_fixtures.py)
reproduces the three models. The thin wooden decks and separate `col_objects`
side walls illustrate collision for both Link and the ship.

## Limits and placement

Each collision mesh is limited to 4,096 vertices and 4,096 triangles. Every local
coordinate must stay within ±20,000 game units, and the extent along each axis
must be at most 20,000 units. These bounds are checked again after actor scale
is applied. Smaller meshes make collision checks cheaper.

Admission also checks estimated native collision storage: at most 512 KiB per
body and 2 MiB in a room. The existing limit of 16 placed actors per room,
across enabled mods, also applies; ordinary actors use those places too.
Rendering and actor memory have their own budgets. The native shared registry must retain at
least 16 free slots. Registration also requires native heap headroom: at least
1 MiB free and a contiguous aligned allocation of at least 256 KiB after the
actor heap is finalized. Passing the mesh limits does not guarantee an
allocation in every room.

Placement checks use the player's installed game resources: the room's collision and its layout, read
where the game stores them (world coordinates, on the sea as in dungeons). An object keeps 50 units clear of:

- the room's walls and ground. Ground wholly below the object may touch its base; going into it is refused.
  The sea's surface (the water plane of an island's square) and lava are not ground: a floating object or a
  pier may reach into them;
- what the island needs, each by its real size: villagers where they stand (a radius of 100 units, in every
  story stage), their walking routes (a corridor 60 units to each side), chests (120), doors (250), a piece
  of heart or heart container lying about (80), switches (150), the places where Link arrives (100), and
  event triggers Link walks into (TagEv, Evsw, SW_C00: a small one whole, a wide one by its core of 300
  units). Small props (lanterns, posts, stakes, enemies) count with 60 units, any other object with 120
  (more when the layout scales it up, up to 600);
- these don't count, an object may stand over them: grass, flowers, trees, palms, birds, crabs, pigs, pots,
  barrels, signs, rupees and hearts, message, hint, sound, attention and island-name areas, the pigs'
  digging spots, salvage points, invisible walls, light and weather areas, the sea chart's island markers
  and the old Tingle Tuner switches. Routes at sea level (boats, swimmers) only count for what reaches down
  to the water;
- other world objects (this mod's and other mods') and the King of Red Lions.

Two static world objects are compared by their own collision, not by boxes around them: an object may
stand on another's walkable top (an island ground, a platform, a pier) and reach up to 150 units into it
where the ground slopes under it, as the islands' own buildings do; only a real overlap of their solid
geometry is in the way (a cliff, a pillar, a wall, the ground higher up the object). Floating and drifting
objects keep their whole motion range clear.

An object must stay inside the indexed room. A sea object that crosses into a neighbouring square is
refused until coverage for that placement is available. Missing, malformed or unsupported collision, event
or route data prevents placement rather than treating the room as empty. Drifting objects stop at the same
things as they move.

A place that isn't clear is still placed in the editor (and saved), but the object isn't in the game until it
is moved where it fits; Check and export list it as a problem, and at game start it is left out with a line in
the log. The editor and MCP give what is in the way, in plain words with the game's own name
("a door of the island", KNOB00D); `placement_conflicts` lists every finding. In the editor the selected
object's footprint shows green or red with the first reason, and hovering it lists them all. Conflicts
between mod objects and collision-memory budgets are reported alongside native placement findings.

## Editor commands

These commands belong to the editor command layer. The existing editor UI is
unchanged. Requests below show the command name followed by its parameters.
An optional `mod` selects another open package.

Prepare a declared static model's collision before placing it during a running
game:

```text
prepare_collision {"model":"island"}
```

Preparation runs in the background. Its result note reports that work has
started. Query the object once its collision is ready; if this is the first mod
model in the running game, a restart may be required.

Place a new actor atomically, without an intermediate invalid cylinder actor:

```text
place_world_object {"id":"island-a","model":"island","behaviour":"static","stage":"sea","room":12,"position":{"x":75000,"y":0,"z":-201500},"collision":{"mesh":"island"}}
```

Optional `rotation` and `scale` use `{x,y,z}`. `behaviour` defaults to `static`.
Floating and drifting declarations also use `buoyancy`, `drift` and `anchor` as
described above; their editor command parameters follow the same manifest fields.
The actor id must be unused. The command creates one undo step.

Change collision and behavior together:

```text
set_world_object {"id":"island-a","behaviour":"static","collision":{"shape":"box","size":{"x":200,"y":100,"z":300},"surface":"wood"}}
```

This also creates one undo step. A rejected change leaves the package untouched.
Omitted `collision` and `behaviour` keep their current values, so this command
also supports parameter-only edits. Set `buoyancy`, `drift` or `anchor` to `null`
to remove that block. Switching to `static` removes inherited motion settings;
switching to `floating` removes inherited drift and anchor settings. Explicit
incompatible settings are rejected. The ordinary `set_actor` command also
accepts `behaviour`, `buoyancy`, `drift` and `anchor`; use `set_world_object` for
coordinated changes so the actor remains valid.

Inspect the prepared geometry or placement:

```text
preview_collision {"id":"island-a"}
placement_conflicts {"id":"island-a"}
```

`preview_collision` returns world-space vertices and edges suitable for a
wireframe, plus placement and budget findings. It does not add a wireframe panel
to the UI. `placement_conflicts` returns the findings without wireframe geometry.
Both queries leave the package and undo history unchanged. Simple shapes need
no model-collision preparation.

## Make a small island in Blender

1. Model a low-poly hill with a beach and a pier near the origin. Use metre
   coordinates. Keep one visible mesh. Give it one material slot per surface
   (sand, grass, rock, wood), each with an Image Texture node feeding Base Color
   and nothing else. Leave Base Color's tint white and the material's other
   inputs at their defaults. For leaves or a fence, use a PNG with transparency
   and connect its Alpha to the Principled BSDF's Alpha. The material must
   export as alpha clip (`alphaMode` `MASK`): in Blender 4.1 and earlier set
   Blend Mode to Alpha Clip; in later versions put a Math node set to Greater
   Than (threshold 0.5) between the image's Alpha and the BSDF's Alpha, as the
   Blender manual's glTF page describes. Turn off Backface Culling for cards
   seen from both sides (exported as `doubleSided`).
2. Duplicate or rebuild the terrain as simple collision geometry. Delete visual
   details that Link cannot walk on. Name the collision object `island_col`.
3. Assign `col_sand` to beach faces, `col_grass` to hill faces and `col_wood` to
   pier faces. Check upward ground normals, outward coast normals and all joins
   between ramps and platforms. Extend coast walls below the sea.
4. Triangulate the visible and collision meshes. Check that collision has no
   collapsed faces and stays within the vertex, triangle and size limits.
5. Export the visible mesh and `_col` mesh into one GLB with the PNGs embedded
   (glTF Binary; Images: Automatic or PNG; no compression or extensions).
   Each image must be 512 × 512 pixels or smaller, with a width that is a
   multiple of 64 and a height that is a multiple of 8.
   Do not hide collision by excluding it from the export: the converter removes
   `_col` meshes from the visible output.
6. Add the model and static actor declarations to the package. Choose an empty,
   fully indexed part of the target room. Inspect collision and placement
   findings before testing the object.
7. Walk from the beach to the hill and pier. Check footsteps on each material,
   sail into the coast, and test a room change and a state load. Recheck placement
   after changing scale, rotation or position.

The complete original example is
[`examples/guest-mods/world-objects-w1`](../examples/guest-mods/world-objects-w1).
Its [`assets/make_fixtures.py`](../examples/guest-mods/world-objects-w1/assets/make_fixtures.py)
reproduces the GLB using only the Python standard library. It contains a textured
hill, beach and pier, with separate sand, grass and wood collision primitives.
[`ASSETS.md`](../examples/guest-mods/world-objects-w1/ASSETS.md) records its origin
and license.

Ship only your original source models and package files. The game prepares its
rendering resources and collision cache locally; generated cache archives and
resources taken from the game do not belong in an exported mod.

## Climbable walls and hookshot surfaces

Assign `col_climb` to an outward-facing vertical collision wall. Leave clear
space in front for Link and connect its upper edge to an upward-facing walkable
roof or platform. The native climb checks require a nearly vertical face
(`abs(normal.y) <= 0.05`); sloped ground does not become a climbable wall merely
by changing its material. Keep beams and other obstacles out of the climb lane.

Assign `col_hookshot` to a visible target's collision triangles. Aim and fire
the hookshot normally: the native reticle identifies eligible polygons within
its 1500-game-unit sight range. Leave room for Link at the destination and an
unobstructed pull path. Hookshot targets use wooden ground sounds. These
materials are authored on GLB collision meshes; simple collision shapes retain
the six ordinary ground materials.

`examples/guest-mods/world-objects-w3/assets/make_fixtures.py` reproduces an
original tower, checker atlas, climb face and hookshot patch. Its stone roof
meets the climb face, and the front crossbeam leaves climb lanes at either
side. The right-side gold patch is the hookshot target. Grapple anchors require
separate object settings; a wooden collision material alone does not create
a rope target.

## Water

A `col_water` surface is water Link can swim in. Author it as upward-facing faces at the water's level,
covering the pond or pool; the faces need no thickness. Link passes through the surface (it is no ground): give
the pond its own floor and banks with the ground materials (for example `col_sand` under the water and
`col_stone` for the banks). Below the surface Link swims as in the sea, and he climbs out at a bank that
is low enough, as on the islands. `col_shallow` is for water too shallow to swim in: Link wades.

The surface is invisible by itself. Show it with a water surface model: a separate model declared as
the actor's `shoreline` with `"material": "water"` (see "Island presentation"); the water of the
game's spring pools moves on it. `"material": "pond"` gives it the calm water of an island's own
shallows instead (Outset's): the blue of the shallows, wave lines over them and the coast foam along the
shore, each from its own material in the model (see "Water surfaces with several materials").

The game looks for a pond only within the height range of the room's own ground (the highest point of
the island, its houses and cliffs in that square). A pond whose surface is higher than that is not
found: Link walks on its floor as if it were dry. In a square of open sea the highest point can be
low (about 1,200 units in the square of the examples below), so keep raised ponds below the top of the
square's island. A pond at sea level is always found.

### Rivers and waterfalls

A shoreline can also carry flowing water: `"material": "river"` gives the model the flowing water of Forest
Haven's streams, `"material": "waterfall"` Forest Haven's falling water (the same texture, three times as
fast). Both scroll along the model's V: map V so that it grows downstream (downwards on a
waterfall), about every 200 units on a river and every 700 units down a waterfall (as the game's own repeat it); U across,
about every 200 units on a river and every 500 on a waterfall. A river fades out towards the mesh's open edges like the
still water, so give it at least one row of inner points; a waterfall is drawn as Forest Haven's falls are, more opaque
and foamy at the top than at the foot.

#### Water surfaces with several materials

A water surface model may have several materials, each named after its part: `surface` (the water
itself, required), `waves` (lines on it), `edge` (the foam along a shore or over a fall's lip) and
`splash` (where falling water lands). Each part gets its own material of the game's water there:

| `"material"` | `surface` | `waves` | `edge` | `splash` |
| --- | --- | --- | --- | --- |
| `pond` | Outset's shallows | Outset's wave lines | Outset's coast foam | |
| `water` | the spring pools' water | | | |
| `river`, `waterfall` | Forest Haven's stream | | Forest Haven's bank foam | Forest Haven's spray |

Map an `edge` part's V from 0 at the shore (or the lip) to 1 out on the water: the foam is strongest at 0.
A model with one material is its `surface`, whatever its name. `foam` shorelines keep one material.

`col_shallow` is the ground of shallow water: put it where Link walks (the river's bed), not at the
water's surface. `col_waterfall` is the ground under a fall: Link standing on it gets the game's splash on
his head; the falling sheet itself has no collision, so Link drops through it.

### Bodies of water made with the editor

The editor's water tool ([Water](mod-editor.md#water-ponds-rivers-and-waterfalls)) makes ponds, rivers and
waterfalls from points clicked on an island's ground. Each is one static object with two original models:

| Body | Its model (drawn, and its collision) | Its shoreline |
| --- | --- | --- |
| Pond | a bowl under the water (`col_sand`), a rim a little above the water that slopes down to the island's ground (`col_grass`, `col_stone` where it can't reach); `col_water` at the level | `pond`: the water, wave lines and (with `foam`) coast foam at the shore |
| River | its bed, a few units above the ground, and banks on both sides that slope down to the ground; `col_water` at the level in deep sections, `col_shallow` on the bed in shallow ones | `river`: the stream, foam along both banks, over each step's lip and where the water lands |
| Waterfall | rock behind the fall (`col_wall`) and a splash pad below it (`col_waterfall`) | `waterfall`: a thin sheet curling over the lip, foam over the lip and a ring of foam round its foot |

A pond's outline is rounded off (each corner cut twice), and its rim and a river's banks are natural ones:
a little above the water, then a slope (about 20 to 27 degrees, wandering a little from place to place, steeper
where the water stands high so that it reaches the ground within about 2.4 m) down to where it meets the island's ground, asked at five distances up to about 8 m out. Where the ground can't
be reached that way (a cliff edge, the open sea) a short skirt goes down instead, and next to a river's step the
banks stop at the lip. The game's island ground can't be lowered, so water that must be deep enough to swim
in (about 200 units) sits that far above the ground: a pond without a level of its own stands its depth above the
lowest ground along its shore, its banks a low mound round it (higher on the downhill side of a slope). Ground
under the sea's surface (a harbour, the sea beside an island) counts as the surface there, so no bank or level
reaches down to the sea floor.
The banks are painted with the island's own ground: each part of a bank asks for the texture of the ground
where its slope meets it (glTF material `extras`: `{"wwhd_island": {"room": n, "points": [[x, z], ...]}}`,
world units), and the converter reads that texture (the band of the ground atlas it uses there) from your
room archive into your model cache. Without the room (another stage, or a missing file) the model's own
plain texture is used.

A river's level follows the ground sampled every 150 units along its centre line: deep sections hold their
depth of water above the bed, shallow ones 30 units; it never flows uphill (a rise is reported, its water
stays level), and where it would drop more than 60 units between two samples it steps down at once, which
is where a waterfall goes. The models' files are named after their content under `models/water/`, and the
tool's own parameters ride along in the first model (glTF `asset.extras`), which is how the editor changes
them later. Because a body of water lies on the island's ground, its floor and banks may reach into the
island's ground and walls (as island buildings do); everything else an object keeps clear of still counts.
The same goes for world objects it lies on or in (an island ground or a terrace of your own): their ground
is ground for the tool (its points, level and banks follow it), and of such an object only what rises
through the water is in the way (a wall or a pillar standing in it, a house reaching into it, ground higher
than the water inside its shore). A house or another object placed later is checked against the water the
same way.

Limits: bodies of water are made on the `sea` stage; a body takes two of a mod's 8 models; a pond has 3 to
48 points, a river 2 to 32 (turns of at most 120 degrees), a waterfall is 100 to 4,000 units wide and falls 80
to 6,000; the water surface has at most 1,024 triangles. Each prepared body takes about 0.6 to 1 MiB of
the room's model memory (the surface's water textures and the banks' ground texture come from your game
files). The game's spring gush (Ygush00) and the ambient water sounds of the islands (their sound emitters)
are not added: they aren't game objects a mod can place yet. A waterfall's splash is its own foam (the
game's splash particles appear where Link lands in the water).

A full save state made with an earlier version keeps its world objects' collision as it was then
(before the water, ice and void materials, and with `col_metal` still hurting Link) until the room
loads again; then the objects get their collision anew.

## Grapple points

A grapple point marks the attachment position on a visible beam or post. Add it
inside the world-object actor, alongside its collision:

```json
"grapple_points": [
  {"id": "beam", "position": {"x": 0, "y": 800, "z": -330}, "yaw": 90}
]
```

Point positions are local game units, relative to the model's origin. The actor's
scale, rotation and movement apply to them. `yaw` is the local post orientation
in degrees and defaults to zero. Use a visible beam and leave space for the
rope, Link's swing and the landing. Collision geometry alone does not create a
grapple point. Targeting, rope movement and release use the game's controls.

Each object accepts at most eight points with unique IDs. Local coordinates
must be within ±20,000 units, yaw within ±360 degrees, and points must lie inside
the object's scaled collision bounds. On floating objects they must also fit
its collision bounding radius. A room accepts at most 16 declared grapple
points across enabled mods. Their actor storage is included in mod budgets.

`place_world_object`, `set_world_object` and `set_actor` accept `grapple_points`.
An omitted field keeps existing points; `null` in an editor update removes them.
`preview_collision` also returns each point's authored world position and yaw.
Removing an attached target requests a native rope release before its temporary
target actor is freed. Full states preserve attachment identities; portable
loads recreate the points with their objects. Release the rope before making
a portable or Last session save: those saves retain the game's existing
restriction on saving while Link hangs from a rope. Grappling mod targets does
not set the game's tutorial or progression flags.

## Island presentation

A static sea object can declare original models for its distant silhouette and
animated coast foam, plus an arrival name and sea-chart marker:

```json
{
  "distant": {"model": "island-lod"},
  "shoreline": {"model": "island-foam"},
  "chart_marker": {"label": "Horizon Isle", "when_flag": "horizon.arrived"},
  "arrival": {"name": "Horizon Isle", "radius": 2200, "flag": "horizon.arrived"}
}
```

These fields are optional and belong on the island's actor declaration.
`distant.model` and `shoreline.model` refer to rigid models declared in the same
package. Both visual models must have 1–1024 triangles, and their bounds must
stay within the main model bounds plus 512 local game units. Keep them aligned
with the full model's origin. A readable silhouette and sufficient height help
an island remain recognizable from neighboring sea squares. Distant geometry
adds visibility, not remote collision or permission to overlap another island.

The full model draws in its home square. Its distant model draws in the eight
neighboring squares, with no distant collision. Shoreline decoration draws in
the home square. Actors and their visual variants share 128 declaration slots;
a sea square accepts at most 16 active visual variants. Removing objects in the
editor retains their identities for undo and state restoration until restart.

A shoreline can also be a water surface instead of foam: with `"shoreline": {"model": "pond-surface",
"material": "water"}` the model gets the water of the game's spring pools, clear water with moving,
bubbling ripples (the default `"material"` is `"foam"`). Model the water surface as a flat mesh at the
water's level with a few rows of triangles; it fades out towards its open edges, so the water blends
into the bank. Repeat U and V about twice per metre (every 50 units, as on the game's own spring). The surface is clear: the pond's colour comes from
its floor and banks below it. An actor has one shoreline: an island with foam and a pond gives
the pond its own static object (its basin as the model, with the `col_water` surface in its collision,
and the water surface as its shoreline).

Author the shoreline model as strips along the model's coast, just above the
water. The game supplies the foam material, texture and animation from the
player's installation. These resources stay in the player's local cache; your
mod ships only its original geometry. The shoreline model's placeholder texture
is ignored. Give it a separate model declaration: it cannot also be used as the
island, collision mesh, distant model or held item.

Map the strips with U running along the coast and V from 0 at the inner edge to
1 at the outer edge. Repeat U to control the pattern's length along the coast;
one repeat per ten metres is a useful starting point. The example's strips are
about five metres wide. Keep their outer edge within the model-bound limits.
The foam is translucent, animates during game updates, and pauses with the
game. Its lighting follows the environment. Shorelines supply no Link or ship
collision, and native waves may cover low parts of the strips.

Arrival names and chart labels allow up to 96 UTF-8 bytes. Arrival radius is
100 to 20000 game units. Entering that radius produces the named flag in the
mod's own progress; it never sets a retail game progression flag. A marker's
`when_flag` can use that arrival flag to reveal the island after discovery.
Use the same spelling in both fields. Keep collision placement within the
usual limits and leave existing islands, events and sailing routes clear.

The chart accepts eight island markers per mod and 32 across enabled mods.
Its overview uses native story rings. A zoomed square has one available ring:
the tracked quest takes priority, followed by the first eligible island marker.
An arrival appears as a mod notice and is shown once per declared mod flag.
Use `null` in an editor update to remove any island presentation block; omit a
block to keep its current settings. Preparing a new visual model may require
a reload or restart before the editor can use it.

`examples/guest-mods/world-objects-w4` contains Horizon Isle, with a 40-triangle
full model, 26-triangle distant model and 8-triangle animated foam strip model. Its
source generator also reproduces the embedded textures and authored footstep
materials. Enable this example separately from the earlier island and tower
examples because their demonstration placements share the same sea area.
