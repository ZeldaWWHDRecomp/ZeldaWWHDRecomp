# Camera-directed usable items — research and partial implementation

Tracking: [#197](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/issues/197). Submission base: `origin/devel`, `4dc4832bc68046076169f5daec34315b78aae2f0`; branch `feat/camera-directed-items`. The original prototype/native human-playtest base was `d23522eb1eb0fd676437a685016cf081f5ecf285`; those historical observations are distinguished below.

## Honest scope

This is a **default-off experimental partial item-facing implementation**, not completion of all-item camera aiming. The user confirmed both grounded Deku Leaf camera-directed gusts (yaw/pitch) and Link's accepted-use horizontal turn work. Initial horizontal facing has additionally been implemented for bows (`27/35/36`), boomerang, hookshot, grappling, qualifying carried-bomb throws, hammer strikes and empty-bottle swings. These new families seed yaw at native accepted entries only: **ongoing first-person aim and projectile pitch remain native**, and the user has now confirmed the expanded prepared-inventory playtest works. This is aggregate human acceptance, not an exhaustive per-item/resource/platform matrix. No release-only SUBJECT exception, forced aim mode or shared camera/input rotation is introduced. Water/pour and bait candidates were removed because native event ownership precedes those sites; they remain native, as do contextual workflows and unsupported inventory families.

Initial source tracing and host fixtures did not establish native reachability. The first human playtest failed: the FIELD-only type check and blanket nonzero-camera-flags check rejected ordinary room gameplay. The correction below is now proven to supply camera yaw/pitch at the real native collision entry in one private checkpoint, in native30 and interpolation. This is **not** visual/hit, range/BG/resource, all-region or all-item acceptance. The feature remains explicitly partial and experimental.

## Primary sources and method

No proprietary function bodies are reproduced. Private generated code was read-only; addresses below are research facts and review locators, not copied implementations. Public declarations are pinned to the provenance commit carried by this repo's guest headers.

- **P1:** [public `d_camera.h`, commit `47e1dbc3886cfd8233859dffd73efc41a04a9130`](https://github.com/ZeldaWWHDDecomp/wwhd/blob/47e1dbc3886cfd8233859dffd73efc41a04a9130/wwhd_src/include/d/d_camera.h), represented locally by [`runtime/guest/include/wwhd/camera.h`](../runtime/guest/include/wwhd/camera.h). Camera process/dCamera layouts and named fields; offsets are assertions, not guesses from the rendered view.
- **P2:** [public `d_a_player.h` at the same commit](https://github.com/ZeldaWWHDDecomp/wwhd/blob/47e1dbc3886cfd8233859dffd73efc41a04a9130/wwhd_src/include/d/actor/d_a_player.h), represented by [`runtime/guest/include/wwhd/link.h`](../runtime/guest/include/wwhd/link.h). Native procedure/equipped item, attention actor and item-local fields. The gust fields remain unnamed (`m3534`, `m3536`, `m3538`) publicly; their meaning is established by N2, not invented names treated as ABI.
- **P3:** [`runtime/guest/include/wwhd/functions.h`](../runtime/guest/include/wwhd/functions.h), the generated native symbol catalog, plus [`runtime/include/guest_addr.h`](../runtime/include/guest_addr.h) and [`runtime/src/guest_addr_identity.c`](../runtime/src/guest_addr_identity.c). Catalog identifies item procedure families and known native entry addresses; EU data-global translation is required; hook-list/function names use canonical USA addresses that the region-aware generator maps to actual regional PCs. Actual EU `setCollision` and its internal sites can differ (this routine maps by +4), so canonical hook addresses are not claims of identical regional instruction PCs. The EU host fixture tests `GD` data translation, not the retail hook-generator PC mapping.
- **N1 (private local primary source):** `/home/mhbxyz/Projects/ZeldaWWHDRecomp/build/gen-quick-use/code_002.c`, `camera_process` initialization and camera getter families; `code_043.c`, camera types/modes and gameplay-camera operations. The play camera slot `GD(0x1046F0B0)+0x5AF8` points at a **camera_process**, whose gameplay `dCamera` is embedded at `+0x248`. Native direction getter uses dCamera `+0x6C`; rendering process center/eye at `+0xDC/+0xE8` and dCamera ViewCache are not gameplay direction inputs.
- **N2 (private local primary source):** `code_033.c`, `procFanSwing_init` `023E79DC` (selects procedure `0x92`, including decision `023E7A3C`); `code_036.c`, `procFanSwing` `024386C4`, gust setup `02438978`–`02438A74` (timer `+0x6984`, yaw `+0x6986`, pitch `+0x6988`, lock-on target branch); `code_034.c`, `setCollision` `024076CC`, gust subpath `02409734`–`02409D54`. The native ray uses sine/cosine of yaw and pitch, performs a background line check at `02409834`, handles rejection/clipping natively, can refine pitch at `02409ABC`, and uses yaw/pitch again for its gust transform at `02409B08`–`02409B14`. `code_035.c`, execution collision calls including `0240E470`, confirms the collision routine is called from native player execution, not an unrelated rendering callback.
- **N3 (private local primary source):** `code_036.c`, gust helper call `02438D4C`; `code_047.c`, `squal_set` `0257DC9C`. This is the environmental/visual squall helper, **not the native gameplay gust collision seam**. A yaw-only hook here was rejected as inadequate. It is not shipped.
- **N4 (private local primary source):** `code_033.c`, `procBowSubject_init` `023E7C98`, `procBowMove_init` `023E88B0`; `code_036.c`, `procBowSubject` `024379C0` / `procBowMove` `02437A8C`; `code_035.c`, `setBodyAngleToCamera` `02416E90`; `code_002.c`, arrow shooting `02053814`. Arrow shooting initializes flight velocity from projectile pitch/yaw at `+0x320/+0x322` (positive pitch raises Y). That is a physics initialization seam, **not proof that free-camera inventory use reaches it without SUBJECT**. Existing manual first-person aiming and newly entered SUBJECT must be distinguished at an earlier decision before future bow support.
- **N5 (private local primary source):** `code_010.c`, `daFan_c` collision/execute family `0213258C` / `0213284C`. This separate fan actor was examined and rejected as the Leaf seam; actor names alone do not establish player item ownership.
- **H1:** [`runtime/src/mods/manager.cpp`](../runtime/src/mods/manager.cpp), [`runtime/src/mods/packages.cpp`](../runtime/src/mods/packages.cpp), manager/profile tests. Built-in registration supplies Mods UI enumeration, settings and package/profile integration; separate package manifests or independent input bindings are unnecessary.

P3 is discovery evidence for unimplemented item families, **not an assertion that every item/context has been traced to a safe setter**. Those research gaps are explicit below. Public links are provenance citations; native source bodies, generated assets and checkpoints are not publication material.

## Camera direction and precedence

The gameplay direction is `dCamera.center - dCamera.eye` at `+0x10/+0x1C`, not the rendered process view, ViewCache, host spectator/free camera, or overlay preview. Conversion is `yaw = atan2(dx,dz)` and native Leaf `X = -atan2(dy,hypot(dx,dz))`, scaled by `32768/pi`, wrapped to a 16-bit angle. Native Leaf ray Y is `-sin(X)`; this sign is **opposite** to the arrow flight convention (N2 vs N4). The native target-angle domain is ±`0x4000`; atan2 naturally bounds elevation to that domain. No general camera pitch clamp is fabricated. Near-vertical/degenerate or non-finite vectors are refused rather than assigning a guessed heading.

Precedence: mod off → native; missing/unfocused/modal host or blocking overlay → native; true60 preview → native; events/non-field camera/transition/first-person → native; Link or camera lock-on/targets → native; wrong owner/item/procedure/ground state or no accepted gust timer → native; otherwise scoped Leaf direction, or a single typed accepted-action horizontal turn. No new item pitch or ongoing SUBJECT override. Native use animation, activation decision and cooldown happen before this seam; nothing is spawned or forced by the mod.

Current context checks: main player from play `+0x5B34`; player/event byte `+0x420` and play `+0x5292`; Link attention target `+0x65B0`; active/unpaused camera; camera player and all relevant target slots; camera lock-on/event state (play `+0x52E4` or camera bit `0x20000000`); no forced type; stable current/next type and mode. Modes 0/12 accept named FIELD or the native room-selected map-tool type only when bounded type/style metadata proves FIELD-equivalent default and active-mode engines, with exact selected-style coherence. Named EVENT/WATER/BOAT/BOATBATTLE/SUBJECT/KEEP/RESTRICT types remain excluded. Ordinary mode-12 flag `0x20` is not event ownership. This is conservative: a valid unusual field submode can remain unsupported rather than having a cinematic or lock-on redirected. Static addresses use `GD`; actor/process pointers are native Mem2 pointers and bounds/alignment checked before use. Regions beyond USA/EU are not claimed.

## Per-item/action and context matrix

Legend: **I** = Leaf gust direction plus accepted-use facing, with scoped human confirmation; **F** = initial horizontal facing only, not full aiming/pitch/launch acceptance; **U** = directional opportunity unimplemented/unproven, stays native; **N** = preserve native behavior. A native aiming initializer may enter from a still-free camera, seed yaw once, and subsequently enter SUBJECT; afterward native first-person owns aiming. Pre-existing SUBJECT/lock and vehicle/swim/air contexts remain excluded.

| Usable item/action (including variants) | Grounded free camera | FP / enemy lock-on | Air / ledge / climb / swim | Boat / cannon / event / minigame | Direction seam and remaining gap |
|---|---|---|---|---|---|
| Deku Leaf accepted grounded gust | **I: yaw + pitch + initial facing** | N | N | N | N2 native input verified; user confirmed gust direction and Link turning. Exhaustive hits/BG/resources/save/platform acceptance is not implied. |
| Deku Leaf open/glide/parachute | N | N | N | N | Native flight/lift/landing control, not a gust; never reinterpret as camera flight physics. |
| Hero's Bow normal arrows | **F: initial yaw** | N | N | N | Accepted Subject/Move entry; actual native free-camera Subject entry observed. Subsequent native aim/pitch and shot refusal remain authoritative; launch/hits unvalidated. |
| Fire and Ice arrows | **F: initial yaw** | N | N | N | Same bow entry, not automatic elemental selection/pitch redirection. Native selection/cost/effects retained; selected-mode gameplay pending. |
| Light arrows | **F: initial yaw** | N | N | N | Same entry only; native mode/magic/projectile behavior retained, not separately accepted. |
| Boomerang throw / marked target path / return | **F: initial aim-entry yaw** | N | N | N | Accepted Subject/Move entry; native free-camera entry observed. No mark/homing/return/flight setter overridden. Full handoff/gameplay pending. |
| Hookshot fire / latch / pull | **F: initial aim-entry yaw** | N | N | N | Accepted aim entry observed. Native pitch/ray/latch/pull/Fly behavior remains untouched and unvalidated. |
| Grappling Hook cast / retract / steal | **F: initial aim-entry yaw** | N | N | N | Accepted Subject/Move entry observed from free camera. Native pitch limits/cast/anchor/steal checks retained; no hang/swing/pull hook. |
| Grappling Hook hang / swing / climb | N | N | N | N | Native locomotion/attachment; camera redirection would change movement, excluded. |
| Bomb take out / place / carry | N | N | N | N | Do not redirect native pickup/placement or create bombs. |
| Bomb throw | **F: qualifying carried-bomb yaw** | N | N | N | Normal native throw policy, carry sentinel/pending bomb selection and kept PID/profile required. No fresh-bomb intent or pitched-arc claim. Native entry replay not reached; human/arc/fuse validation pending. |
| Bombs fired from King of Red Lions cannon | N | N | N | N | Distinct boat native aim/recoil/ammo path; explicitly unsupported. |
| All-Purpose Bait place / scatter / toss | U for directional toss; N for placement | N | N | N | P3 `procFoodSet` `0243F670` / `procFoodThrow` `0243F4DC`; bag consumption and AI trigger preserved, no setter proven. |
| Hyoi Pear consumption / seagull possession/control | N | N | N | N | Item changes controlled actor; not player's ordinary directional use. No seagull controls intercepted. |
| Empty bottle scoop / swing / capture | **F: accepted swing yaw** | N | N | N | Native land swing entry; focused native callback changed yaw exactly to camera and preserved GPRs. Capture/contact and broad human acceptance pending. |
| Bottled water / Forest Water pour or splash | U | N | N | N | P3 BottleOpen `0243DBFC`; water expiry/quantity/interactions must remain native, safe direction setter not proven. |
| Bottled fairy release / automatic revive | U for intentional release; N for revive | N | N | N | Release/restore context not traced; no camera-directed autonomous revival. |
| Bottled potions (red/green/blue), Elixir Soup (servings), bottled contents consumed or drunk | N | N | N | N | P3 BottleDrink `0243D838`; self-use has no directional action. |
| Bottle-held firefly or other collectible contents release | U if native release is directional | N | N | N | Content-dependent behavior not traced; do not assume a common bottle projectile. |
| Wind Waker conducting / learned songs / wind-setting | N | N | N | N | Musical input and selected world wind are intentional native systems, not camera heading. |
| Telescope zoom / first-person look | N | N | N | N | Existing SUBJECT/first-person aim is authoritative; no second aiming system. |
| Picto Box and Deluxe Picto Box photos | N | N | N | N | Existing photo first-person framing and capture validity preserved. |
| Tingle Bottle message/bottle use (HD) | N | N | N | N | UI/content action; no demonstrated directional combat/use seam. Tingle Tuner is the GameCube item, not this HD target. |
| Spoils Bag offerings: Joy Pendants, Skull Necklaces, Boko Baba Seeds, Golden Feathers, Knight's Crests, Red/Green Chu Jelly and native bag variants | N | N | N | N | NPC/menu presentation/collection, not projectile aim. No item-number list is used to bypass this distinction. |
| Delivery Bag offerings: letters, deed/coupon, trading flowers/flags/statues and native mail/quest variants | N | N | N | N | Native presentation, mailbox and trade/placement interactions stay native. Any world placement direction would require its own investigated seam. |
| Hero/Master Sword (all upgrades), quick melee/slash/thrust/spin | U for heading; N for invented pitch | N | N | N | P3 sword collision/procedure families; native animation/root motion and hit volume must not be rotated by broad player facing. No directional melee setter proven. |
| Shield / Mirror Shield guard or light reflection | N | N | N | N | Defensive pose/first-person reflection handling preserved; do not fabricate a pitched shield plane. |
| Skull Hammer swing / overhead attack | **F: strike-entry yaw** | N | N | N | Side51/front53 only, not ready52/recovery54/equip. Front native entry observed; damage/side/gameplay pending. Native arc/pitch/hit keys retained. |
| Picked-up enemy weapons (sword, spear, machete, club, stick), pots/rocks and other carried actors: throw | U | N | N | N | P3 GrabThrow candidate; actor type, player ownership and native arc/collision validation remain research gaps. |
| Carried actor swing / place / put down | U for directional swing; N for placement | N | N | N | No broad shared move/facing patch; each native action/volume needs its own seam. |
| Sail / Swift Sail use and boat control | N | N | N | N | Boat propulsion/native wind/steering, explicitly outside item-use directional scope. |
| Power Bracelets, Iron Boots, Magic Armor, Hero's Charm (activation/equipment) | N | N | N | N | Equipment/status or passive interaction capabilities, no identified directional action. |

Charts/maps/Triforce shards, wallet/quiver/bomb-bag/magic upgrades, hearts/rupees/ammo pickups, dungeon keys/compass/map, pearls, clothing and quest flags are collectibles/passive data, not independent directional usable items. Their inventory/equip acquisition logic is not hooked. Weapon upgrades are grouped above; **`0x37` is not a bow whitelist entry**. Only bow inventory upgrades `27/35/36` qualify for initial facing; selected elemental arrow mode remains native.

Cross-cutting refusal gates: overlays/setup/modal text prompts; lost focus; movie/cutscene/event/non-field or forced-lock camera; camera type or mode transitions; paused/missing camera; invalid/non-player ownership; existing SUBJECT; Link/camera target lock (including Leaf's native `mModeFlg & 0x80`, set/cleared at `0243871C`/`0243873C`); wrong item/procedure; lack of ground bit or accepted positive signed gust timer. Swimming/climbing/parachuting/boat/cannon and known first-person minigame procedures cannot meet the grounded Leaf procedure gate. **There is no stage-wide minigame classifier**: a hypothetical minigame using an otherwise ordinary field-camera grounded Leaf action is an unvalidated exclusion gap, not proven refused. Only the stated gates establish I; otherwise the original function is called exactly once.

## Implementation seam and lifetime

`hook_024076CC` wraps native `setCollision` once, not a global camera or input function. Only during an accepted Leaf gust collision pass, RAII temporarily supplies its two **item-local** direction fields at Link `+0x6986/+0x6988`. Native code consumes them for its full ray/BG-check/geometry pipeline and remains free to refine pitch within that call. Both fields are restored on normal return, toggle changes, and exception unwind. These are **temporary guest-memory writes**, not a register-only/no-memory-write implementation. The coordinator explicitly approved this scoped item-local technique for the partial prototype; the collision wrapper does not alter Link actor/current/shape rotation, movement vectors, camera fields, render views, speed, cooldowns, resources or gust timers. The separate accepted-use facing hook below intentionally updates current/shape yaw once; it is not part of this transient collision scope. The original procedure determines origin/range/timer, animation, activation, background rejection/clipping, collision registration and effects.

This scopes the override across native collision refinement rather than replacing a final capsule transform (which could accidentally ignore BG clipping) or changing only the visual squall. Direction is resampled for each native active gust collision pass, not latched across saves or player changes. There is no cached guest pointer/direction or queued use token. Mod-off/lock-on/focus/event/load boundaries therefore have no deferred steering to drain. Native timer/refusal writes are not rolled back; only the temporary direction slots are restored. The native pitch-refinement write at `02409ABC` is effective for the remainder of that call but **is restored too**, not retained for the next pass. N2's pitch helper consumes target/ray position and yaw, not the previous pitch as an iterative clamp; each directed pass supplies a fresh camera pitch. This changes the persistence of that item-direction slot intentionally, rather than claiming every native write survives.

The wrapper runs on the emulation thread only; UI changes only the atomic toggle. Existing [`runtime/src/threads.cpp`](../runtime/src/threads.cpp) `try_entry_park` refuses entry parking while a hook/HLE host-state frame remains on the stack, so a live RAII scope is not a guest-only restartable function entry. This is the relevant source safety contract, **not** a tested snapshot guarantee: an isolated save/load during native collision use, including any ordinary HLE wait/freeze behavior, remains an acceptance gate. No new background guest writer or snapshot serialization is introduced. Native visible squall particles/Link swing animation still use their native presentation, so full visual alignment must be checked before removing the prototype label.

Settings ID `camera-directed-items`, getter/setter, default-off atomic, exact env opt-in `WWHD_MOD_CAMERA_DIRECTED_ITEMS=1`, standard manager persistence/profile selection and manager-generated Mods UI. No standalone package installation and no new key binding. Existing movement/direct-camera/sprint mods remain independent. Host focus is published atomically from SDL/AppKit so the game thread does not call platform UI APIs.

## Verification and remaining acceptance gates

Initial builds were confined to ignored `build/camera-directed-items/`; stub generation was **not a rebuilt retail/native game**. A later user-authorized native build and playtest exposed the eligibility bug. Coordinator diagnosis used separate copied-save/checkpoint processes; no original save was written, no user instance was attached/stopped, and nothing has been committed/pushed. Native diagnostic details follow below.

- USA and EU host ABI fixtures run the same runtime wrapper with region-translated play global. Assert default-off/env opt-in; wrong owner/player pointer; active/pause/target/lock/event/type/mode checks; focus/overlay/preview; SUBJECT/wrong item/procedure; grounded/timer gating; NaN/degenerate views; gameplay vs deliberately disagreeing render camera; cardinal yaw and upward/downward pitch; no Link rotation change during collision; accepted-use facing modifies only two actor yaw words and no CPU bits; original collision called once; native timer side effects/results retained; direction restored after disable/unwind/load-like fixture reset. These tests do **not** execute native collision physics or prove a visible user path.
- Manager fixtures cover default-off, stable built-in ID, env mapping, save/reload and independent toggles. Package/profile fixtures check the setting travels with a profile while other profiles preserve their value.
- Clang Release full `wwhd` stub runtime and all host targets built/linked; complete final CTest suite passed **188/188**, including 7/7 targeted camera/env/manager/profile tests. No native play. Linux SDL focus build compiled; AppKit implementation is source-inspected but cannot be built/run on Linux.

Reproduction (all paths remain inside the ignored tree): `python3 tools/recomp/stubgen.py build/camera-directed-items/gen`; use the existing configured `build/camera-directed-items/cmake` with its `GEN_DIR` pointing to those stubs; `cmake --build build/camera-directed-items/cmake -j 4`; `ctest --test-dir build/camera-directed-items/cmake --output-on-failure`. Logs: `all-build.log`, `final-ctest.log`, `final2-build.log` in `build/camera-directed-items/`. `git diff --check` passes.

Before general availability: broader coordinator-managed isolated Leaf gameplay (USA and EU), reachability beyond the single verified room/checkpoint, simultaneous context transitions, slopes/walls/ceilings/range/occlusion, native magic/activation/refusal/animation, visible gust coherence, disable/profile changes and save-load during use. Then trace bow's initial decision handoff without overriding manual first-person, and implement separately investigated seams for every U row. An all-item acceptance claim is currently **false**.

## Native eligibility regression and correction

The first manual native test failed. An isolated GDB-driven replay then reproduced
it twice with the unmodified native binary
`a04d2577c1a02f44e2d693b8c22ee2545d8cd96df7d28646ad9dd14f62482456`.
The real hook was reached during an active grounded Leaf gust, with focus, correct
player ownership and no lock/target/event. Native collision still received
Link's yaw/pitch `35195/0`, instead of camera direction `36032/4208`.

Two independent false exclusions were responsible:

1. The room selected custom type 60 (DungeonUp), while the named FIELD type was
   7. Native `nextType` uses room/map-tool selection at `024FA700–024FA764`; a raw
   FIELD-ID equality is not an ordinary-field-camera classifier.
2. Camera flags were `0x20`. Native `Run` at `024FE7EC–024FE810` sets that bit for
   normal mode 12. Actual event ownership is play `+0x52E4` or camera flag
   `0x20000000`, independently used at `024FA43C–024FA454` and
   `02514E44–02514E58`. Rejecting every nonzero flag disabled a legitimate view.

The correction does not whitelist type 60 or accept every map-tool camera. It
requires stable modes 0/12, named FIELD or the selected native room type, the
same default **and active-mode algorithm** as FIELD, and exact selected-style
coherence. Native metadata comes from the pinned public `camTypeStyle` and
`camStyleAlg` interfaces: type count `10049348`, type rows `10049350` (stride40,
20 signed style slots at18), style count `10044878`, style rows `1004487C`
(stride84, algorithm at4). All static addresses use `GD`, IDs/counts are checked
before reads and address arithmetic is widened. Named special types, pending
forced type/force byte, native event ownership, lock, owner, focus, overlay,
preview and action gates remain authoritative. Custom cinematic rows with a
manual-looking active engine but a different default engine are rejected.

Validation recorded after the correction:

- The native-context regression initially failed both USA/EU host fixtures.
  The final expanded fixture independently linked against the **unchanged old
  runtime object** still failed the camera-angle assertion (exit134); the fixed
  implementation passed. Fixtures now cover ordinary room metadata, a second
  synthetic equivalent room type, mixed event/mode flags, room/SUBJECT changes,
  default/active class mismatches, stale styles, invalid IDs/counts, native room
  lock gates and full bitwise CPU-state preservation.
- Full **188/188 CTest** passed; complete USA and EU-mapped camera fixtures also
  passed under UBSan. These are predicate/ABI tests, not EU gameplay.
- The rebuilt native binary
  `c2fb02b50b87f70cecea2ce09a42b8d3c984d53a5e416027712b8df3bf80ad7e`
  supplied exactly `36032/4208` to the real original collision function in the
  same private checkpoint. Native30 and interpolation steering checks passed;
  mod-off and enemy-lock controls preserved the original native input angles.
  No guest field was forced by the debugger: breakpoints only observed the
  actual hook/original boundary in separate copied-save processes.
- Two true60 attempts lacked SDL keyboard focus and correctly retained native
  angles. They **do not establish active focused true60 steering acceptance**;
  focused true60 and visible/collision-effect verification remain pending.
- Independent read-only native-source/predicate review found no source-proven
  blocker for this narrow fix. Original-save hashes remain unchanged; no
  diagnostic logging/instrumentation was added to the runtime source.

Private reproduction artifacts remain under the root checkout's ignored
`build/camera-leaf-diagnosis/` (`probe.py`, `run.py`, baseline/fixed reports and
old-runtime red control). `python3 -B build/camera-leaf-diagnosis/run.py case 0 on`
creates a fresh private run automatically; mode1 selects interpolation, `off`
and `lock` select native controls. The runner shows its own test window and may
be rejected for lack of focus; it never attaches to or stops a user's existing
instance. These artifacts and full checkpoints are not for publication.

This correction establishes the previously missing native direction handoff at
one checkpoint, **not** collision hits, coherent particles,
wall/slope behavior, resources, save safety, or completion of the all-item goal.

## Follow-up: Link facing at accepted Leaf use

The user subsequently confirmed that camera-directed gust use works, but reported
that Link himself still faced the native direction. That feedback expands the
requested behavior from item-local gust direction to **one action-local horizontal
actor turn**, not continuous camera-following movement or an invented body pitch.

New instruction site `@023E7A70` runs in `procFanSwing_init`, after native proc92
and lower-animation acceptance and before fan-model preparation. The original
instruction is still executed; the callback uses live `r30` as Link and changes
no CPU bits. The proc92 reentry rejection at `023E7A0C–023E7A34` bypasses this
site. Full existing camera/player/grounded-Leaf/focus/overlay/preview/event/lock
eligibility is reused, except that initial use correctly does not require an
already-active gust timer.

The callback deliberately writes just current yaw `+0x322` and shape yaw
`+0x32A` once. These are **durable guest actor-state changes**, not temporary
item-local slots: the native world/save owns them afterward. There is no heading
cache, continuous per-frame enforcement, disable-time rollback or raw reset
restoration. No camera fields, positions, velocities, body pitch, native resource
or refusal state are changed. Collision retains its separate transient yaw/pitch
scope and never reorients the actor. Native recoil at `02438910` intentionally
sets current yaw to shape yaw plus `0x8000`; this remains untouched, so later
current/model yaw equality would be an incorrect acceptance criterion.

Native tracing found no yaw-result/store seam inside Leaf init/model preparation.
`setBodyAngleToCamera` uses native camera-slot bit `0x10`, which later native
observations show is also present in ordinary free-camera entries; the bit is not
exclusive first-person ownership. This shared body/camera helper is still not a
proven explicit item-local free-camera pitch initializer. Independent read-only review accepted the localized durable fallback
while identifying an ABI integration mistake: instruction hooks require the
`site_023E7A70` symbol, not `hook_`. The final source/fixture use `site_`; the new
native generation calls it through `ppc_host_call`, and the Release native link
and exported symbol were verified.

Evidence for the new facing slice:

- The positive actor/CPU fixture failed USA and EU with a no-op facing callback.
  The **final expanded fixture**, linked to the frozen previous camera runtime
  plus a no-op instruction adapter, still fails at the facing assertion (exit134).
- Final **188/188 CTest** and both USA/EU-mapped UBSan fixtures pass. Actor-wide
  byte comparison permits exactly the two yaw words; CPU-wide comparison permits
  no changes. Coverage includes cardinal/signed-wrap headings, the shared negative
  guards, every other byte-sized item/procedure, timer-independent initial use,
  invalid owners, unchanged body pitch and native recoil/camera motion not being
  continuously overwritten by collision.
- Rebuilt native binary SHA256:
  `c320862da8c19b7d300e5ba7dbcfba36d5c3e3e52bc91feb472b7f11c20bc612`.
  Isolated native facing attempts observed no SDL focus and unchanged facing at
  the collision boundary; they did not measure focus at the earlier init site.
  They are **inconclusive for enabled focused facing**, not evidence that its
  positive native path passed. First-tap, moving/full-update/native-region
  and new human body-facing acceptance remain pending.
- The user's existing old-build playtest was not attached/stopped or hot-patched.
  The facing build requires an explicitly coordinated restart in the private
  sandbox. Original saves remain unchanged; nothing has been committed/pushed.

**Subsequent Leaf acceptance:** the user confirmed the restarted facing build works
correctly and requested expansion to other objects. This supersedes the earlier
pending Leaf-facing human status, not exhaustive collision/save/platform evidence.

## Expansion: accepted initial facing, not full projectile camera aiming

Twelve additional canonical instruction sites now reuse the same live gameplay
camera calculation and strict player/ground/camera/target/event/focus/overlay/
preview checks. Each reads its live caller register, validates the exact native
procedure/item family, writes only actor current/shape yaw once and leaves all
CPU bits alone. Native original instructions and downstream aim/geometry remain
in place; no `commonProcInit`, global input, shared camera or per-frame facing
interception is added.

| USA site | Live Link | Native proc | Accepted initial-facing scope |
|---|---:|---:|---|
|023E7CF4 / 023E88F4|r31|94 / 95|Bow inventory27/35/36, before native readiness/strafe math|
|023E8D34 / 023E8E40|r31|80 / 81|Boomerang2D aim entry, not catch/homing/marks/return|
|023E8A58 / 023E8B20|r31|83 / 84|Hookshot2F aim entry, not Fly/latch/pull|
|023E902C / 023E9130|r31|76 / 7D|Grappling25 aim entry, not ready/hang/swing/anchor locomotion|
|023E9E98|r30|71|Normal throw policy0, carry sentinel100/pending31, bounded kept actor, matching PID and profile126|
|023EDFC4 / 0243C830|r31|51 / 53|Hammer33 actual side/front strikes, not ready52/recovery54|
|023EDE00|r30|A5|Empty bottle50 swing, native ship-result r29 zero|

All new site bodies are source-mapped EU-compatible (USA+4), not EU gameplay
accepted. Native Move initialization deliberately derives current yaw from shape
plus/minus4000; native Leaf recoil uses plus8000. Those subsequent writes survive:
initial paired yaw equality is not a continuous movement invariant. No additional
pitch is written. Native body pitch/BG checks, rope's pitch constraint, arrow
mode/magic, marks, anchors, resource costs, creation and refusal still belong to
the original pipeline. Initial aiming-procedure acceptance is not shot success.

### Two native-context blockers discovered and resolved honestly

- **Water/bait callbacks removed:** ordinary water init writes Link+420=5 at
  023EF48C before candidate023EF4B4; ordinary bait similarly writes at023EEE94
  before023EEEE8. Direct synthetic positive fixtures omitted that event state.
  Independent review caught their incompatibility with the unchanged event
  guard. Their runtime callbacks, manifest sites and misleading positive tests
  were removed; no event exception or alternative pre-event hook is shipped.
  Water/Forest Water pours, bait and other contextual use remain native.
- **Camera-slot bit10 is not exclusive FP ownership:** a proposed extra
  play+5B00 bit10 refusal was disproven by actual native free-camera bow,
  boomerang, hookshot, grappling, hammer and bottle entries. They have stable
  DungeonUp60/mode12 camera metadata, ordinary camera flags20, no target/event,
  and slot status10. The erroneous extra filter was removed for every family,
  including Leaf. Real SUBJECT metadata and transitions remain refused; no
  global SUBJECT allowance was introduced. A real-context positive regression
  now includes slot10, while a settled SUBJECT+slot10 negative still refuses.

### Evidence and remaining scope

- Final **188/188 CTest** and both USA/EU-mapped UBSan fixtures pass. The new
  callback fixture tests all twelve exported adapters, native register choices,
  full bitwise CPU preservation and full actor-region comparison allowing just
  the two yaw words, all byte-sized procedure/item alternatives plus100/FFFF,
  cardinal/wrapped headings, guard negatives, bomb PID/profile/policy and
  bottle ship-result. All three bow inventory upgrade IDs are covered, not all
  selected elemental modes. The final expanded fixture linked against frozen
  human-accepted Leaf runtime `c320862d...` plus no-op new-site adapters fails
  specifically at bow initial facing (exit134), so old behavior is detected.
- Separate copied-save/checkpoint native30 replays observed the actual accepted
  **bow, boomerang, hookshot, grappling, front hammer and empty bottle** entries
  from a still-free camera, with correct live registers/procedures/items and
  preserved GPRs. Five samples lacked SDL focus: they prove native entry/context
  reachability and native fallthrough, **not positive focused turning**.
- Empty bottle obtained an actual focused positive: camera yaw36032, before
  current/shape35195/35195, after36032/36032, GPRs preserved. The final Release
  native binary was retested for this positive and has SHA256
  `1dd365e67170eead6ff999a9b0f3a179a2849b9429fbcf271a67c55ee631f4b4`.
  This observes accepted actor heading, not successful capture/visual/hit effects.
- The automated bomb inputs did **not** reach the accepted throw site; bomb
  entry/arc/fuse/accounting remains a native/human coverage gap, not a fake
  successful replay. Empty-bottle setup changed three declared inventory/
  assignment bytes in a private derivative of an already-owned bottle slot;
  this is fixture setup, not a new untouched human checkpoint.
- Independent source review found the event blockers above; they are resolved
  by exclusion. New focused ranged first-use/hand-off, already-manual FP,
  marked boomerang, hookshot latch/pull, grappling anchors, side hammer damage,
  bomb ownership/resource/refusal, true60 and native save/load acceptance remain
  unproven by an exhaustive instrumented matrix. At this stage human confirmation
  covered Leaf only; subsequent expanded-playtest acceptance is recorded below.

Private observations, baseline binary/object, no-op regression adapters, edited
checkpoints and scripts are under ignored root `build/camera-items-expansion/`.
They are not publication assets. The user game was neither attached, stopped nor
hot-patched during expansion; original saves are unchanged and camera work
remains local/uncommitted/unpushed. A restart is needed for a new human test.

### Interactive full-checkpoint startup prerequisite

The inventory-edited human checkpoint must be loaded **after entering the copied
normal game file**, not directly at the title screen. User failure was reproduced
with the actual prepared checkpoint: full-state loading refuses NetBossThread
(exited in snapshot, still running at title). In a separate isolated native child,
with the same binary and exact same checkpoint/load frame, normal A/file-entry
before F2 restores successfully (113.1ms). The nonfatal rebuilt-allocation tag
warnings are not this blocker. No thread validation bypass, source save edit,
checkpoint rewrite or current-user-process attachment/restart was required.
Private deterministic comparison: `build/camera-items-expansion/check_checkpoint_load.py`;
results alongside it. Correct user steps: enter the existing copied quest file,
wait for controllable Link, then F2. The earlier title→F2-only instructions omitted
this necessary startup precondition.

## Expanded human acceptance and submission scope

After the startup prerequisite was explained, the user confirmed that everything
works correctly and explicitly authorized pushing the branch and opening a PR.
The isolated inventory derivative supplied upgraded bow, hookshot and hammer in
X/Y/R, plus bombs, boomerang, grappling hook, Leaf and empty bottles in inventory,
99 arrows/bombs and filled existing magic. No infinite resources, sword/song/
story upgrades or original-save changes were used. Inventory preparation scripts,
full checkpoints and binaries remain private ignored build artifacts, not part
of the feature diff.

This is an aggregate hands-on confirmation, not a recorded per-item, selected
arrow-mode, native hit/damage/resource/refusal, EU/platform, true60 or mid-action
save/load matrix. It supersedes earlier pending broad human-playtest status,
without turning unfocused automated observations into positive proofs or filling
the automated bomb-entry gap. The implementation remains a partial prototype:
Leaf yaw/pitch plus supported initial horizontal facing, not all-item projectile
pitch or continuous third-person aiming. #197 remains open for the uncovered
inventory/context rows and broader acceptance. The PR is independent of the
lock-on and quick-use branches.

## Submission rebase and verification

Before any push, the user requested `devel` rather than `main` as the base. The
single feature commit was rebased directly onto `origin/devel` at `4dc4832`.
Conflict resolution retained devel's auto-PiP EU target, SDL overlay window-focus
routing, per-controller AppKit input path and traversal initialization/catalogue
entries. AppKit's focus publication remains in the aggregate controller poll,
not a device-only poll; the built-in catalogue is now14 rather than devel's13.
The item runtime and its actor/CPU fixtures are unchanged by this rebase.

A full Linux/Clang Release stub-runtime build and **223/223 CTest** pass on the
rebased source. The USA and EU-mapped item fixtures were freshly compiled against
the rebased headers and pass under UBSan. Historical native collision/entry and
human observations above refer to the pre-rebase native build, not native
acceptance of devel's intervening runtime changes. No post-rebase native rebuild,
EU/macOS/Windows gameplay or repeated human matrix is claimed here. The existing
user playtest was not rebuilt in place, attached or stopped during submission.
