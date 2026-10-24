# Actors and scenes (SDK v3 preview)

**Preview.** This is an experimental extension to [SDK v2](mod-sdk-v2.md), and it may still change.
Mods can place a small set of the game's own actors, add static and animated models of their own with
behaviour from mod code, give them talk actions and play scenes with camera paths and choices. Mod actors
use the game's lighting, shadow, collision, attention and dialogue systems, and survive room changes and
save-state loads.

The worked package is [sdk-v3-windfall](../examples/guest-mods/sdk-v3-windfall/manifest.json).
It contains original text, names existing game actors and includes an original
carved-post model and rigged tide sprite; it includes no game resources. It requires the Story Notebook package and a build with code-mod
support. The example is a data-only content package. A content package with valid, nonempty
world declarations can use an empty content directory without replacement game files.
Empty generic content packages remain invalid. World declarations are
read at startup; enabling or changing a package requires a restart.

## Placing actors

`actors` is an array. Each native actor has an `id`, `actor`, `stage`, `room`, and
`position` with numeric `x`, `y`, `z`. Optional `rotation` uses **degrees** on each
axis, limited to ±360. Optional `scale` has three positive components, each at
most 10; the default is one on each axis. Positions must be finite and within
±350000 game units. Room numbers are 0–63 and stage names are at most eight
ASCII letters, digits or underscores.

The initial allowlist is deliberately small:

| Actor | Names accepted | Parameters |
| --- | --- | --- |
| Sign | `sign`, `kanban`, `PROC_KANBAN` | A mod-owned `flag`; optional dialogue |
| Resident | `resident`, `NpcPeople`, `PROC_NPC_PEOPLE` | `parameters: {"resident": "uo2"}`: one of the villagers below; default and legacy numeric `9` select Um1 |
| Chest | `chest`, `Tbox`, `takara`, `PROC_TBOX` | A mod-owned `flag`; what it holds in `parameters.item` (below) |
| Breakable prop | `pot`, `barrel`, `crate` | Required mod-owned `broken_flag` and `drops` list |
| Latched floor switch | `switch` | Required mod-owned `switch_flag`; fixed yellow switch |
| Rotating gate | `door` | Required `switch` naming a switch actor in the same mod |
| Native enemies (preview) | `chuchu`, `bokoblin`, `moblin`, `keese`, `miniblin`, `peahat`, `darknut`, `rat_hole`, `bokobaba`, `bubble`, `octorok`, `stalfos`, `redead`, `mothula`, `morth`, `magtail`, `poe`, `armos`, `armos_knight`, `kargaroc`, `wizzrobe`, `warship`, `gyorg`, `floormaster`, `big_octo`, `wall_cannon` | Required mod-owned `defeated_flag` and `drops`; fixed ordinary variants |

Resident names are exact native object codes in lower case. Each placed copy uses fixed ordinary
parameters: it stands where you put it (unless you give it a route, below) and has none of the
character's quest or story variants.

| Code | Who it looks like | Notes |
| --- | --- | --- |
| `uo2` | The man from the Windfall bar | |
| `um1` | The windmill keeper | |
| `uo1` | The old man on the sea-view bench | |
| `uo3` | The well-dressed man walking through town | A placed copy stands still unless it has a route |
| `ub1`, `ub2` | The two women chatting on the steps | Each is placed on its own |
| `ub3` | The woman on the harbour pier | |
| `ub4` | The woman by the town gate | Not in the photo studio (`Ocmera`) |
| `uw1` | The young woman working in the bar | |
| `uw2`, `um2` | The young woman and young man of the town square | A placed copy is there whatever their story |
| `sa1` | The sailor who drinks in the bar at night | |
| `sa2` | The sailor on the high lookout | |
| `sa3` | The sailor on the stone dock | |
| `sa4` | The sailor at the end of the wooden pier | |
| `ym1` | A seated man | See below |
| `rotena` | The Goron merchant in the striped hat (green pack) | See [Goron merchants](#goron-merchants) |
| `rotenb` | The Goron merchant in the blue hat (brown pack) | |
| `rotenc` | The Goron merchant in the red vest (pale blue pack) | |

Day and night: a copy looks like the character at the time of day it was created.
Other names and numeric values are rejected. These Windfall people are not placeable, because
their ordinary behaviour changes the game: `um3` (his telescope talk sets the game's progress at
night), `sa5` (he moves the game's pigs), `ug1` and `ug2` (they follow the game's week and its
hide-and-seek event). The other characters of the NPC catalogue are separate actor classes with
their own quests and are not placeable yet. Each placed copy uses mod dialogue
and flags; retail originals keep their own dialogue and quests. Placed copies
are excluded from retail named cohorts and figurine registration, while normal
world freezing, attention and collision remain native. Restored open dialogue
rebinds its mod text; deletion or room teardown cancels without running `then`.

`ym1` uses the native seated action with fixed parameters. Its initializer affects
only the placed copy and does not change the game's progress prerequisites or
save flags. Author the seated character's height and location; this adapter does
not ground-correct, walk, or support scene `move`/`face`. Its narrow native
relocation area near x=-202066, z=320123 is rejected. Camera following and dialogue
remain available. Other Ym1/Ym2 staff variants are unsupported.

### Goron merchants

The three travelling Goron merchants of the sea can be placed as residents: `rotena`, `rotenb` and
`rotenc`. They are the game's own merchants, loaded from the player's game files: three Gorons
with their big packs, each with a look of his own (his own hat, clothes and pack: the game has three
merchant models and no other Goron). Place as many as you like (within the actor limits below), for example a family of them:

```json
{
  "id": "elder", "actor": "resident", "parameters": {"resident": "rotenb"},
  "stage": "sea", "room": 44, "position": {"x": -201700, "y": 168, "z": 312050},
  "rotation": {"x": 0, "y": 20, "z": 0},
  "talks": [
    {"when": {"quest": "homeland", "step": 1}, "lines": ["The mountain calls us home, brother."],
     "then": [{"action": "quest_step", "id": "homeland", "step": 2}]},
    {"lines": ["Welcome to the island, brother!"]}
  ]
}
```

A placed Goron stands where you put it, turns his head to Link and talks with
your `lines` or `talks` (with their conditions, quest steps and `then`), as every resident does. He
appears whatever the player's story: the game's own merchants only come out once the story has them,
a placed copy is always there. He keeps none of the merchants' trade: showing him an item does
nothing, he takes no rupees and sets none of the game's flags (his first talk, the exchange counts
and the map) — the merchants on the sea keep their own talks and trades unchanged. A Goron doesn't
walk a route (refused with "a Goron merchant stands where it is placed and does not walk a route")
and doesn't take scene `move` or `face` steps; scene camera and `say` steps work.

`scale` makes a smaller or larger Goron, such as a young one: `"scale": {"x": 0.7, "y": 0.7, "z": 0.7}`
(uniform, 0.5 to 1.5). His model, pack and hat scale; his talk distance and the circle Link bumps into
stay the merchant's own size. There is no other colour or model than the three merchants' own.

### Residents walking a route

Every placed Windfall resident except the seated `ym1` can walk a route with the game's own walking:
`uo1`, `uo2`, `uo3`, `um1` and `um2` with their own walking animation, and the women and sailors
(`ub1`–`ub4`, `uw1`, `uw2`, `sa1`–`sa4`), who have none, with a borrowed one (below). It starts at its
`position` and walks to the route's points:

```json
{
  "id": "baker", "actor": "resident", "parameters": {"resident": "um1"},
  "stage": "sea", "room": 11, "position": {"x": 600, "y": 95, "z": -199000},
  "lines": ["Fresh bread at the market!"],
  "path": {
    "points": [{"x": 820, "y": 95, "z": -199100}, {"x": 700, "y": 95, "z": -199250}],
    "mode": "loop", "speed": 70, "pauses": true
  }
}
```

| Field | |
| --- | --- |
| `points` | 1 to 15 points after the position; each more than one unit (across) from the one before |
| `mode` | `once` (default: it walks to the last point and stands there), `loop` (back to its position and round again) or `back_and_forth` (it turns at each end) |
| `speed` | game units per second, from half to twice the character's own pace; without it, its own pace (`uo1`, `uo2` 30, `uo3` 60, `um1` 63, `um2` 72, `ub1`–`ub4` 36, `uw1`, `uw2` 34, `sa1`–`sa4` 60) |
| `pauses` | `true`: after walking for some seconds it stands for a moment, in the character's own rhythm as the game's walkers do; default `false` |

It walks as the game's own walkers do: with its own walk and idle, turning on the spot at corners,
following the ground, stopping to look at Link when he is close in front of it (and walking on when he
leaves), and talking as usual (your lines). `uo2` still startles at a roll or a loud noise nearby. It has
no ledge check of its own, so the port watches it: when it gets no closer to its next point for about a
second and a half (a wall), it goes on to the point after; when the same point stays out of reach three
times, or it gets nowhere over three such skips, it stops where it is. When it drops far below
its route (an edge), it is put back where it last stood on its route and stops. Check the route in the
world editor: segments over no ground, a drop or a step too high are drawn red.

A route walks in the room where the resident is placed; when the room loads again, it starts again at
its position (a `once` route too). Full save states keep where it is on its route. At most 32 residents
of all enabled mods have a route.

**Borrowed walks.** The women (`ub`, `uw`) and the sailors (`sa`) never walk in the game, so they
have no walking animation. They walk with another villager's walk, fitted to their own body: the women
with the calm walk of the man from the bar (`uo`), the sailors with the windmill keeper's walk (`um`).
All the Windfall people share one skeleton, so only the joint rotations are taken over: each character
keeps its own proportions, its feet stay on the ground, and the walk is timed to its own stride, so
its feet don't slide at any speed. The port builds these walks when the game starts (or when the editor
opens), from your own game files; nothing of the game is in your package or in the port. If your game
files lack one of the archives, those villagers stand at their position and the log says why. They turn
at corners in a tighter arc than the game's own walkers, so a slow walker doesn't circle wide past a
corner. Their long skirts and coats hide most of the legs; the walk shows in the body, the arms and the
steps below the hem.

The seated `ym1` does not walk and is refused with "this villager is seated and does not walk". Each placed resident
may have a route or none; `um3`, `sa5`, `ug1` and `ug2` stay unplaceable (see above).

Other actor names and unsafe parameters are validator errors. Only ordinary chest
raw `params: 0` is accepted at present. The port owns the remaining chest/sign
parameters so a package cannot select a game switch or opened bit.

**What a chest holds.** `"parameters": {"item": ...}` says what Link gets when he opens
the chest: the game's item-get pose with the item and its message, as with the game's
own chests.

| Value | Link gets |
| --- | --- |
| `rupee_green`, `rupee_blue`, `rupee_yellow`, `rupee_red` | a green (1), blue (5), yellow (10) or red (20) rupee |
| `rupee_purple`, `rupee_orange`, `rupee_silver` | a purple (50), orange (100) or silver (200) rupee |
| `heart_piece`, `heart_container` | a piece of heart or a heart container |
| `item:<id>` | one of your mod's items (declared in `items`); with `hold_up` and a `held_model`, Link holds your model up |
| (none) | an orange rupee |

These are the items a treasure chart's chest can hold, and for the same reason: they
change only Link's rupees and hearts. No story or key item can be chosen, and the
chest never sets one of the game's save bits: whether it is open is its mod `flag`.
A piece of heart or a heart container changes Link's hearts for good (turning the mod off
doesn't take them back), as one from a chart's chest does, so use them sparingly. A chest that holds one of your items needs the
story system's chest hook; in the world editor (`--editor`) it is always there, so such a
chest added while the game runs appears at once.

`flag` is a symbolic ID in the declaring mod's story records. It is not a game
save-bit number. Chest opening and sign reading use this namespace. The runtime
rejects colliding flag keys across loaded actors and omits an actor if no story
record can be reserved for its flag.

There are at most 64 declarations per package, 128 across loaded packages and
16 per stage/room across loaded packages. The room cap bounds asynchronous actor
creation and the amount of additional actor work. It is a conservative policy,
not a measurement of spare game heap.

Optional `when` conditions can name `quest` plus `step`, an owned mod `item`
(`item:<id>`), `time` (`day` or `night`), and `flag` / `not_flag` (a mod flag that
must be set / unset). `quests` accepts up to eight `{"quest":"a","step":2}`
entries; every entry must match. A single `quest` / `step`, if also present, must
match too. Conditions are checked when the room
is populated and reconciled in the current room on game logic steps. Pending
creation/deletion retains its identity; an active conversation finishes before
its partner is removed. Actors belong to the native room layer and unload with it.

## The game's own objects (game objects)

A mod can place the game's own static objects, such as palm trees, large trees and tables, by
naming them: `"actor": "game:palm"`. Nothing of the game goes into your package: the object
loads from the player's own game files, exactly as the game draws and handles it, with its
wind sway and its collision (Link and the boat bump into a palm's trunk).

```json
{
  "requires": {"game_objects": 1},
  "actors": [
    {"id": "palm-1", "actor": "game:palm", "stage": "sea", "room": 11,
     "position": {"x": -1500, "y": 322, "z": -199700}, "rotation": {"x": 0, "y": 45, "z": 0}},
    {"id": "seat", "actor": "game:table", "variant": "stool", "stage": "sea", "room": 11,
     "position": {"x": -1850, "y": 303, "z": -200200}}
  ]
}
```

A manifest that uses game objects says which version of the port's game objects catalogue it
needs: `"requires": {"game_objects": 1}`. A port with an older catalogue refuses the mod with
"update the port"; an object added in a later version can't be named with an older number.

A game object takes only `id`, `actor`, `variant`, `stage`, `room`, `position`, `rotation` and
`scale`. It turns about the vertical axis only (`rotation` x and z stay 0), and its `scale` is the
same on all three axes and within the object's limits below. `variant` picks one of its forms (the
first is the default). Parameters, flags, `then`, paths, conditions and dialogue are refused: the
port gives each object the game's ordinary settings with no switch, save bit or event, so a game
object never changes the player's progress. A package that places game objects must not contain
game files (`.bfres`, `.szs`, `.sarc`, `.pack`, `.dzb`, `.ftex` and the like, under any name);
the mod manager refuses it.

| Id | What it is | Variants | Scale |
| --- | --- | --- | --- |
| `game:palm` | A tall palm tree whose leaves sway in the wind | `palm` | 1 |
| `game:tree-large` | A large leafy tree | `tree` | 0.5–1.5 |
| `game:plant-pot` | A leafy potted plant that sways when Link brushes past | `plant-pot` | 0.5–1.5 |
| `game:flower-red` | A tall red flower (drops nothing) | `flower-red` | 0.5–1.5 |
| `game:table` | A wooden table, or the small stool that goes with it | `table`, `stool`, `stool-open` (Link walks through it) | 1 |
| `game:table-small`, `game:table-long` | Wooden tables that settle onto the ground below them | one each | 1 |
| `game:shelf` | A tall shelf with pots and books | `shelf` | 1 |
| `game:lamp-cafe` | A ceiling lamp that turns slowly (no collision) | `lamp-cafe` | 0.5–2 |
| `game:lamp` | A small lamp with a flame that lights its surroundings | `lamp` | 1 |
| `game:nest` | A large bird's nest Link can stand in | `nest` | 1 |
| `game:flag-rito`, `game:flag-sea` | Flags on poles that flutter in the wind | one each | 1 |
| `game:lookout-tower` | A tall wooden lookout platform that stands in the sea | `tower-ladder` (Link climbs its ladder), `tower` | 1 |
| `game:stall` | A travelling trader's stall: posts, a counter and a cloth awning, as Zunari's on Windfall (catalogue version 6) | `stall-small`, `stall`, `stall-large` | 1 |

**The volcanic kit** (catalogue version 6, `"requires": {"game_objects": 6}`; the palette's "Fire and
lava" and "Trees and plants"): torches, flames, fire jets, lava bubbles and Dragon Roost's bomb flowers, the game's own actors with their own effects, sounds and behaviour.

| Id | What it is | Variants | Scale |
| --- | --- | --- | --- |
| `game:torch` | A lit standing torch; its flame lights its surroundings and bends in the wind | `torch`, `torch-stand` (another stand) | 1 |
| `game:flame` | A burning flame on its own with its light (no model, no collision): for a brazier or a torch of your own | `flame` | 1 |
| `game:fire-jet` | A vent that shoots a column of fire every few seconds; the fire burns Link | `fire-jet`, `fire-jet-large` | 1 |
| `game:lava-bubbles` | Bubbles that rise and burst over a lava lake, scattered in a wide ring (1300 to 2700 units) around it | `lava-bubbles` | 1 |
| `game:bomb-flower` | Dragon Roost's bomb plant: Link picks its bomb (A), throws it (or holds it) and it explodes as a bomb does, breaking what a bomb breaks (pots, barrels and boulders, for example) and hurting Link when he is too close; a new bomb grows on the plant after a while | `bomb-flower` | 1 |

The stall is scenery with its own collision; it sells nothing, so a
Goron or another resident behind it does the talking. On Windfall the game shows its stalls only once the
player has gathered Delivery Bag items; a placed stall is always there.

**Rocks and blocks** (catalogue version 6, the palette's "Rocks and blocks"):

| Id | What it is | Variants | Scale |
| --- | --- | --- | --- |
| `game:push-block` | The dungeons' stone block: Link grabs it (A) and pushes or pulls it | `stone`, `statue` (with a stone statue on top) | 1 |
| `game:heavy-block` | A block too heavy to move without the Power Bracelets | `heavy-block` | 1 |
| `game:rock` | A small rock Link lifts, carries and throws; it breaks | `rock` | 1 |
| `game:boulder`, `game:boulder-small` | Boulders that block the way; a bomb blows them apart | one each | 1 |
| `game:stone-head` | A heavy stone head; Link lifts it with the Power Bracelets, bombs don't break it | `stone-head` | 1 |

A rock or boulder that is broken stays gone until its room loads again, and a pushed block starts at
its place again then, as the game's own do when no switch keeps them: none of them uses a switch or a
save bit.

The torches and flames are always lit: a torch that Link lights with fire is not available (the game keeps
whether it burns in a switch of the room, which a game object never uses). The bomb flower needs no bomb
bag. The lava bubbles are left out (with a line in the log) once the player has frozen Fire Mountain with
an ice arrow: from then on the game leaves out its own too. Fire Mountain's eruption smoke is part of the volcano itself
(an actor that freezes with an ice arrow and starts the game's events), so it is not offered, and the
game's steam vents are not either: their steam comes from their own island's effects and shows nowhere else.

Grass, flowers and small trees are the game's shared batches rather than single objects. Each variant
plants a group of a given size; none of them drops anything when cut.

| Id | Variants (units) | Mods' share per stage |
| --- | --- | --- |
| `game:grass` | `grass-1` (1), `grass-7` (7), `grass-21` (21) | 300 of the stage's 1500 |
| `game:flower-white` | `flowers-1` (1), `flowers-7` (7), `flowers-17` (17) | 60 of 200 (with pink) |
| `game:flower-pink` | `flowers-1` (1), `flowers-7` (7) | 60 of 200 (with white) |
| `game:tree-small` | `trees-1` (1), `trees-3` (3), `trees-5` (5) | 16 of 64 |
| `game:tree-bushy` | `tree-bushy` (1) | 40 of 200 |

A batch is planted once per visit of its room, a moment after the room's own grass, and only on a
stage that has that kind of batch itself (most islands do; Windfall has flowers but no grass). It
leaves a quarter of the stage's batch free for the rooms that load next; when there isn't enough
room it is left out for that visit (with a line in the log). Cut grass grows back, and a removed or
moved batch goes from the game only when its room loads again.

Objects whose collision keeps the game's size stay at scale 1, so what you see and what Link
bumps into always match.

The stone block (`game:push-block`, `"requires": {"game_objects": 6}`) slides on the ground Link pushes it
over, the islands' or a world object's, and falls when it leaves it. It has no switch, so it starts at its
placement whenever its room loads again, as the game's own blocks without one do: a block pushed into a
corner, or off a cliff, is back after the next visit. It stands on its highest ground under it, so it can
cover a hole narrower than itself (about 140 units across for its 150) and Link pushes it aside to open the
way; leave room around such a spot, so it can be pushed off in more than one direction.

Game objects count toward the 16 actors per room (all packages together). The game objects of a
room also share a memory budget of 3 MiB (each object about 15–20 KiB; their archives are already
in memory). A game object returns when its room loads again, and full and portable save states
keep it. If the player's game doesn't have an object (another game version), it is left out with a
line in the log, and the rest of the mod works.

To see which objects your game has, run
`python3 tools/story_mods/game_objects.py --game <game folder> --list` (add `--placements` to see
how often the game itself places each one). It reads your game files and prints no game data.

## Island buildings (game pieces)

A mod can also place some of the islands' own buildings: `"actor": "piece:outset-house-small"`.
These are parts of an island's model that stand on their own there (whole buildings, never a cut
through a larger mesh). When the game starts with such a mod, or when you place one in the editor,
the port builds the building once from the player's own game files into the player's model cache;
nothing of the game goes into your package. A building looks as it does on its own island: it is
drawn with the island's light (day, dusk, night, weather and fog), its windows light up at night,
and it keeps the island's collision, so Link walks on its floors and balconies and bumps into its
walls. Its doors lead nowhere: the doorways are solid.

```json
{
  "requires": {"game_objects": 2},
  "actors": [
    {"id": "house", "actor": "piece:outset-house-small", "stage": "sea", "room": 12,
     "position": {"x": 1200, "y": 140, "z": -199000}, "rotation": {"x": 0, "y": 90, "z": 0}}
  ]
}
```

Buildings share the game objects catalogue version and need version 2 or later. A building takes
only `id`, `actor`, `stage`, `room`, `position` and `rotation` (about the vertical axis): no scale,
because its light is baked for its size and its collision is the island's own. The position is the
middle of the building at its lowest point.

| Id | What it is | Window at night |
| --- | --- | --- |
| `piece:outset-house-ladder` | A two-storey island house with a balcony and a side ladder | yes |
| `piece:outset-house-lookout` | A family house on posts with a rooftop lookout and a crawl hole | yes |
| `piece:outset-house-terrace` | A tall island house with a steep roof and a wooden terrace | yes |
| `piece:outset-house-small` | A small island house on posts with a pointed roof | yes |
| `piece:outset-lookout-tower` | A tall wooden watchtower on a single post with a boardwalk | |
| `piece:outset-well-shelter` | A small open wooden shelter with a slanted roof | |
| `piece:flight-platform` | The tall two-level wooden flight platform on stilts, with its ramp, ladder and roof (without the goal posts) | |
| `piece:oasis-cabana` | The Private Oasis holiday cabana with its thatched roof, veranda and lamps | |
| `piece:tingle-tower` | Tingle's painted wooden tower with its ladder and fenced lookout (without the head on top) | |

**Island parts** (the palette's "Island parts", catalogue version 3) are made the same way from other
islands' room models: Dragon Roost's shrine pieces and walkways and its sea rocks, Windfall's windmill
cap, and the Forsaken Fortress shipwrecks. Each has collision of its own and takes the light of the
room it stands in.

| Piece | What it is | Its base |
|---|---|---|
| `piece:dragon-roost-shrine` | Dragon Roost's round stone shrine platform with its steps and torii | 1000 units under its walkable top: place it at ground height minus 1000 |
| `piece:dragon-roost-torii` | the shrine gate alone: two pillars and their cross beams | at the ground |
| `piece:dragon-roost-walkway` | a straight wooden plank walkway | at the ground |
| `piece:dragon-roost-walkway-turn` | a wooden plank walkway with a bend | at the ground |
| `piece:dragon-roost-rope-posts` | two wooden posts joined by a rope | at the ground |
| `piece:sea-rock-spire` | a tall, slim rock standing in the sea | 1000 units under the sea: place it at y -1000 |
| `piece:sea-rock-pillar` | a rock pillar rising from the sea | 1000 units under the sea |
| `piece:sea-rock-stack` | a narrow rock stack in the sea | 1000 units under the sea |
| `piece:sea-rock-broad`, `piece:sea-rock-low`, `piece:sea-rock-small`, `piece:sea-rock-mid` | more of Dragon Roost's sea rocks: a broad one with a flat top Link can stand on, a low one, a small stack and one half as tall as the spire (catalogue version 6) | 1000 units under the sea |
| `piece:fire-mountain-rock`, `piece:fire-mountain-rock-small` | the two dark volcanic rocks from beside Fire Mountain (catalogue version 6) | 500 units under the sea: place it at y -500 |
| `piece:windfall-windmill-cap` | the top of Windfall's windmill: its pointed roof, the gallery ring and posts | at its gallery floor |
| `piece:shipwreck-hull` | the hull of a wrecked ship, upright in the sea with its broken mast | about 370 units under the sea |
| `piece:shipwreck-bow` | the bow of a wrecked ship lying in the sea | about 1050 units under the sea |

A piece's position is its lowest point; the rocks, the shrine and the wrecks reach under the sea in their
own islands, so their position is that far below the sea or ground. The island's water surface is not
ground for the placement check, so they may stand in the sea.

The buildings of one island share their textures and are built together (about 5.1 MiB of game
memory for the Outset set while a room uses it). All the buildings of a room, every package's,
share a budget of 24 MiB: each set once, and each building about 50 KiB (all four sets of today
together take about 17 MiB). A mod that goes over it is refused with the reason. Buildings count toward the 16 actors per room, return when their room
loads again, and full and portable save states keep them. A building may stand on an island, but not
in the way of what the island needs, by the same rule as every world object (world-objects.md): its
footprint (with 50 units around it) must keep clear of the island's villagers and their walking routes,
chests, doors, items, switches, the places where Link arrives, cutscene and message areas and other objects
(an object whose size isn't known counts with a radius of 120 units; pots, signs, rupees, message and hint areas don't count, see world-objects.md). Grass, flowers, trees, palms and
birds may be under it, and unlike other world objects it may stand against the island's slopes and walls
as its original did. It also stays in its room's square and clear of other world
objects. A place that isn't clear is kept but not in the game: in the editor the footprint turns red and says what
is in the way, an MCP `add_actor` or move returns `not_active` with every reason (with the game's own
names), Check and export list it, and when the game starts it is left out with a line in the log.

The buildings of one set share their materials: copies of one set in neighbouring rooms that are seen
at once may share the light of one room.

If the player's game files can't make a building (another game version, or the island's model
differs from what the port expects), its placements are left out with a line in the log, and the
rest of the mod works. The package must not contain game files, as for game objects.

## Island grounds

The ground of some small islands can be placed too, in the palette's **Islands** category: the
island's rock and turf with its cliffs and its base under the sea, bare (no grass, flowers, trees or
other objects, which are the game's actors, not part of the ground). The few trees and rocks that are part
of an island's ground model itself (Cliff Plateau Isles, Mother & Child Isles) come along. Like the
buildings, an island is built once from the player's own game files; nothing of the game goes into your
package.

```json
{
  "requires": {"game_objects": 4},
  "actors": [
    {"id": "crescent", "actor": "piece:crescent-moon-island", "stage": "sea", "room": 44,
     "position": {"x": -174600, "y": 0, "z": 301125}, "rotation": {"x": 0, "y": 30, "z": 0}}
  ]
}
```

The first six island grounds need catalogue version 4, the others version 5 (`"requires":
{"game_objects": 5}`; the editor writes the version an island needs). The position is the island's middle at its sea level: place it
at `"y": 0` on the sea (the editor does this for you). Like a building, an island takes only `id`,
`actor`, `stage`, `room`, `position` and `rotation` about the vertical axis.

| Id | What it is |
| --- | --- |
| `piece:crescent-moon-island` | Crescent Moon Island: a low crescent of rock around a sheltered cove |
| `piece:triangle-island` | Triangle Island: a small grassy islet with a shallow cliff and a rock beside it |
| `piece:five-star-isles` | Five-Star Isles: five rocky pillars in a row with flat tops |
| `piece:star-island` | Star Island: star-shaped, with grassy slopes and a pit in the middle |
| `piece:bomb-island` | Bomb Island: a long island of grassy cliffs with a small pit |
| `piece:fairy-island` | Northern Fairy Island: a heart-shaped grassy island with the fountain's spiked stone hut; its doorway leads to a hole |
| `piece:thorned-fairy-island` | Thorned Fairy Island: a fairy island with the fountain's spiked stone hut and a small islet beside it; its doorway leads to a hole |
| `piece:shark-island` | Shark Island: terraced cliffs with a pit on its top |
| `piece:horseshoe-island` | Horseshoe Island: a ring of grassy cliffs around a lagoon, with three small hollows |
| `piece:rock-spire-isle` | Rock Spire Isle: a row of rock arches and spires on a grassy base, with a pit |
| `piece:seven-star-isles` | Seven-Star Isles: seven rock pillars rising from the sea |
| `piece:overlook-island` | Overlook Island: tall pillars of grassy rock with steep walls and a pit on top |
| `piece:stone-watcher-island` | Stone Watcher Island: a grassy island with cracked stone walls and a pit |
| `piece:cliff-plateau-isles` | Cliff Plateau Isles: flat-topped cliffs in a cluster, with a few trees and two pits |
| `piece:angular-isles` | Angular Isles: two blocky islands of stepped, angular rock |
| `piece:boating-course` | Boating Course: two small grassy islets, each with a post |
| `piece:gale-isle` | Gale Isle: a tall rocky mountain with a sandy beach; its doorway into the rock leads nowhere |
| `piece:headstone-island` | Headstone Island: a great dome of rock with a sandy beach; its doorway into the rock leads nowhere |
| `piece:greatfish-isle` | Greatfish Isle: a broken island of rocky slopes, sandy beaches and the ruins of a house |
| `piece:cyclops-reef` | Cyclops Reef: a ring of stepped orange rock walls around a lagoon with a small islet (no cannons) |
| `piece:two-eye-reef` | Two-Eye Reef: a ring of stepped grey rock walls around a lagoon with two islets (no cannons) |
| `piece:diamond-steppe-island` | Diamond Steppe Island: grassy terraces with ponds on three levels, joined by waterfalls, and a pit on top |
| `piece:mother-and-child-isles` | Mother & Child Isles: a great tree-topped rock with its spring pool, and a small islet beside it |
| `piece:fire-mountain` | Fire Mountain: a volcano of dark rock, without its eruption; its crater is a pit that can hold a lava pool |

Bird's Peak Rock is not available: its textures alone are larger than the 12 MiB an island's archive may
have. Islands that exist several times in the game (the Triangle Islands, the fairy islands, the reefs,
Seven-Star Isles and the Star Belt Archipelago) are offered once or twice.

An island keeps its own collision: the ship stops at its shore, Link gets out and climbs onto it,
walks its slopes and bumps into its cliffs and walls, as on the island itself. Its paths into
caves and fountains lead nowhere: the pits and holes (Star, Bomb, Shark, Rock Spire, Overlook, Stone
Watcher, Cliff Plateau and Diamond Steppe's pits, the fairy islands' holes, Fire Mountain's crater) and
the doorways of Gale Isle and Headstone Island are drops that send Link back to the room's restart point,
as a bottomless pit does.

Its ground keeps the island's own ground types, the same ones your own models' collision uses
(world-objects.md, "Author collision in the GLB"):

| Ground | On an island of the game |
| --- | --- |
| grass, sand, stone, wood, metal, ice | the island's ground: Link walks (or slides) and his steps sound as on the island |
| wall | its cliffs and walls |
| water, shallow water, waterfall | the ponds, pools and falls of the islands that have them |
| void | the pits, holes and doorways that lead nowhere |
| lava | (no island has lava of its own: add a lava pool, for example in Fire Mountain's crater) |

Diamond Steppe Island and Mother & Child Isles bring their own water: the ponds, the spring pool and the
waterfalls with the island's own moving water, and the water's collision, so Link wades and swims in it as
on the island. The splash and spring effects and the sound of the falls are the game's actors and don't come
along. Fire Mountain comes without its eruption (an actor and effects of the game, not part of the ground).
Its crater can be filled with lava: the editor's water tool makes a lava pool there (a pond with **Lava**,
`add_pond` with `"lava": true`; its outline round the crater's mouth, its level a little below the rim), drawn
with the mountain's own moving lava and burning Link as the game's lava does (see world-objects.md, "Lava").
An island also brings its own distant view: from the eight squares around its own, the island's
low-detail model (the one the game shows for it from afar) stands where you placed it. Only Link's own
square has the island's collision.

An island is large (up to about 13,000 units across: Greatfish Isle and the reefs): choose a stretch of open sea in the square, away
from the square's own island and its reefs, routes and objects. The usual placement rules apply
(world-objects.md): what is in the way is listed in plain words, and an island that doesn't fit is kept
in the editor but not in the game. The sea's surface around an island is not ground and doesn't count.
An island costs about 0.5 MiB (Triangle Island) to 7.2 MiB (Mother & Child Isles; most are 1.2 to 4.3 MiB)
of its room's 24 MiB budget for buildings and island grounds, and its distant view about 0.05 to 0.2 MiB
in a neighbouring square.

## Breakable native props

The experimental prop adapter uses the player's native models, ground physics,
target collision and break effects. Packages ship no native models or profile
metadata. A private runtime profile separates these copies from retail pot
searches and shop/container cohorts. Native acceptance of this new adapter is
still pending.

```json
{
  "id": "garden-pot", "actor": "pot", "stage": "sea", "room": 11,
  "position": {"x": 800, "y": 1040, "z": -201500},
  "broken_flag": "garden-pot.broken",
  "drops": [{"item": "item:coral-shard", "quantity": 2}]
}
```

The item must be declared by the same package. `drops` is required and may be
empty or contain up to eight distinct items, each with quantity 1–254. Each prop
needs its own distinct `broken_flag`. These markers have a separate story-record
namespace from ordinary actor flags. Breaking adds all declared counts directly
to the mod Delivery Bag and persists the broken marker in one transaction.
Capacity, stack or invalid-store failures grant nothing and set no marker;
the physical prop can still break. A failed instance is not immediately respawned:
it can retry after a real room/stage transition or a full/progress load. A committed
marker prevents later respawn. Deletion, room teardown and eligibility changes
never grant drops. Full states restore both the native instance and mod story state.
A restored prop must match its captured package, actor ID and complete declaration.
Changed or reordered declarations fail closed until a real room/stage transition;
compatibility across edited manifests is not promised for native residents.

Pots and crates keep native sword break behavior. Barrels require a native bomb
or hammer hit; an ordinary sword does not break them. Props permit yaw rotation
only and uniform XYZ scale from 0.1 to 4. Existing `when` conditions are supported.
Raw parameters, game drops, switches, health overrides, custom models, dialogue,
`then`, paths, carrying and throwing are unsupported. Owned copies cannot be lifted
or hook-carried, activate native weight switches, or register outgoing attack
collision; their incoming target collision and solid collision remain native.
These restrictions apply to placed copies; retail props keep their original behavior.

### Switches and doors

Switches use a mod-owned flag. A door refers to a switch actor ID, including a
switch declared later in the actor list. Several doors may share a switch.

```json
{"id":"courtyard-switch","actor":"switch","stage":"sea","room":11,
 "position":[100,1040,-201500],"switch_flag":"courtyard.open"}
```

```json
{"id":"courtyard-gate","actor":"door","stage":"sea","room":11,
 "position":[400,1040,-201500],"switch":"courtyard-switch"}
```

The switch latches when fully pressed. The gate follows that flag using its native
opening and closing motion and solid collision. Retail switches, doors and save
flags retain their original behavior. These adapters do not run retail event
scripts, pause requests, room transitions or key checks. Only the fixed yellow
floor switch and rotating gate are accepted; raw parameters and other variants
are rejected. Use yaw-only rotation and scale 1. Dialogue, generic `flag`, `then`,
drops and custom models are unsupported for these types.

Full-state restoration requires the captured declaration to match the current
mod declaration. A mismatch disables that copy until a real room/stage transition.
Progress loads recreate switches and gates using the current mod-owned switch
flags; they do not rewind those flags. Full-state loads restore the captured
flags and actor motion, subject to the port's normal thread-safety checks.

## Native enemies (preview)

Place these fixed enemy variants with native movement and combat using the
player's game resources. The declaring mod owns their rewards and defeat
persistence. The preview has been checked in game at 30 fps, including each type's
defeat and save safety, with targeted full-state and progress-load checks.
The allowlist below defines the supported variants.

| `actor` | Fixed variant |
| --- | --- |
| `chuchu` | Ordinary green ChuChu, or a declared colour (below) |
| `bokoblin` | Ordinary type-4 Bokoblin, or a declared weapon variant (below) |
| `moblin` | Ordinary Moblin |
| `keese` | Ordinary Keese, the native KI bat, or the Fire Keese (below); not Morth |
| `miniblin` | Ordinary Miniblin: hidden until Link comes near and looks its way; never respawns |
| `peahat` | Ordinary Peahat: it flies; knock it down (the boomerang, the Deku Leaf's wind, the hookshot), then strike. Or the Seahat (below) |
| `darknut` | Darknut guarding its place with its sword, or a declared variant (below) |
| `rat_hole` | A rat hole three rats come out of (see [Rat holes](#rat-holes)) |
| `bokobaba` | Ordinary Boko Baba: a biting plant that stays where it is placed (put it on the ground). Strike its head to stun it, then cut its stem. It bites, but never grabs and chews Link. When it dies its head doesn't fly off and leave a Boko stick |
| `bubble` | A red Bubble, or a blue one (below): a flaming skull that stays near the ground below it |
| `octorok` | The small Octorok: it rises out of water and spits rocks at Link. Reflect a rock with the shield, or use arrows or the boomerang. Never the big Octorok that fires bombs |
| `stalfos` | Ordinary Stalfos with its club: strike it until it falls apart, then hit its head before it pulls itself together (bombs and the Skull Hammer finish the head at once) |
| `redead` | Ordinary standing Redead: its scream freezes Link, then it grabs and bites him; light stops it |
| `mothula` | Mothula on the ground, or the winged one (below) |
| `morth` | A single Morth: it clings to Link and slows him; a spin attack or rolling throws it off |
| `magtail` | Ordinary Magtail: strike its head to curl it into a ball, then strike the ball (or lift and throw it) |
| `poe` | Ordinary Poe with its lantern: without the lantern it possesses Link for a while |
| `armos` | Armos statue: it wakes when Link comes near; strike its back |
| `armos_knight` | Armos Knight: only a bomb in its open mouth (or a Light Arrow) defeats it |
| `wizzrobe` | Red Wizzrobe: it appears and vanishes around where it was placed and casts fire at Link; or a summoner (variants `summoner_*`, below) |
| `warship` | The enemy warship (sea stage only): it fires its cannon at Link; three hits from the boat's cannon sink it. Variant `route`: it sails one of the mod's paths (below) |
| `big_octo` | **A target only:** the Big Octo (sea stage only), variants `four`, `eight` or `twelve` eyes (default `four`); it never attacks |
| `floormaster` | Floormaster: a hand that rises from its hole, grabs Link for a moment and lets go. Variant `wandering`: its hole glides along one of the mod's paths (below) |
| `gyorg` | A group of Gyorgs (sea stage only): variants `one` to `five` (default `three`); they come out of the sea when Link is near and bite him |
| `kargaroc` | Kargaroc: it circles where it was placed and swoops at Link (variant `perched`: it sits until Link comes near; `moblin_rider` or `bokoblin_rider`: it carries a rider along one of the mod's paths, below) |
| `wall_cannon` | The wall cannon: it turns to Link and fires cannonballs while he is inside its firing area, one of the mod's [room paths](#room-paths) (`"path": "<id>"`, an area, required); a bomb destroys it |

All of them use the same declaration fields:

```json
{
  "id": "courtyard-bokoblin", "actor": "bokoblin",
  "stage": "sea", "room": 11,
  "position": {"x": 100, "y": 1040, "z": -201500},
  "rotation": {"x": 0, "y": 90, "z": 0},
  "defeated_flag": "courtyard-bokoblin.defeated",
  "drops": [{"item": "item:coral-shard", "quantity": 1}]
}
```

Use any other `actor` name of the table for the other fixed variants,
with a distinct actor ID and defeat flag for each copy. Declare reward items in
the same package. `drops` is required and may be empty or contain up to eight
distinct same-mod items, each with quantity 1–254. Only yaw rotation is accepted;
X/Z rotation must be zero and scale must be one on every axis. Existing `when`
conditions apply. Place enemies on suitable native terrain and leave enough
space for their movement and attacks.

A recognized defeat atomically records `defeated_flag` and adds the declared
mod-item counts. Bag placement follows the existing mod inventory rules. The enemy-defeat marker has its own story-record
namespace, separate from ordinary actor and broken-prop flags. Capacity, stack
or store failures grant no items and set no marker; the native enemy may already
have completed its defeat. A committed marker prevents later respawn. Deletion,
room teardown, eligibility changes and constructor failure never grant rewards.
Full states rewind the native instance and mod records together. Progress loads
preserve the current mod story records while rebuilding eligible native actors;
defeated copies stay absent. Restored bindings must match the captured declaration
and ownership generation; compatibility after editing manifests is not promised.

The adapter fixes native parameters and persistence sentinels. Packages cannot
supply raw parameters, retail save bits or item tables, health overrides, capture
or prison variants, dialogue, `then`, paths or custom models for these enemies.
Own Bokoblin/Moblin weapon copies and a Darknut's sword cannot be lifted or taken by Link in this
preview. Combat is restricted to verified Link interactions; owned enemies and
weapons must not damage retail actors or join retail enemy cohorts. Native item
drops are replaced by the declared Bag rewards. Some native disappearance effects
are omitted because their native effect actor also creates retail drops; identical
retail defeat effects are not promised.

What these enemies do to Link stays the game's own and ends on its own: a Bubble's curse or flames,
a Fire Keese's burn and an Octorok's rock wear off as they do in the game. An Octorok's rocks are the
game's own rocks: like the original's, a rock reflected with the shield can hit other enemies of the
game nearby. A Seahat or an Octorok of a mod never counts towards the game's own tally of sea enemies.

### Patrolling Bokoblins and Moblins

A Bokoblin (any variant) or a Moblin with a `path` patrols it with the game's own patrol: it
walks from its position through the points and turns round at the ends (`"mode": "back_and_forth"`)
or goes round (`"mode": "loop"`). When it sees Link it fights him, then goes back to its route. Its
pace and pauses are its own, so `speed` and `pauses` are refused, and so is `once`.

```json
{"id": "beach-patrol", "actor": "moblin", "stage": "sea", "room": 11,
 "position": {"x": 0, "y": 1040, "z": -201500},
 "path": {"points": [{"x": 0, "y": 1040, "z": -201000}, {"x": 500, "y": 1040, "z": -201000}], "mode": "loop"},
 "defeated_flag": "beach-patrol.defeated", "drops": []}
```

The route is the port's own record (the one walking residents use), handed to the game's patrol
code in place of a room path; the game's room data is never read for it. A route has up to 15
points. In the editor, a Bokoblin's or Moblin's Inspector has the Route section.

### Room paths

Some of the game's actors take their path from the room they stand in: a line to follow or an area
to watch. A mod declares its own such paths in a top-level `paths` list, and its actors name one by
id (`"path": "<id>"`, a string; the route objects above stay the way residents, Bokoblins and Moblins
walk):

```json
"paths": [
  {"id": "fort-yard", "stage": "sea", "room": 11, "closed": true,
   "points": [{"x": -800, "y": 0, "z": -197200}, {"x": 800, "y": 0, "z": -197200},
              {"x": 800, "y": 0, "z": -195600}, {"x": -800, "y": 0, "z": -195600}]}
]
```

| Field | |
| --- | --- |
| `id` | the path's id, unique in the mod |
| `stage`, `room` | where it is; an actor uses only a path of its own stage and room |
| `points` | 2 to 32 points, each more than one unit (across) from the one before |
| `closed` | `true`: an area (the last point joins the first; at least 3 points, enclosing an area, its edges not crossing); default `false`: a line |
| `next` | optional: the id of another of the mod's paths in the same room that follows this one (a Wizzrobe summoner summons at its points) |
| a point's `release` | optional `true`: a Kargaroc lets its rider go at that point |

The game's room data is never changed or read for these. Each path gets a number the room's own
paths never use (the game's largest room has 56 paths; mod paths are numbered from 192), and when one
of the mod's own actors asks its room for that number, the port answers with the path's record, in
the game's own layout. The game's own lookup always runs first: a number the room has stays the room's,
and the game's own actors never see a mod path. An area may be drawn either way round. The game's
area test takes the point of the area nearest to what it tests and looks at the corner there, so keep
areas convex (no corners pointing inwards); the editor's `path_inside` command uses the same test.
A mod has up to 16 paths; the enabled mods together up to 64, and 24 in one room. Full save states
keep the paths' records; after a load they are written again from the manifest.

### Routes on room paths: warships and wandering Floormasters

A warship with `"parameters": {"variant": "route"}` and a Floormaster with `"variant": "wandering"` need
`"path"`: one of the mod's [room paths](#room-paths) in their own room, a line or an area.

```json
{"id": "patrol-ship", "actor": "warship", "parameters": {"variant": "route"}, "stage": "sea", "room": 11,
 "position": {"x": -800, "y": 0, "z": -195800}, "path": "ship-lane",
 "defeated_flag": "patrol-ship.defeated", "drops": []}
```

Both move with the game's own path movement, which always goes round: after the last point they head
back to the first, so a line of two points is sailed (or glided) there and back as a loop. The warship
turns in its own wide circle, so it may turn before reaching an end; it sails at the sea's height (the
points' heights don't matter), slows to a stop to fire at Link and sails on. The Floormaster's hole
glides along the points and rises when Link comes near, then sinks and glides on. Neither has a wall
check of its own: the editor warns (it never refuses) when a warship's route crosses land (a route over
land holds the ship at the shore) or a Floormaster's route leaves the floor (`check_path`, and the
Inspector's Path section). The anchored warship and the ordinary Floormaster take no path.

### Kargarocs with a rider

A Kargaroc with `"parameters": {"variant": "moblin_rider"}` (or `"bokoblin_rider"`) carries a Moblin (or a
Bokoblin) and flies one of the mod's room paths (`"path"`, its own room): round if the path is an area,
there and back if it is a line. At a point of the path marked `"release": true` it lets the rider go;
the rider drops and fights Link. A rider path needs at least one release point:

```json
"paths": [{"id": "raid", "stage": "sea", "room": 11, "closed": true, "points": [
  {"x": -800, "y": 900, "z": -197000}, {"x": 800, "y": 900, "z": -197000},
  {"x": 800, "y": 700, "z": -196200, "release": true}, {"x": -800, "y": 900, "z": -196200}]}]
```

The rider is an owned enemy of its own: its id is the Kargaroc's with `.rider`, its defeat flag the
Kargaroc's with `.rider`, and it gives the Kargaroc's `drops` (the Kargaroc gives them too); its native
drop is replaced like any owned enemy's. A rider still being carried goes with the Kargaroc (removed,
unloaded or defeated); a released rider fights on until it is defeated or the room unloads. A rider that falls into
the sea drowns as the game's own do, and that is its defeat (its flag and drops). A defeated rider is
not made again: the Kargaroc then flies without one. Ids and defeat flags of rider Kargarocs are at most
26 characters. In the editor, a point of the path is marked in the Path section ("Lets its rider go
here") or with `set_path_point`.

### Wizzrobe summoners

A Wizzrobe with a summoner variant appears at the points of one of the mod's paths (`"path"`, its room)
and summons enemies at the points of that path's next path (the path's `"next"`, another of the mod's
paths in the same room; both are required). It is the game's own summoner: it throws a summon ball,
the ball opens a summon door, and the door brings out one group of its wave (the game picks the group
at random each time); when all of them are gone it summons again.

| `variant` | The wave it summons |
| --- | --- |
| `summoner_keese` | Keese (up to 8) or Magtails (1 or 2) |
| `summoner_keese_bubbles` | Keese (up to 8) or Bubbles (1 or 2) |
| `summoner_bubbles` | Bubbles (1 or 2) |
| `summoner_morths` | Morths, 15 or 20 at a time |
| `summoner_chuchus` | ChuChus, 8 or 10 at a time |
| `summoner_few_morths` | Morths, 8 or 10 at a time |

```json
"paths": [
  {"id": "hall", "stage": "M_NewD2", "room": 0, "closed": true, "next": "spots", "points": [...]},
  {"id": "spots", "stage": "M_NewD2", "room": 0, "closed": true, "points": [...]}
],
"actors": [{"id": "conjurer", "actor": "wizzrobe", "parameters": {"variant": "summoner_morths"},
  "stage": "M_NewD2", "room": 0, "position": {"x": -1000, "y": 150, "z": 8300}, "path": "hall",
  "defeated_flag": "conjurer.defeated", "drops": []}]
```

The summoned enemies are the port's own: owned like any placed enemy (the same rules for what they do
to Link and to the game), but they have no flag and give no drops; only the summoner's own defeat sets
its flag and gives its drops. A summon that is defeated is gone for that wave; the next wave brings new
ones. Summons still there when the summoner goes (defeated, removed or unloaded) go with it. A
summoner's summons don't count toward a room's 16 actors; they count toward the 128 owned actors of
all enabled mods (a summoner of Morths counts 21). One summoner per room, two per mod. Its id and
defeat flag are at most 23 characters (its summons add `.summon20`).

### Wall cannons

A wall cannon needs its firing area: one of the mod's [room paths](#room-paths), closed, in its own
stage and room, named by `"path"`:

```json
{"id": "fort-cannon", "actor": "wall_cannon", "stage": "sea", "room": 11,
 "position": {"x": 0, "y": 900, "z": -196000}, "rotation": {"x": 0, "y": 180, "z": 0},
 "path": "fort-yard", "defeated_flag": "fort-cannon.defeated", "drops": []}
```

It is the game's own cannon: it turns towards Link (about 50 degrees to either side of where it faces)
and fires while he, and the spot it aims at, are inside the area; it leads its shots when he is far.
Its cannonballs hit only Link and his boat. A bomb (or the boat's cannon) destroys it in one hit; its
defeat sets `defeated_flag` and gives the `drops`, and the destroyed cannon goes from the room. It sets
no switch, never waits hidden for one and has no native drop. Keep its area convex and in front of it.
Several cannons may share one area.

### Rat holes

`"actor": "rat_hole"` places a rat hole. Three rats come out of it, one after the other: the next
one when the one before is defeated, while Link is near the hole (within about 13 metres, not right
on top of it). They are the game's own rats: they climb out of the hole, run at Link when he has his
back to them, jump on him and steal rupees, which they drop again as ordinary rupees when they are
hit. The variant picks the rats:

| `variant` | Rats |
| --- | --- |
| `rats` (the default) | Three ordinary rats |
| `bomb_rats` | Three bomb rats: each lights a bomb, carries it to Link, throws it and runs back into the hole |
| `mixed` | An ordinary rat, a bomb rat, then an ordinary rat |

```json
{"id": "cellar-hole", "actor": "rat_hole", "parameters": {"variant": "mixed"},
 "stage": "sea", "room": 11, "position": {"x": 0, "y": 1040, "z": -201500},
 "defeated_flag": "cellar-hole.closed", "drops": [{"item": "item:coral-shard", "quantity": 1}]}
```

Every rat is an enemy of its own: defeating it records `<defeated_flag>.rat1` (`.rat2`, `.rat3`)
and gives the hole's `drops`, so `drops` are given once per rat. When all three are defeated the
hole closes: its `defeated_flag` is set and the hole is gone for good; a `defeat_switch` on the hole
is set at that moment. A rat that gets away comes out again later: a bomb rat throws its bomb at Link and
runs back into the hole, and a rat that falls into deep water or carries something off is gone for now
too. Only a rat Link defeats counts.

A bomb rat's bomb belongs to the mod: its blast hurts Link and the mod's own enemies, but never the
game's pots, barrels, switches, enemies or anything else of the game, and Link cannot pick it up.
The hole is not blocked by objects set on it, as the game's own holes are, and it never calls the
rat shop keeper. The hole's `id` and `defeated_flag` are at most 27 characters (the rats add `.rat1`
to `.rat3`), and no other actor may use those rat names. A hole counts as four actors (itself and its
rats) toward the 16 per room and 64 per mod; a room has at most two rat holes. In the editor, a
placed hole's Inspector has **Variant**; removing the hole removes its rats too.

### What the other enemies do, and what they leave out

Each of these is the game's own enemy with its native movement and attacks; the declaring mod
owns its defeat flag and drops, as above. Some parts of the original are left out where they would
reach outside the mod:

- **Stalfos:** its club belongs to it and leaves with it; Link cannot take it. A spin attack that
  breaks it does not split it into a walking upper body and kicking legs (the upper body would be an
  enemy of the game with its own drops): it falls apart as from any other finishing blow, and the
  head must still be hit.
- **Redead:** its scream and its bite hold Link the way the game does. If the Redead is removed (by
  the editor, a room change or a state load) while it holds him, Link is free at once. It is not
  created on the four stages whose game events have no Redead scream (`Ebesso`, `kazan`, `Msmoke`,
  `tincle`).
  Outdoors it can be stopped with reflected light only where the game has light; Light Arrows
  destroy it.
- **Mothula:** it fights alone; it does not release Morths (they would be the game's own Morths with
  their drops). Place Morths next to it instead. A winged one that dives far from where it was
  placed turns back. Its wings fly off as in the game.
- **Morth:** place several for a swarm. A Morth that clings to Link stays in its room: when the room
  changes, or the Morth is removed, it lets go of Link.
- **Magtail:** Link can lift the curled ball and throw it; a thrown ball only hurts Link and the
  mod's own enemies, not the world's objects.
- **Poe:** its lantern belongs to it and leaves with it. Without the lantern it possesses Link (the
  stick turns around for a while); removing the Poe ends the possession at once. Outdoors, where the
  game has no light, only Light Arrows defeat it.
- **Armos:** Link cannot carry a stunned Armos (carried away from its room it would vanish without a
  defeat). In water it returns to where it was placed.
- **Armos Knight:** it needs Link to have bombs (or Light Arrows). If it is removed while it holds a
  swallowed bomb, the bomb goes with it. Its spikes only hurt Link and the mod's own enemies.
- **Wizzrobe:** only the red one; the summoner (it calls other enemies) is not offered. Its fireballs
  belong to it: they only hurt Link (they never light the world's torches) and vanish with it.
- **Warship:** only on the sea; it stays where it was placed (never the ones that sail a route or the
  golden treasure ship). Its flag and its cannonballs belong to it: the cannonballs only hurt Link and
  his boat and go with the ship when it is removed. As in the game, its aim is loose: many of its
  shots miss. The other "ship" pieces of the sea (the lookout
  platforms, the Forsaken Fortress searchlights and wall cannons) are not offered.
- **Big Octo (a target only):** this is not the game's full fight. It never swallows the boat, makes
  no whirlpool and does not attack: it rises out of the sea when Link sails within its range on his
  boat, and he shoots its eyes (arrows, the boomerang, the hookshot, bombs or the boat's cannon). When
  the last eye is hit it is defeated and vanishes at once (no death scene, no earthquake). It has no
  battle music.
- **Floormaster:** it grabs only Link (never bombs, pots or other characters), and only for a moment:
  it lets go instead of dragging him down, so it never sends him anywhere. It never makes a skull pot.
  As in the game it only comes up on flat floor (all round its hole): on slopes, stairs or a narrow
  pier it stays below.
- **Gyorgs:** only on the sea, and, as in the game, only once Link has the boomerang (until then
  nothing appears; the log says why). The whole group is one enemy with one `defeated_flag` and one
  set of `drops`: it is defeated when every Gyorg of the group has been killed. Gyorgs that dive away
  because Link left leave it undefeated, and the group comes back with the room. They only bite Link
  and his boat, and killing them does not count towards the game's own Gyorg tally.
  As in the game, they only come out in open water: not close to an island, a pier or one of the
  sea's ships (Beedle's shop, rafts, the salvage ship), so place them well out at sea.
- **Kargaroc:** it never carries Link or another enemy (the game's riders are not offered). It eats
  bait Link throws near it, as in the game, and goes back to where it was placed when he leaves.

None of them reads or writes the game's switches, and none sets the game's own enemy records; a
fall out of the world (into deep water, or off the island) removes the copy without a defeat, and
it comes back with its room.

### Opening a door when enemies are defeated

`"defeat_switch": "<switch actor id>"` on an enemy (a native one or your own combat model) names one of
the mod's switch actors. When every enemy of the mod naming that switch is defeated, the switch's
`switch_flag` is set, so the doors that follow it open. The game's own switches are never used. The
defeat markers and the switch flag are mod story records, so a save state or a progress load restores
them together; a defeat that happened just before is applied on the next game step.

```json
{"id": "gate-switch", "actor": "switch", "stage": "sea", "room": 11,
 "position": {"x": 0, "y": 1040, "z": -201500}, "switch_flag": "gate.open"},
{"id": "gate", "actor": "door", "stage": "sea", "room": 11,
 "position": {"x": 300, "y": 1040, "z": -201500}, "switch": "gate-switch"},
{"id": "guard-1", "actor": "bokoblin", "stage": "sea", "room": 11, "position": {"x": 100, "y": 1040, "z": -201200},
 "defeated_flag": "guard-1.defeated", "drops": [], "defeat_switch": "gate-switch"},
{"id": "guard-2", "actor": "miniblin", "stage": "sea", "room": 11, "position": {"x": -100, "y": 1040, "z": -201200},
 "defeated_flag": "guard-2.defeated", "drops": [], "defeat_switch": "gate-switch"}
```

Stepping on the switch still sets it as well. In the editor, an enemy's Inspector has
**Sets switch when defeated**, and a switch's or door's Inspector lists the enemies that set it.

### Variants

Some enemies have declared variants: name one with `"parameters": {"variant": "..."}`.
Without it the enemy is its ordinary kind. A variant is a fixed choice the port has
checked; packages still cannot give raw parameters.

| `actor` | `variant` | What it does |
| --- | --- | --- |
| `chuchu` | `green` | The ordinary ChuChu (the same as no variant) |
| `chuchu` | `red` | Jumps at Link when he comes close |
| `chuchu` | `blue` | Sparks with electricity: a sword hit shocks Link; bombs or a stun first |
| `chuchu` | `yellow` | Jumps at Link and sparks with electricity |
| `chuchu` | `black` | Swords bounce off. Light (the Mirror Shield's reflection, the game's light beams) turns it to stone for a while: Link can lift and throw it to break it, or break it with the Skull Hammer |
| `bokoblin` | `stick` | The ordinary Bokoblin with a stick (the same as no variant) |
| `bokoblin` | `shield` | Carries a shield as well: it blocks and takes more hits |
| `bokoblin` | `machete` | Fights with a machete instead of the stick |
| `darknut` | `darknut` | The ordinary Darknut in armour with its sword (the same as no variant) |
| `darknut` | `shield` | Carries a shield as well: it blocks Link's sword |
| `darknut` | `helmet` | Wears a helmet and carries a shield: it takes twice the hits |
| `darknut` | `mighty` | The Mighty Darknut: helmet, shield and cape, and it notices Link from further away |
| `keese` | `normal` | The ordinary Keese (the same as no variant) |
| `keese` | `fire` | The Fire Keese: its bite sets Link on fire for a moment; fire doesn't hurt it. (The game has no Ice Keese.) |
| `peahat` | `peahat` | The ordinary Peahat (the same as no variant) |
| `peahat` | `seahat` | The Seahat: bigger, it hovers over the water and dives at Link only while he sails the boat. Place it over the sea |
| `bubble` | `red` | The Red Bubble (the same as no variant): its flames set Link on fire. Strike it, or first put its fire out with the Deku Leaf's wind or a bomb |
| `bubble` | `blue` | The Blue Bubble: its touch curses Link, who can't use his sword or items until the curse wears off. Put its fire out with the Deku Leaf's wind or a bomb (a sword or the boomerang only bounce it off while it burns), then strike the skull |
| `mothula` | `ground` | The Mothula on the ground, without wings (the same as no variant) |
| `mothula` | `winged` | Flies and dives at Link; strike its wings off first |
| `kargaroc` | `flying` | Circles where it was placed and swoops at Link (the same as no variant) |
| `kargaroc` | `perched` | Sits where it was placed until Link comes near |

```json
{"id": "pier-chu", "actor": "chuchu", "parameters": {"variant": "blue"},
 "stage": "sea", "room": 11, "position": {"x": 100, "y": 1040, "z": -201500},
 "defeated_flag": "pier-chu.defeated", "drops": []}
```

Every colour uses the ordinary ground ChuChu with no game switch; defeat, drops and
saves work as for the green one. A blue or yellow ChuChu can only be cut while it is not
sparking: stun it first (the boomerang or a bomb), then use the sword. The stone black
ChuChu is the one placed enemy Link may lift; breaking it counts as its defeat. Every Bokoblin variant is the ordinary waiting and
searching Bokoblin without a path or game switch; its weapon stays the placed copy's
own and cannot be taken. A Bokoblin can also patrol (above). The Moblin has no safe variant beyond the ordinary one: its lantern mode is the
Forsaken Fortress searchlight guard (it raises the fortress alarm and reads the game's collection
state), its flying mode needs a Kargaroc carrying it, its statue mode reads the game's Hyrule
state. Its patrol is offered (above). Other names, and variants for an enemy that has
none, are validator errors.

Every Darknut guards the place you put it and fights Link when he comes near; it has no path and
no game switch (defeating it opens no gate and makes no chest appear in the game; use your own
`defeated_flag`, or `defeat_switch` to open one of your doors). Its armour, helmet and shield come off as in the game, and its sword can be
knocked out of its hands; then it lies on the ground until the Darknut picks it up again. A Darknut
picks up only its own sword, and Link cannot take it. The Mighty Darknut's cape tears under Link's
sword (other attacks pass through it) and falls away with the armour. When the Darknut
is defeated, removed in the editor or its room unloads, its sword and cape go with it. A full save
state keeps it as it was (pieces off, sword on the ground); a portable one, like a room change,
brings it back whole unless it was defeated. The game's frozen Darknuts (Hyrule Castle), the
patrolling ones and the event Darknuts (the Master Sword chamber, the mini-boss ones) read game
switches, paths or events and are not offered.

Packages ship no game models, weapons, animations, shaders or global resource
replacements for these declarations. Retail originals retain their own behavior
and progress. The variants above are the entire preview allowlist; other elemental,
quest and alternate weapon variants are unsupported.

## Original mod enemies (combat preview)

An original skinned mod actor can opt into `combat`. The original patrol example is in
[`examples/guest-mods/sdk-v3-enemy`](../examples/guest-mods/sdk-v3-enemy/).
Its guest behavior requests attacks; the runtime owns health and reactions.
The example was exercised at 30 fps against native Link combat, including
guarded and unguarded attacks, Link's damage invulnerability, sword flinch and
defeat, boomerang stun, lock-on priority, and native heart pickup.

Add these fields to a placed `kind: "mod"` actor using a declared skinned model:

```json
{
  "collision": {"radius": 35, "height": 140, "mode": "solid"},
  "combat": {
    "health": 6,
    "invulnerability_frames": 15,
    "flinch": {"clip": "flinch", "frames": 12,
               "knockback_speed": 180, "knockback_frames": 6},
    "stun": {"clip": "stun", "frames": 30},
    "death": {"clip": "death", "frames": 30},
    "attack": {"damage": 1, "radius": 55, "height": 100,
               "forward_offset": 55, "y_offset": 0,
               "active_frames": 8, "cooldown_frames": 45},
    "target_priority": "normal"
  },
  "defeated_flag": "patrol-enemy.defeated",
  "drops": [],
  "native_drops": [{"item": "heart", "quantity": 1}]
}
```

The model must contain `idle` plus the named stun and death clips; any named
flinch clip must also exist. Combat requires yaw-only rotation, scale one and
solid body collision with radius at most 200 and height at most 400. Dialogue,
prompts, `then` and generic actor flags cannot be combined with combat.
`when` conditions still control eligibility. Configuration is immutable after
registration; attack requests cannot supply new damage or collision types.

| Field | Accepted values |
| --- | --- |
| `health` | Required integer 1–255 |
| `invulnerability_frames` | 0–900; default 15 |
| Reaction `frames` | 0–900; a named clip requires a positive value |
| Flinch `knockback_speed`, `knockback_frames` | Speed 0–1500; frames 0–30; both positive or both zero |
| Attack `damage` | Required integer 1–4 native quarter-heart units; native Hero Mode rules still apply |
| Attack `radius`, `height` | Required 1–200 and 1–400 |
| Attack `forward_offset`, `y_offset` | Optional 0–300 and −200–300 |
| Attack `active_frames` | Required integer 1–150 |
| Attack `cooldown_frames` | Required integer from `active_frames` through 900 |
| `target_priority` | `low`, `normal` or `high`; default `normal` |

All frame durations use 30 Hz game logic, never display frames. Priority changes
only the owned actor's native attention weight; it does not rewrite retail actors
or guarantee selection over every competing target. Knockback uses native
background wall/ground correction and can be refused at a boundary.

Incoming combat accepts exact Link attacks or explicitly verified Link projectile
lineage. Zero-power ordinary hits do not remove health. Verified boomerang and
hookshot hits apply damage-free stun; Link hammer hits can damage and stun.
Native outgoing collision supplies Link's ordinary shield and invulnerability
path. The 30 fps game runs confirmed that guarding prevents damage during active
attack windows and that an unguarded hit follows the native damage-wait timer.
Sword and boomerang reactions were also checked in game; hookshot and hammer
lineage are bounded by the same source contract but were not separately exercised
in those runs.
The runtime protects reaction/death animations and disables attacks while the
actor is reacting, dying, scene-owned or quarantined.

`defeated_flag` and a `drops` array are required; `drops: []` is valid. Nonempty
mod drops follow the same atomic marker/count and Bag placement rules as native
enemy rewards above. Each copy needs a distinct defeat flag. Optional
`native_drops` is empty or contains exactly one `heart` with quantity 1. After a
newly committed mod reward, the runtime requests that native heart once. The
native queue is separate from the atomic mod-record transaction: allocation or
asynchronous creation can fail without undoing the committed reward, and there
is no automatic retry. Native Hero Mode may suppress the heart. Queue status
never means the heart was created or picked up; successful native pickup is what
changes Link's life. Packages do not ship the game's heart resources.

The additive C API preserves the existing v1 behavior, actor-state and hit packet
layouts. Initialize `wwhd_actor_combat_state_v1` with `size = 64` and
`version = 1`, then call `wwhd_actor_combat_get(handle, &state)`. It reports health,
phase, flags, last damage sequence, reward/drop status and remaining timers.
The output must be owned package memory or live actor userdata; a failed query
leaves it unchanged. Queries are allowed during draw. `wwhd_actor_attack_begin`
requests the immutable declared attack and fails while unavailable, scene-owned
or cooling down. `wwhd_actor_attack_cancel` disables it without refunding cooldown.
Both mutations are denied during draw. `on_hit` remains informational: runtime
health has already been handled, so the callback must not deduct damage again.

Invalid ownership, callback faults or mismatched captured configuration fail
closed through quarantine. Full states rewind native combat objects, health,
timers, mod records and once-only queue state together. Progress loads preserve
current mod story records while recreating eligible actors; a committed defeat
keeps that copy absent. Restored identities must match the declaration fingerprint
and ownership generation. Editing a manifest is not a supported way to preserve
compatibility with an old full state.

## Carrying original mod actors (preview)

A noncombat original `kind: "mod"` actor can opt into Link's normal lift, carry,
put-down and throw controls with `"carry": {"break_on_impact": false}`. Static
and skinned models are supported. Use solid body collision with radius at most
100 and height at most 200, yaw-only placement and scale one. Carry cannot be
combined with dialogue, attention prompts, look-at, pushable collision or combat.
Lift, put-down, throw and ground landing have been exercised in the native game.

Link's native grab procedures attach the actor to his hands and determine when
put-down or throw releases it. The runtime pauses its authored path while held
or airborne and resumes the saved path cursor after landing or put-down.
Position, rotation, collision, animation, path and scene mutations are refused
while carry owns the actor. Draw does not advance carry physics.

Flight uses the native release speed (19 horizontal and 34 upward units per
logic tick), bounded authored gravity of −2 units per tick and a −50 terminal
vertical speed, with the game's native wall/ground correction. This is not the
native pot's complete material-specific bounce or fracture simulation. A wall
stops horizontal travel; an unbroken object then falls to the ground. Flight is
bounded to 300 logic ticks and malformed ownership or motion quarantines the
actor. Held and airborne bodies do not register their solid cylinder or deal
contact damage to other actors.

To break on a thrown wall or ground impact, set `break_on_impact: true` and add a
distinct mod-owned `broken_flag` plus a `drops` array using the same-mod item
syntax as native props. An empty array is valid. Impact atomically records the
marker and grants those drops before requesting deletion; failure grants nothing
and quarantines the copy. Put-down and ordinary deletion do not grant a reward.
This preview removes the model on break; it does not provide native pot shards.

Full states retain the carry mode, exact actor/Link identities, paused path and
flight velocity in guest memory. Progress loads recreate only eligible copies;
a committed broken marker keeps its copy absent. Changed declarations cannot
rebind an old full-state actor. Native room transitions, interrupted carry and
throw-camera cleanup still require acceptance evidence before release.

## Original item models in Link's hand (preview)

An inventory item may reference a same-mod static entry from `models` using
`held_model`. A successful item **Use** requests a separate model presentation
attached to Link's animated left hand. It does not equip a native weapon or
change Link's equipment slots. This preview does not add a custom use animation.
The native Delivery Bag assignment and Use path has been exercised with an
original textured model visible in Link's hand.

```json
{
  "models": [{"id": "lantern", "kind": "static", "source": "assets/lantern.glb"}],
  "items": [{
    "id": "lantern", "name": "Lantern", "description": "An original lantern.",
    "icon": "assets/lantern.png", "use": {"action": "event"},
    "held_model": "lantern", "held_frames": 90
  }]
}
```

Author the original model with its grip at the origin; the native left-hand
joint supplies its transform. The model follows the existing hand pose. The
static-model validation and player-local conversion requirements below apply.
Skinned item models and item-model animation are currently unsupported.
`held_frames` is an integer from 1 to 300, default 90, measured in game logic
frames at 30 Hz. Loading uses this same bounded interval. Rendering does not
advance it. An item needs a `use` action (or `hold_up`, below); failed Use and Info do not request a
presentation. An event action is suitable when no modal information page is
wanted. At most eight item model declarations are accepted per session.

One presentation is active for Link. A later successful modeled Use replaces
it; expiry, room/stage change, Link replacement and a real Quest Log load retire
it. Full-state loading restores its remaining interval and native allocation
when its item/model fingerprint still matches; it does not replay Use. Resource
loading may delay replacement while an outstanding native load finishes.
Original retail equipment and resource tables remain separate.

**Holding it up when Link gets it.** With `"hold_up": true` the item is shown the
way the game shows its own items: when Link gets it (a `give` action, a letter that
encloses it, a quest reward, the chest of one of your treasure charts or your code),
he plays the game's item-get pose and holds `held_model` up over his head, with the
game's get message ("You got Lamp crystal!" and the item's description). The model
takes the place of the item the game would hold up, turning the same way. The pose
waits for a free moment (no talk, scene or other event running) and then plays once
per get; when the Delivery Bag's pages are full, the caption says so instead. A full
save state taken during the pose ends it the same way after loading. `hold_up` needs
`held_model`; such an item needs no `use` action (with one, it is also shown in
Link's hand when used).

```json
{"id": "crystal", "name": "Lamp crystal", "description": "It still glows faintly.",
 "icon": "assets/crystal.png", "held_model": "crystal", "hold_up": true}
```

Without `hold_up`, a get shows the item's caption with a small jingle, and a chest (a placed
one or one of your charts') holds up the item's `icon` over Link's head, with the game's get
message saying the item's name and description.

## Static mod actors

Declare original assets in `models` and refer to their IDs with `kind: "mod"`.
A mod actor uses the same placement, conditions, dialogue and `then` actions as
other placed actors. It must not specify `actor`, `params` or `parameters`.

```json
"models": [
  {"id": "carved-post", "source": "assets/carved_post.glb"}
],
"actors": [
  {
    "id": "post", "kind": "mod", "model": "carved-post",
    "stage": "sea", "room": 11,
    "position": {"x": 100, "y": 1040, "z": -201500},
    "collision": {"radius": 42, "height": 160, "mode": "solid"},
    "attention": {"prompt": "check"},
    "lines": ["A diamond is carved into the wood."],
    "then": [{"action": "flag", "id": "post-checked", "value": true}]
  }
]
```

`source` is a package-relative `.glb` path with no parent components. Up to eight
models may be declared per package. Source files are limited to 8 MiB, 4096
vertices and 8192 triangles per model. A model contains one mesh with up to 32
indexed triangle primitives and up to 16 materials. Each material has one
embedded RGB/RGBA PNG base colour texture and is opaque or a cut-out
(`alphaMode` `MASK`); several primitives may share a material. Texture width and
height are at most 512; width must be a multiple of 64 and height a multiple of
eight. All textures of one model together take at most 1 MiB as decoded RGBA
(one 512 × 512, four 256 × 256 or sixteen 128 × 128 textures). See
[Several materials](world-objects.md#several-materials) for the material kinds.
The original example and its reproducible source are listed in
[ASSETS.md](../examples/guest-mods/sdk-v3-windfall/ASSETS.md).

In Blender, model in metres with positive Y as the exported up axis. Put the
object origin at its foot for ground placement, triangulate the mesh, give each
surface its own material slot with one PNG base-colour texture (an Image Texture
node feeding Base Color), and export **glTF 2.0 Binary (.glb)** with normals and
UVs. Export only the selected model and disable animations, skinning, morph
targets and optional compression/extensions. The converter accepts a single
instance in the active scene and bakes rigid node transforms, including scale;
one exported metre becomes 100 game units. The manifest's position and
collision dimensions use game units. The converter preserves the authored
origin. Use base colour textures with no tint. Alpha clip (`MASK`) cut-outs and
double-sided materials are supported; blended transparency and additional PBR
texture inputs (metallic-roughness, normal, occlusion, emissive) are refused.

The player's machine converts the original GLB to Wii U BFRES using a static
material, toon texture and shader from the player's own game (for several
materials or a cut-out, the opaque and cut-out materials of another get-item
model, so all materials are lit alike). Each model receives
its own archive and model identity. Generated BFRES and shaders belong in the
package's local data cache; do not ship them in packages. Editing the model or
changing the player template invalidates its cache key. Packages and this
repository must never include game-derived material or shader templates.

### Game materials by name

A static world object's model can use the game's own materials instead of, or next to, its own textures: declare
it with `"kind": "kit"` and a kit room, and leave the texture out of each material that names a game material.
Name such a material exactly as the game names it (the editor's `export_glb` and `list_kit_materials` give the
names):

```json
"models": [
  {"id": "rock", "kind": "kit", "source": "assets/rock.glb", "kit": {"stage": "sea", "room": 13}}
]
```

The materials `Txe_gake_v`, `Txe_hai_v` and `Txe_ita_v` of `rock.glb` then draw as Dragon Roost Island's rock,
ash and planks: its textures, toon shading and any texture animation. They are built from the player's own game
files when the game starts or when the editor imports the model. A misspelt name is refused with the names the
room has. Materials with an embedded PNG stay your own, with the limits above; game materials don't count toward
the 1 MiB of textures. A material that is a game material may never embed a texture, because a package never
carries the game's pictures. The built model counts toward its room's 24 MiB for island buildings and grounds
(the example's island with three Dragon Roost materials takes about 3.6 MiB, mostly its 2048 × 2048 rock
texture). A kit model is a static world object at scale 1, turned about the vertical axis. Its distant model may
be a kit model too. See [Game materials](world-objects.md#game-materials) for the naming rules, the limits, the
editor commands (`import_model` with `kit`, `reload_model`, `set_model_kit`) and a Blender workflow.

For walkable meshes and solid shapes that also block the ship, see
[World objects](world-objects.md). The following cylinder form remains
available for ordinary model actors.

`collision` specifies an upright cylinder with explicit game-unit dimensions:
`radius` is greater than zero and at most 1000; `height` is greater than zero and
at most 2000. The cylinder stays upright and uses these declared dimensions
independently of the model's rendered XYZ rotation and scale. Choose `mode: "solid"`
or `"pushable"`. `attention.prompt` chooses `"check"` (the default) or `"talk"`.
Dialogue uses the existing message and action system; static actors face Link by
turning their whole body. Skinned models add the bounded features described below. Model counts and geometry caps bound input
size. The runtime also measures native game heap allocations and enforces
2 MiB per model, 8 MiB per mod and 32 MiB per room (island buildings count toward the room's
32 MiB and have their own 24 MiB within it, not the mod's 8 MiB). Shared model resources count
once; actor instances count separately. Cached model resources remain charged
until the game unloads them. Loading preserves at least 8 MiB in the backing
archive heap and 1 MiB in the actor heap's parent. A model that cannot fit fails
creation and retries on a room reload, rather than allocating repeatedly.
These budgets cover game heaps; they do not describe GPU memory usage.


## Skinned models and motion

Set a model declaration's `kind` to `"skinned"`; omitted `kind` means `"static"`.
The geometry, material and embedded PNG limits above also apply to skinned GLBs:
up to 16 materials (opaque or `MASK` cut-out, optionally double-sided) in up to 32
triangle primitives, and 1 MiB of decoded textures per model. Every primitive
needs `JOINTS_0` and `WEIGHTS_0` and is bound to the same skin, so the parts of a
model with several materials move together. A vertex on the border between two
materials is exported once per material; give both copies the same joints and
weights (Blender does) and the border stays closed in every pose. Every material
copies the animated material of an object in the player's own game, so all of
them are lit alike. The example in
`examples/guest-mods/sdk-v3-skinned-materials` is an animated creature with cloth,
bark and cut-out leaf materials that walks a route and plays its clips in a demo
step; see [Several materials](world-objects.md#several-materials).
Export one identity mesh node with one skin, and a connected, parent-first skeleton
of at most 32 uniquely named bones. Bone names use ASCII letters followed by
letters, digits or underscores, at most 31 characters. Bones use translation and
rotation with unit scale; bone matrices, multiple roots, morph targets and glTF
extensions are unsupported. Include inverse bind matrices. Each vertex has at
most two distinct positive joint influences, with finite nonnegative weights
summing to one (tolerance 0.0001). All four exported joint indices must be valid,
including slots with zero weight.

For Blender export, apply mesh transforms, keep bone scale at one, select the mesh
and armature, export glTF Binary with embedded PNG, normals, UVs, skinning and
named animation actions. Check the exported bone hierarchy and inverse binds.
Clips use skeletal translation or rotation channels with `LINEAR` or `STEP`
interpolation. Scale channels and `CUBICSPLINE` are rejected. Up to eight named
clips are sampled at 30 Hz, with at most 900 timeline frames per clip and 2 MiB
of sampled animation data per model. Clip IDs use lowercase letters, digits,
underscore, dot or hyphen, at most 32 characters. Conventional `idle`, `walk`
and `talk` clips support actor motion and conversation; extra clips such as
`wave` are selected by name. Skeleton-only assets are also accepted.

Skinned actors may declare `look_at` and `path`:

```json
"look_at": {
  "head": "head", "yaw_degrees": 65, "pitch_degrees": 30,
  "rate_degrees": 120, "radius": 650,
  "upper_body": {"bone": "body", "yaw_degrees": 25, "fraction": 0.3}
},
"path": {
  "points": [{"x": 300, "y": 1040, "z": -201500},
             {"x": 500, "y": 1040, "z": -201500}],
  "speed": 45, "mode": "loop"
}
```

Look-at yaw is bounded to 180 degrees, pitch to 90, rate to 1080 degrees/second,
and radius to 1000000 game units. The optional upper-body bone must be an ancestor
of the head; its fraction is 0–1 and yaw limit 0–180 degrees. Look-at requires
bones authored with +Z forward and +Y up: the head, optional upper-body bone and
every ancestor must have identity bind rotation within 0.0001. This is a checked
rig restriction, not automatic bone-axis retargeting. Nonidentity bind rotations
remain valid for ordinary skinning and animation, but cannot enable look-at on
that chain. Look-at actors must remain upright: X/Z rotation is zero modulo
360 degrees and XYZ scale is uniform and positive; yaw rotation is supported.
The angle limits constrain the local joint pose, not exact world-space targeting
under arbitrary animated ancestor roll. Paths have 2–16
points, speed greater than zero and at most 1500 game units/second, and mode
`"once"`, `"loop"` or `"back_and_forth"` (it walks back to the first point and out again).
`"pauses": true` makes it stand for a moment now and then (it walks for about 7 to 10 seconds,
stands for 2 to 3, with its idle clip). `"walk"` and `"idle"` name the clips it walks and stands
with (default `walk` and `idle`; both must be in the model). Adjacent points, including a loop's closing edge, need more
than one game unit of horizontal separation. Native ground and collision checks
can refuse movement; the path advances from accepted positions. Actors stop at
Link's capsule rather than walking through it. Path-driven body facing turns
at at most 240 degrees/second on game logic steps. Positions follow the authored
piecewise-linear segments, so sharp corners can still cause foot sliding. Explicit
`face` and rotation commands remain immediate. Scene walks are once-only leases
and restore the actor's previous motion and animation clip/frame afterward.
They retain the actor's resulting position and facing. The backup lives in the actor's state-captured memory and is restored only for its matching process and lease.
A scene `quest_place` uses
that mod's visible active quest marker X/Z in the current room, retaining the
actor's height; it does not teleport between stages or rooms.

## Guest actor callbacks

The public declarations are in `runtime/guest/include/wwhd_guest.h`. Data-only
packages, including Windfall, do not need guest behaviour code. The original
[coral creature example](../examples/guest-mods/sdk-v3-creature/manifest.json)
demonstrates wandering, informational hit reactions and a real PCM music hook.
Code packages
register an owned declared actor ID with `wwhd_actor_register` and a
`wwhd_actor_behavior_v1` table (`size=32`, `version=1`, `userdata_size=0..256`).
Keep the table in the module's static region or its `wwhd_malloc` memory;
callbacks must be exact functions in that module. Foreign tables, interior code
addresses and arbitrary game pointers are rejected.

The required callbacks are `create(handle, userdata, reason)` and
`execute(handle, userdata, dt_seconds)`. Optional callbacks are `draw(handle, userdata)`,
`talk(handle, userdata, phase)` and `on_hit(handle, userdata, hit)`.
Execute runs once per 30 Hz actor logic step. Draw must not advance simulation:
actor mutation services reject Draw calls and calls while a scene owns the actor.
Callbacks restore the caller's CPU state. An exception quarantines the actor's
registration until restart. Hit packets are informational, 64-byte version-1
records; they do not provide combat damage or retail save-bit access.

Handles are scoped to the owning module, never reused, and valid only for the
current native process. `wwhd_actor_find`, `wwhd_actor_get` (80-byte version-1
state) and `wwhd_actor_userdata` query them. Full state restore issues a fresh
handle and calls create with `WWHD_ACTOR_RESTORED`, preserving userdata; update
stored handles there. Room/progress recreation calls `WWHD_ACTOR_NEW` and clears
userdata. Position, rotation (degrees), collision, named animation, walk,
quest-walk and stop services reject stale or foreign handles. State/path buffers
must lie in the owning module's region or its live actor userdata; stack and
foreign buffers are unsupported. Walk accepts 2–16 points, speed in (0,1500],
and loop 0/1. Collision flags are `WWHD_ACTOR_COLLISION_ENABLED` and
`WWHD_ACTOR_PUSHABLE`; animation blend is 0–2 seconds and loop 0/1.

## Dialogue and actions

An actor's `lines` is a string or an array of at most three strings (240 UTF-8
bytes combined). The game displays it through its message window. Its `then`
array contains at most eight actions:

```json
"then": [
  {"action": "give", "id": "item:token", "count": 1},
  {"action": "quest_step", "id": "lantern", "step": 2},
  {"action": "scene", "id": "lighthouse"}
]
```

`take` uses an item reference and count. `flag` uses a mod flag ID and boolean
`value`. Referenced items, quests, quest steps and scenes must be declared in the
same manifest. Actions run in order after the talk closes; a failing action stops
the sequence. Skipping or aborting a scene does not apply its end actions.

`{"action": "stage", "id": "sky", "room": 0, "point": 0}` takes Link into one of
the same package's own stages ([mod stages](mod-stages.md)), at spawn `point` of
room `room` (both default to 0), with a fade. It works in a talk's `then` and a
scene's `end.then`; at the end of a demo that carries Link (see
[Carrying an actor on another's joint](#carrying-an-actor-on-anothers-joint)) it
makes a ride into the stage. It fails (and stops the sequence) when the stage is
not built.

`{"action": "give", "id": "chart:keeper-map"}` gives Link one of your treasure charts
(declared in the same manifest's `charts`, see
[Charts and markers](mod-sdk-v2.md#charts-and-markers)). It works like a chart a letter
encloses: the MAP screen's chart list shows it and the story notebook knows it. A chart is
given once; giving it again (or after it is finished) changes nothing. It takes no `count`,
and `take` can't take a chart. This action works in an actor's `then`, a talk's `then` and a
scene's final `end.then`.

`{"action":"quest_done","id":"lantern"}` completes a quest declared in the
same manifest. It gives the declared reward once, marks the quest done in the
notebook, and clears its tracking selection. Completing an already completed
quest does nothing. This action works in actor dialogue and a scene's final
`end.then`; `quest_step` remains forward-only.

Flag conditions read only the declaring mod's flags. Every referenced flag must
have a producer in the same manifest: an actor's read/open, switch, break or defeat
flag, or a `flag` action in an actor, talk or scene end. A reference alone does not
declare a flag. Foreign mod IDs and game event bit numbers are rejected.

Use `talks` to give a character different reactions to quest progress:

```json
"talks": [
  {"when":{"quest":"lantern","step":1},
   "lines":["Bring the lantern back."],
   "then":[{"action":"quest_step","id":"lantern","step":2}]},
  {"when":{"quest":"lantern","step":2},
   "lines":["Thank you!"],
   "then":[{"action":"quest_done","id":"lantern"}]},
  {"lines":["The lantern is safe."]}
]
```

Residents, signs and mod actors without combat or carry support can have up to
eight talks. The first matching talk is selected;
an entry without `when` is a fallback, so put it last. Each talk has the same
line and eight-action limits as the single-dialogue form. The actor's top-level
`when` still controls whether it appears. Do not combine `talks` with top-level
`lines` or `then`; the old single-dialogue form remains valid. There is no `any`
condition: all supplied conditions must hold. Once a conversation starts, its
selected text and actions stay fixed. Restoring a conversation whose selected
definition has changed must not run replacement actions.

A placed character belongs to its declaring mod: its actor ID is local to that
mod. Multiple mods may place separate characters using the same native resident
model. They do not compete for that placed character's lines. Existing catalogue
resident takeovers keep SDK v2's rules: tracked quest first, then start order;
one quest per talk. The existing choice box chooses among eligible quest topics.
A talk's `lines` remains linear. Scene `choice` steps provide two explicit forward branches.

## Scenes

`scenes` contains up to 16 scenes per package. Each scene has an `id` and 1–64
`steps`. Linear scenes have one final `end`; branching scenes may have branch-local `end` steps, and the final step must also be `end`. Timed steps require positive `frames` or
`seconds` (not both). Frames are 60 Hz timeline units; the base 30 Hz game logic
advances two units per service. Validation caps the longest possible path at 18000 units (five
minutes). Fast-forward intentionally follows the game clock.

| Step | Fields |
| --- | --- |
| `camera` | `eye`, `target` or `target_actor`, duration; optional `pan: true`, `eye_to`, `target_to` |
| `say` | `actor`, `text`, duration as a completion deadline |
| `move` | A placed resident or mod `actor`, `position`, duration |
| `face` | A placed resident or mod `actor`, `position` or `target_actor` |
| `wait` | Duration |
| `sound` | A validated game UI sound name |
| `fade` | `direction: "in"` or `"out"`, duration |
| `camera_path` | 2–16 `points` with `eye`, `target`, and outgoing `frames`; final point omits `frames`; optional `easing: "linear"` or `"smoothstep"` |
| `camera_follow` | `actor`, `eye_offset`, `target_offset`, duration; optional `half_life` in seconds (0–10, default 0.2) |
| `animation` | Owned skinned `actor`, named `clip`; optional `blend_seconds`, `loop` (default true); starts immediately |
| `walk` | Owned skinned `actor`, `speed`, deadline; either 2–16 `points` or owned quest ID `quest_place` |
| `choice` | `text`, exactly two `options: [{"text": "…", "next": "step-id"}, …]`, deadline; optional `default: 0` or `1` |
| `music` | Textual `cue`, `action: "start"` or `"stop"`, optional `fade_frames` (0–600) |
| `particle` | `effect: "auction-sparkle"` or `"beacon-sparkle"`; exactly one `actor` or world XYZ `position`; optional `scale` and `alpha`; emits once |
| `end` | Optional `then` actions |

Character references name a placed actor ID or `link` where supported. Movement
and facing support placed residents and mod actors; they cannot move or turn Link, signs
or chests. Unknown
references, missing durations, nonfinite coordinates and unsupported sound names
are errors. Dialogue deadlines abort if the message has not completed; they do
not silently award the scene's rewards.

An optional `trigger` uses `type`: `room` (stage/room), `area` (also position and
radius), `talk` (actor), `quest_step` (quest/step), or `salvage` (chart). A dialogue
`scene` action can also request playback. Requests wait for native events to end;
only one scene holds control at a time, with a global FIFO of at most 16 waiting
requests and duplicate-request suppression. An interruption or state load clears
waiting requests. Plus/Start skips the current scene while allowing other queued
scenes to continue. Already fired room/area triggers do not immediately fire again
after an interruption; loading a state also suppresses those triggers in the loaded
room until the room changes.

A room or area scene plays once per visit of its room. An `area` trigger with `"again": true` plays
again once Link has left the area (by 60 units past its radius) and walked back in, in the same room
visit: a way up that can be taken again, such as a Helmaroc King that carries Link up from Outset's
pier each time he walks out to the lookout after falling into the sea.

```json
"trigger": {"type": "area", "stage": "sea", "room": 44, "position": {"x": -194790, "y": 136, "z": 314530},
            "radius": 120, "again": true}
```

A scene plays on one of two routes. The **fallback route** runs the scene's
`steps` through native compulsory-event admission, the game's camera and message
functions, and native event teardown. It does not replace the stage's event-list
resource. Native admission provides the event control lock; camera trimming
supplies the letterbox framing.

The **native route** is an optional, experimental alternative: a scene may add a
`native` block that the game's own event system plays instead of the steps.
Its only `type` is `"turn_camera"`: two placed Uo2 residents turn toward fixed
points in parallel and wait, while the game's FIXEDFRM and UNITRANS camera
actions hold a starting view and then move to an ending view. A scene with an
eligible `native` block uses the native route when the runtime's readiness
checks pass; every other scene uses the fallback route.

The [harbor meeting example](../examples/guest-mods/sdk-v3-native-scene/README.md)
contains two residents, a talk trigger and both routes.

| Native field | Meaning and limits |
| --- | --- |
| `actors` | Exactly two distinct same-mod placed resident IDs, both declared with `parameters.resident: "uo2"`, in the same stage and room. Each entry contains `id` and a fixed XYZ `turn_to`. |
| `wait_ticks` | Native WAIT duration, 1–9000 logic ticks. Both residents turn toward their respective positions in parallel, then each performs WAIT. |
| `camera` | `hold_ticks`, `move_ticks`, `start_eye`, `start_target`, `end_eye`, `end_target`, and `fovy`. The native camera holds the starting view with FIXEDFRM, then moves to the ending view with UNITRANS. |
| `camera.hold_ticks`, `camera.move_ticks` | Each is 1–9000; their sum must not exceed 9000. |
| `camera.fovy` | Finite field of view, 5–120 degrees. Eye and target must remain at least one world unit apart throughout the linear move. |
| `timekeeper_ticks` | 1–9000; at least `wait_ticks` and at least `hold_ticks + move_ticks`. |
| `watchdog_ticks` | At most 18000 and strictly greater than `timekeeper_ticks`; a completion deadline, not an extra native action. |

These timers use 30 Hz game logic ticks, independently of rendering speed and
the fallback timeline. All native XYZ components must be finite and within
−350000 to 350000. Unknown fields are errors. Stage and room come from the two
actor placements; a room or area trigger must match them. The native route has
been checked with the game running at 30 fps; at other frame-rate settings it is
still experimental.

The native alternative is an explicitly authored parallel timeline, rather than
a conversion of every fallback step. Native selection currently covers only
the following fallback shapes:

| Fallback scene content | Native alternative eligibility |
| --- | --- |
| `face` | Either of the two named Uo2 residents, facing a fixed `position`; no `target_actor`. |
| `camera` | Fixed XYZ eye and target; an optional pan also uses fixed ending positions. No actor target. |
| `wait` | Supported with a valid duration. |
| `end` | Exactly one final END; its `then` actions remain the scene's completion actions. |
| Dialogue, movement, animation, camera paths/follow, choices, sound, music, fades, particles | The whole scene uses the fallback route. |
| Branches or intermediate `then` actions | The whole scene uses the fallback route. The eligible fallback graph must be linear. |

Fallback `steps` remain mandatory, including for a scene with `native`. If the
alternative is unsupported or readiness fails before the native scene starts,
the whole fallback scene runs instead. Once the native scene has started, a
failure stops it; it does not replay the fallback scene. When the game is briefly
busy before the start (control, camera or event contention), the request stays
at the front of the queue for up to 60 scene ticks and then falls back to the
whole fallback scene. A waiting request holds nothing: skip, abort and state
load discard it without completion rewards.

Plus/Start skips a native scene too: both residents and the camera are restored
and control returns. Save states made during or after a native scene load
normally, and the scene can be started again afterwards. Restoring a save state
taken in the middle of a native scene works only in the same game session that
made it; after a restart the port refuses to load such a state. A state taken
before any native scene started loads in any session.

The fallback route releases its scene ownership on completion, timeout,
interruption or state load. Its non-natural exits reset only the scene-owned
message through the native message core, so restoring control does not require
another A press. The native route releases its ownership only after the game's
own event has finished or been cancelled cleanly; if that cannot be confirmed,
the scene stays blocked rather than freeing actors the game still uses.
The lease snapshot is stored in state-captured runtime memory so loading an older
mid-scene state can restore control even after the host player has finished that
scene. The port checks ownership before resetting anything: an unrelated native
event must retain its camera and control. Press **Plus/Start** during a scene to skip it; the skip press is consumed so it
does not also open the pause menu. A/B remain available for the message window.
The scene route remains experimental; native execution is limited to the
eligible `turn_camera` alternative described above.

Every scene step may have an `id`; `next` names a strictly later step. Without
`next`, playback continues to the following step. Choice options name their own
strictly later targets and cannot also use step-level `next`. All steps must be
reachable and every branch must end. Option labels are single-line, at most 32
UTF-16 units. Keep labels short: the native pane may wrap them, and the memory
limit is not a guarantee of rendered width. The native choice pane disables cancel;
there is no third cancel branch. `default` selects the initial cursor, rather than
automatically choosing an answer at the deadline. A choice deadline aborts the scene.
Camera path eye and target must remain distinct throughout interpolation.

A `music` step requires that code from the same mod registered a real
`wwhd_scene_music_register` callback before scene preflight. There is no implicit
retail BGM service or successful no-op. The callback receives cue, START/STOP/
RESTORE phase, fade frames and stop reason, and returns nonzero only after
handling the event. It owns playback through audio services or authored bindings.
RESTORE is sent once per distinct cue whose START or STOP callback was invoked,
even if that invocation failed, when the lease ends, including abort or load.
The callback must restore its prior audio state; a failed restore prevents natural
end rewards. Stop reasons are 0 Finished, 1 Skipped, 2 Aborted, 3 StateLoaded,
4 Interrupted, 5 Failed and 6 TimedOut. START/STOP receive reason 0; RESTORE receives
the actual lease exit reason. A data-only scene without
a registered hook must not declare music steps.

### Demo steps (preview)

A `demo` step plays keyframe tracks through the game's own cutscene system, the
one its story cutscenes use. The port turns the tracks into the game's demo data
when the step starts. The data never comes from a package file. The game then plays
the demo and ends it.

```json
{"type": "demo", "frames": 240, "origin": "link",
 "camera": {"eye": {"keys": [{"frame": 0, "value": {"x": 0, "y": 700, "z": -1200}}]},
            "target": {"curve": "linear", "keys": [{"frame": 0, "value": {"x": -2500, "y": 1800, "z": 3000}},
                                                   {"frame": 240, "value": {"x": 2500, "y": 900, "z": 3000}}]},
            "fovy": {"keys": [{"frame": 0, "value": 55}]}},
 "actors": [{"id": "bird", "actor": "Dk", "spawn": "always", "params": 2,
             "position": {"curve": "linear", "keys": [{"frame": 0, "value": {"x": -2500, "y": 1800, "z": 3000}},
                                                      {"frame": 240, "value": {"x": 2500, "y": 900, "z": 3000}}]},
             "rotation": {"keys": [{"frame": 0, "value": {"x": 0, "y": 90, "z": 0}}]},
             "clips": [{"frame": 0, "archive": "Dk", "file": "fly1.bck"}]}],
 "sounds": [{"frame": 20, "id": 22820, "actor": "bird"}]}
```

- **Frames and timing.** `frames` (1–9000) is the demo's length in the game's 30 Hz cutscene frames. Every key, clip, sound and particle has a `frame` inside it.
- **Origin.** `"origin": "link"` makes every position relative to where Link stands when the step starts. An `{x,y,z}` origin uses that point instead, and `angle` turns the whole demo around the vertical axis. Without an origin, positions are world positions.
- **Tracks.** A track is `{"curve": ..., "keys": [{"frame": n, "value": ...}]}` with 1–64 keys in increasing frame order. A value is `{x,y,z}`, or a number for `fovy` (5–120) and `roll` (degrees).
  - `curve` is `linear` (the default), `smooth` (passes through every key), `bspline` (rounder, does not pass through inner keys) or `step` (jumps at each key).
  - After its last key, a track holds its final value.
- **Camera.** `eye` and `target` come together. `fovy` and `roll` are optional. Without a camera track, the camera keeps its gameplay view.
- **Actors.** An actor names one of four things:
  - a game actor by its stage name, with `"actor": "Dk"`, for example. When the room has none, the port creates one for the demo at the first key of its position track (or beside Link), moves it along its tracks also where its own code doesn't know cutscenes (a pig, an enemy), and removes it afterwards; `"params"` (a number or `"0x..."`) are its creation parameters. `"spawn": false` requires the room's own actor instead; without it the step fails with a reason in the log and the scene ends cleanly.
  - When the room has one, the demo uses the room's own actor, and that actor's own code decides whether it shows: an actor that waits for an event of the game stays hidden (Outset's Helmaroc King waits for its story event). `"spawn": "always"` gives the demo its own one even then, created with `params` and removed afterwards; the room's stays as it is. The game's log names the room's actor when the demo uses it.
  - Link, with `"actor": "Link"`.
  - a model from the player's game, with `"model": {"archive", "file"}`.
  - an actor this mod placed (a resident villager, a mod actor, an enemy), with `"character": "<its id>"`. It follows the tracks even where its own code doesn't know cutscenes; a mod actor plays its own clips by name, as `"clips": [{"frame": 0, "clip": "walk", "loop": true}]`.
- **Actor tracks.** Every actor can have `position`, `rotation` (degrees) and `scale` tracks.
- **Clips.** `clips` play a `.bck` from any archive at `speed` (default 1) on game actors whose code supports cutscenes (story characters, the Helmaroc King and other cutscene actors). A clip repeats until the next clip; `"loop": false` plays it once and holds its last pose, the way the game's own cutscenes play a grab, a landing or a turn before the next loop: `[{"frame": 50, "archive": "Demo04", "file": "04_dk_cut08_hokaku.bck", "loop": false}, {"frame": 81, "archive": "Demo04", "file": "04_dk_cut08_esc.bck"}]`.
- **Link.** With a `position` track, Link stands at its first key when the demo starts (facing its first `rotation` key, if any); without one he stays where he is. He takes demo `modes` instead of clips:
  - `{"frame": 0, "mode": "walk"}` makes him walk along his position track. The names are `wait`, `walk`, `dash`, `turn`, `talk`, `holdup`, `keep`, `look_around`, `salute`, `surprised`, `lookup`, `nod`, `present` and `smile`; the game's own mode numbers work too.
  - `{"frame": 60, "clip": "udeage_onetime.bck"}` plays one of his own cutscene animations (from the game's Link demo animation archive that is loaded at the time: LkD00 in the early game, which has his Outset and Forsaken Fortress cutscenes). A clip repeats; `"loop": false` plays it once and holds its last pose.
  - `"btp"` and `"btk"` (both) give him the cutscene's face, his eyes and mouth: `{"frame": 30, "clip": "04_cl_cut7_odoroki.bck", "btp": "04_cl_cut7_odoroki.btp", "btk": "04_cl_cut7_odoroki.btk", "loop": false}`. A clip's `.btk` often covers several cuts (`07b_cl_cut4_5_6_7_10.btk`); the editor's file list shows them.
  - `"blend"` (0–60 frames) blends from his previous pose into the clip instead of the game's default.
  - `"data"` (hex) passes a raw cutscene data block, for modes the list doesn't cover.
- **Archives.** Archive and file names refer to the player's own game. The port looks up their ids and loads the archives before the demo starts. The game's cutscene archives (`Demo02`, `Demo04`, `Demo07`, ...) lend their animations too: the Helmaroc King's Outset grab is in `Demo04`, its Forsaken Fortress beak grab and throw in `Demo07`. The demo itself always plays from the port's own data, so the game's special cases for its cutscenes by name don't apply.
- **Sounds.** `sounds` play a game sound id at a `position` or following an actor, with `volume` and `pan` (0–1).
- **Particles.** `particles` start a game particle id at a `position`, or on an actor, optionally on one of its joints (`joint`).
- **Skip and save states.**
  - Plus skips the scene; the demo stops where it is. An open message window closes first (a short out animation).
  - Loading a state saved during a demo ends the demo and gives control back. It is never resumed.

A demo step can follow another demo step directly: it starts as soon as the
previous demo has been cleared (about a third of a second, longer while a message
window closes). The step's deadline is its length plus a margin. A demo that the game does not
start within a few seconds fails the scene instead of keeping control.
`messages` (`{"frame", "text"}`, at least two frames before the end, up to 16)
show their text in the game's message window at their frame. The demo then
waits there until the player closes the message with A, and continues. Tracks
that are moving along a curve keep moving while a message is open; the demo's
next keys wait. Each message adds 20 seconds of reading time to the step's
deadline. Messages are plain text; choices are not available inside a demo
(use a `choice` step before or after it).

#### Carrying an actor on another's joint

`attach` puts one demo actor on a joint of another from a frame on; `detach`
lets it go. This is how the game's own cutscenes have the Helmaroc King hold
someone in its beak or claws.

```json
"attach": [{"frame": 0, "actor": "baker", "to": "bird", "joint": "j_dk_ashi_r3",
            "offset": {"x": 150, "y": 0, "z": 0}, "upright": true}],
"detach": [{"frame": 150, "actor": "baker", "fall": true}]
```

- `actor` and `to` are ids from the step's `actors`. The carried actor can be a
  game actor, Link or one of your mod's characters (a villager or a mod actor).
  The carrier can be a game actor that plays cutscene animation (the Helmaroc
  King, story characters), Link or a mod actor. Models from the game's archives
  (`"model"`) can't carry or be carried, and villagers can't carry.
- `joint` is the carrier's joint name, as its model names it. When the name is
  wrong, the log lists the model's joints and the actor is not carried.
- `offset` (in the joint's own axes and scale) and `rotation` (degrees) place the
  carried actor relative to the joint. A joint's axes rarely point the way the
  world does, so try a few offsets. `"upright": true` keeps only the joint's
  heading, so the carried actor stays upright.
- `blend` (0–60 frames) moves the carried actor smoothly from where it is onto
  the joint over that many frames instead of putting it there at once: a grab
  that lifts Link off the ground without a jump (`"blend": 8`).
- While it is carried, an actor ignores its own position and rotation tracks.
  After `detach` it stays where it was let go. With `"fall": true` it falls
  from there to the ground below it. It stays there until its next position
  key or its next `attach`, so leave out position keys between the detach and
  where you want it to move again. Its own code may still turn it (a villager
  goes back to its waiting pose).
- Link can only be carried while he plays one of his demo animations (`"clip"`),
  from the attach frame until the detach.
- A grab that looks like one: time a one-shot clip of the carrier (`"loop": false`)
  so that its claws close at the attach frame, with the carrier's position track
  bringing the claws down to the actor; then the carrier's loop. The carried
  actor's own clips do the rest (Link: startled, struggling, then hanging). A scene
  can do this with the Helmaroc King's `04_dk_cut08_hokaku.bck`, Link's Outset clips and
  an attach `blend`.
  When an offset is hard to find, attach to the joint with no offset first and
  run with `WWHD_DEMO_TRACE=1`: the log gives the carried actor's place, the
  carrier's and the joint's every second.
- An actor has one carrier at a time, and a carried actor can't carry another.
  A demo has up to 8 attachments.
- Villagers only turn around their vertical axis, so they hang upright whatever
  the rotation.

#### Time and weather

`time` and `weather` change the time of day and the rain at a frame. The
game's own event system does this (the same instant change its story events
use).

```json
"time": [{"frame": 40, "hour": 22}],
"weather": [{"frame": 80, "rain": 1}],
"keep_time": false, "keep_weather": false
```

- `hour` is 0–24 (fractions are minutes: 22.5 is 22:30).
- A time change never starts a new day, even when it goes back past midnight
  (22:00 to 06:00): the day of the week, letters and the game's daily events
  stay as they were.
- `rain` is 0–1: 1 is the game's full rain, 0 stops the rain, and values in
  between give lighter rain. Rain also turns the sky and colours rainy. On the
  Great Sea the port holds the demo's rain while the demo runs, so the game's
  own changing weather doesn't take it away.
- When the demo ends, is skipped, or a state is loaded, the time and weather
  from before the demo come back. `"keep_time": true` or `"keep_weather": true`
  keeps the demo's values instead. On the Great Sea the game's own weather
  takes over again after a kept weather change.
- A demo changes time and weather at up to 8 different frames. Each list is in
  frame order.
- Thunder and wind can't be set.


## Native particles (preview)

The curated effects `auction-sparkle` (`8153`, blue geometric sparkle) and
`beacon-sparkle` (`8152`, orange/yellow beacon burst) use callback-free native
effects. Native 30 fps proof covers both visible bursts and their exact emitters'
return to the native free list. `door-debris` and `dust` are unavailable in the
public preview. The stock dust effect needs its retail callback to stop; it is
not a safe callback-free one-shot.

The [guest C header](../runtime/guest/include/wwhd_guest.h) provides:

```c
u32 wwhd_actor_particle(wwhd_actor_handle actor, const char* effect,
                       f32 scale, u32 alpha);
u32 wwhd_particle_at(const char* effect, f32 x, f32 y, f32 z,
                    f32 scale, u32 alpha);
```

The actor call copies the current position of a live handle owned by the calling
mod and requires the actor to be in Link's current room. The place call accepts
explicit world XYZ in that current room; it does not infer height from a map or
quest marker. Coordinates must be finite and within ±1000000 game units, scale
must be 0.1–4, and alpha must be 0–255. Both calls reject Draw callbacks and return
1 only when the native emitter is accepted. C API calls share a global limit of
four emission attempts per logic step across mods; an attempt may consume the
budget even when emission fails.

```c
wwhd_actor_handle actor = wwhd_actor_find("tide-sprite");
if (actor) wwhd_actor_particle(actor, "auction-sparkle", 1.0f, 255);
/* Explicit XYZ, using Link's current room. */
wwhd_particle_at("auction-sparkle", 100.0f, 1040.0f, -201580.0f, 1.0f, 255);
```

A scene can emit at a declared character (including `link`) or at an explicit
world position in the scene's room:

```json
{"type":"particle","effect":"auction-sparkle","actor":"link"}
```

```json
{"type":"particle","effect":"auction-sparkle",
 "position":{"x":100,"y":1040,"z":-201580},"scale":1,"alpha":255}
```

Use exactly one of `actor` and `position`; `scale` defaults to 1 and `alpha` to
255. The step emits once when entered and has no duration field. A missing or
stale actor, room mismatch, or unavailable native effect fails the step and aborts
the scene rather than silently continuing to its end rewards.

Effects use the currently loaded native bank. Availability can change between
rooms; an absent effect returns failure without replacing or loading a bank.
These calls create a one-shot at a copied position: the effect does not follow
an actor, expose a cancellation handle, or retain a guest pointer. The native
particle system owns its expiry.

## Textures on owned native props (preview)

A placed `pot`, `barrel`, or `crate` may name a PNG in its own package. The SDK
replaces the supported base-colour texture on that copy of the native object;
other copies in the room continue using the game's texture. Lighting and the
native toon texture still come from the object. For example:

```json
{"actors":[{"id":"harbor-pot","actor":"pot","stage":"sea","room":11,
  "position":{"x":100,"y":1040,"z":-201580},
  "broken_flag":"harbor-pot.broken","texture":"textures/harbor-pot.png"}]}
```

The path must be a relative `.png` inside `textures/` or `assets/` in the
package. The file must be at most 2 MiB and decode as a noninterlaced 8-bit RGBA
PNG. Width and height may be at most 512 pixels; width must be a multiple of
64 and height a multiple of 8. Invalid or missing files reject the declaration.
The preview supports one named base-colour slot for a pot or crate, and the
barrel's primary base-colour slot; other native objects and material slots are
not accepted. Up to eight texture definitions can be active in one session.

Mod-shipped simple JPAC sprites are not registered in this preview. The isolated
writer is experimental: native draw selectors, private texture-context binding,
and snapshot-safe bank ownership and retained texture/emitter lifetimes still
need integration and native proof before custom effects can be loaded.
