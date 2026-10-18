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

Keep one visible mesh with one indexed triangle primitive, one opaque material
and one embedded PNG texture, as for existing static mod models. Additional
collision meshes may have multiple triangle primitives. Name each collision
mesh or its scene node with an `_col` suffix, for example `island_col` or
`pier_col`. These meshes are omitted from rendering.

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

Use separate material slots for different ground types. Triangle direction
matters: orient ground faces upward and obstacle faces outward. The converter
preserves node transforms and corrects winding for mirrored transforms. It
rejects zero-area faces, invalid indices, nonfinite coordinates and unsupported
material names.

When no `_col` mesh exists, collision can be derived from the visible triangles.
Equivalent positions are welded, but the converter does not reduce a detailed
model to an arbitrary triangle budget. The visible material’s `col_*` name
selects the fallback surface; other visible material names use stone. Use a
separate, simpler `_col` mesh when the render mesh exceeds the collision limits.

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
wind or current movement. Drifting collision respects islands, protected areas,
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

Placement checks use the player's installed game resources. They protect native
collision bounds, authored actors and fixed events, and routes. The current
index is deliberately conservative:

- Every authored native actor or tag, across layers, reserves 10,000 units around
  its position in X/Z.
- Native routes reserve a 2,000-unit corridor.
- Protected geometry and routes receive an additional 100-unit placement clearance.
  Native ground wholly below the object may touch its base; penetrating it is refused.
- An object must stay inside the indexed room. A sea object that crosses into a
  neighbouring square is refused until coverage for that placement is available.
- Missing, malformed or unsupported collision, event or route data prevents
  placement rather than treating the room as empty.

A conflict can therefore reserve more space than the visible island or event.
Move or shrink the object and query again. Do not bypass a coverage error to
place an island beside an unknown event. Conflicts between mod objects and
collision-memory budgets are reported alongside native placement findings.

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
   coordinates. Keep one visible mesh and one opaque textured material; use a
   texture atlas for several visible colors.
2. Duplicate or rebuild the terrain as simple collision geometry. Delete visual
   details that Link cannot walk on. Name the collision object `island_col`.
3. Assign `col_sand` to beach faces, `col_grass` to hill faces and `col_wood` to
   pier faces. Check upward ground normals, outward coast normals and all joins
   between ramps and platforms. Extend coast walls below the sea.
4. Triangulate the visible and collision meshes. Check that collision has no
   collapsed faces and stays within the vertex, triangle and size limits.
5. Export the visible mesh and `_col` mesh into one GLB with its PNG embedded.
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
