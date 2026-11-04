# TWWHD Randomizer core mod (in development)

This source port targets TWWHD Randomizer 1.2.0. It runs through the guest mod SDK
on the original USA game, with changed content supplied by the player. It does
not load or execute randomizer RPX code.

The current implementation ports new-file initialization, opening-scene control
flow, starting location, progressive starting gear, and progressive item grants
through the native item dispatcher. Seed reward data is installed before game
entry, covering 37 data bytes; 13 mailbox rewards have been read through the
original native mailbox routine in a separate live test. Tingle’s first two gifts
and Ankle’s all-statues reward use seed values through a function replacement,
with unrelated monument and chart payments delegated to the original game.
Bow and bomb acquisition preserve existing ammunition; the Deku Leaf preserves pending magic changes. Both a default seed and
a swordless seed matched the official randomized build's entire packed save,
four non-name HD save blocks, and starting position in private macOS tests.
This is initialization evidence, not evidence of full randomizer support. A
separate live dispatcher test checks 44 assertions across all nine upgrade
families, capped upgrades, ammunition preservation, and pending magic changes.
The shared demo-item and field-item paths select progressive IDs with 78 live
parameter checks. Expanded resource descriptors are installed after the native
resource constructor; 597 live checks cover 194 field descriptors, 35 item
descriptors and 368 collision-data bytes. A real mirror-shield model is visible
in the captured frame; its pickup event grants and equips the shield, returns to
play, saves both portable states and exits normally. A separate late base-sword
reward resolves to the Master Sword text and saves exactly one upgrade. Shop presentation preserves the raw inventory slot while loading the progressive
model; a real Master Sword shop model has been rendered without granting it.
Beedle purchase branches now preserve seed slot identity and use concrete
progressive IDs only for the ownership/animation decision. A private comparison
passes 2,816 native-versus-adapter branch cases plus three seeded sold-out flag
cases. Its callbacks deliberately stop before granting an item; a full in-game
purchase and other item-get sources still need validation. The normal first slot
uses source flag 6902 rather than the item ID, and switches the entire alternate
stock set after purchase. Another 117 native checks cover stock descriptors,
availability, special-shop fallback and first-slot purchase bookkeeping.

Reward selection is also ported for Flight Control, Orca, the six Great Fairies,
Maggie, Mrs Marie, Lenzo, and Ivan. Private native-code tests cover their reward
branches, with controlled callbacks where the surrounding quest needs actors or
events. These tests do not substitute for playing the actual quests. Ivan's test
uses a second seed whose gift differs from the original game. The Hurricane Spin
item sets its persistent ownership event. Its spin initializer now consumes that
separate ownership flag while retaining the temporary training override, native
sword/magic/minigame gates and effect callbacks. Host checks cover all minigame
values and independent story progress; native combat/effect proof remains pending.
Battlesquid reward selection passes 24 native reward-path checks, including
repeat rupees and actor-creation failure. Its actual minigame remains untested.
Withered-tree rewards use asynchronous item loading, deferred launch momentum
and stationary reload placement. A private synthetic-tree test uses real actors
and native matrix operations for a Golden Feather seed reward; 15 lifecycle
checks pass. The actual watering quest and full tree construction remain untested.
Spoils and bait can drop before their bags are owned, matching the randomizer;
256 native conversion checks preserve the other original item conversions.

The recollection inventory snapshot routines are disabled, matching the randomizer;
three live calls preserve the complete native save and a deliberately stale
recollection buffer. Boat dialogue removes the two direct sail tests through complete public CC0
message consumers. Nested player-status, event and SafeString callbacks retain
real sail ownership; no shared ownership-query replacement is installed. Earlier
force/bomb/status priorities remain, while the late milestone chain now follows
the seed's KoRL hint policy. Host regressions cover both sail states, early guards,
boarding-scope restoration and SafeString capture/reload/bounded comparisons;
strict PowerPC compilation passes. The added hint paths still need native
comparison and actual conversations/sailing remain untested.

Boss rewards now preserve the six native death flows while passing separate seed
items through the boss factory, the four delayed death clouds, and respawn ACTR
parameters. Their field items use source pickup flag `0x15`; ordinary heart
containers increase/refill health without marking a dungeon's boss reward taken.
The randomizer boss-cloud ABI uses parameter byte 24..31 for the item ID, byte
16..23 as `0xFF`, and drop discriminator 2. Other cloud drops and unscoped boss
factory callers retain their native behavior. Host fixtures cover all 256 item
IDs, argument/return preservation, scope nesting, timer gates and save-side-effect
isolation. Real boss fights and source pickup persistence still need native tests.

Dungeon-specific grants now target the matching saved dungeon or active dungeon
state: five small keys, five big keys, six maps and six compasses. Keys in a real
dungeon room use the native pending-key update; the other paths increment the
correct byte with upstream wrap behavior. The five Tingle statues use ownership
events independently of their source chests. Scoped native queries cover the 23
upstream ownership callsites and the quest screen statue count, leaving ordinary
chest queries unchanged. Host fixtures cover every byte key count, all 32 active
stage IDs, both room types, repeated grants, all statue ownership combinations,
invalid queries and nested scopes; the integrated grant header compiles for
PowerPC. Actual dungeon traversal, monument payments and quest-screen rendering
still require native gameplay validation.

Both custom switch actors are source-ported through the native actor-profile SDK
bridge (#375): `SwOp`/0x1E4 and `DngSw`/0x1E5, with the upstream profile sizes,
priorities, status and culling fields. Their 17 source functions cover switch
logic, timers, event requests/completion, dungeon flags and actor lifecycle.
Profiles register from GameMain entry; no randomizer profile/name-lookup patches
or executable bytes are loaded. This requires the shared profile bridge, rather
than a second replacement of the port's native profile lookup.

The port preserves literal upstream state numbers where comments disagree:
direct activation enters CheckUnmet (4) for both continuous settings; a timer
without an event deactivates (5) only in continuous mode, while non-continuous
mode stays in Timer and repeats the output write. XOR means exactly one input,
not odd parity. The C implementation keeps the actor pointer and dungeon-loop
variables across calls and honors the ABI, instead of reproducing assembly
volatile-register/epilogue corruption. Native switch/dungeon helpers still
validate their original input domains; invalid room or dungeon selectors are
not silently reinterpreted. `test_switch_actors.py` covers all operators, empty
and wrapped inputs, every byte-sized delay, event state transitions, all 65,536
stage masks and all three-bit raw selectors with host helper stubs. Real placed
randomizer actors and cutscene ordering still need playthrough validation.

Remaining instruction-based rewards, gameplay patches, seed
preparation, and isolated save identity are still required. This directory
is source under development, not an installable randomizer release. Do not use
it with a live save.

`seed_init_data.h`, `seed_reward_data.h`, `seed_resources.h`, `seed_boss_data.h`, and `seed-content/`
must be generated privately from the player's official randomized output. They are deliberately excluded from git.
Generate the six boss constants with
`python3 extract_boss_rewards.py /path/to/randomized/code/cking.rpx /private/module/source`;
keep the matching randomized stage assets, whose respawn ACTR parameters carry
the same item IDs. The extractor rejects unsupported operand layouts and only
reads the six constant operands; it does not import randomizer executable code.
No game assets, seed output, or compiled guest module belong in this directory.
The native-save helper and observer-register SDK fixes are dependencies.

Run `python3 test_item_get_presentation.py` to check late parameter conversion
and restoration. Run `python3 test_field_pickup.py` to check demo collision cleanup
and native pickup passthrough. Run `python3 test_reward_tingle.py` to check gift selection, native fallback,
actor-creation failure and successful event completion. Run
`python3 test_progressive.py` to check all progressive item aliases, byte
states, capped upgrades, and ordinary item passthrough with a native C compiler.

The opening control flow follows the public CC0 WWHD decompilation. Randomizer
initialization and progression follow SuperDude88's MIT-licensed source; its
complete notice is in `LICENSE-TWWHD-Randomizer.txt`.

The magic-upgrade handler now sets both pending current and maximum MP to 32,
matching the randomizer's fixed-value patch. The Big Octo fairy keeps its native
particle, emitter fade, light shutdown and cut completion, while removing its
separate hardcoded magic increase. Host fixtures cover pending values, emitter
alpha boundaries, callback-modified counters, counter wrap and pending-cut flags;
the complete module compiles with strict PowerPC warnings. Another 4,816 native
comparison cases pass for the lifecycle, callback mutations and fixed-value grant,
with the complete save restored and fresh initialization matching the official
second seed. The actual Big Octo fairy interaction remains untested.

Salvage light eligibility now follows chart slots 1, 3 and 5 for deciphering,
independently of the randomized item. Scoped native queries preserve opened and
completed chart checks, unrelated salvage kinds, and helper calls elsewhere.
Host tests cover all byte chart/item values, deciphered combinations and nested
queries; the full module compiles for PowerPC. A separate native comparison passes
38,002 assertions across 6,272 real proc_wait cases, nested calls and outside
queries, restoring the complete save and global pointers. Actual ocean rendering,
crane use and collection still need gameplay validation.

The Medli and Makar creation adapters bypass the sword predicates identified by
upstream without changing actual sword ownership. Fishmen creation similarly
bypasses its first Gohma prerequisite. Remaining story/stage checks and native
creation return values stay intact; query context is restored around nested
NPC creation. The shared item-query replacement composes this with Lenzo's
source flag. Host regression and strict full PowerPC compilation pass. A native comparison
passes 10,517 assertions across 512 Medli, 768 Makar and 32 Fishmen creation pairs,
with real opposite ownership predicates, nested context, full actor data and
factory arguments checked. Resource/heap callbacks stop before model allocation;
NPC models, dialogue, sage re-entry and other story gates remain untested or
separate remaining work.

Full sea-chart reveal is now conditional. Seed preparation must export
`init_reveal_full_sea_chart` in `seed_init_data.h`; the section-bounded reader
passed to `seed_options.extract_startup_options` decodes the supported option
and rejects unfamiliar initializer variants. With the option off, the core mod
leaves the native map initializer untouched, including its three drawn cells.
With it on, all 49 visited bits are set without changing other map flags. Host
fixtures test both paths and encoding rejection; all three existing private seed
fixtures decode as enabled. A real option-off seed boot remains to be tested.

The Deku Leaf fan shape now takes its visible/usable path with zero MP, matching
the randomizer's removed branch. Native phase wrapping and sine-table sampling
remain intact; no magic value or flying routine is modified. The host fixture
checks all 65,536 phase values with a null model and a complete actor-byte
comparison, and the full module compiles for PowerPC. Native material-chain
comparison and the actual zero-MP fan puzzle remain pending.

Pearl grants now use the native symbol-ownership method, then mark only the
matching pearl as placed. The tower-raised and pearl-tower-cutscene bits are set
only when the saved pearl byte is exactly seven, as upstream does; existing story
flags are never cleared to imitate ownership. Hyrule warp creation and execution
now use complete source consumers and require the real native Triforce count to
be at least eight. The initializer retains the conditional Zelda-awakened/FF3
event update and omits its forced Tingle-mail call. No Hyrule query/write/stock
scope adapters remain; the obsolete duplicate replacements have been removed.

The goddess statue's finishing branch retains the real native placed-pearl count,
sets the durable tower-raised bit when appropriate, then follows the native wait
and event-finish path without ordering the tower-rise cutscene. Its HD state field
is at 0xB98 (the decompilation retains the older field name `field_0x8AC`). Other
states, counters, animation/collision work and message counts remain native.
`test_pearl_progression.py` checks all pearl inventory/placement-bit combinations,
all acquisition orders, native event-query passthrough for all Triforce bitfields,
goddess/ship scopes and the finish branch's side effects. Native actor and actual progression
validation remain separate requirements.

The early Medli letter predicate now always returns delivered, matching upstream,
without changing actual letter ownership or delivery-event bits. The two linear
sea-chart marker methods return immediately, also matching upstream. These are
source/PowerPC covered. A private native fixture passed 128 Komali scenarios
with complete actor/save comparisons and observed two actual MAP initializations.
The eight stock marker panes retained their native default visibility; no displayed
MAP screenshot or natural NPC encounter is claimed.
The SDK currently disables notebook/chart marker overlays when a guest mod owns
the same chart-update function, so that overlay feature cannot coexist with this
randomizer UI patch. Other SDK services are not covered by this limitation.

### Swordless Phantom Ganon damage (bounded native proof)

`swordless_combat.h` gates Phantom Ganon's normal damage continuation on the
real hammer attack bit when `init_swordless` is enabled. It preserves the native
collision/status prefix, reflected-ball and special-variant branches, and ports
the public CC0 damage tail without inventing sword ownership or changing the
shared collision object. A scoped power-check adapter consumes only the expected
actor hit-position/timer/action site; it clears the caller's private stack-local
attack-info pointer to take its existing null-return after executing that tail.
Normal seeds and unrelated power checks delegate unchanged.

`test_swordless_combat.py` checks eligibility and tail effects across option,
variant, action, attack mask, death, depletion and scope cases with 32-bit guest
word layout. The entire module also compiles as strict PowerPC C. A private
muted native run constructed the retail Fganon actor and passed 68
checks across 11 injected collision cases, including real collision/status
prefix, power/health checks, normal-sword baseline, special-variant rejection
and hammer tails (exit 0). This is not a natural combat/playthrough test.
Temporary swords and shieldless defense have separate consumers below; full
swordless gameplay still needs a playthrough and Ganondorf final-blow test.

The six tree/Korok methods now use the same post-pearl branch inputs as upstream,
without granting Farore's Pearl or changing symbol flags globally. The native
pre-pearl BJ4/BJ6/BJX0 initializers stay inactive, BJX1 can initialize, and tree
state/message selection retains its other story checks and callbacks. Only the
first matching Farore query is adapted in each method; later and outside queries
read actual ownership. Host tests cover all symbol-byte values, story branches,
early message returns and nested scopes; full PowerPC compilation passes. Actual
native actors and the watering quest remain separate validation work.

### Temporary sword lifecycle (bounded native phase proof)

`swordless_lifecycle.h` gives an unequipped swordless player the fully charged
Master Sword at GTower phase entry. Its late native seam observes the play
unit's empty SafeString assurance after actor initialization: the same exact
GTower literal object must be assured twice, followed by the current-stage
object with the expected native vtable. Only then does it apply the upstream
Orca grant or ownership-aware cleanup. Between this seam and the upstream
write are only native character comparisons; stage callbacks, FF1 clearing and
recollection retain their original order. Observers preserve native registers,
and nested phase scopes restore the previous markers.

The host fixture covers 524,288 option/stage/ownership/equipment combinations,
wrong strings/objects/vtables, before-anchor calls, nesting, repeated calls and
FF1 ordering; strict whole-module PowerPC compilation passes. A private native
run also passed 75 checks through six real phase transitions:
sea, Orca, GTower, a boss rematch, Orca with native real-sword acquisition, then
sea again (exit 0 after 7,200 frames). It observed equipment before the final
assurance and after it at the actual audio/recollection callbacks, verified
cleanup before recollection, and preserved the genuinely acquired sword.
This proves the bounded native phase ordering on macOS/USA, not a complete
swordless playthrough or Ganondorf final blow.

The mailbox execute method bypasses only its opening Din's Pearl delivery gate.
Its native readable-letter checks, event/collision/animation callbacks and return
value remain unchanged; subsequent ownership queries see actual inventory.
The shared symbol adapter consumes this scope separately from tree/Korok scopes.
Host and strict PowerPC checks cover this source adapter; native mailbox delivery
and letter-specific requirements still need verification.

The ordinary packed save/load paths retain the native block order, native sound
mode callback, slot stride, padding and checksum ownership. Loading refills current
MP from the just-loaded maximum after all copies; it does not add pending MP.
Saving raises HP below three hearts only to the lesser of three whole hearts and
the maximum rounded down to whole hearts. HP already at least three hearts keeps
its native value. Game-over OPEN uses the same bounded restore value, while all
other game-over states execute the original native state machine and callbacks.
The control flow and layouts follow the public CC0 HD decompilation at a874e1a9;
the changed rules are the upstream MIT `misc_rando_features.asm` patches.

A private native save-path fixture passed 1,875 original-versus-adapter save/load
pairs over three slots and 175 game-over cases (15,719 checks, zero failures,
normal exit). It compared complete card buffers, save allocation, canaries and
sound-callback observations. This verifies the bounded methods; actual game-over
UI and a disk-save/relaunch playthrough remain separate checks.

The temple tablet adapter ports Create and the two Execute song-check states
from the public CC0 consumers. It keeps the real item queries and all native
model/event/talk callbacks, but ignores only the three query results that select
the upstream randomized branches. No song predicate override remains active
through callbacks. Other Execute states delegate to the native method.
The host fixture covers 2,880 Execute cases plus Create and nested ownership
queries from model, event, cut, talk, matrix and audio callbacks. Strict whole-core
PowerPC compilation passes. Actual native original-versus-consumer comparisons
and randomized tablet assets remain pending. The selective Execute dispatch uses
the entry state; the two native game-info getters are pure. An external getter
hook that changes the actor state during those calls is outside this adapter's
supported composition/proof scope.

The Forest Haven leaf-return tag also bypasses its Farore-owned stop condition.
The override arms only after the active actor's native constructor bit8 is set,
so constructor callbacks see actual Farore ownership. Nested active actors restore
the previous scope; unrelated actors and non-target item/flag queries cannot arm
it. Native constructor, collision status/cylinder initialization and return value
remain in place. Host and PowerPC checks pass; actual collision/return behavior
still requires native validation.

`seed_startup.py` provides a pure startup-data decoder for a separately validated
1.2.0 USA image. It requires bounded reads, terminated gear and consistent room/
spawn operands; unknown layouts fail. It emits no executable bytes. Synthetic
negative tests cover every short field and malformed operands; the private
chart-off seed also decoded with its expected room, health and chart option.
This is not the player setup flow: profile validation, remaining header/asset
extraction and isolated saves are still required.

The six tree/Korok methods passed a separate native comparison over 24,576 paired
scenarios (339,969 assertions, zero failures, clean exit). The fixture restored
complete save and tree globals, using controlled terminal action callbacks on
private actors. This does not verify the full watering quest or later adapters.

`seed_rewards.py` decodes the implemented NPC/minigame reward operands using
explicit ISA field schemas. Alternate tree and Beedle branches must agree.
Tests cover every item byte, short reads, changed layouts and inconsistent
alternate branches; the decoder also accepted the private official chart-off
seed. It returns data only and still requires external profile validation.

Ship boundary checking skips the first three story-path polygons on sea stages,
while preserving their native query calls, undersized-path handling, all remaining
polygon geometry, wraparound angles, nearest-point selection and boundary messages.
Non-sea paths are unchanged. The separate boarding/wind restriction in ship `execute` adapts only the first
matching native event-1980 query, retaining the original query call. The other
native consumer in `checkInitMessage` suspends that scope; nested executes restore
the enclosing state. Other prerequisites and later boarding/story flags remain native.

### Shieldless hammer defense (bounded actual-player proof)

`shieldless_hammer.h` ports both complete input/guard consumers from the public
CC0 source. While actually holding the hammer without an equipped shield,
the normal defense predicate also accepts the hammer. Existing early returns,
input priority, held-actor checks, animation blending/frame coupling and
collision shield flags remain in their native order. All other equipment states
delegate directly to the original functions; no saved equipment is invented.

The host fixture checks 67,071 non-target equipment/shield combinations,
targeted attack/defense priority and exclusions, helper callback order, live
blend/release and aliasing, collider flags, and whole-save isolation. Strict
whole-module PowerPC compilation passes. A private native run on the actual
player, with a natively built hammer model, passed 145 checks (exit 0). Both
consumers matched original real-shield baselines for helper callback order,
control/return results, R status, upper animations, every joint blend weight
and collider flags, with no save changes. It also demonstrated the original
no-shield crouch and the corrected defense proc. This controlled native
callback proof is not a full naturally played combat/playthrough test.

Hero-mode damage now uses the seed multiplier in the complete native damage
consumer, retaining normal-mode arithmetic, pending-life updates, soup/sword
flags and magic-armor rupee handling. `seed_startup.py` decodes the finite
0..80 multiplier; generated startup headers must define `init_damage_multiplier`
as `f32`, with no default substituted. Host branch/order/save-isolation tests and
strict PowerPC compilation pass. Native damage and nondefault-seed gameplay
verification remain pending.

Startup, implemented reward and boss decoders accepted four private official
outputs: default, second, swordless and chart-off. They distinguished swordless
and chart settings correctly; all four use damage multiplier 2.0. This is data
extraction evidence, not nondefault-damage gameplay or complete seed preparation.

`seed_content.py` plans a content-only overlay by hashing the player's original
and randomized content folders. It rejects links/special files, case collisions,
unsupported deletions, deep/large trees, changing files and SDK size-limit
violations. The chart-off output yields 147 files / 379,870,275 bytes, matching
its previously tested overlay. The planner neither copies assets nor creates an
installable package; those setup steps remain unfinished.

To prepare only the asset overlay, run `python3 seed_content.py ORIGINAL/content
RANDOMIZED/content --output NEW_PRIVATE_DIRECTORY` on one line. Without
`--output` it only reports the planned size. Copying refuses existing destinations,
verifies each copied file against the plan and writes its completion record last;
failed partial directories have no completion record. This is an asset-preparation
step, not an installable core mod or compatibility check.

`seed_headers.py` renders bounded startup and implemented-reward declarations
from decoded data, rejecting unknown/missing fields and invalid values. The
initialization-event policy is an explicit reviewed input, not recovered game
instructions. Fresh second-seed startup/reward/boss declarations compiled with
the complete module; resource tables and the reviewed event policy were retained
from its existing private fixture. Full public setup still needs those producers
and compatibility validation. Reward declarations can be combined with startup
output; do not also retain duplicate declarations in the reward-data header.

Startup decoding and header rendering reject unsupported preference/boolean,
magic, health and starting-location domains before compilation. All four official
test configurations pass these checks; malformed byte-sized values are covered
by negative tests.

A separate ship native run passed 1,296 polygon comparisons and the resource-backed
boat boarding predicate (5,191 checks, zero failures, clean exit). It retained
native angle/double-square-root helpers and restored save/play/actor state.
Boarding compared the tested attention/return branch, not every model byte; real
sailing and complete story traversal remain playthrough requirements.

Zunari's Magic Armor trade reward uses one decoded seed item, validated against
both native immediate operands and the upstream custom reward byte. The talk and
get-demo consumers preserve callback order and native Town Flower/Sail message
priority. A source-specific event flag (`6940`) records presentation, even when the
present actor creation fails, matching upstream. Repeat-offer checks use that
source flag rather than actual Magic Armor ownership. Other Zunari gifts keep
their native items; equal seed-item collisions retain upstream flag behavior.

The isolated damage consumer passed 576 native comparison pairs / 3,456 checks
with six multipliers, normal/hero branches, damage immunity and sword flags
(clean exit). Hero-mode selection and the armor callback were controlled; native
armor sound/rupee spawning and nondefault-seed combat remain untested. Complete
save, actor and play bytes matched the native reference after each pair.

The invisible-wall appearance predicate now ignores sword ownership only for
switch index FF, matching upstream. Its always-present flag and ordinary switch
queries retain native order and signed room handling. Host checks cover all
switch bytes, signed rooms, both switch values, parameter callback mutation and
actor preservation; strict whole-module PowerPC compilation passes. Native wall
appearance/collision verification remains pending. Pig aggression is separate.

`seed_resources.py` decodes the fixed field/item descriptor and collision-info
metadata tables from separately validated original/seed images, relocating archive
names into module-owned strings. It rejects unsupported pointers, changed texture
pointers, malformed names and short reads. The second seed yields 194 field and
35 item descriptors plus 368 info bytes; its generated declarations match the
previously native-tested header exactly apart from the comment and compile with
the complete module. No game resource data is committed with this extractor.

`seed_reward_tables.py` extracts only the 37 reviewed original-game reward data
bytes. It generates the GameMain initializer independently of reward operand
constants; remaining reward families need their own consumers. The second seed's
generated function matches its native-tested initializer exactly and compiles
with the full module. Short/malformed table data is rejected.

The flexible-entrance auction exit now follows local room0/SCLS1 and preserves
the original fade and dialogue cleanup. Dungeon map selection invalidates the UI
when a valid dungeon's map pointer changes; native invalid-index handling remains.
Host checks cover callback bid mutations, unrelated auction messages and 32,768
map pointer/index cases, and strict whole-core PowerPC compilation passes. Actual
auction/SCLS traversal and dungeon-to-dungeon UI transitions with matching seed
entrance assets remain unverified. These two adapters do not complete entrance
shuffle support; forced crawl and missing-boat spawn safety are separate work.

`initialization_policy.py` supplies the 41 source-defined new-file event flags
from the upstream MIT initialization routine in their original order. Header
rendering uses that reviewed policy by default, removing the old private-fixture
input. Swordless/rematch-specific events remain separate runtime decisions.

Salvage Corp records its gift with source event `6980`, rather than checking
ownership of the original chart. Both presentation branches mark the source after
item creation, including a failed creation. The event substance callback still
runs first; room and the live signed gift word (or actor fallback item) are reloaded
after it. The seed's gift byte `101C63BF` is already included in the validated
37 reward-table writes, so this family needs no additional seed constant.

`seed_prepare.generate_headers` combines all data decoders into one in-memory
header set after external compatibility validation. It decodes every input before
returning artifacts, so a late failure yields no mixed partial header set. All
four official test outputs produced their four headers and compiled the complete
module without private fixture headers. This API still needs a bounded/profile-
validated file reader and package installation flow before player use.

Forced crawl spawn posture uses the native playerInit late resource seam:
resetSeAnime, then Always texture0x71 and Link image0x72. Only that ordered
sequence can apply the spawn's original X-rotation low bit to last-scene mode;
existing forward/backward crawl modes remain intact. Earlier heap/model
callbacks and unrelated resource requests are untouched. This is the upstream
ACTR spawn flag, separate from the port's portable-state crawl restoration.
The 8,192-case host sequence/nesting fixture and strict whole-core PowerPC
compilation pass. Actual native sequence and crawl-space travel with matching
randomized entrance assets still require validation.

The café postman's two one-time predicates use source event `6A02`, leaving
actual reserve-item ownership unchanged. Event ordering keeps the native talk and
Hoskit conditions. After ordering another event, it reloads signed `mEventIdx`
(`+9DA`) and marks the source only when that value is zero, including a failed
order. This follows upstream's event-index check rather than adding an NPC-type
filter, and preserves callback changes to the index and the callback's return.

The shared bounded RPX reader and hash-only hook compatibility checker are
available under `tools/`. Their 23 focused tests pass; ordinary USA section,
symbol and relocation parsing remains identical, and four official randomized
outputs pass 449 hook-point / 332 function checks. This checks hook compatibility,
not complete source-port feature coverage or a playable seed.

Salvage Corp's separate native fixture passed 85,096 checks with full-state
restoration (clean exit). Café postman predicates/event ordering passed 72,466
checks (clean exit); that includes deliberately invalid-condition memory cases
which do not represent supported gameplay states. Actual NPC/event traversal
and collection remain playthrough requirements.

Development preparation command:

```sh
python3 seed_prepare.py ORIGINAL_USA_GAME RANDOMIZED_GAME NEW_PRIVATE_DIRECTORY
```

Add `--content` to copy the verified asset overlay too. The command checks the
original USA identity, bounded randomized input and current hook profile, pins
header data to the actual parsed RPX hash, and writes `seed-preparation.json`
last. Existing destinations and paths inside either input game are refused.
The second official seed prepared and compiled through this command; input-race
and late-failure tests pass. Building/installing a playable module, complete
feature support and packaged save-isolation validation remain unfinished.

Doc Bandam's first green/blue potion presentations decode two custom seed bytes
(`028FA474/475`). Only live message `7627` substitutes the selected green/blue
potion argument; repeat gifts, purchases and other items stay native. Completion
still updates the original potion unlock flags from the shop potion code, after
the native shop/drug callbacks and before returning to the wait action. The new-
potion message case retains its empty-bottle query but proceeds even without an
empty bottle; ordinary purchase/refill bottle checks continue through native code.

Prepared save identity combines the randomized RPX hash with a digest of every
seed content file, including files unchanged from the base. Two worlds with the
same RPX but different assets therefore receive different identities. Header-only
preparation has no complete seed identity; `--content` supplies it after verified
copying. Tests cover both identity inputs and unchanged-content differences.

The missing-boat adapter retains one native makeBgWait prelude. Its ordered
setWaterY/setRoomInfo seam distinguishes the four earlier ground failures from
the later absent-ship return. Only spawn5 in the three native boat modes can
continue through the public-source fallback and model-update tail. Other
failures and successful native paths retain their original return values.
The 12,288-case host dispatcher fixture, 48 fallback combinations, nested
failure controls and strict whole-core PowerPC compilation pass. A bounded
native comparison passed 897 pairs / 53,026 checks with a clean exit, covering
192 missing-boat fallbacks, 384 ship-present paths and 320 early failures. The
actual player initializer also reached the late writer in positive and negative
controls. Collision/action/model endpoints were controlled, and the native
reference's classification was narrowly normalized; matching randomized assets
and actual travel remain unverified. The seam assumes the canonical native helper order; external hooks
that repurpose that order or mutate cached spawn fields are outside this proof.

The 100-rupee auction retains its seed item byte and native winner/availability
code. Only its obtained-event halfword is initialized to source flag `6B01`, after
the seed reward-table writes. Winning records that source independently of Swift
Sail ownership; other auction entries and neighboring table fields stay native.
A private native run passed 32,931 checks with a clean exit: 1,536 source-query
pairs, 96 availability cases and 3,072 winner cases. It observed the initialized
`6B01` value and checked failed item creation, callback mutations and complete
save/play/table restoration. Item creation and camera backends were controlled;
actual bidding, cutscenes and item collection remain untested.

Kogoli's availability initializer now ignores Medli's awakened flag while still
executing the native flag query and action initializer in order. It preserves
the action initializer's return value and never changes the story flag. Host
checks cover all byte-sized query/results and nested initialization; the whole
module compiles for PowerPC. Actual actor spawning remains unverified.

The complete preparation CLI also passed on a player-owned randomized game with
`--content`: all four headers and 147 changed content files were emitted, with
the completed seed identity binding the RPX and complete content digest. This
checks preparation only; it does not establish module packaging or playability.

Windfall pedestals now spawn independently of Delivery Bag ownership. The
complete creation consumer retains the real bag query, one-time NPC/collision
construction, both resource phases, heap failure and native initialization.
Host checks cover 1,296 phase/query/construction combinations, including flags
changed by constructor callbacks; strict full-module PowerPC compilation passes.
Actual pedestal spawning and the decoration quest remain unverified.

Jabun's whirlpool and stone-door existence decisions no longer depend on Endless
Night. The door retains its complete native creation routine and the real flag
query. A scoped override arms after the final collision-constructor array, so
earlier constructor callbacks see the actual story state; it is consumed before
the target query's callbacks. The constructed bit must also be set, so a later
array-return observer cannot consume the override before the native caller
finishes construction. Already constructed actors reach the query without
earlier native calls. Host checks cover nested scopes and unrelated constructor
returns, and strict full-module PowerPC compilation passes. This seam assumes
the canonical native callback order; actual resource/collision traversal remains
unverified.

Time passage no longer waits for Wind Waker ownership. The full daytime consumer
retains the three SafeString resolver calls per stage comparison, the sea room-14
layer stop, event/night stops, `A_umikz` and `ENDumi` exceptions, native float and
day-wrap rules, and day/audio callbacks before save writes. Other item-ownership
queries are untouched; the upstream patch removes this one ownership call.
Stage comparison preserves the native distinction between its pre-callback
pointer-identity capture and post-callback byte-comparison reload. Regression
controls cover a stage resolver changing the literal pointer in both paths.

Lenzo's framed-gallery completion flag now requires his Picto Box conversation
flag first. The complete photo-tag event consumer preserves event-index capture,
end-check order, event reset, cut callbacks and attention-flag restoration. Host
checks cover 8,192 completion/prerequisite/cut combinations, including callback
changes to the save pointer and event indices. Strict full-module PowerPC
compilation passes; native event and exterior-door validation remain pending.

Zephos's Wind's Requiem AI no longer grants a second item before the event's
seeded reward. Its complete tact-message consumer preserves status changes,
message sending, cancellation flags, staff-index capture and cut completion.
Host checks cover all 16-bit flag values across the three relevant message
statuses, cancellation and callback mutations (786,432 combinations). The full
module compiles for PowerPC; native event/reward playback remains unverified.

The SDK now supports declarative seed save profiles selected from the trusted
startup plan before recovery or backups. Default saves/backups and state slots
are separate; explicit save paths retain the player's exact destination. Full
states require matching seed and trusted code/dependency identity, while portable
states retain the seed boundary across compatible code updates. The SDK change
passes 96 native tests and 18 Java recovery contracts, plus production-source and
Android JNI syntax checks. The private package builder declares the complete
prepared seed identity; live-game/Windows/Android-device validation remains.

The Dragon Roost pond wind now stops after Gohma is defeated, independently of
Din's Pearl. Its complete creation consumer retains the discarded environment
getter, constructor flags, parameter capture and separate weather getters after
the wind callbacks. It reads actual boss completion rather than the Fishman
availability override. Host checks cover 2,048 parameter/constructor/boss cases,
and the full module compiles for PowerPC. The related quake, heat, cloud and
distant-model source gates are implemented below; actual weather rendering is
unverified.

The scheduled quake creation gate also follows actual Gohma completion. Its
complete consumer retains two separate type queries, draw-queue insertion,
virtual stage scheduling, duration/range initialization and HIO creation/count
updates. Host checks cover 131,072 combinations, including a changed second
type, every schedule byte and HIO counter wrap; the stage virtual call is a
controlled fixture endpoint. Strict full-module PowerPC compilation passes.
Actual quake/camera/sound playback remains unverified.
Pot damage reads the saved Z-rotation high byte (`+798`); stone heads read the
home X-rotation low byte. Custom IDs `01..FE` use native direct item creation with
type 3 and action 5, retaining item-save bit, room, angles and scale. `00` and `FF`
use native table creation and its original action. Stone heads retain that table
call even for table 63, matching upstream's removed early exit. Pot carry cleanup,
assert/effect dispatch and break-sound order remain native, with fields reloaded
after callbacks rather than rewriting parameters or global item tables.

Dragon Roost LOD secondary-model loading follows Gohma completion rather than the
cloud-cutscene event. The complete public CC0 consumer preserves resource-name
virtual callbacks, primary-model failure, Windfall lights and Forsaken Fortress's
event condition. Host tests cover all 64 room values, both independent flags,
resource failures and parameter mutation during the first resource lookup; strict
whole-module PPC compilation passes. Native comparison remains pending.

Dragon Roost's name-zero cloud creation also follows Gohma; the alternate
Nayru cloud retains its original symbol condition. The full public CC0 consumer
keeps once-only base/BTK construction, initial parameter capture, all load phases,
resource-loaded marking, native heap callback and matrix/action ordering. Host
tests cover 768 combinations including a null model after a successful controlled
heap callback. Strict whole-module PPC compilation passes; native proof is pending.
## Private development package builder

`seed_package.py` creates an **experimental, incomplete source-port package**, not
a supported playable randomizer. It requires the source checkout, a PowerPC-capable
Clang, the completed `seed_prepare.py --content` output, and both player-owned game
folders. Example (run from this directory):

```sh
python3 seed_package.py PREPARED NEW_PRIVATE_PACKAGE \
  --base-game ORIGINAL_USA --randomized-game MATCHING_RANDOMIZED_USA \
  --ppc-clang PATH_TO_POWERPC_CLANG
```

The builder independently validates both RPXs, regenerates all four headers,
checks every prepared overlay file, reconstructs the complete content digest from
the original game plus that overlay, and compares it with the randomized game.
It refuses header-only preparations, altered/stale inputs, extra assets, links,
traversal paths and existing output directories. It compiles a fresh PowerPC
relocatable `mod.elf` using the SDK declarations, translates every function with
the SDK inspector, and records source/SDK hashes, source revision, compiler flags,
ELF hash and hook/import inventory. A prior diagnostic `module.elf` in the
preparation directory is ignored. A compiler returning success without producing
a fresh ELF is a failure. Failed copies have no final runtime manifest.

The resulting directory is the package to select in **Settings > Mods > Installed
packages > Choose folder > Install package**. Its `manifest.json` points to the
new `mod.elf` and the exact `content/` overlay; the preparation's `assets/` folder
is not itself a package. The normal SDK runtime translates/builds the ELF for the
host when preparing the installed guest package. Activation still requires code
mods and the usual explicit experimental/native-code confirmation and restart.
No installer automation or completed playthrough is claimed.

The manifest requires manager 1.7.0, declares `supported_game_builds: ["USA"]`,
and selects `save_profile` provider `twwhd-randomizer` with the complete prepared
seed identity (code-derived data **and** all randomized content). `game_id` alone
does not restrict regions: its old `wwhd-usa` spelling is an any-region alias.
The runtime checks the explicit build constraint before content activation/save
selection, and its generic save-profile feature keeps saves, backups, cloud
choices and state slots separate. An explicit `--save` remains an intentional
exact-directory override. This package contains player-owned assets: keep it
private and never commit or distribute it.

A frozen integrated private package has booted to the sea stage on macOS with
the save-profile SDK runtime and exited cleanly. It loaded 181 functions / 162
hooks, selected namespaced save/state folders without an explicit save override,
and wrote full and portable states carrying the profile identity. Ordinary save
and backup sentinel folders stayed untouched. A second run restored both states,
returned Link to the saved sea position, and exited cleanly with those sentinel
folders unchanged. This validates same-profile capture/restores on that frozen
package, not cross-seed live rejection, later source changes or gameplay
completion.

The pond heat-haze create consumer now uses Gohma completion instead of Din's
Pearl only in Adanmae. Its complete public CC0 flow retains the pre-constructor
mode, post-constructor path parameters, stage-string resolutions, native particle
arguments, callback/layer ordering and live emitter reload. Host tests exercise
262144 lifecycle combinations, all parameter bytes and callback replacement of
the global emitter; strict whole-module PPC compilation passes. Native proof is
pending; these checks do not establish visual behavior in the pond.
Reverse small-key doors and the Cabana reverse exit now have complete source
consumers for door10 `setEventPrm` (02127080), door12 `setEventPrm` (0212919C) and
knob `actionVilla` (021A44DC). They follow the public CC0 `d_a_door10.cpp`,
`d_a_door12.cpp` and `d_a_knob00.cpp` at decomp commit
`a874e1a9243f573af44c2649a2f2107dbebb50be`, with only upstream's MIT entrance
changes. Boss keys, forward entry, bars, grabbed-actor lookup, area tests and event
writes retain native order. Reverse Cabana event setup uses WAIT (1) only during
`setEventPrm`, then writes VILLA (8); no saved key or Deed flag is fabricated.

These changes require the new decoded `init_entrances_shuffled` field. Upstream
applies the group only for `anyEntrancesShuffled()`: the decoder requires all three
pinned helper branches or all three native operands, rejecting partial/unknown
variants. Existing preparations must be regenerated so their four header hashes
match. The actual second seed decodes the option as off. Host fixtures exercise
104,960 combinations plus callback traces and whole actor/save preservation;
strict complete-module PowerPC compilation passes for actual option-off data and
a clearly labeled synthetic option-on declaration. Native actor validation is
still pending. This covers these consumers, not the entire entrance patch group.

Komali's duplicate room initializer now retains its real Din's Pearl query and
callbacks but returns false without changing status or setting an action. His
door's exact exception case always allows entry; every other door still delegates
to its original predicate. Host checks cover all 16-bit door angles and 512 room
query/callback cases. Strict whole-module PowerPC compilation passes; actual room
spawning and door travel remain unverified.

The Dragon Roost pond's solidified-lava cover now stops after its normal one-time
actor construction, matching upstream's false pearl predicate. Its resource,
heap and background creation paths are unreachable; the bomb-flower puzzle
therefore remains required. Host checks cover 131,072 constructor-flag/callback
cases with full actor-byte comparisons, and the complete module compiles for
PowerPC. Actual pond rendering and puzzle traversal remain unverified.

Mesa's door ignores its day/night lock only when the seed carries the separate
door-shuffle patch. The decoder validates the exact conditional-branch family and
reviewed destination, keeps this option separate from other entrance shuffles,
and rejects inconsistent settings. The door dispatcher delegates every unrelated
case. Host tests cover both policies across all 16-bit door angles; complete
PowerPC compilation passes with independently regenerated option-off seed data.
Official shuffled-seed validation and native door travel remain pending.
Seed preparation now uses an explicit **source-core data-only validation policy**
(`tools/recomp/source_core_compat.py` and `source-core-usa.json`). The original
randomized-executable setup policy is unchanged. The source policy adds one
reviewed complete-function hash for `dComIfGs_setGameStartStage` (025217F8–02522198,
2,464 bytes): the official entrance option changes only its call at 025220C4 to
upstream's last-visited-ocean-sector helper. All hook-word aggregates, layouts,
imports and other function checks remain intact, and the extension is bound to
the exact normal profile hash. Unknown variants still fail closed. The result is
a data-only seed identity, not a build/address map for compiling that RPX.

The official `CodexCoreDoorEntrances117` seed was generated privately with the
upstream CLI using one worker. It decodes `entrances_shuffled=1`; the source-data
policy accepts it and the normal randomized-executable policy still rejects the
same input. Data validation alone does not establish entrance savewarp behavior
or certify a playable entrance-randomized core mod; the source consumer below
implements that behavior separately, with native verification still pending. Previously prepared
records must be regenerated to carry the explicit data-only policy report.

Fire Mountain's firewall creation now reads room0 switch5 in place of the old
Endless Night event gate. The exact upstream branch returns creation error5 when
the switch callback returns1; every other full-word result enters event Wait2.
The native constructor/resource/collision prefix and fused sound-ring positions
are retained. Host regression passes 16,128 creation scenarios; strict full PowerPC
compilation passes, with actual fmadds instructions and no new libm import. Native
wall creation and actual Fire Mountain traversal remain unverified.


Custom pot/stone-drop native validation: 10752 actual pot consumer pairs, 1536
stone cases and six callback-mutation cases passed 98363 checks with zero failures
in a private muted native run. Full actor, save, play and callback traces matched
after the intended direct-item argument substitution. Stone table63 had an actual
native no-create negative control and explicit expected new-path arguments. Native
parameter getters/angle construction ran; spawn, effect and sound backends were
controlled. Full save/play/effect-table bytes were restored. This does not establish
real item collection or visual break effects.


All five Dragon Roost environment create consumers passed a private native
comparison: 2048 wind, 1024 quake, 1024 LOD, 1536 cloud and 12288 heat pairs, plus
three actual heat SafeString mutation cases (250926 checks, zero failures, clean
exit). The reference executes actual boss completion at the exact replaced native
predicate; stored pearl/event ownership stays identical through both invocations
and every callback. Native base/BTK constructors and parameter helpers execute;
draw/resource/HIO/particle/layer backends are controlled. Full actor/save/play,
ordered callback traces and affected globals match and all save/play/global bytes
are restored. This does not prove outdoor visual behavior or a playthrough.

New Game+ save initialization now omits the upstream five pictograph-completion
operations: C407=7, the misc treasure bit, the Deluxe Picto Box inventory store
and both camera ownership calls. The complete public consumer retains held event
registers, initialization event bits, sea switches, validated HD progress bytes,
settings, picture count and the original play-item selection fields. Host checks
cover 2,048 carry-over combinations including every first HD progress byte and
a save-pointer replacement by the initializer;
strict complete-module PowerPC compilation passes. Native callback-mutation and
actual New Game+ save creation remain unverified.

Boss exit warps now use the upstream complete `checkEndDemo` replacement, which
returns true independently of pearl/sword ownership and the Tower event. The
native creation routine still requires actual boss completion and retains its
effects, `WARP_WIND_AFTER` event lookup and end-animation setup. This replacement
does not set completion flags or grant rewards. Strict full-module PowerPC
compilation passes; actual boss-exit traversal remains unverified.

When verified seed metadata enables entrance shuffling, the complete public CC0
setGameStartStage consumer preserves its native branch priority and callbacks,
but the map-ocean branch supplies the last visited ocean sector to the return-place
setter. The changed room reaches setter entry observers and the native body
together; no caller-address inference or global current-room spoof is used. The
port's built-in mod-stage hook remains outside the native-body replacement.
Non-entrance seeds delegate the original body. Host checks cover all branches,
live save-pointer/ocean-byte mutation during coordinate callbacks, all256 sector
bytes and SafeString capture/reload/bounds. Strict whole-module PPC compilation
passes with the feature disabled and enabled. A private native comparison passed
3,288 pairs and 73,641 checks with zero failures and clean exit. Actual native getter
and setter calls preserve all other state and ordered entry/return arguments; tests
include fresh-save-pointer/ocean-byte changes, nested setter observers and six
actual SafeString third-callback mutations. Full save/play/global state is restored.
Controlled stage/scene/time backends do not establish entrance traversal or an
official shuffled-seed boot. The earlier private LR experiment is ABI evidence only.

The Big Octo Great Fairy's complete message getter now advances message12014 to
item-hint message12015 for every bow tier, retaining the native bow-byte read and
leaving every unrelated message untouched. Host regression checks all256bow bytes
against seven message boundaries with complete actor/save comparisons; strict
full-module PowerPC compilation passes. Actual fairy conversation remains
unverified. Native message comparison covers all 256 bow bytes and six message
values, including the original Hero's Bow control.

Ho Ho's complete first creation phase now omits the three disappearance switch
queries, while retaining native construction, parameter helpers and case2's
0520 event prerequisite. The resource flag is written only when that phase
advances. All256parameter bytes, both construction states and four full-word
event results pass2,048 host cases; strict full PowerPC compilation passes.
Native phase comparison includes real saved switch/event inputs and getter
callbacks. Actual spawning through resource loading remains unverified.

Ho Ho's complete execution consumer now also omits those three switch queries
and the resulting deletion path. Parameter helpers, event/talk decisions, direct
and virtual member dispatch, movement, collision, attention transforms and matrix
updates remain. Host regression covers3,072 parameter/dispatch/event/command cases
with callback-driven position and data changes; strict full-module PPC compilation
passes. Private native comparison passed 4,592 pairs plus six unmodified-native
stop/delete controls: 82,775 checks, zero failures and clean exit. Actual native
constructors/getters and movement/matrix/vector helpers execute; resource, animation
and collision backends are controlled. Full actor/save/play, ordered callback traces
and affected globals match and are restored. Live hint conversations and an actual
hint playthrough remain unverified.

Daily mail now checks actual stage2 boss completion separately for Aryll's and
Tingle's letters; Tingle still requires rescue flag0B80. The complete public daily
consumer retains all five delivery calls, capped counters, inventory conditions,
event/temporary resets and Friday work, reloading the active save after callbacks.
Host regression passes65,536 eligibility/counter/callback-mutation cases; strict
full-module PPC compilation passes. Orca's mailbox condition is handled separately; actual delivery remains unverified.

The complete mailbox initialization consumer now stocks Orca's letter after
stage4 boss completion instead of the skipped event. Other letters retain their
own prerequisites. Animation, model-pointer reload, null-model culling, collision,
price, mode, effect and event-actor setup remain native calls. Host regression
passes16,384 eligibility/wallet/model cases, including model replacement during
calculation; strict full PowerPC compilation passes. Actual mailbox gameplay remains pending; the separate complete Hyrule initializer
removes its forced Tingle-mail call.

The full Hyrule-warp initializer no longer force-stocks Tingle's letter. Its
existing Zelda-awakened event update remains, and the initial end-animation gate
now checks for at least eight Triforce shards as upstream requires. Model scale,
translation snapshots, water fallback, event lookup and callback-driven model
reloads remain. Host regression passes1,024 shard/event/water cases, with model
replacement during both water and matrix callbacks; strict full PPC compilation
passes. Actual warp/mail behavior and full progression remain unverified.

Grandma's music uses a complete public CC0 source port of
`JAIZelBasic::setScene` (0202677C), from `JAIZelBasic_7.cpp` at decomp commit
`a874e1a9243f573af44c2649a2f2107dbebb50be`. The unhealed scene-0xB track is selected
regardless of the audio object's shield status, including layer 9; this matches
the upstream unconditional branch, which bypasses that layer's old silence path
as well as the shield check. The real healing query still selects the normal
scene table afterward. No inventory or global ownership query is changed.

The complete selector retains its other sea, Outset, Windfall, dungeon, story,
layer and early-return gates, native callback order, HD field offsets and table
reloads. Host fixtures pass 126,885 cases, including all shield bytes and callback
changes to the save allocation, table waves and audio flags. The complete module
passes strict PowerPC compilation and SDK translation. Native audio/gameplay
validation remains pending; these checks do not certify all music behavior.

The complete buried pig-reward consumer now calls asynchronous `createItem`
with type3/action5, and never treats its returned ID as an item actor. The native
random calls, request/actor checks, home/current room distinction, switch handling,
enemy parameter path and completion flags remain. Host regression passes32,768
item/lifecycle/callback cases, including failed and pending creation IDs; strict
full PPC compilation passes. Actual pig digging, delayed model loading and item
collection remain unverified.

Lenzo's complete legendary-photo availability predicate now uses the upstream
milestones: exactly eight shards for the King, Helmaroc/Jalhalla/Molgera completion,
Jabun's broken-wall switch and Ballad of Gales ownership. The unchanged day2 event
sequence still short-circuits in its native order. Host regression passes13,824
weekday/result/event combinations, including nonboolean and signed boundaries;
strict full PPC compilation passes. Native conversation/shop behavior remains
unverified; automatic related-figurine grants are a separate remaining task.

The gallery's complete figure-making consumer now grants the upstream related
figurines for Octoroks, Valoo, Ganondorf and Wizzrobes. The King's photo grants
the pirates, Tetra and Zelda in the reviewed order; existing other pairings stay.
The selection count is captured once and the per-photo index wraps at256 as in
the native routine. Host regression passes3,096 mapping/selection/count cases,
including overlapping mappings and count mutation from grant callbacks; strict
full PPC compilation passes. Actual gallery submission/save behavior is unverified.
The Forbidden Woods hanging flower now falls when all five ropes are cut, even
without boomerang ownership. Its real inventory query still runs; only the
upstream ownership branch is removed. The complete public CC0 movement, matrix,
background and camera consumer preserves the other modes and callbacks. The
six-byte GHS angle-add return is expanded locally while retaining its native
constructor and returned-pointer behavior; observers on the outer angle-add
entry are consequently outside this source path. Host tests cover 31,104 rope
state/ownership cases, constructor mutation and aliasing, particles, vibration,
camera reset and counter wrapping. Strict full PowerPC compilation passes;
native flower execution and actual Forbidden Woods traversal remain unverified.

The four Mk/Uk visit talk/event consumers now skip the pictograph-ownership
special-event path, retaining the first unrelated story event and the final1208
query that precedes upstream's unconditional branch. Partner lookup, captured
event ID, state/demo dispatch and1E04 writes keep their native order. Host
regression passes512 event/result/partner cases with active-save changes after
callbacks; strict full PPC compilation passes. Actual NPC conversations remain
unverified.

The bomb-shop talk consumer now applies the upstream constant-true ownership
result, removing its special head-swing path while retaining cursor, trigger and
message-state behavior. Host regression passes288 trigger/send/status cases,
including full-word recheck differences and callback-updated message IDs; strict
full PPC compilation passes. The remaining seven constant-true query sites now
live in complete public CC0 `CreateInit` (022098D0), `_create` (02209DB4), and
`CreateHeap` (02208CDC) ports. These retain native action cleanup/initialization,
HD subobject constructors and vtables, the real eventA02 query, asynchronous
resource phases, heap failures, HIO accounting, model/animation allocation,
signed joint checks, callback installation, collision setup and shop cursor.
No pearl inventory or story flag is synthesized. Host fixtures cover2,560
initializer cases,49,152 creation cases and73,728 heap/failure cases; whole-module
strict PPC compilation and SDK translation pass without duplicate replacement
owners. Actual NPC creation and purchases remain unverified.

Hyrule warp `normal_execute` (024D5354) is now a complete public CC0 source
consumer with the execution-time shard gate from `remove_cutscenes.asm`. It
computes native horizontal geometry before the real signed `getTriforceNum>=8`
check, preserves the emitter captured before callbacks, animates before updating
that emitter's live flags, and retains native `check_warp` and its conditional
state write. The skipped event2D08 is neither queried nor globally overridden.
The physical native square-magnitude/sqrt ABI passes F1 explicitly and keeps the
result double-precision across callbacks; it does not rely on the public shim's
ambient-FPR/float prototype. Host fixtures pass2,084 shard/boundary/mutation cases
plus a locked invalid-emitter check. Strict full-module PowerPC compilation and
SDK translation pass. An integrated snapshot also passes replacement-address
ownership and import checks: one owner each for warp initialization/execution,
no conflicting replacements, five supported host-service imports and373 native
function targets present in canonical generation. Native actor/gameplay proof
remains pending; this does not complete the Ganondorf, Molgera or barrier groups.

The Deku Tree's complete opening-message consumer now takes the upstream direct
question-text branch for every original dialogue type. It preserves the type-byte
read, first/repeat question messages and the one second-talk-byte update. Host
regression checks65,536 type/seen combinations and complete actor preservation;
strict full PPC compilation passes. The following message-choice and chart-icon
bottle gates still need porting; actual chart marking remains unverified.

Korok chart-icon frame updates now omit the bottle query while retaining the
busy-UI hide path, completion/marking flags, marker permutation and animation
owner reloads. Captured frame constants stay fixed across callbacks; completion
flags are queried for each marker. Host regression passes4,096 flag/bit/mutation
cases; strict full PPC compilation passes. The remaining Forest Water message
choice still needs porting, and actual map rendering remains unverified.

The complete Forest Water message-choice consumer now tests marking flag3940
and uses upstream's exact equality-to1 branch. All other dialogue transitions,
selection fields, boss checks and the store-before-marking callback remain. Host
regression covers1,310,720 message/result/selection combinations, including every
16-bit message ID and callback mutation of the output message; strict full PPC
compilation passes. This completes the grouped chart-marking source changes, but
native dialogue and actual chart rendering remain unverified.

A fresh private package of the integrated source through2d53b3d2b passed a muted,
hidden boot on the frozen SDK runtime: opening initialization completed, Link
reached active gameplay, and the scheduled exit at frame1,801 returned0. Default
save/state directories used the seed's mod profile; four vanilla save/backup
sentinel directories stayed unchanged. This snapshot predates the newer pig,
KoRL and remaining bomb-shop consumers. It does not establish actual progression,
state restoration, Windows behavior or a full playthrough.
KoRL hint metadata now reads the pinned 1.2.0 symbols at 028F9F24 (big-endian
last message) and 028F9F5C (Hyrule-text boolean), validates the signed message
range D73..INT_MAX and strict 0/1 flag, and emits required seed declarations.
No per-seed value is hardcoded. Messages inside the hint range advance one at
a time or terminate at its end before the native message-manager lookup; all
other messages delegate the complete native consumer. The existing init-message
consumer retains early guards and no-sail behavior, then selects D73 directly or
compares only Hyrule for D77 when the seed flag enables it. Removed late milestone
queries are not globally overridden.

All 127 source tests pass, including every 16-bit current message value, four
validated ends, native return propagation, signed boundaries, all old milestone
inputs and both Hyrule flags. Actual first/second/swordless seed extraction yields
3443/0; non-default paths use synthetic validated metadata. Whole-module PPC
compilation and SDK emission pass for both actual and maximum-end/Hyrule-enabled
metadata. Ownership checks find one init-message owner and one next-message owner,
no duplicate replacements and all imported native targets in canonical generation.
Native hint interaction and a seed with non-default hint text remain unverified.

Moblin creation (`daMo2_Create`,021CCB00) now has a complete public CC0 source
port. The upstream swordless branch sets the native jail-mode byte to zero at
its original point without querying or changing sword ownership. Resource phases,
HD constructor, asynchronous lantern attachment, stage strings, death/spawn/path
switches, HIO, model/collision/fire setup and both initial native executions are
retained. Host fixtures cover524,996 cases including callback mutations, all
mode/death-switch bytes, phase failures and lantern states; strict full-module
PPC compilation and SDK translation pass. Native actor/gameplay verification remains pending.
### Swordless pig aggression (source port; native proof pending)

`daKb_Execute` (`02190CE0`) is a complete CC0 source consumer, split into
attack, normal, carry and swim helpers. It applies the two upstream
`make_game_nonlinear.asm` changes: damage is no longer reset when a pig is hit
without an equipped sword, and a completed throw always decrements its health.
Other movement modes, native resource/physics/audio callbacks, timer wrapping,
PPC NaN addition and SafeString pointer/termination semantics remain present.
There is no global inventory or collision-query override. All vector-return
operations here use output pointers; no aggregate-return ABI extension is needed.

The host regression covers 262,144 combinations of signed health, actual sword
byte and pig size across both modified gates, plus common dispatch/timer/carry
restoration paths, swim cleanup, NaN payloads and SafeString callback/boundary
cases. Its resource, physics and combat endpoints are controlled fixtures. A
strict complete PPC module build passes; actual native Execute parity and live
pig behavior still need verification.

The pig's random and sqrt aliases retain the native F1 value as `f64` until
the actual caller rounding/comparison point. Precision regressions cover
controlled callback returns around timer, movement and comparison boundaries;
they do not assert a reproduced stock-helper precision failure.
Tower servants no longer schedule the east servant's tablet-disappearance event,
while west/north switch and completion queries retain their callbacks. Finishing
servants retain the 1B01 query and use the beam animation; other finish states
delegate the complete native action. The TotG hsehi1 tablet now marks 2510 after
its deletion timer reaches zero and drawStop completes. This is a different actor
from the existing Earth/Wind temple-tablet consumers. West/north door glow retains
real song/event queries but ignores only their original unlock decisions; animation
and sound follow the native callback order. Host branch/byte/callback tests and
strict whole-module PPC/SDK emission pass, with one owner for each new consumer.

Integrated verification through the Tower servant/tablet/door consumers passes
137 core tests and strict full PowerPC compilation. SDK translation emits198
hooks with162 unique replacement owners. These are host/build checks; the
inactive-servant executor and native Tower interaction remain pending.

The complete enemy-weapon Execute consumer now preserves the Phantom Ganon
maze sword after event3A08. Its actual event query remains local and unchanged;
only a nonzero result reads the current stage, after the callback, and performs
the upstream byte comparison against GanonJ. Other stages retain disappearance.
The full spawn delay, GHS member-function dispatch, raw position copies, hit/fire/
water/particle/light lifecycle, collision registration, mass, chain and animation
paths remain. Host tests cover86,535 cases and callback mutations; strict full
PowerPC and SDK translation pass. Actual native weapon parity and maze gameplay
are unverified. The separate Phantom Ganon chest-gate consumer remains pending,
so this does not complete the grouped maze task.

The Dragon Roost pond switch-wait and level-move methods now have complete CC0
source consumers. They apply the upstream fivefold spread/rise and animation
speeds and62-frame geyser sound duration without modifying shared game data.
The real switch query retains its exact result==1 condition, particle creation
keeps its callback position, and the level transition retains the frame threshold
across its callback. Host fixtures cover528,392 switch/room/result combinations
and callback mutations. Full strict PowerPC and SDK translation pass; actual
native animation/puzzle timing remains unverified. The Komali shortcut spawn
change remains pending, so the grouped Dragon Roost task is still incomplete.
The complete public servant update consumer now resets the home room to FF when
its unsigned current room differs from the loaded room, immediately before the
initial-room callback. All later movement/collision, PMF checks, pedestal handling,
water emitters, event ordering and typed smoke-lighting copies remain in the source
consumer. The host test covers all 65,536 room/stay byte pairs, callback-mutated
room values, finished/creating servants, stop/restart and a full carry update.
Strict whole-module PPC compilation and SDK emission pass with one execute owner.
Native servant, tablet, physics and door comparisons remain unverified; these
source/host checks do not establish dungeon traversal or a complete native proof.

The complete Komali initializer now mirrors the saved Dragon Roost shortcut
switch into the currently loaded stage at the upstream insertion point. Both
original story-event queries and the conditional action setup retain their order;
the shortcut read happens afterward, including on the original false return path.
Save pointers reload after callbacks, and the switch write preserves unrelated
bits.524,288 host cases cover every saved/current switch byte and signed event
results. Full PowerPC/SDK checks pass. Together with the pond consumers this
finishes that grouped source implementation; native shortcut/puzzle checks remain.
PPC compilation and SDK translation pass.

The separate per-frame `daMo2_Execute` (021C9B1C) and all five inlined helpers
now have complete public CC0 source ports. The native jail-mode byte is cleared
at the execution-time sword-check block; callbacks can still change it later.
Ice/fire/death transitions, gated frame timers, full hit/damage reactions,
camera events, smoke/weapon lifecycle, collision and ground slopes, and attack
windows retain their native paths. The ground-crossing ABI preserves F1 as a
double across later callbacks and rounds the native single-precision arithmetic
explicitly. Host fixtures cover30,998 cases plus conversion boundaries and the
full strict PPC/SDK translation checks pass. Native Moblin creation, capture
and ordinary combat/gameplay verification remain pending.

A bounded actual-native pig Execute comparison passed2,788 synthetic cases,
5,576 paired runs and410,081 checks with zero failures, plus two actual native
swordless negative controls. Every callback observed the genuine equipment byte;
actor/morf/emitter/save/play/matrix/static snapshots and callback traces matched.
The game exited cleanly at frame3,000. All six integrated pig headers match the
passing probe hashes. The initial diagnostic caught the attention-position offset
error, now corrected; particle IDs also retain the native zero extension.
Resource, background/physics, hit/damage, particle/audio and other scene endpoints
were controlled. This proves bounded caller parity, not a real pig encounter,
combat utility behavior, current aggregate boot or randomized playthrough.

The first Tower native comparison exposed omitted cached-light records in the
servant smoke-lighting copy. Both records now copy all original fields with
their native widths/order; the regression verifies the records and untouched
padding. All142 integrated host tests and fresh full package compilation/SDK
translation pass after the correction. The earlier native diagnostic is retained
as a failure; the corrected native comparison remains pending.

The complete Hyrule barrier break-start consumer now requires a signed shard
count of at least8 at the upstream insertion point, after the native equipped-
sword check. Its real3980 query keeps exact equality-to1, geometry retains the
native double F1 handoff, and sword-active/attack-type/event ordering remains.
Host regression covers655,456 sword/cut/shard/active and distance/callback cases,
including NaN and the captured player pointer across later callbacks. Native
barrier interaction and cutscene safety remain unverified. Ganondorf hit/reflect
and Molgera placement changes still remain in the grouped final-boss task.

The complete reflected-arrow consumer now skips Ganondorf's special reflected-
shot cutscene above unsigned health25, after the real search and lock-on queries.
The retained target-X load precedes the new health gate. The ordinary reflected
velocity, targeting angles, timer and particle paths remain; Ganondorf's output
pointer reloads after the angle callback. Physical double F1 velocity survives
callbacks, with explicit native fmuls operand rounding at the single-precision
products. Host regression covers69,636 health/search/lock/angle/callback cases
and wider-F1 controls. Actual reflected shots and boss phase transitions remain
unverified; Ganondorf hit sound and Molgera placement are still unimplemented.
### Sage dungeon re-entry (source implemented; native proof pending)

Complete Makar (`0221F700`) and Medli (`02286084`) Create consumers apply the
upstream seven-position reset tables. In dungeon mode, they always call restart
and priest setters, including when the saved priest flag is inactive. A signed
start-spawn match rewrites position, rotation and room; a miss preserves that
record and the captured rotation/room. The original position pointer survives
the injected GameInfo callback, and the save singleton reloads after restart
for the priest destination. No global sage predicate override is installed.

The earlier availability branch changes are folded into these consumers while
retaining genuine item/collection calls. The separate Fishmen scope is shielded
during nested sage creation. Constructors, stage/story checks, resource phases,
heap failures, initialization and companion publication remain in source.

The 14 static MIT upstream records match first, second, swordless and shuffled
entrance official outputs exactly. Source-seed preparation rejects missing or
altered tables instead of substituting constants. Host tests cover every signed
spawn ID, all priest flags, pointer/callback ordering and key Create phases;
strict complete PPC compilation passes. Real dungeon re-entry and native Create
comparison remain untested.

A fresh frozen aggregate through3945caadc reached active Link gameplay and exited
cleanly at frame1,802. The seed-specific save/state directories were selected;
four vanilla save/backup sentinel directories remained unchanged. This package
includes corrected Tower lighting, Moblin, KoRL, Dragon Roost, barrier and arrow
changes, but predates the new sage creation ports. It does not establish native
progression, individual new encounters, Windows behavior or a full playthrough.


Corrected Tower consumers passed a bounded paired-original-native comparison:
12,250 cases / 148,159 checks / zero failures, followed by a clean frame-3000 exit.
The first comparison caught missing cached smoke-lighting records; both records
were added with their native field widths and order and the corrected run repeated
all cases. The matrix compares the full private actor, glow, returned emitter,
save, play and Tower-global snapshots plus ordered callback arguments. It covers
all 256 servant arguments for event scheduling, finish and tablet timers, door
ownership combinations, unsigned room cases, post-player-room callback mutation,
pedestal positions and native movement across yaw/vertical-speed boundaries.
Only the upstream gate decisions are normalized in the original-native reference;
real event/switch/song ownership stays unchanged. Final save/play/global restores
pass. Actual native correction uses disabled private collision state with controlled
ground/plane providers; model, animation, collision registration, particle, sound
and restart endpoints are controlled. The fixture holds event processing active,
so the normal idle/player-action branch is not covered. Dungeon traversal, puzzles
and real geometry/effects remain unverified.


A bounded native Moblin check now covers both complete consumers during real
resource-backed creation in the sea stage: separate original/source processes
reach phase4, each performs the two creator-initialization update calls,
and each passes9 resource/state/sword-visibility checks. Full morf, model, save
and HIO snapshots match byte-for-byte; the full actor differs by7 bytes in four
random-associated timer/waypoint words. Aside from the three intended omitted
sword queries, the callback traces differ by one random-waypoint attempt inside
the unchanged native movement function. The runs did not capture/normalize RNG
state, so they establish neither exact whole-consumer parity nor the cause of
that difference. They preserve genuine owned-sword visibility; they do not test
actual swordless capture, natural paused-scheduler updates, ordinary combat,
mode1 lantern attachment, other platforms or a playthrough. No engine-resource
rollback or replay of the creator on a constructed actor was used.

The complete cc_at_check collision-hit consumer now applies Ganondorf's normal
hit-sound path when his health is nonpositive. The original dead marker remains;
only that actor bypasses the finishing sound/particles and critical classification,
using normal hit-pause adjustment instead. All native power/damage, hookshot,
stealing, spin damage, other actor sound/particle and final callback-return paths
remain. Host regression covers197,130 cases, including callback-mutated health,
particle position and actor return, every critical sound byte and physical double
speed thresholds. Actual native collision parity and boss gameplay are unverified;
Molgera's complete camera consumer and safe post-fight placement are described below.
Phantom Ganon's complete `move` consumer0213B3AC now ports the maze chest gate.
Only PG2/type1's standby query uses native stage8 chest0; PG3/type2 still checks
real Light Arrow ownership. Standby, emergence, disappearance, failure/sword
handoff, both flight variants, ball volleys and spinning attacks retain their
full inlined source paths. The other seven native action/damage consumers stay
in normal SDK dispatch, including the existing hammer-damage port. Camera and
low-player-height guards, collision displacement and knockback remain part of
the complete consumer. Host fixtures pass4,988 cases including chest/item
independence, all actor types, action/mode branches, callback mutations, collision
line lifetimes, FMA, NaN and physical sqrt/RNG precision. Strict full-module PPC
compilation and SDK translation pass with one replacement owner at0213B3AC.
Actual maze actors, chest progression, PG3 combat and native gameplay remain
unverified.
### Puppet Ganon final transition (source port; native proof pending)

`post_puppet_layer.h` replaces the complete CC0 `daBgn_Execute` consumer at
`0207E7B4`, including its inlined camera, rope and Keese-spawn paths. Its sole
intended gameplay change is MIT upstream `remove_cutscenes.asm`'s final
`GanonK` transition layer, from 9 to -1. Movement remains the original native
call, and the transition byte is checked after all preceding callbacks and
cleared before `setNextStage`. No global stage-call interception is used.

The native signed camera-mode byte makes public-source case 150 unreachable;
it is retained without converting it to -106. Native Keese scratch storage
writes X, Y, then X again and never initializes Z. The source preserves that
raw third word as untouched storage passed by address, without reading an
uninitialized C float. Its entry-stack contents are outside deterministic
validation. Host fixtures cover state/camera branches, argument contracts,
callback-induced transitions, timers and rope matrix capture. Strict PPC
compilation and direct-target checks are source evidence; they do not prove
a real Puppet Ganon fight or native cutscene playback.

Integrated verification through the complete Phantom move and Puppet Execute
consumers passes153 core host tests and strict whole-module PowerPC compilation.
SDK emission has208 hooks and172 unique replacement owners; every host import is
in the reviewed runtime-service set. This does not establish native maze combat,
Puppet cutscene playback or a full randomized playthrough.

### Hyrule enemy layer (source port; native proof pending)

`hyrule_enemy_layer.h` replaces complete CC0 `getLayerNo` (`025250C0`).
MIT upstream `make_game_nonlinear.asm` changes the matching `Hyrule` branch
at `025255F4` to return the existing day/night layer, skipping its shard and
story queries so castle enemies remain available. Other stages, room rules,
time/night-stop selection and explicit layer overrides retain their original
queries and order. The layer override still reloads GameInfo for its return.

The per-unit SafeString callbacks, literal addresses, pointer capture before
the final callback, bounded byte comparison and save-singleton reloads remain
explicit. Host matrix and callback tests plus strict PPC/SDK checks establish
source coverage; native original-versus-port comparison and real Hyrule visits
have not yet been tested for this consumer.

The remaining five NPC sail-dialogue jumps now use three complete source
consumers: Windfall townspeople getMsg, Zunari getMsg (composed with its existing
6940 reward-query context), and Fishman modeEventFirstWait. Each retains the first
real normal-sail78 query and its callbacks, then discards the decision and skips
the Swift-Sail77/fallback block. Night, photo/talk-item, endless-night, shop reward,
delivery-bag, Dragon Roost boss and ship-riding conditions remain live. The shared
People consumer keeps all other NPC cases and reads native message/pointer tables
in place, including table reads after callbacks. Native square/sqrt results stay
unrounded f64 through the People200-unit and Fishman radius comparisons. Host
checks cover all sail bytes, target/non-target NPCs, live tables, nested Zunari
contexts and callback mutations. Strict full PPC compilation/SDK emission pass;
all imported native targets exist and each consumer has one replacement owner.
Actual native NPC interaction and dialogue traversal remain unverified.

### Windfall townspeople one-time rewards (source complete; native proof pending)

Complete People reward, predicate, animation, interaction, phase, message-status,
creation and execution consumers now track Pompie/Vera with6904, Minenco6908,
Kamo6910 and Dampa6A80. The existing People dialogue owner composes these
queries with its sail changes; Lenzo's corresponding availability phase uses
6904. Other conditions, callbacks and native constructor/helper boundaries remain.

The packed-item producer passes only the low item byte to presentation, then
sets the upper event half even on factory failure, preserving the original
success-only partner update. Startup applies invariant table event halves,
validated against the supplied seed. Existing per-seed item-table bytes remain
authoritative; Dampa's custom seed byte028FA240 is extracted as a scalar reward.
Dampa records6A80 before assigning its first reward and retains five rupees on
repeat. No global chart-ownership override or caller-address interception is used.

Host fixtures cover all ten consumers, repeat/creation-failure timing, mutable
callbacks, parameter/day/stage gates, normal NPC paths, GHS dispatch, raw vector
copies and wide sqrt boundaries. Strict whole-module PPC compilation, SDK service
validation and unique replacement checks pass with actually generated seed
headers. Actual native paired comparisons and real quest visits remain pending;
this is not a complete randomizer playthrough claim.

Ivan's complete visit01 now retains its initial 1F80 query and all manual talk,
demo, movement, angle and event-name calls while skipping only the automatic
birthday-order branch. Existing visitTalkInit/visitSetEvent and reward consumers
remain their single owners. Marie's complete getMsg/wait01 use the actual 1F80
Killer Bees reward flag (not 1E04 or 3F80) for Joy Pendant eligibility, retaining
first-talk, night, pendant count, morph and gift behavior. Tingle's complete rescue
completion retains 0B80, event reset, delete and environment-fade order, and omits
only the gfx+243 menu flag store. Its timer uses native 32-bit wrap before the signed
comparison. Host tests cover all 65,536 Ivan angle values including EXTSH after
negation, manual branches and callback captures, independent Marie story flags,
all register-byte values, and Tingle callback-mutated save/play/environment pointers
and timer boundaries. The full 154-test host suite, strict whole PPC compilation
and SDK emission pass, with unique replacement owners and canonical native imports.
Actual native NPC/cutscene interactions remain unverified.

Molgera's complete camera consumer02101F30 now retains all16 sequence states,
entry context callbacks, event acquisition, camera transitions, item/warp creation,
ground-check lifetime, shake/report/audio tail and the existing nested boss-reward
factory scope. Only the upstream death-start target seam changes: after native
XYZ stores, targets within450 units (including an unordered distance) move to
XZ(0,777), leaving Y and callback mutations intact. Fallthrough into death tracking
continues even if the sqrt callback changes the actor mode. The native variadic
report ABI, physical double RNG/ground/sqrt results and callback-sensitive reloads
are retained. Host coverage passes1,907 complete-camera cases plus321,618 helper
cases; strict full-module PPC compilation and SDK translation pass with one
replacement owner. Actual Molgera fight/camera parity, post-fight warp progression,
Windows/EU and a full playthrough remain unverified.

Phantom Ganon's complete start02139AA0 now adds the upstream one-sided height
gate before the native XZ radius test. A player1,000 units or more above the home
position does not start the fight; the lower side stays unbounded, and unordered
height follows the original patch's rejection branch. All start modes, attention
clears, native animation/music/voice ordering, facing, movement and live register
reloads remain. Host coverage passes10,581 cases, including boundary/NaN heights,
physical double sqrt thresholds and callback mutations. Strict full PPC and SDK
translation pass; actual boss encounters and native gameplay remain unverified.
### Morth deletion counter (source port; native proof pending)

Complete `daKS_Delete` (`021A9EC8`) retains resource, ripple and fire cleanup.
MIT upstream's create-status guard decrements the total only for status2;
failed creation therefore cannot make the counter negative. The original zero
transition still calls GameInfo before clearing the three related counters.
Unsigned word arithmetic preserves wrap behavior. Host status/callback/counter
matrices and strict PPC checks pass; actual Morth deletion comparison is pending.

### Dampa pending pig text (source port; native proof pending)

Complete `checkPig` (`022C3E78`) retains the genuine minigame temporary-bit
query, then retries native speak-event ordering and returns without counting
when a pig's text is pending. Normal searches, busy flags, distance/height
conditions, relocation and temporary-register writes remain. Host callback,
geometry/NaN and pending-state matrices pass alongside strict PPC/SDK checks;
actual minigame/damage timing is not yet tested.

Arrow procMove02054040 now ports its complete moving consumer and clears the
on-hit callback only when a joint hit enters the stopped-on-actor procedure.
The clear occurs after the member-pointer delta store and before stopped-state
parameters, effects or matrix callbacks. Reflected light arrows, Puppet Ganon
hits, blocked rebounds, negative joint-query outcomes, background/water impacts,
all elemental effects and the distance/blur/model/collision tail retain native
flow. Inline model setters snapshot all12 matrix words; native magnitude/sqrt
and atan calls preserve their physical floating-point registers. Host coverage
passes668 cases across every hit/type/background branch, callback mutations,
precise distance thresholds and overlapping matrix-copy storage. Strict whole
PPC compilation, SDK translation, native-import/service audit and unique-owner
checks pass. Actual simultaneous-hit crash reproduction and native projectile
parity remain unverified.
The complete message tag dispatcher026005F8 now supports key-bag group7 commands
4B..4F for the five dungeon key counts. It reads the live stage memory inside the
matching dungeon and its saved stage record elsewhere, then uses native ASCII
number insertion and localized Unit_Key_00/01 labels. The selected stage pointer
survives number insertion; big-key ownership is read afterward so callback
mutations remain visible. Ordinary tag groups retain their complete native
argument dispatch and unknown commands still return-1. Host coverage passes
234,336 cases across all16-bit group/command values, live/saved counts, flags,
nested callbacks and32-bit length/index wrapping. Strict full PPC compilation
with -Wall -Wextra -Werror (no unused suppressions), SDK translation and unique
replacement ownership pass. Actual localized key-bag rendering and native text
buffer insertion remain unverified.

For seeds starting below eight quarter hearts, complete heart-alarm and resting
animation consumers now apply the low-health policy. The urgent two-quarter-heart
alarm remains; the next alarm tier checks maximum health. Resting animation
requires at least two missing quarter hearts after excluding unfinished heart
pieces. Demo eligibility resolves the real save allocation explicitly. Host
coverage includes 57,408 option/health/guard/countdown cases and callback mutation;
strict PowerPC and SDK checks pass with normal12 and synthetic4 starting health.
Actual low-health-seed audio and animation remain unverified.
### Outset scene selection and unload sub-BGM

The complete public CC0 `JAIZelBasic::setScene` consumer (0202677C) also
implements the randomizer's Outset policy: it omits the introductory 3510
prerequisite and replaces the kidnapping predicate with persistent stage-13,
chest-5 state. This deliberately reads the unloaded save-memory block even
when stage 13 is active. The preceding story queries, strict native boolean
comparisons, layer suppression and scene-change callbacks retain their order.
The same persistent-chest predicate is used by the complete `startIsleBgm`
(0202389C) and `zeldaGFrameWork` (02024478) source consumers in
`outset_music_frame.h`, covering all four upstream Outset BGM query sites.

`stage_unload_bgm.h` ports the complete public CC0 `dScnPly_Delete`
(025B0630, `d_s_play_7.cpp`, decomp a874e1a9). It stops sub-BGM immediately
after preload-resource deletion and before the original final play getter and
initialization, as specified by upstream `fix_vanilla_bugs.asm`. Display
readiness, all destructors and return-value forwarding, short-circuit boss-stage
checks, signed HIO/preload indices, live global reloads, renderer cleanup and
all four scene-mode paths remain in the original order. Its SafeString helper
preserves the native literal capture for pointer identity and subsequent
reload for byte comparison, including mutation by the third assurance call.

Focused host checks cover 127,900 complete scene selections, 1,440 complete
unload callback sequences, resource-array mutation, changed save allocations,
stop-callback play-allocation mutation, finalize-callback keep-state mutation,
early-return nonmutation and SafeString callback mutation. Strict whole-module
PPC compilation and SDK translation pass. These are source/host checks;
paired actual-native verification and real stage-transition audio checks are
still pending.

### Poe light-arrow/Jalhalla cleanup (source port; native proof pending)

Complete `Big_pow_down_check` (`0244A67C`) accepts ordinary or light-arrow
Poe deaths while retaining processed/id/search/type gates and Jalhalla's health
and last-Poe bookkeeping. Its last-four-frame reform path restores health and
cancels the light-arrow timer. Host signed-health/counter/reform matrices and
strict PPC/SDK checks pass; actual Jalhalla combat is not yet verified.

### Miniblin light-arrow death switch (source port; native proof pending)

Complete `daPt_Create` (`02446E54`) retains its original creation phases,
resources, parameter decoding, heap/HIO, collision and enemy-effect setup.
After the attack sphere is set, a non-respawning actor copies its live death
switch into its enemy-ice metadata; respawning actors keep existing metadata.

This deliberately corrects an upstream ABI mistake: its helper treats r29 as
the actor, but the actual caller holds actor+644 there. The source uses actor
3D0/3D4/AAD and leaves the erroneous A14/A18/10F1 locations untouched. Host
regressions cover those locations and callback timing; strict PPC/SDK checks
pass. Native combat and actual room-switch verification remain pending.

Integrated checks through the stage-unload, Poe and Miniblin ports pass179 host
tests and strict whole-module PowerPC compilation. SDK translation has237 hooks
and199 unique replacements, with only reviewed host services. The Miniblin
consumer deliberately uses the real actor base instead of the upstream helper
register mistake. Actual combat and transition audio remain unverified.
The complete audio-frame port preserves timer saturation/decrement order,
sub-BGM recovery and countdowns, scene/camera ambience, queued scene music,
sound-handle reloads and its final dispatch through the native heart-sound
entry (so the separate low-health policy still composes). It retains the
island room across the substituted chest query and the original strict
`== 1` test. Camera reads preserve the original zero-address behavior rather
than introducing a fallback. PPC `fmuls`/`fmadds` helpers preserve explicit
single rounding; the two native square-root results remain double precision
through threshold comparisons, with the original subsequent single-precision
subtraction/division sequence. Host tests exercise 59,935 complete frame and
island selections, including all room bytes, queue states, sub-BGM IDs and
subtypes, all timer bytes, every scene, special islands, finite/nonfinite
camera positions, callback mutation, full unrelated actor-byte preservation
and unchanged save/play memory. Actual-native frame/audio verification is
still pending; the host coverage does not establish audible stage behavior.

The first FF warp UI source stage covers complete resource lookup, opening,
warp-area initialization and confirmation-text consumers. A validated fixed
quadrant9 value0 is supplied through the full resource getter, all10 destinations
are searched/opened, all10 child areas are initialized, and the FF confirmation
uses localized label00076. Ordinary labels, missing-child fallback and callback
reloads remain native. Host fixtures pass66,891 cases plus fixed-data rejection
checks; strict PPC/SDK translation and unique replacement ownership pass.
This is not the complete FF UI: the static coordinate initializer, extra pane
factory and navigation connections still require source integration, followed
by actual native UI/navigation testing.

Integrated validation through the complete Outset music consumers and first FF
warp-menu stage passes184 host tests, strict whole-module PowerPC compilation,
and SDK translation with246 hooks/208 unique replacements. Official second-seed
preparation validates the fixed FF quadrant. Warp constructor/factory/connections
are still incomplete, and actual native menu/audio behavior remains unverified.

### Independent D-pad item icons

`dpad_shortcuts.h` ports four complete public CC0 consumers from
`hd_screen_item_shortcut.cpp` at a874e1a9: kind selection (026DD56C), appearance
(026DD6C4), change entry (026DD894) and construction (026DDA04). They implement
upstream `fix_vanilla_bugs.asm`'s independent Wind Waker/grapple/bomb bits
(1/2/4) and invalid sentinel 8. Grapple and bombs remain ship-only; owning bombs
no longer makes an unowned grapple appear. The original native state machine,
Miiverse indicator, animation helpers and bomb-number handling remain active.
Each animation callback retains the original subsequent field reloads.
Construction preserves allocation failure, base-constructor ordering, vtables,
padding and all unrelated fields. A 71,938-case host matrix checks every
inventory byte, independent item/status combinations, every low-byte old/new
flag transition, appearance callbacks, allocation and full unrelated actor/save
bytes. Strict PPC compilation and SDK ownership/import checks pass.

Upstream explicitly does not fix the layout-animation case without the Wind
Waker; this port preserves that limitation. These source/host checks do not
establish visual correctness of those animations or actual GamePad rendering;
paired native/visual verification is pending.
The second FF stage adds the complete navigation and child-factory consumers.
Both grid clearing passes remain, with10 warp cells and the exact conditional
M&C/FF diagonal links. The child factory scans the original10 names followed by
L_WarpArea_09 at index10, retaining bounded SafeString comparison callbacks,
allocation/constructor failures, captured allocation slots and fresh setup-slot
reads. Host fixtures pass1,026 graph cases (all1,024 presence masks) and1,887
factory cases, including callback mutations and long-string bounds. Strict full
PPC/SDK and unique ownership checks pass. The full static coordinate initializer
and actual native UI/navigation testing are still pending.

Combined validation through independent item shortcuts and FF navigation/child
creation passes188 host tests, strict whole-module PowerPC compilation and SDK
translation with253 hooks/215 unique replacements. The final FF static
initializer is still pending integration. Native UI/rendering remains unverified.
### Seed-selected casual clothes and reflection

The complete public CC0 `J3DModelPacketS::draw` consumer (02082ED0,
`d_a_bgn.cpp`, a874e1a9) uses the validated seed's
`init_should_start_with_heros_clothes` for the visible-ear exclusion. It keeps
native context checks, renderer-state construction/application, joint and mesh
traversal, Buffer first-entry fallbacks, hidden eye/eyebrow handling, additional
material/line packets and final renderer-state restoration. Save ownership and
Second Quest flags are not changed. The SafeString comparison preserves the
native capture before the final callback and reload for fallback. Its 1,817-case
host matrix covers outfit/face/visibility/name combinations, table bounds,
nullable packet components, all unrelated memory and callback mutation. Strict
PPC compilation and canonical SDK ownership/import checks pass.

The final FF source stage adds the complete sea-menu static initializer, retaining
all354 SafeString fields and five registration/object callbacks. The new FF
coordinate(-3,-3) is stored exactly between the original final coordinate's X/Y
stores.64 host fixtures check every name assignment against the pinned public
source digest, exact bit-preserving bounds, callback timing and all10 coordinates.
All9 upstream FF sites now have complete source consumers (plus the full resource
getter for validated fixed data). The actual official seed's English, French and
Spanish assets were privately checked for10 warp panes, the FF pane's position
and label00076. Final full-module strict PPC/SDK and ownership checks pass.
This establishes source/host/asset coverage only: live native pane construction,
animation, navigation, confirmation and warp traversal remain unverified.

A fresh frozen package throughbcfd40004 boots into active Link gameplay and
exits cleanly at frame1,800. Its196608-byte module allocation and seed-specific
save/state paths work; four vanilla save/backup sentinels stay unchanged. This
run includes the complete FF source group and partial casual reflection, but
does not establish live warp-menu interaction, native combat, visible outfits,
Windows behavior or a full randomized playthrough.
### Stalfos light-arrow split-body cleanup (source port; native proof pending)

Complete `daSt_Execute` (`02493FB0`) preserves its death, ice, movement,
inline damage/collision, part/weapon, emitter and spin-smoke paths. At the
original upper-body lookup, a live upper light-arrow counter starts an idle
lower-body counter without changing a running counter or the returned actor.
The existing upper death and carried-weapon cleanup still follows the lookup.

Host fixtures cover the byte counter matrix and representative inline paths;
strict PPC/SDK checks pass. Random results retain double precision until native
integer conversion, and raw model/vector copies preserve bit patterns. No
paired native Execute or actual split-Stalfos combat has been verified yet.

The corrected Stalfos integration passes191 combined host tests; its model scale
loads use330/334/338 and preserve neighboring shape-angle data. Orca's complete
battle consumer additionally covers every equipped-sword and battle-mode byte,
plus direct/virtual member dispatch and callback/conversion boundaries. No-sword
initialization omits only counter creation; real swords select the appropriate
icon. Strict whole PowerPC/SDK checks pass with257 hooks/219 unique replacements.
Actual native split-Stalfos combat and Orca training/counter rendering are pending.

The complete item animation entry now handles the imported rainbow resource
marker7, advancing the signed frame by0.15 and cycling6→0→6 through its absolute
value. Ordinary constant/live BRK and all other animation callbacks retain their
order and model reloads. Host coverage passes65,536 marker/mask/frame cases plus
callback mutation; strict whole PPC/SDK checks pass with258 hooks/220 unique
replacements. Actual native rainbow-rupee rendering remains unverified.
`player_clothes_init.h` also ports the complete public `playerInit` body
(02410BE8, `d_a_player_main_04b.cpp`). The existing `entrance_spawn.h` remains
the sole replacement owner and invokes this source body inside its nested
actor/resource-phase context. There is no production call back to the original
playerInit. Both late resource return hooks still execute through native SDK
calls. The late clothing decision reads the validated seed option, omits the
intro-event and Second Quest predicates, and preserves an already-set casual
actor flag exactly as upstream does.

The source retains model/joint callbacks and material classification,
hands-model hiding/matrix dirtying, animation heaps, collision setup, item
heaps, sound reset, sight resources, start-demo selection/light setup, all nine
texture-header words, relative offsets, bounded original-header storage,
PowerPC dirty-mask shift behavior, hero-texture restoration and final state
cleanup. A 7,168-case complete host fixture exercises both outfits, existing
actor outfit flags, all start and saved modes with the actual spawn wrapper
active/inactive, signed camera IDs, texture counts including Buffer overflow
and the 64-bit mask boundary, exact callback ordering, unrelated actor-byte
preservation and unchanged save/play bytes except the specified spawn mode.
Strict full PPC and SDK checks confirm one playerInit owner and canonical
native endpoints. These source/host checks do not establish native rendering,
resource-backed boot compatibility or the visible outfit; those checks remain
pending.

Traversal speed stage1 ports the six complete crawl, ladder-speed, push/pull,
ladder-up-end and climb-up-start consumers. Crawl uses6, push/pull and ladder
finish use1.4, ladder speed adds1 to the stick, and the climb-start relocation
changes both rate and morph to1.6. Existing runtime traversal hooks remain
outermost; their optional settings are not changed. Fixed metadata validation
checks10 relocation addends, three addi fieldsets, all target floats and13 complete
block rows, allowing only the26 duration halfwords to become12. A startup observer
applies those halfwords and the reviewed grapple float25 without touching other
row bytes.67,614 focused cases plus metadata mutation/startup/prepare checks and
strict full PPC/SDK checks pass. Stage2 adds complete hang-wall-catch and
sidle-blend consumers, retaining ground/animation callbacks, late state reloads
and native fused arithmetic/physical-FPR precision; only hang rate1.6 and upright
sidle rate2 change.134,520 focused cases and strict full PPC/SDK checks pass.
Stage3 adds the complete conversation camera02508A60 with all shot styles,
event/actor selection, collision searches and weighted transition tail. Its
initial transition is10 frames instead of20.65,810 host cases and strict full
PPC/SDK checks pass; all38 native call targets remain. Stage4 adds the complete
rope0216F7FC, including every motion state, boss-demo state and shared stick/camera
tail. Launch delay is REG+10, coil maximum step is f27+20, and the coil counter
increments by the truncated ratio+6; its scale multiplier reads the live patched
global25.68,487 controlled cases and strict full PPC/SDK checks pass, retaining
all43 native call targets and both virtual player callbacks. These checks cover
source control flow and ABI, not native traversal/camera gameplay.

The optional SDK world-object grapple proxy also claims0216F7FC. Its existing
ownership checks reject a competing owner; this source package does not declare
world-object grapple points. Composition with a separate world-grapple package
and native traversal/camera gameplay validation remain pending.

The integrated complete outfit initialization and first traversal stage pass197
host tests, strict full PowerPC compilation and SDK checks with265 hooks/226
unique replacements. A fresh frozen package through38fbb91af reached active Link
gameplay and exited0 at frame1,801 with the full source-owned player initializer.
The seed selects Hero's Clothes; casual outfit rendering remains unverified.
Its262144-byte module allocation works, seed save/state paths are selected and
four vanilla save/backup sentinels stay unchanged. This is a bounded startup
check, not full traversal, native combat, Windows or playthrough validation.

### Fast chest opening

`fast_chests.h` ports the complete public CC0 `actionOpenWait` consumer
(024B7394, `d_a_tbox.cpp`, a874e1a9). As in upstream `remove_cutscenes.asm`,
all chest shapes skip buildup music and light setup and request the short
`DEFAULT_TREASURE_A` event. Both original shape-query calls still run; item
creation, failure handling, event flags, live play-allocation reloads, the
8-byte GHS action PMF, staff lookup and demo processing retain their order.

The three nonwooden opening-animation IDs are initialized to 9 before actors
can cache them. Seed preparation validates the complete 48-byte model table
against canonical data plus exactly those three halfword changes; unrelated
model, texture-animation and collision IDs cannot be silently accepted. The
original table has IDs 8/8/8; all five retained official seed fixtures (first,
second, swordless, chart-off and shuffled-entrance) validate as 9/9/9 with every
other table byte unchanged. No RPX executable code is copied.

Focused host verification covers 72,704 complete command/item/callback cases,
including all 16-bit commands, all item bytes, invalid demo-item IDs, callback
allocation/condition mutation, unrelated actor/play bytes and idempotent table
initialization. Every single-bit alteration of the validated table and short
reads are rejected. Strict full PPC/SDK checks pass. Paired native chest
opening, storage-duplication behavior and visible animation checks remain
pending; host tests do not establish those gameplay results.

### Inter-dungeon warp-pot smoke (source port; native proof pending)

Complete `createInit` (`023BA4E0`) gives event indices2/5 the upstream bright-red
primary and dark-purple outline smoke; other indices use the original default
color tables. It preserves model/cull setup, live background pointers across
registration callbacks, lid mode, collision, physics, position/cooldown and
matrix initialization. Preparation rejects custom colors or default tables that
differ from the reviewed upstream policy; there is no silent fallback.

All four audited official outputs share that policy. Host initialization
matrices and pointer-changing callbacks, strict PPC/SDK checks and policy
negative tests pass. Actual smoke rendering and warp-pot gameplay remain
unverified.

### Blue/yellow ChuChu hookshot shock (source port; native proof pending)

Complete `body_atari_check` (`0210CE9C`) preserves native collision, hit-actor,
stealing, sword/wind/elemental damage, sound and particle branches. Its hookshot
reset retains the cylinder's live attack-collision bits, matching the electric
particles. Other reset fields and the already-damaged/action3 path retain their
native behavior. The preceding attack-bit read remains; only its clear store is
omitted, matching the upstream patch.

Host full-consumer matrices include all colors, actions/states and attack/cut
families, callback changes to the attack bits, native room/status contracts and
raw hit-position copies. Strict PPC/SDK checks pass. Actual hookshot contact and
shock damage in gameplay have not yet been verified.

### Magtail light-arrow respawn cleanup (source port; native proof pending)

Complete `daMt_Execute` (`021D87A0`) preserves lava and appearance gates,
movement/fight/body helpers, damage, background checks, material/joint updates
and particles. A head-sphere light-arrow hit now also sets `mDeleteRequested`
(`1DD4`), retaining the native shrink-counter write and existing body-hit path.

The short-angle Add aggregate return is expanded from public source, retaining
its native constructor, returned pointer and capture-before-write semantics.
Its original Add entry observer is not reproduced. Native random and square-root
results retain double precision until their original single operations/stores.
Host complete-consumer/helper cases and strict PPC/SDK checks pass. Actual
Magtail fighting, native paired Execute and room respawn remain unverified.

### Warp/song visual-speed consumers

`warp_song_npcs.h` ports complete public CC0 Tott `demoInitPatten`
(022EFAD8, `d_a_npc_tt.cpp`) and Makar `init` (0221F3C0,
`d_a_npc_cb1.cpp`) at a874e1a9. Upstream `remove_cutscenes.asm` changes only
Tott's missing-script-count default from two dances to one, and Makar's defeated
boss variant from a boss-flag query to the wait action. Explicit Tott counts
retain their native byte narrowing. Dance initialization, late staff capture,
resource queries, all other Makar variants, event indices/callbacks, loaded PMFs,
ship checks, event/parameter reloads and collision setup retain their order.
Existing sage-reentry native calls compose with Makar's new single owner.

The 2,434-case host fixture checks count/null behavior, signed parameters,
independent event and ship answers, PMF words, callback mutation of flags and
parameters, exact callback order, unrelated actor bytes and unchanged saves.
Strict full PPC and canonical SDK ownership checks pass. The Link song and ship warp pairs below complete this source group; actual-native
NPC initialization/visible choreography has not been verified by these host tests.


`warp_song_ship.h` additionally ports complete ship `procTactWarp_init`
(0247DBF8) and `procTactWarp` (02482438), using the same pinned public source.
It removes only the upstream warp sound-start call and altitude branch. The
original reverb query preceding that sound call still executes. Ship/camera
state, particles, creation/error handling, acceleration, tornado scaling and
matrix tracking, emitters, scene/mute callbacks and final angle reloads remain
in order. The preceding altitude arithmetic/comparison is retained even though
its result no longer gates the warp. Initialization keeps the 8-byte GHS PMF
and trailing padding. The source retains both per-particle play getters and
all twelve particle-call arguments.

A 7,494-case host fixture covers independent actor lookup/creation/chase states,
phases, below-threshold and nonfinite positions, cancellation, callback mutation,
particle arguments, matrix layout/dirtying and unrelated actor/play bytes.
The native unordered comparison rules are explicit: NaN scale permits
acceleration, NaN speed clamps to 50, and modeled/fallback tornado-joint
comparisons retain their distinct unordered behavior; signed zero is covered.
Strict whole-module PPC and canonical SDK checks pass. Its resource, chase,
camera and particle endpoints are controlled host callbacks; actual-native
ship warp and visible timing remain pending.

### Aryll pirate outfit and wait state (source port; native proof pending)

Complete `bodyCreateHeap` (`0227E3BC`) always installs Aryll's pirate-body
texture. It retains the original conditional type/clear-count reads, material
SafeString callbacks and allocation/constructor results, model failure/deletion,
texture-name iteration, forward image copy/offset relocation/change masks and
joint callbacks. Complete `wait_action1` (`02283038`) still queries the real2A80
story flag on initialization, then selects state4 unconditionally. Subsequent
wait/talk modes remain native; no second-quest or story flags are altered.

Host heap/wait matrices cover callback changes, partial allocation/failure,
texture-mask shift boundaries, copy overlap and signed state/joint indices.
Strict PPC/SDK checks pass. Actual Aryll rendering/dialogue remains unverified.

Seed preparation validates classic mode across all seven complete function
bodies and selects their source consumers together. Unknown or mixed variants
are rejected before output. Native classic-mode validation remains pending.
Music removal is decoded and source-implemented separately below.


`warp_song_link.h` completes the group with full Link `procTactPlay_init`
(02439D54) and `procTactPlay` (0243AC64) consumers from the same pinned public
source. It skips the initial song textbox and enables the existing replay-skip
path for first-use songs. The patched learned-song byte/mask reads still execute;
all other ship, partner, manual-event, message, timer and usability gates remain.
It preserves stage virtual dispatch, tornado creation, item-follow emitter,
message/system pointer captures and reloads, signed halfword/full-word fields,
return-value propagation and callback order. The native frame-rate BGE's
unordered case is explicit: NaN, like an ordered low rate, enters the completion
path. No broad song/item/event flag override is used.

A 22,353-case complete host fixture covers signed song IDs, argument narrowing,
initialization callback mutation, all stage types, independent ordinary/manual/
partner/message gates, learned/unlearned states, signed-zero and nonfinite rates,
exact callback traces, late actor/song/global pointer reloads, captured staff and
manager arguments, untouched actor/play bytes and save preservation. Strict
PPC/SDK checks confirm all six new consumers present once with canonical native
imports. Actual-native song playback, visual timing and a full randomized
playthrough remain pending. Optional classic Wind Waker dives belong to the
separate original `procTactWait` (0243A094), which this group does not replace.

The integrated package through `cb2b8f695` loads 334 translated functions and
281 hooks, completes the opening seam, reaches active Link gameplay and exits
normally at frame 1,801 in the isolated muted runtime. Its 262144-byte module
allocation works and all four vanilla-save sentinels remain unchanged. This
boot check includes the integrated enemy, chest, outfit, conversation-camera
and six warp/song source consumers; it does not exercise their individual
encounters, outfit appearance, dialogue or warp timing. Those checks and a
full randomized playthrough remain pending.

### Instant-text seed option (source port; native proof pending)

A coherent decoder checks all11 semantic patch sites against the validated
original image and emits `init_instant_text_boxes`. It supports enabled and
disabled variants and rejects mixed, short or unknown instructions; no original
instruction arrays or executable patches are shipped. Complete message-window,
selection, scroll and key-icon handlers preserve native callbacks and both
branches. Only the exact nine patched loads use held input when enabled; later
selection reads still use edge input. The two removed animation gates retain
their native animation predicates and the other key-icon branches.

The upstream MSBT transformations (instant draw and selected wait-tag removal)
come from the player's randomized language packs through the existing complete
content overlay. This differs from holding A or changing game speed. Host
handler/decoder matrices and strict PPC/SDK checks pass. All four audited
outputs enable the option; no official disabled seed or actual dialogue flow
has been tested yet.


### Optional music removal

`seed_music_policy.py` decodes all six upstream `remove_music.asm` functions by
complete reviewed function hashes (140–4,988 bytes each). All six must agree on
enabled or disabled; mixed, unknown and truncated bodies fail before preparing
output. The decoder contains hashes and canonical boundaries, with no original
instruction bytes or tiny recoverable hashes. Normal randomized-executable
compatibility remains unchanged.

Prepared startup data now includes a mandatory validated music flag and
`WWHD_RANDOMIZER_REMOVE_MUSIC` compile-time scalar. Disabled seeds register no
music replacements at all. Enabled seeds register exactly the four upstream
void early returns, `prepareSoundByLabel` (02897F48) returning 2, and
`streamIsPlaying` (02030F1C) returning 1 so readiness cannot stall progression.
These complete early-return consumers read or write no game state; unrelated
sound effects and native code remain outside this policy. Old prepared headers
without this flag must be regenerated rather than assuming a default.

Host checks cover both coherent states, every mixed six-function combination,
full-body mutation/short reads, preparation atomicity, metadata domains, zero/six
registration and all return-value input words in the 16-bit domain. All five
retained official seed fixtures decode as disabled, and actual second-seed
preparation passes. Strict whole-module PPC/SDK checks verify zero music owners
when disabled and exactly six when enabled, with no duplicate owners; emitted
PPC for each enabled consumer is precisely an immediate return or constant
return. The enabled hash/compile fixture is an in-memory derivation of the
reviewed upstream policy, not an officially generated enabled seed or a native
audio test. Real enabled-seed boot, credits progression and audible behavior
remain unverified.

An additional output generated by the pinned official randomizer with music
removal enabled passes source preparation, strict whole-module PPC compilation
and SDK translation. Its decoded flag is one and all six music replacements
are present exactly once (299 total hooks, 259 unique replacements through
`1f4b23e4c`). This replaces the earlier derived-only enabled-option coverage;
real audio behavior, credits progression and enabled-seed gameplay remain
unverified.

The shieldless hammer button consumer also composes the port's quick-use
bookkeeping immediately before eligible hookshot, rope and boomerang native
actions. A ready-item callback can change the live equipment after the initial
hammer guard; these branches retain their original eligibility and callback
order. `wwhd_item_action_dispatch` runs the existing runtime observer on a CPU
copy and accepts only those three item IDs. Host callback-mutation and native/
shifted-address runtime fixtures cover this seam; no new in-game playthrough
is claimed.

A private translated guest-module probe verifies both new item SDK services
against the rebuilt runtime, with quick-use disabled and enabled. Native
trigger pass-through, unsupported-dispatch rejection and guest stack canaries
pass; both bounded boot runs exit normally. This proves service resolution and
calling convention in the loaded module. Accepted live item actions remain
covered by the native/shifted synthetic fixtures, not by this idle boot probe.

The optional inverted sea compass now has the complete update0264C5A0 consumer.
Its decoder recognizes only the two reviewed212-byte whole-function fingerprints;
unknown variants fail before headers are emitted. A mandatory binary seed macro
selects the owner at compile time: disabled seeds emit no replacement or imports,
so their original consumer remains literal. Enabled seeds retain actor lookup,
late pane selection, opaque X/Y rotation words and dirty flags; only heading
negation is removed.262,402 controlled cases, whole-function mutation tests,
preparation/package regressions and strict PPC/SDK checks for both variants pass.
No runtime entry/instruction hook owns this address; map_hud's adjacent compass
destructor hook is unaffected. Native inverted-compass display is unverified.

### Optional particle performance tuning

`seed_particle_policy.py` validates the complete 1,016-byte original-USA
`createCommon` function (025A7838), accepting only the reviewed OFF body or the
MIT `performance.asm` variant. Unknown or truncated bodies fail before output;
only whole-function hashes are retained. Prepared data must contain the decoded
`WWHD_RANDOMIZER_PARTICLE_PERFORMANCE` flag; older headers require regeneration.
Classic mode, fixed Helmaroc RNG, quiet swift sail and inverted compass remain
unsupported and fail closed. The music policy remains independent.

OFF registers no particle replacement. ON registers the complete public CC0
`createCommon` consumer and changes only its emitter-manager maximum particles
from 3,000 to 1,500. Allocation size 0x580BB8, emitter limit 150, group limit 200,
texture/resource setup, reports, assertions and context restoration are retained.
The USA OSReport import is called through the SDK's ordinary game dispatch, not
its native-original entry API. No new SDK service or instruction patch is used.

Host regression covers 256 combinations of allocation/constructor failure,
unsigned renderer counts, optional texture loading and context restoration, with
callbacks mutating heaps, resource-controller pointers, registered manager and
simple IDs. Assertions that return still continue setup. OFF/ON strict PPC and
SDK checks verify zero/one owners. Five retained official fixtures decode OFF;
actual second-seed preparation succeeds. A separately generated official output
with all six optional policies enabled also prepares and passes strict PPC/SDK
checks. Enabled-seed boot, visual load and performance remain unverified.

Quiet Swift Sail is decoded from either reviewed complete procSteerMove hash.
OFF emits no replacement or sailing-service imports; ON replaces the complete
CC0 consumer and omits only the three upstream sail-transition sound calls.
The normal-sail automatic-wind and configured sail-speed settings compose through
semantic SDK services, sampling the live Swift flag at the original scaling
point. Restoration uses a local original/written bit comparison before jump
checks, so an obstacle target remains intact. Preparation rejects partial or
unrelated edits; only the source-core data-input profile admits the reviewed ON
body. Four official fixture seeds are OFF; an official combined-option output
now decodes ON and passes strict PPC/SDK checks. Native sailing/audio gameplay
remains unverified.

### Staged classic NPC and pause consumers

`classic_abe.h` and `classic_pause.h` provide three complete public CC0 consumers
behind mandatory binary `WWHD_RANDOMIZER_CLASSIC_MODE` metadata. They are staged
without a `mod.c` include while the complete classic family and decoder are
integrated. Missing/invalid metadata fails closed; OFF emits no descriptors.
No existing optional-policy guard is relaxed by these headers.

Abe's complete `createInit` (022F7D68) omits only the MIT upstream coordinate
relocation, preserving initialization dispatch, both cylinders, transforms,
collision/background queries and cached morph scalar. The two in-game UI owners
(0270D70C/0270F01C) remove only the later `mMenuAllowed` branch and available-song
branch. Their getters/volatile loads remain. Initial game-over handling, demo
and mode gates, event priority, indirect song query and sound/guide callbacks
remain intact. No global ownership or input spoof is used.

Focused synthetic host tests cover 2,097,152 play-event/gate/busy combinations,
73,728 takt-event/song/mode cases, and 192 Abe initialization outcomes with
callback-mutated staff and angles. Strict full-module PPC/SDK checks of private
staged aggregates verify zero/three extra owners, unique descriptors and native
call targets. These are source/host/compiler checks; no classic-mode game run or
trick execution is claimed.


The optional `fix_rng` policy is decoded over the complete9,116-byte Helmaroc
move consumer0206D644. Only the reviewed original and coherent three-change
whole-function hashes are accepted; mixed or unknown variants fail before
output. A mandatory binary seed flag selects compilation. Disabled seeds emit
no owner or imports for this consumer and retain its literal native body.

The enabled source consumer retains all sixteen action selectors, every native
action callback, the inline flight/landing/wait/jump/peck/stomp logic, particles,
wall queries, animation continuation and RNG calls. It changes only the three
upstream branch choices. The earlier wind, HIO, health and distance gates still
apply; other random choices remain random.1,953 controlled cases cover action
and state matrices, callbacks, RNG short circuits, float boundaries, opaque
word copies and overflow, with strict whole-module PPC/SDK checks for both
variants. All38 native targets remain and both debug reports use the native
variadic ABI. Actual default-seed preparation passes. An official output with
classic mode, fixed RNG, particle tuning, compass inversion, music removal and
quiet sailing enabled together also prepares and compiles successfully. Native
Helmaroc fights and cross-option gameplay remain unverified.


### Integrated classic-mode family

The earlier staged consumers are now included by `mod.c`. Preparation decodes
all seven complete classic-mode functions coherently and emits mandatory binary
`WWHD_RANDOMIZER_CLASSIC_MODE` metadata. OFF retains the native bodies for six
optional replacements; the existing pig owner retains its ordinary caps. ON adds
those six replacements and removes the pig caps within its existing owner.
The temporary instruction-site rejection guard has been removed; complete-body
validation replaces it. All 126 mixed family combinations fail before creating
an output directory. Unknown and truncated bodies also fail closed.

The complete TactWait consumer preserves real cancellation, beat and animation
callbacks, including the unrounded first-beat double and unsigned phase math;
only the upstream falling-cancel branch is omitted. Host tests cover countdown,
ground, cancel, completion, judge, ownership and phase paths. These checks do not
establish native storage glitches, loading-zone behavior or musical input timing.

The Helmaroc unordered-comparison regression also uses the runtime's actual
`cr_set_f` helper over169 operand pairs. BGE skips on !LT and BLE on !GT,
including unordered inputs; positive BLT/BGT and negated-skip source paths
therefore agree. Explicit NaN movement, wait, animation-frame, peck, stomp,
landing and jump cases preserve those outcomes. No production predicate was
changed by this check.


### Fixed title and Windfall animation data

Preparation validates all 14 Aryll animation entries against the original,
allowing only entry 4's BCK index to become 5. An early hook changes that byte,
preserving texture animation, morph, speed and loop data. The same early phase
sets the three MIT title sparkle values after validating their exact seed data.
The native title constructor runs later and copies the updated vertical constant;
the scale getters read the updated globals. These RPX data changes cannot come
from the content overlay alone. Host preservation tests, malformed-input
rejection before output, official second-seed preparation and strict PPC/SDK
compilation pass. Native Aryll interaction and title appearance remain unverified.

### Permanent-resource heap allocation

The port still honors guest expanded-heap sizes: host RAM does not automatically
expand a `MEMCreateExpHeapEx` range. Upstream increases the data/PermanentResource
heap requested by `HdRootTask::prepare` (0203EE2C) by 0x50000 bytes. The integrated
complete CC0 consumer in `permanent_resource_heap.h` changes only that request
from heap overhead + 0x0D400000 to overhead + 0x0D450000. Other budgets, parent
heaps, archive and shader-range setup, callbacks, guards and child-task arguments
remain native. WPAD/account calls use the verified USA import slots to which the canonical
compiler folds their non-dispatchable import thunks.

`permanent_resource_policy.py` validates the complete 2,116-byte reviewed seed
function using a whole-function hash; unknown or truncated code is rejected.
It stores no original instructions or small recoverable hashes. Preparation
validates this mandatory policy before producing output; `mod.c` includes the
complete consumer and the release bundles both public source inputs.

Focused host tests cover all eight heap requests and exact callback order in 64
combinations: zero/nonzero relative offsets, null child tasks, existing config
and type guards, callback-mutated manager pointers and save pointer, and the font
resource captured across a virtual heap-adjust callback. Five retained official
seed fixtures validate. A private aggregate passes strict PPC/SDK checks with a
single new owner, native call targets and those four verified import slots. No heap-pressure or native startup run
is claimed by this source/host/compiler verification.

`test_permanent_resource_translated.py` additionally executes the actual compiled
PPC heap consumer through the SDK translator and `WWHDGuestHostV1` module ABI.
It loads a focused module into synthetic big-endian guest memory with a real
32-bit guest stack. Host dispatch mocks reuse the independent callback
expectations above, including virtual endpoints and legal caller-save register
clobbers. All 128 cases (64 each at SP modulo 16 of zero and eight) verify the eight
heap budgets, callback-mutated state, stack canaries, SP/LR and nonvolatile GPR
preservation. A second synthetic entry checks twelve integer parameters,
including stack arguments 9–12 and a by-reference write into the original
caller stack. No RPX, game assets,
renderer, engine allocation or real startup is involved. This check closes the
host fixture's `uintptr_t` limitation without adding production SDK behavior.

The translated fixture now compiles with `-mstackrealign`. Native GHS callers
only guarantee eight-byte stack alignment, whereas Clang's PowerPC EABI can
assume sixteen bytes. Without realignment, optimized address construction may
produce incorrect pointers even when argument registers are correct. The
flagless eight-mod-sixteen entry reproduces the first archive-load failure;
`-mstack-alignment=8` alone does not fix it. Every SDK/core PPC compilation must
use realignment before translating the resulting ELF. Translation cannot
repair an ELF already compiled with incompatible stack assumptions. No runtime
stack copying or heap-source alignment workaround is introduced here.
