# Building a dungeon with an AI assistant

An AI assistant such as Claude Code can build a [mod stage](mod-stages.md) with you. It models the rooms in
Blender, puts them into your mod with the [world editor](mod-editor.md), walks in, takes a screenshot, and
fixes what looks wrong. It does all of this through two MCP connections: one to Blender and one to the game's
editor. You stay in charge. The game asks you before anything is replaced, every step can be undone, and
everything shows in the editor's **Log**.

This page sets up both connections and then goes through one round of the loop.

## What you need

- The game, with the editor (`--editor`) and code mods working ([setup](setup-code-mods.md)).
- [Blender](https://www.blender.org/) 3.6 or newer.
- An MCP server for Blender. This page uses the community add-on **Blender MCP**
  (<https://github.com/ahujasid/blender-mcp>). It is not part of this project, so follow its own install
  notes when they differ from the short version below. Any other Blender MCP server that can run Python in
  Blender works the same way.
- Claude Code, or another MCP client that can hold two servers at once.

## Setting up the two connections

### Blender

1. Install the Blender MCP add-on (its `addon.py`) under **Edit → Preferences → Add-ons → Install**, and
   enable it.
2. In Blender's 3D view press **N**, open the **BlenderMCP** tab and press **Connect to Claude** (the add-on
   then listens on your computer only).
3. Add the server to Claude Code once:

   ```sh
   claude mcp add blender uvx blender-mcp
   ```

   (`uvx` comes with [uv](https://docs.astral.sh/uv/).)

### The game's editor

1. Start the game with `--editor` and open **Settings → Editor → Claude / MCP**.
2. Turn on **Claude / MCP access**, press **Copy command** and run the copied line in a terminal:

   ```sh
   claude mcp add --transport http wwhd-editor http://127.0.0.1:47615/mcp --header "Authorization: Bearer <token>"
   ```

The token is new each time you turn the switch on. After a restart of the game, run
`claude mcp remove wwhd-editor` and add the new line. For the details, see
[Using Claude or another MCP client](mod-editor.md#using-claude-or-another-mcp-client).

Start Claude Code in any folder. `claude mcp list` should show both `blender` and `wwhd-editor` as connected.

## What a room model needs

Tell the assistant these rules, or paste this list into your first message. They are what makes a room look
right in the game:

- **One room per `.glb` file**, in metres: one metre is 100 game units. A 20 × 20 m hall is 2000 × 2000
  units. Rooms of one stage share coordinates, so put each room where it belongs around the origin.
- **Named materials for each kind of surface**: for example `floor`, `wall`, `ceiling`, `pillar`, `trim`.
  The game draws each with a material of the template's room (its *kit*). With one material, or with
  materials that have no names, floors, walls and ceilings all look the same.
- **UVs on every visible mesh.** Without texture coordinates a surface shows one colour of the texture. In
  Blender: select the mesh, **Edit Mode → UV → Smart UV Project** (or **Cube Projection** for blocky rooms).
  Scale the UVs so that a texture repeats every 2–4 metres.
- **Faces point into the room.** A room is seen from inside, so walls and ceilings face inwards
  (**Mesh → Normals → Recalculate Inside**).
- **Collision** (optional): a mesh whose name ends in `_col` is collision only. Without one, the visible
  mesh is the collision. Material names such as `col_stone`, `col_wood` or `col_water` choose the surface
  ([list](mod-stages.md#the-ground-model)).
- **A floor where Link starts.** The spawn and every exit box must stand on floor.
- Export with **File → Export → glTF 2.0**, format **glTF Binary (.glb)**. Leave out cameras and lights.

The editor's `check_stage` says when a room has no UVs, a single material or materials without names, with
the part of the stage it means.

## The loop

The example builds a small dungeon: a hall with a locked door to a vault. The assistant's steps are below.
You can ask for each step yourself, or ask for the whole thing in one message ("Build me a two-room dungeon
behind the door in the lava cave …") and watch it work.

### 1. Model the rooms in Blender

> Make a 20 × 20 m stone hall, 6 m high, with a 3 m wide doorway in its north wall, and a 10 × 10 m vault
> behind it. Give them the materials `floor`, `wall` and `ceiling`, unwrap them with Smart UV Project, and
> export each room to `hall.glb` and `vault.glb` in my `dungeon` folder.

The assistant uses Blender's tools (or runs a short Python script in Blender) to build the meshes, assign the
materials, unwrap them and export the files.

### 2. Start a mod and put the rooms in

In the editor:

```json
{"command": "new_mod", "params": {"id": "my-dungeon", "name": "My dungeon", "stages": true, "confirm": true}}
{"command": "import_ground", "params": {"file": "/home/me/dungeon/hall.glb", "confirm": true}}
{"command": "add_stage", "params": {"id": "keep", "ground": "assets/hall.glb", "template": "Cave01", "title": "The Keep"}}
{"command": "import_ground", "params": {"file": "/home/me/dungeon/vault.glb", "confirm": true}}
{"command": "add_room", "params": {"stage": "keep", "ground": "assets/vault.glb"}}
```

`file` is the model's full path. The game asks you to **Allow** the new mod and each file it reads from outside the mod. `template` chooses the
light, the camera, the doors and the kit room. `title` gives the stage a dungeon map.

### 3. Check, build and walk in

```json
{"command": "check_stage", "params": {"stage": "keep"}}
{"command": "rebuild_stage", "params": {"stage": "keep"}}
{"command": "warp", "params": {"stage": "stage:my-dungeon/keep", "room": 0, "point": 0}}
{"command": "screenshot", "params": {}}
```

`check_stage` lists problems such as a room nobody can reach, a locked door without a key before it, or a
model without UVs. `rebuild_stage` builds the stage. When a kit material name is wrong, its result names the
materials the kit room has. The screenshot shows the room as the player sees it.

### 4. Look and adjust

The assistant looks at the screenshot and decides what to change. For example:

- **The floor looks like the wall.** It maps the materials to the kit's:
  `{"command": "set_stage_field", "params": {"stage": "keep", "field": "rooms/0/materials", "value": {"floor": "<a kit floor material>", "wall": "<a kit wall material>"}}}`.
- **The room is too dark, or the wrong shape.** It changes the model in Blender, exports it again, runs
  `import_ground` with the same file (replacing the old one), and then `rebuild_stage`.

When Link is inside the stage, `rebuild_stage` loads the room again where he stands, so the next screenshot
shows the change straight away.

### 5. Doors, keys, chests and enemies

Walk Link (or warp him with a `position`) to a spot and place things there:

```json
{"command": "place_in_stage", "params": {"stage": "keep", "what": "chest", "item": "small_key"}}
{"command": "place_in_stage", "params": {"stage": "keep", "what": "door", "to_room": 1, "lock": "small_key"}}
{"command": "place_in_stage", "params": {"stage": "keep", "what": "enemy", "actor": "bokoblin"}}
{"command": "place_in_stage", "params": {"stage": "keep", "what": "warp"}}
```

A chest or an enemy goes a little in front of Link. A door goes where he stands, facing his way. For an exact
spot, give `"at": "position"`, `"room"` and `"position": {"x": …, "y": …, "z": …}`. Then run `check_stage`
again: it says whether the key comes before its door. Then `rebuild_stage` again.

### 6. The way in

Warp to the place in the game where the dungeon should start, stand there, and place the entry:

```json
{"command": "place_in_stage", "params": {"stage": "keep", "what": "entry"}}
```

Walking into that spot takes Link into room 0 of the stage. Each room's exits (`what: "exit"`, with `to`)
lead back out.

### 7. Save and share

`save_package` writes the mod. Its package holds your `.glb` files and the stage's description, never any of
the game's files. Players build the stage on their own computer from their own copy of the game. See
[Mod stages](mod-stages.md) for every field, and [Checking and exporting](mod-editor.md#checking-and-exporting)
for sharing the mod.

## Tips

- Ask for one change at a time and a screenshot after each. A short loop finds problems sooner than a long
  plan.
- For a place in the open (an island in the sky, a mountain top), ask for a template with a sky first:
  `{"command": "list_stage_templates", "params": {"sky": true, "door": "boss"}}`.
- Keep a room under 20000 triangles. Blocky shapes with bevelled edges read well with the game's textures.
- If a rebuild fails, its result says why (a material name, an exit box without floor, a room over budget),
  and the assistant can fix that directly.
- The editor's **Stages** tab shows the same steps with buttons: a new stage from a model, Rebuild, Warp in,
  Check, and placing spawns, exits, doors, chests and enemies where Link stands.
