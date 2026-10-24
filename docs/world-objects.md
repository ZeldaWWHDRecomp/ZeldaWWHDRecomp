# World objects

World objects let a mod add walkable ground, solid obstacles and floating bodies using its
own models. Link and the ship use the game's background collision. Ground
materials select the game's footstep sounds and surface effects.

Static objects stay in place. Floating bodies follow the sea, and drifting bodies
also move with wind or a current. Collision materials can make walls climbable
and surfaces accept the hookshot. Objects can also declare grapple points.
Static islands can add distant models, shore foam, sea-chart markers, arrival
notices and calm water for a boat landing.

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
| `col_lava` | Lava: Link burns, jumps out, loses half a heart and is sent back to the room's restart point, as on the game's lava (see "Lava") |

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
Blended transparency (`alphaMode` `BLEND`) isn't supported: a package with such a
model is refused with a message. The world editor's import turns it into a cut-out
(`MASK` at 0.5) when the texture has transparent parts, else opaque, and says so
([What the import fixes](mod-editor.md#what-the-import-fixes-for-you)). Paint a
cut-out's soft edges hard, or use `MASK`.

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

## Game materials

A model can draw some or all of its surfaces with the game's own materials instead of pictures of its own:
Dragon Roost Island's rock, Forest Haven's grass and falling water, the stone of Hyrule Castle. Such a model is a
**kit model**. Each texture-free material of the GLB names a material of one room of the game, the kit room, as a
[mod stage's ground](mod-stages.md#kit-materials) does. When the game starts (or when you import the model in the
editor), the port builds it from the player's own game files into the player's model cache (`Data/.kit-models`),
as it builds island buildings. The surfaces look exactly like the game's: its textures, toon shading, vertex-colour
shading and moving textures (water, foam, lava) where the room animates them. Nothing of the game is in your
package.

```json
{
  "models": [
    {"id": "rock", "kind": "kit", "source": "assets/rock.glb", "kit": {"stage": "sea", "room": 13}},
    {"id": "rock-far", "kind": "kit", "source": "assets/rock-far.glb", "kit": {"stage": "sea", "room": 13}}
  ],
  "actors": [
    {"id": "rock", "kind": "mod", "model": "rock", "behaviour": "static", "collision": {"mesh": "rock"},
     "distant": {"model": "rock-far"},
     "stage": "sea", "room": 13, "position": {"x": 214000, "y": 0, "z": -186000}}
  ]
}
```

| Model field | |
|---|---|
| `kind` | `kit` |
| `kit` | The kit room: `{"stage": "sea", "room": 13}` (Dragon Roost Island's square), `{"stage": "sea", "room": 41}` (Forest Haven), `{"stage": "Hyroom", "room": 0}`: any room of any stage. |
| `materials` | Optional: the model's material names mapped to kit materials (up to 16), for materials whose names you want to keep. |
| `material` | Optional: the kit material for every other texture-free material. |

### Naming the materials

A texture-free material is drawn with the kit material that `materials` maps it to, or else the kit material of
its own name. A Blender copy suffix is ignored, so `Txe_gake_v.001` is `Txe_gake_v`. An unnamed material, or a
`col_*` one, takes `material`, or the kit room's main material (the one most of its triangles use). A name the kit
room doesn't have is refused. The game's log and the editor say which names the room has:

```
material "Txe_hia_v" has no texture, and "Txe_hia_v" is not a material of the kit room (sea room 13). It has:
Txe_gake_v, Txe_hai_v, Txe_ita_v, ... The exact names are those of the room's materials in the editor's export_glb
and list_kit_materials
```

The exact names come from the game itself. The editor's `export_glb` command writes a room as a GLB whose
materials carry the game's names (see [Export the game's islands for reference](#export-the-games-islands-for-reference)),
and `list_kit_materials` lists a room's materials with their textures, how many triangles use each and the
texture coordinates the room gives them. Pick materials by what they show, not by their names. Some textures are
atlases of several surfaces in bands; see [Kit materials](mod-stages.md#kit-materials).

| Dragon Roost Island (`{"stage": "sea", "room": 13}`) | |
|---|---|
| `Txe_gake_v` | the island's grey-brown rock cliffs (texture 2048 × 2048) |
| `Txe_hai_v` | the ash on its slopes |
| `Txe_ita_v` | the wooden planks of its walkways |
| `Txe_tesuri_v`, `Txe_rope_kui_v` | its railings and rope posts |
| `Txe_jimen_v`, `Txe_gake_siba_v`, `Txe_gake_hai_v` | its ground, grassy cliffs and ashen cliffs |

### Your own textures in the same model

A material with an embedded PNG base-colour texture is your own, as in any mod model ([Several
materials](#several-materials)): opaque or cut-out (`MASK`), optionally double-sided, up to 512 × 512, and 1 MiB
of decoded texture for the model's own textures together. Game materials don't count toward that 1 MiB. The
`lookout` of the example below has a rock base and a plank floor of Dragon Roost and a cloth roof of its own. The
own materials are lit as objects are; the game materials are lit as the room lights its ground.

**A game material never carries a texture.** A package never contains the game's pictures. A material that is a
game material may not embed one, and the model is refused, in the editor, when the package is checked, and when
the game builds it. That covers a material named after a game material or mapped to one in `materials`, and a
material copied from an `export_glb` reference (it keeps the game's name in its extras even when renamed). Remove
the material's image before you export, and the game draws its own from the player's files.

### What a kit model is

- **Its light.** The game materials are drawn like the islands' own buildings: with the background light of the
  room the model stands in, so they have that place's colours through the day, at dusk and at night, its fog and
  its weather.
- **Its parts.** A kit model is drawn in at most two parts: the game materials of each of the kit room's models
  they come from (its ground model, its water model), and your own textures. So it can have game materials of one
  of the room's models and textures of its own, or game materials of two of its models. The second of those is
  drawn with the room's second background light set. When the kit room moves a material's texture (falling water,
  foam, spray), the kit model's surface moves the same way; one part of a model can be animated.
- **Its collision** is the model's own `*_col` meshes with `col_*` materials, as for any world object (the
  visible triangles when it has none; a game material's surface is then stone). `collision.mesh` names the model
  itself.
- **Its place.** A static world object, turned about the vertical axis only, at scale 1 (as island buildings).
  It is drawn to the sea's far distance, so a large island stays in view across its square, fading into the sea's
  fog far off as the game's own islands do (island grounds of the game pieces too). Its placement is checked as any
  world object's. Parts made together can form one
  [composition](#limits-and-placement).
- **Its distant model** may be a kit model too: `"distant": {"model": "rock-far"}`. That model has up to 1,024
  triangles and draws in the eight squares around. Its game textures larger than 256 × 256 are drawn at 128 × 128
  (the game's own distant views are small too), so it costs a fraction of the full model.
- **Limits.** Up to 20,000 visible triangles, a GLB of up to 16 MiB, every point within 100,000 units (1 km) of
  the origin. The built model (its game textures, shader archives and models) may have up to 12 MiB. It counts
  toward the room's 24 MiB for island buildings and grounds
  ([game pieces](mod-sdk-v3.md#island-buildings-game-pieces)), not toward a mod model's 2 MiB.
- A kit model can't be held or used as a shoreline. If it can't be built (a name the kit room doesn't have,
  another game version), it is left out with the reason in the log, and its placements with it.

What it costs, measured with the example's models (Dragon Roost's materials):

| Model | Game textures | Shader archives | Archive (all) |
|---|---|---|---|
| `rock`: rock, ash, planks (262 triangles) | 3.0 MiB (the rock texture alone is 2.7 MiB) | 0.46 MiB | 3.6 MiB |
| `lookout`: rock, planks and a cloth roof of its own | 2.8 MiB, and 64 KiB of its own | 0.56 MiB | 3.6 MiB |
| `rock-far`: its distant model | 0.13 MiB | 0.46 MiB | 0.65 MiB |

In the game, the room loads the archive as it is: the `rock` island took 3.6 MiB of its room's budget for island
buildings and grounds, plus 57 KiB for its resource table and 17 KiB for its model. The textures of one material
are the game's full size, so the cost is mostly that of the materials you pick, not of the model's size. Two models
of one mod with the same kit materials each carry their own copy.

### In the editor

`import_model` with `kit` (and optionally `materials` and `material`) adds a kit model and builds it at once. Its
actors appear as soon as it is ready, without a restart:

```json
{"command": "import_model", "file": "/Users/me/rock.glb", "kit": {"stage": "sea", "room": 13}}
```

After you change the model in Blender and export it over the package's file, `reload_model {"model": "rock"}`
builds it again and the actors drawing it take the new one at once. `set_model_kit` changes a kit model's `kit`,
`materials` or `material` and builds it again. The package and its actors save and load as any other.

### A short Blender workflow

1. In the editor, run `export_glb {"stage": "sea", "room": 13}`, or `list_kit_materials {"stage": "sea", "room":
   13}`, and import the exported GLB into Blender as a reference. Note the names of the materials you want.
2. Model your object in metres, with its origin at its foot (an island's at its sea level), Y up in the exported
   file (Blender's glTF exporter does this).
3. Give each surface a material named exactly as the game material, for example `Txe_gake_v`, with no Image
   Texture node. A material appended from the reference keeps its name and texture: delete its Image Texture node.
4. UV-map it at the game's scale. Dragon Roost's rock repeats about every 16.6 m, its ash every 6.5 m, its planks
   every 3.9 m. Add a colour attribute (vertex colours, exported as `COLOR_0`) to darken the foot of cliffs and
   undersides as the game's rooms do; white draws at full brightness, and Dragon Roost's own rock averages about
   0.45.
5. Model the collision as a separate mesh named `<name>_col` with `col_stone`, `col_sand`, `col_wood` ...
   materials.
6. Export glTF 2.0 Binary (.glb) with normals, UVs and vertex colours, no animation, then `import_model` it with
   its `kit`.

`examples/guest-mods/world-objects-kit` is an island built this way. A rock island next to Dragon Roost has
Dragon Roost's rock, ash and planks, a distant model of the same materials, and a lookout with a roof texture of
its own. Its [`assets/make_models.py`](../examples/guest-mods/world-objects-kit/assets/make_models.py) reproduces the
models.

## Simple collision shapes

Use a shape instead of a collision mesh for a small obstacle. All shapes are
centered at the actor origin, including their vertical dimension. They follow
the actor's rotation and scale. Their optional `surface` defaults to `stone` and
accepts `stone`, `grass`, `wood`, `sand`, `metal` or `wall`.

```json
{"shape": "box", "size": {"x": 200, "y": 100, "z": 300}, "surface": "wood"}
```

Box `size` gives full width, height and depth, each up to 100,000 units; a cylinder's height
is up to 100,000 and a cylinder's or sphere's radius up to 50,000. A box with height 100 spans local
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

Each collision mesh is limited to 8,192 vertices and 8,192 triangles. The storage budget below is the closer
limit for a large mesh: a body of about 6,000 welded triangles fits in its 512 KiB, a mesh at the full 8,192 of
each does not. Every local
coordinate must stay within ±100,000 game units, and the extent along each axis
must be at most 100,000 units (1 km): one sea square, so a whole island fits in one
collision mesh, without sections or seams. The object must still stay inside its own
sea square. These bounds are checked again after actor scale
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
  pier may reach into them (the same goes for the lava of a mod's own objects: nothing stands on a `col_lava`
  surface);
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

A standing object is measured by its own collision (its `_col` meshes or its collision shape), not by a box
around it: an L-shaped island extension whose corner is open water may wrap around a villager, a start point
or a route that none of its triangles comes near. Each of the things above counts when one of the object's
triangles comes within its reach; the room's walls and ground count when the object's triangles cross them
more than 150 units above its base (resting on the ground, or reaching a little into a slope under it, is
fine). Only the room's square is checked by the box: the whole object stays inside the room. A boat's route
(the one around Windfall at sea level) is named as a boat route; it counts like any other route.

Two static world objects are compared by their own collision, not by boxes around them: an object may
stand on another's walkable top (an island ground, a platform, a pier) and reach up to 150 units into it
where the ground slopes under it, as the islands' own buildings do; only a real overlap of their solid
geometry is in the way (a cliff, a pillar, a wall, the ground higher up the object). Floating and drifting
objects keep their whole motion range clear.

**Parts of one composition.** Static world objects of one mod that name the same `"group"` were made together:
an island's ground and the ruins standing on it, say, each with materials of its own [kit room](#game-materials).
Their collisions are not compared with each other, so the ruins' foundations may reach into the ground as the
model has them. Everything else counts for each part as for any world object: the room, the island's own things,
other world objects and other mods' objects. A group is an id (`"group": "rock-isle"`), for static world objects
in one room; objects without one, or in another group, are compared as always, even when they share an origin.

An object must stay inside the indexed room. A sea object that crosses into a neighbouring square is
refused until coverage for that placement is available. Missing, malformed or unsupported collision, event
or route data prevents placement rather than treating the room as empty. Drifting objects stop at the same
things as they move.

A place that isn't clear is still placed in the editor (and saved), but the object isn't in the game until it
is moved where it fits; Check and export list it as a problem, and at game start it is left out with a line in
the log. Moved in the editor to a place that fits, it is put into the game there, whether it was left out at
start-up or later. The editor and MCP give what is in the way, in plain words with the game's own name
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

## Export the game's islands for reference

Before modelling a new island, you can open the game's own islands and
buildings in Blender to see how big things are, how the game shapes them and
which materials it uses. The editor's `export_glb` command writes a `.glb` file
from your own game files:

```text
export_glb {"stage":"sea","room":44,"collision":true,"actors":true}
export_glb {"stage":"sea","room":11,"name":"windfall"}
export_glb {"piece":"outset-house-ladder","collision":true}
```

- A **room** (`stage` and `room`; sea room 44 is Outset Island, 11 is Windfall
  Island) exports every model of the room in place: the ground, cliffs, houses
  and rocks, the shore and water models and the window glows. Each model of the
  room is one object with its parts as children. Any stage of the game works
  (`{"stage":"M_NewD2","room":3}`).
- A **game piece** (`piece`: a building, island part or island from the editor's
  Game objects palette, `list_game_objects`) exports exactly what the piece
  places, with the piece's own origin.
- `collision` adds the collision as one more object, `<name>_col`, with one
  material per ground type: `col_grass`, `col_sand`, `col_wood`, `col_stone`,
  `col_wall`, `col_water`, `col_shallow` and the others listed under
  [Author collision in the GLB](#author-collision-in-the-glb). The names are the
  ones your own collision meshes use.
- `actors` adds an empty object for each actor placed in the room (trees,
  grass, signs, doors and so on), named after the actor, at its position and
  facing. Only the place is exported, not the actor's model.
- `name` sets the file name (default `sea_room44` or the piece id), and
  `max_texture` (128 to 2048) halves larger textures to keep the file small.

The file goes to the `Exports` folder of the mod manager's storage
(`<host config directory>/ModManager/Exports`), never into a package. An export
takes a few seconds for a small island and up to a minute for a large one. The
command returns the file's path and its triangle, material and texture counts.

The file uses the same conventions as your own models. One metre is 100 game
units, Y is up, and Blender's glTF importer turns it to Z up. The textures are
embedded as PNG and the vertex colours are kept. A room is centred on its own
sea square: the square's centre is the origin and the island sits where it sits
in the square. A piece's origin is where the placed piece's origin is.

**Material names are kit materials.** Every material in the file has the game's
name for it, for example `A_jimen_a_v` for Outset's ground. That is the name a
mod stage's `materials` and `material` fields take (see
[Kit materials](mod-stages.md#kit-materials)). In Blender, give your own
surfaces your own material names and map them to these names in the manifest,
or look up a texture's name in the material's custom properties
(`wwhd.samplers`, for example `{"_a0": "Txa_jimen_a"}`). The custom properties
also hold the room's offset on the sea and each actor's parameters.

Use the export as a reference only:

- Build **original geometry**. Measure the export, copy its proportions and
  style, then model your own island and buildings and paint your own textures.
- Leave the game's textures in the game. Where your stage should use one, refer
  to it by its material name as a kit material. The game draws it from the
  player's own files.
- **The exported models and textures are your own copy of Nintendo's content.**
  Keep them on your machine. Do not share them, upload them, or put them or
  anything traced from them into a mod package, an asset folder or a
  screenshot set for a release.

The same exporter runs outside the game:
`python3 tools/story_mods/export_glb.py --game <game folder> --stage sea --room 44 --out outset.glb --collision --actors`
(or `--piece <id>` in place of `--stage` and `--room`).

## Build next to a game island

To extend one of the game's islands (a new hill beside Windfall, a pier at Outset), model your part in
Blender next to the island's export, so it fits the island's shape, and bring only your part back. It comes
back at the same place and the same scale:

1. In the editor, export the island's room: `export_glb {"stage": "sea", "room": 11, "collision": true}`
   (Windfall). Import the file into Blender (File → Import → glTF 2.0). It is room-local: the sea square's
   centre is the origin, 1 m is 100 game units, Y is up in the file (Z up in Blender).
2. Model your part in its own object, next to the island, in the export's coordinates. Leave the object's
   origin where it is: don't move the model to the origin. Make its collision a second object named
   `<name>_col` (for example `windfall_ext_col`) with `col_*` materials.
3. Export only your objects: select them and export with Include → **Selected Objects** (or hide the
   export's objects), with the settings in [What the import fixes](mod-editor.md#what-the-import-fixes-for-you).
   The export's own objects and textures are the game's: never in your file or your mod.
4. In the editor, import the file (Outliner → Models, or `import_model`). Several objects, quads, vertex
   colours, a blended material or too large textures are fixed on import, each with a note.
5. Go to the island (Link in its sea square) and add it: **Add actor → Your model**, or `add_actor
   {"type": "model", "model": "windfallextension"}`. A model made away from its origin is placed at the
   room's origin, the square's centre at sea level, unturned, so it stands where you modelled it; the status
   bar says so. A model with a `_col` mesh (step 2) is added as a static world object on that collision, so
   Link walks on it and its walls stop him; its collision is prepared at once and the object appears when it
   is ready. The same with a command, at a position you choose:

   ```text
   place_world_object {"id": "windfall-ext", "model": "windfallextension", "collision": {"mesh": "windfallextension"},
                       "stage": "sea", "room": 11, "position": {"x": 0, "y": 0, "z": -200000}}
   ```

   The position is the square's centre: for sea room `n`, x = −300000 + 100000 × ((n − 1) mod 7) and
   z = −300000 + 100000 × ((n − 1) div 7), so Windfall (11) is at (0, 0, −200000) and Outset (44) at
   (−200000, 0, 300000). Don't scale or turn it: scale 1 and no rotation keep it where you modelled it.
6. `focus {"id": "windfall-ext"}` shows it. After a change in Blender, export again over the package's
   copy (`models/<id>.glb` in the mod's folder) and run `reload_model {"model": "windfallextension"}`: the
   import's fixes are applied to it again; a kit model is built again at once, another model is taken at
   the next start.

**Your model** without a `_col` mesh gets a small solid cylinder at its origin, as a prop; with one (or as a kit
model) it is a world object that uses its collision and, as a kit model or with a distant model, is seen from
across its square ([Island presentation](#island-presentation)). An actor added before this, with the cylinder,
keeps it: remove it and add the model again, or give it its collision with `set_world_object {"id":
"windfallextension", "behaviour": "static", "collision": {"mesh": "windfallextension"}}`.

**Why your part looks brighter than the island.** A model with textures of its own is lit as the game lights an
object (a barrel, a sign): with the object light of the place it stands, at the full brightness of its texture.
The island's own ground is lit with the room's background light and darkened by its vertex colours (Windfall's
grass by about a third). Your textures, even a copy of the island's, are therefore drawn brighter and flatter
than the ground next to them, whatever their size. Use the island's own materials (below) and it is drawn as
the island is.

### Game materials on your own sea models

Your part looks most like the island when it uses the island's own materials: Windfall's grass, its
stone walls, its wooden planks. Don't keep the export's textures in your model (they are the game's
pictures, and a package never carries them); name your materials after the game's and let the game draw
them from the player's own files. Such a model is a [kit model](#game-materials):

1. In Blender, give each surface a material named exactly as the export's material for it, for example
   the name of Windfall's grass material from the reference. A material appended from the export keeps
   its name; delete its Image Texture node, or keep it for Blender's preview: the editor's import removes
   it from the package's copy and says so. A `.001` suffix Blender adds is ignored. `list_kit_materials
   {"stage": "sea", "room": 11}` lists the room's material names with their textures and how much of the
   room uses each.
2. Materials with textures of your own (a new sign, a roof) keep their Image Texture as before; they are
   drawn as any mod model's.
3. Import it as a kit model of the island's room:

   ```text
   import_model {"file": "C:/Users/me/windfall_ext.glb", "kit": {"stage": "sea", "room": 11}}
   ```

   To keep your own material names, map them: `"materials": {"ext_grass": "<grass material>"}`.
4. Place it with `place_world_object` as in step 5 above (a kit model is a static world object, turned
   about the vertical axis only, at scale 1). It is lit with the room's light, its fog and its weather,
   and its game textures move where the room animates them (water, foam).

A material that names a game material and still carries a texture is refused, with the reason, in the
editor and when the package is checked: remove its image. [Game materials](#game-materials) has the
details and the limits.

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

### Lava

A `col_lava` surface is lava, as the game's own lava floors are (Dragon Roost Cavern, the lava caves, the inside
of Fire Mountain). Author it as upward-facing faces at the lava's level. Like water, the surface is no ground:
Link sinks into it, burns, jumps out with a cry, loses half a heart and starts again at the room's restart point
(where he came in), exactly as on the game's lava. A bomb that falls into it explodes, a pot thrown into it
breaks with a splash of lava, and other things dropped into it are gone. The lava gets a bottom of its own,
1 metre (100 units) below the surface, where things sink to: keep that space free of other ground. Lava's
triangles count twice towards the collision limits (the surface and its bottom).

The sounds, effects and camera are the game's own lava handling: Link's cry and the sound of falling into lava,
the flames on him, the bomb's lava burst, and the camera, which passes through the surface as on the game's
lava. Unlike a pond, lava is found at any height (no need to stay below the top of the square's island). The
game has no lava where its boat sails, and the boat has no lava handling: lava is not water for it, so keep lava
pools out of the boat's way, above the sea or inland.

The surface is invisible by itself. Show it with a lava surface model: a separate model declared as the
actor's `shoreline` with `"material": "lava"` gets Fire Mountain's lava: the lava the volcano pours down its
sides at sea, its bright molten cells and red veins moving as they do on the mountain, by day and by night (a pool
glows all over; the dark crust between the mountain's streams is left out). Repeat U and V about every 10 m (1000
units), as the mountain's own lava does. The water tool makes a lava pool with both models (below).

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
| `lava` | Fire Mountain's lava | | | |
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
| Lava pool (a pond with `lava`) | the same bowl and rim in rock (`col_stone`, dark rock unless you choose a floor colour); `col_lava` at the level | `lava`: Fire Mountain's lava |
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
must be within ±100,000 units, yaw within ±360 degrees, and points must lie inside
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
neighboring squares, with no distant collision. It is drawn as the game's distant islands are: from up to
about 150,000 units away, hazing from where the room's own fog ends (a little over 300 m) to eight times that
distance, so an island in the next square is a pale shape on the horizon. Paint a distant model's colours as
plain areas; fine detail is lost at that distance. Shoreline decoration draws in
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
100 to 100000 game units. Entering that radius produces the named flag in the
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

## Calm water

Out on the open sea the waves can reach about 3 m. A pier or a beach just above the sea gets flooded:
Link falls back into the water when he steps off the boat. The game's own landings (Windfall's pier,
Outset's beach, Dragon Roost) have no such problem because the game calms the sea around its islands.
A static sea object can ask for the same with `calm_water`:

```json
{
  "calm_water": {"radius": 3000, "falloff": 12800, "offset": {"x": 0, "z": 1500}}
}
```

| Field | Meaning |
|---|---|
| `radius` | The still water: within this distance of the centre the waves are gone. 100 to 100000 units. |
| `falloff` | Optional. How far beyond the radius the waves come back in full. 800 to 25600 units, default 12800 (the game's own fade around its islands). |
| `offset` | Optional. The centre, from the object's position (`x` and `z`, turned with the object). Default: the object's position. |

It works like the game's islands: the calm follows Link. As he sails towards the island the whole sea
around him calms down, smoothly over the fade; inside the radius it is flat, at the sea's own still level
(the level of the game's own landings). Everything uses this one sea: what you see, the water height Link
swims and wades in, the boat's height and rocking. Sail away and the waves come back the same way.

Choose a radius that covers the landing and the water the boat comes in on, and leave the fade at 12800
unless the island is close to another one. A short fade makes the waves die down fast as the boat comes
in; it can't be shorter than 800. A pier at the game's own heights stays dry in calm water: Windfall's
boarding step is 35 units above the still sea, its pier about 95.

A mod may have eight calm zones. Zones of several mods work together: where they meet, the calmer one
counts. A save state keeps the sea as it was, and the zone works again after a room reload or a restart.
Without `calm_water` the sea is exactly the game's. In the editor, **Calm water** in the Inspector of a
static sea object turns it on and sets the radius, fade and centre; the zone shows in the picture
(see [Calm water](mod-editor.md#calm-water)).

`examples/guest-mods/world-objects-w4` contains Horizon Isle, with a 40-triangle
full model, 26-triangle distant model and 8-triangle animated foam strip model. Its
source generator also reproduces the embedded textures and authored footstep
materials. Enable this example separately from the earlier island and tower
examples because their demonstration placements share the same sea area.
