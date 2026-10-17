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
| Resident | `resident`, `NpcPeople`, `PROC_NPC_PEOPLE` | `parameters: {"resident": "uo2"}`, `"um1"` or seated `"ym1"`; default and legacy numeric `9` select Um1 |
| Chest | `chest`, `Tbox`, `takara`, `PROC_TBOX` | A mod-owned `flag`; safe rupee `parameters.item` |
| Breakable prop | `pot`, `barrel`, `crate` | Required mod-owned `broken_flag` and `drops` list |
| Latched floor switch | `switch` | Required mod-owned `switch_flag`; fixed yellow switch |
| Rotating gate | `door` | Required `switch` naming a switch actor in the same mod |
| Native enemies (preview) | `chuchu`, `bokoblin`, `moblin`, `keese` | Required mod-owned `defeated_flag` and `drops`; fixed ordinary variants |

Resident names are exact native object codes. This preview supports only `uo2`, `um1` and seated `ym1`, with fixed ordinary parameters and no native rail or quest variants.
Other names and numeric values are rejected. Each placed copy uses mod dialogue
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

Other actor names and unsafe parameters are validator errors. Only ordinary chest
raw `params: 0` is accepted at present. The port owns the remaining chest/sign
parameters so a package cannot select a game switch or opened bit.

`flag` is a symbolic ID in the declaring mod's story records. It is not a game
save-bit number. Chest opening and sign reading use this namespace. The runtime
rejects colliding flag keys across loaded actors and omits an actor if no story
record can be reserved for its flag.

There are at most 64 declarations per package, 128 across loaded packages and
16 per stage/room across loaded packages. The room cap bounds asynchronous actor
creation and the amount of additional actor work. It is a conservative policy,
not a measurement of spare game heap.

Optional `when` conditions can name `quest` plus `step`, an owned mod `item`
(`item:<id>`), and `time` (`day` or `night`). Conditions are checked when the room
is populated and reconciled in the current room on game logic steps. Pending
creation/deletion retains its identity; an active conversation finishes before
its partner is removed. Actors belong to the native room layer and unload with it.

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
| `chuchu` | Ordinary green ChuChu |
| `bokoblin` | Ordinary type-4 Bokoblin |
| `moblin` | Ordinary Moblin |
| `keese` | Ordinary Keese, the native KI bat; not Morth |

All four use the same declaration fields:

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

Use `"actor": "chuchu"`, `"moblin"` or `"keese"` for the other fixed variants,
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
Own Bokoblin/Moblin weapon copies cannot be lifted or taken by Link in this
preview. Combat is restricted to verified Link interactions; owned enemies and
weapons must not damage retail actors or join retail enemy cohorts. Native item
drops are replaced by the declared Bag rewards. Some native disappearance effects
are omitted because their native effect actor also creates retail drops; identical
retail defeat effects are not promised.

Packages ship no game models, weapons, animations, shaders or global resource
replacements for these declarations. Retail originals retain their own behavior
and progress. The fixed variants are the entire preview allowlist; elemental,
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
advance it. An item needs a `use` action; failed Use and Info do not request a
presentation. An event action is suitable when no modal information page is
wanted. At most eight item model declarations are accepted per session.

One presentation is active for Link. A later successful modeled Use replaces
it; expiry, room/stage change, Link replacement and a real Quest Log load retire
it. Full-state loading restores its remaining interval and native allocation
when its item/model fingerprint still matches; it does not replay Use. Resource
loading may delay replacement while an outstanding native load finishes.
Original retail equipment and resource tables remain separate.

`held_model` does **not** replace a chest's get-item model. Custom chest get-item
presentation is unsupported in this preview; existing chest rewards retain
their existing stand-in presentation.

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
vertices and 8192 triangles per model. Models contain one mesh, one indexed
triangle primitive, one opaque material and one embedded RGB/RGBA PNG. Texture
width and height are at most 512; width must be a multiple of 64 and height a
multiple of eight. The original example and its reproducible source are listed
in [ASSETS.md](../examples/guest-mods/sdk-v3-windfall/ASSETS.md).

In Blender, model in metres with positive Y as the exported up axis. Put the
object origin at its foot for ground placement, triangulate the mesh, assign a
single material with a PNG base-colour texture, and export **glTF 2.0 Binary
(.glb)** with normals and UVs. Export only the selected model and disable
animations, skinning, morph targets and optional compression/extensions. The
converter accepts a single instance in the active scene and bakes rigid node
transforms, including scale; one exported metre becomes 100 game units. The
manifest's position and collision dimensions use game units. The converter
preserves the authored origin. Use opaque base colour with no tint; additional
PBR texture inputs, transparency and double-sided materials are unsupported.

The player's machine converts the original GLB to Wii U BFRES using a static
material, toon texture and shader from the player's own game. Each model receives
its own archive and model identity. Generated BFRES and shaders belong in the
package's local data cache; do not ship them in packages. Editing the model or
changing the player template invalidates its cache key. Packages and this
repository must never include game-derived material or shader templates.

`collision` specifies an upright cylinder with explicit game-unit dimensions:
`radius` is greater than zero and at most 1000; `height` is greater than zero and
at most 2000. The cylinder stays upright and uses these declared dimensions
independently of the model's rendered XYZ rotation and scale. Choose `mode: "solid"`
or `"pushable"`. `attention.prompt` chooses `"check"` (the default) or `"talk"`.
Dialogue uses the existing message and action system; static actors face Link by
turning their whole body. Skinned models add the bounded features described below. Model counts and geometry caps bound input
size. The runtime also measures native game heap allocations and enforces
2 MiB per model, 8 MiB per mod and 16 MiB per room. Shared model resources count
once; actor instances count separately. Cached model resources remain charged
until the game unloads them. Loading preserves at least 8 MiB in the backing
archive heap and 1 MiB in the actor heap's parent. A model that cannot fit fails
creation and retries on a room reload, rather than allocating repeatedly.
These budgets cover game heaps; they do not describe GPU memory usage.


## Skinned models and motion

Set a model declaration's `kind` to `"skinned"`; omitted `kind` means `"static"`.
The geometry, material and embedded PNG limits above also apply to skinned GLBs.
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
`"once"` or `"loop"`. Adjacent points, including a loop's closing edge, need more
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

A placed character belongs to its declaring mod: its actor ID is local to that
mod. Multiple mods may place separate characters using the same native resident
model. They do not compete for that placed character's lines. Existing catalogue
resident takeovers keep SDK v2's rules: tracked quest first, then start order;
one quest per talk. The existing choice box chooses among eligible quest topics.
A placed character's `lines` remains linear. Scene `choice` steps provide two explicit forward branches.

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

The implementation uses the **fallback route**: native compulsory-event admission,
the game's camera and message functions, and native event teardown. It does not
replace the stage's event-list resource. Runtime event-list generation would need
additional ownership and resource-lifetime verification. Native admission provides
the event control lock; camera trimming supplies the letterbox framing.

Completion, timeout, interruption and state load release the scene's ownership.
Non-natural exits reset only the scene-owned message through the native message
core, so restoring control does not require another A press.
The lease snapshot is stored in state-captured runtime memory so loading an older
mid-scene state can restore control even after the host player has finished that
scene. The port checks ownership before resetting anything: an unrelated native
event must retain its camera and control. Press **Plus/Start** during a scene to skip it; the skip press is consumed so it
does not also open the pause menu. A/B remain available for the message window.
The scene route remains experimental; native event-list generation is not part
of this preview.

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
