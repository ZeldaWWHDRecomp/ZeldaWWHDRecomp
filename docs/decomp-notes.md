# WWHD ↔ GameCube decompilation: findings

Function names for WWHD (`cking.rpx`) come from
`python3 tools/decomp/match.py game/code/cking.rpx tww build/names.tsv` (about 1 minute). Needs
`tww/` (zeldaret/tww) built from your own GameCube disc image (`tww/build/GZLE01`). Outputs (all
in git-ignored `build/`):

- `names.tsv`: address, name, source file, evidence, score (match probability for `graph`/`tu`).
- `wwhd_to_gc.tsv`: WWHD address → GameCube mangled symbol, source file, module, GameCube
  address, evidence, both sizes. Stage 2 (checking each function against the source) builds on it.
- `coverage.tsv`: per translation unit, GameCube functions vs. matched.
- `regions.tsv`: long unmatched stretches (HD-only code).

Helper tools: `layout.py` (structure offsets, below), `timers.py` (frame timers, below),
`datamap.py` (WWHD addresses of GameCube globals), `heldout.py` (precision test), `train.py`
(refits the pair-scoring model).

### Evidence and measured precision

| Evidence | Meaning | Count | Held-out precision |
|---|---|---|---|
| `manual` | hand-verified (`tools/decomp/manual_names.tsv`) | 11 | — |
| `assert` | assert text (file + condition) | 191 | near-certain |
| `profile` | actor profile method tables (process-name IDs) | 1,477 | near-certain |
| `strings` | rare string/float literals + file neighbourhood | 353 | high (used as test truth) |
| `vtable` | virtual tables and other function-pointer tables (method tables, PTMF tables) aligned slot by slot; also tables identified through the constructors that store them | 623 | 100% (1,689/1,689) |
| `graph` | call-graph neighbourhood + content model, mutual best, p ≥ 0.6 | 3,732 | 97.1% (271/279); 94.3% strict |
| `tu` | unmatched functions between matched functions of the same file, or between two files (alphabetical link order), same model, p ≥ 0.6 | 6,024 | 99.7% (998/1,001); 97.5% strict |
| `tu-margin` | same windows, decided relative to the file's other unmatched functions (score ≥ −1.5, ≥ 2.0 above the runner-up), after the stages above converge | 755 | 96.8% (30/31) held-out; 98.8% (80/81) offline; **medium confidence** |
| `dup`, `dup-graph`, `dup-file` | WWHD's per-file copies of inline functions: named only when that file's GameCube object has a copy of the same function | 40 | not measured (too few) |
| `callgraph-legacy` | the first call-graph matcher, run last, only where nothing else matched | 785 | ~85–90% (older test); **lower confidence** |

The content model scores a pair with 25 features. It is a conditional logit (`train.py`), fitted
on 8,900 trusted pairs against decoys from the same file, from call-graph neighbours and random.
Since round 2 it includes:
- register-agnostic instruction shape (`shape.py`): opcode histogram cosine, opcode-bigram overlap,
  branch/loop/compare/float-op counts, with prologue/epilogue bookkeeping removed;
- per-class field maps (`field_map`): GameCube field offsets accessed through `this` (or the first
  pointer argument) are translated into WWHD offsets with maps learned from matched pairs (as in
  `layout.py`; actor classes fall back to the fopAc_ac_c map) and compared with the WWHD accesses.

Cross-validated, it names 1,527 of 2,500 test pairs at p ≥ 0.7 with 97.6% precision.

How the held-out test works (`heldout.py`): trusted names (assert/strings/profile + tables) are
hidden at random, the matcher runs from the rest, and each hidden function it names again is
checked. Round-2 figures are summed over five runs: three hide 50% of the `assert`/`strings` names
(functions without table evidence, the harder case), two hide 50% of everything. "Strict" counts
naming a wrapper after its own callee as wrong. In WWHD, an actor's `daXxx_Create` often has
`_create` inlined, so its body is the callee's. The "offline" `tu-margin` figure re-decides 2,714
trusted pairs with each one hidden in turn and the rest known.

Known wrong names are listed with `!` in `manual_names.tsv`. For example, `025200D4` is a tiny
static-instance accessor, not `JAIZelBasic::seStart` (and not `__ptmf_scall`). The compiler runtime
(`PowerPC_EABI_Support/Runtime`) is never used as a match target.

### Structural facts used by the matcher

- WWHD links translation units in **alphabetical order per directory** (`SSystem/c_*`,
  `JAZelAudio`, `d/` with the actors in `d/actor` sorted into it, `f_op`, `f_pc`, `m_Do`, ...,
  JSystem, `dolphin/mtx`). Each unit is contiguous. Function order inside a unit is often
  preserved (c_lib) but not always (d_camera).
- Green Hills vtables: 8 bytes per slot (`delta:index` word, function pointer). A zero header slot
  starts each table. Actor classes gained a virtual destructor in slot 0, so WWHD actor tables
  have one more slot than the GameCube ones. `028F036C` (`li r3,13; b ...`) is the
  pure-virtual stub.
- Identical code folding is not done: a GHS build keeps a copy of each out-of-line inline
  function per translation unit (≈12,300 functions in ≈1,000 identical-body groups; 1,504 copies
  of one 20-byte trivial deleting destructor).
- Function discovery in `tools/recomp` merges functions that are only reached by tail calls
  (`b`) into their predecessor. The matcher splits 1,932 of them (`binmodel.refine_functions`).
  The recompiler's own list is unchanged.

## Camera (for frame interpolation)

| WWHD | Function | Notes |
|---|---|---|
| `024FFA3C` | `camera_execute` | per logic tick |
| `024FFC40` | `camera_draw` | builds the matrices each frame from `view.mLookat` (eye, center, up), `mFovy`, `mAspect`, `mNear`, `mFar`, `mBank` |
| `025F1EAC` | `mDoMtx_lookAt` | view matrix (called from `camera_draw`) |
| `028E9948` | `C_MTXPerspective` | projection; called with view_class fovy/aspect/near/far (manual) |
| `028E91EC` | `PSMTXInverse` | inverse view |

Interpolation hook: the inputs of `camera_draw` (eye/center/up/fovy/bank) are a handful of
values; blending them between two ticks gives the in-between camera.

## Models

| WWHD | Function | Notes |
|---|---|---|
| `025E2DE0` | `mDoExt_modelUpdateDL` | per-model update from actor Draw |
| `027F4FE4` | `J3DModel::update` (structure) | 52 bytes: calls `calc` then `entry`; direct calls, no vtable in WWHD |
| `027F4D5C` | `J3DModel::calc` (structure) | joint/world matrices; called from actor Execute (e.g. `daBoko_c::execute`) |
| `027F4F1C` | `J3DModel::entry` (structure) | |
| `025E2BF4` → `027F55FC` → `027F53CC` → `027FDA54` | likely `viewCalc` → `calcDrawMtx` chain | world × view → draw matrices; to confirm |

| `027F55FC` | `J3DModel::viewCalc` | world → view draw matrices (`027DE8A0`, per screen); from `025E2BF4` (mDoExt_modelEntryDL part) and `daShip_c` |
| `027F5018` | J3DModel UBO update (WWHD) | job on the `update_ubo` thread: world matrices → uniform buffers, concurrent with the main thread's draw |
| `027F273C` | shape packet entry | stores `&world[joint]` for rigid shapes (called from `entry`) |

Layout (WWHD J3DModel): `+0x2C` → joint matrix block (`+0x10` world matrices, 3x4 row-major,
`+0x2C` u16 count), `+0xBC` base scale, `+0xC8` base transform (3x4), `+0x6C` draw-buffer index,
`+0xAC` model data. The painter does not read the view-space draw matrices for skinned/rigid
models seen so far; what reaches the screen is what `027F5018` copies from the world matrices.

WWHD restructured J3DModel (Hexa Drive's HD renderer): the GameCube virtual calls are direct
calls here, so names must be confirmed by call structure rather than vtables.

## Interpolation (runtime/src/interp.cpp)

Logic stays at 30 Hz; the per-frame function runs at 60. Hold passes draw everything at step N+1
and record each model's world matrices; logic passes draw with the camera inputs and every model's
world matrices halfway between N and N+1 (viewCalc and the UBO update run on the halfway values,
the exact ones are restored right after each). Models without a previous step (spawned, not drawn
last time) or that jumped further than 400 units use the current matrices.

### 120 and 240 fps (N in-between frames per step)

The same structure with N hold passes per logic step instead of one (N = 3 at 120 fps, 7 at 240;
`runtime/src/interp_pacing.h` holds the arithmetic, `runtime/tools/interp_pacing_test.cpp` tests
it). The logic pass draws at t = 1/(N+1); hold passes 1..N-1 ("blended hold passes") draw at
t = (k+1)/(N+1) without logic; hold pass N, the *record pass*, draws step N+1 exactly and records it
(the "before" of the next step: model matrices, camera, material animation frames, sea grid). Every
blend takes the fraction: lerp, s16 angles the short way, colours, slerp for rotations and the
camera's orbit direction (constant angular speed; at t = 1/2 the original halfway arithmetic, so
60 fps frames are bit-identical). Values only logic computes (particles, wave crests, sway slots,
weather sprites) are blended on the logic pass, which keeps their before/after pairs, and re-applied
at each blended hold pass's own fraction (`fx_hold_blend`). Sound starts are suppressed on every
hold pass, the sound listener follows only the record pass's camera, the audio frame runs once per
logic step, and the controller read repeats its previous sample until the step's last pass, so
input reaches the logic once per step.

GX2's virtual vsync (gx2_core.cpp) ticks 2 or 4 times per 59.94 Hz vsync at 120/240 fps (quarter-
vsync ticks rounded to the rate's granule; 30/60 fps keep the 59.94 Hz timing exactly), so the
game's swap interval 2 becomes N+1 flips per step. The drawn rate is capped to what the display
shows when presentation waits for its vsync (Metal, Vulkan FIFO): 240 fps on a 120 Hz display draws
120, on a 60 Hz one 60 (`interp::output_fps`; the hosts report the refresh rate: NSScreen
maximumFramesPerSecond, SDL's display mode, Android's Display, which the game asks for a faster
mode at 120/240 fps).

Paced ("Keep game speed", on by default at 120/240 fps): the logic pass plans the step's in-between
frames from the recent logic and in-between pass durations (as many as fit into 33.3 ms + 2 ms),
spaces them evenly (t = k/(n+1)), and after each pass checks the rest still fits; if only one pass
fits it skips to the record pass (without one the next step has no "before" and is drawn exactly,
showing the same picture twice), otherwise drops the rest and the next logic pass waits for its
time. Pass durations are averaged per kind (a slow logic pass must not keep cheap in-between
passes out), a single sample counts at most twice the average (one hitch kept them out for 10 s),
and after 10 s without an in-between pass one probe re-measures them.

Logic stays at 30 Hz. For each rendered tick, record per model (J3DModel pointer) the world
matrices produced by `calc` and the camera inputs of `camera_draw`. For the in-between frame,
replay the frame's GX2 command list with draw matrices recomputed from blended world matrices and
the blended camera. Models without a previous tick (spawned, teleported) use the current matrices.

## True 60 fps (runtime/src/true60.cpp, true60_link.cpp)

Link and the camera at 60 steps per second ("true 60", Graphics menu / key 7 / `WWHD_TRUE60=1`;
exclusive with interpolation). Experimental, off by default.

### Design: the full pass is the 30 fps step, the half pass a preview

The interpolation pass structure is reused: the per-frame function runs every vsync. *Full
passes* run everything as a normal 30 Hz frame. *Half passes* (interpolation's hold passes) run
only the converted processes, Link and the follow camera, and draw.

The rule that makes true 60 exact: **every full pass is exactly the 30 fps game's step.** A
half pass computes a *preview* of the next step for Link and the camera (dt = 0.5: animation
frame + rate/2, movement by half the speed with gravity and root motion, collision correction,
model and item matrices, camera smoothing with 1-(1-r)^0.5). It is drawn, so Link and the camera
move at 60 Hz on screen, and then **taken back**: at the start of the next full pass
(`pass_begin`) Link and the camera are restored to the state the last full pass left, and the
full pass runs the whole 30 Hz step with dt = 1. Game state after each full pass is therefore
bit-identical to the 30 fps game's state after the same step.

What a preview does not do (fences, `true60_link.cpp`):
- **No decisions.** Link's procedure is not called (the bctrl at execute 0240D6D8/0240D6F8 gets a
  no-op), nor the decision functions before it (changeDemoProc, changeDeadProc,
  changeAutoJumpProc, changeSwimProc, changeDamageProc, changeBoomerangCatchProc,
  checkItemAction, setGetDemo) or startRestartRoom / deleteEquipItem. Per-step outputs of the
  procedure that the rest of the execute uses are replayed from the last full pass: the
  root-motion mode m34C2 (+0x68DE) and the "sword attack active" reset flag (+0x3C0 bit 0x2).
- **No lasting effects.** Process creation (fpcSCtRq_Request, fpcM_FastCreate), particle emitters
  (JPAEmitterManager::createSimpleEmitterID), collision registration (cCcS::Set: a hit found on
  a full pass stays for the next full step, as at 30 fps), events and fades
  (dEvt_control_c::order/orderOld, mDoGph_gInf_c::fadeOut), save-data writes
  (dSv_event_c::setEventReg, setBottleItemIn), sound starts (none on any half pass), random
  numbers (cM_rnd's seeds 101FF9D4.. are put back after each preview execute and each draw-time
  call on a half pass).

What is taken back at the next full pass:
- Link: his process (0x8284 bytes), every word his preview execute changed in his actor heap
  (from the heap object fopAc_ac_c::heap +0xF4: allocation state, model joint matrices, old-frame
  pose blocks, animation buffers), in the d_a_player statics 1046CD10..1046CD54 (his kept
  position) and in the play state 10473FE8..10474C68 (player position/status words, the do/A/R
  button statuses). Taken back word by word where the *execute* changed them: the actor heap also
  holds the models' draw packets, which the draw links into the frame's draw lists (taking those
  back broke the lists).
- The camera: its process (0xB28 bytes, from its profile) and the play-state words its preview
  execute changed (1046F0B0..10485000).
- Draw state the half pass's previews changed, which the full pass's draws use before its own
  camera_draw: view matrix 104B45F8, clipper 1048CFF0, the camera slots' draw data 1047E720 and
  104846B0, Link's lighting position 10474DE0. Not the scene-lighting block g_env_light: it holds
  packet pointers (restoring it made J3DDrawBuffer assert after scene changes).
- For 30 Hz actors drawn on a half pass: words their draw changed in their own memory and their
  culling matrix (+0x348) are put back right after the draw (draw-time counters such as daItem's).

Other parts that only step on full passes in true 60: the HUD update (02593B10, it ran twice per
step in both 60 fps modes), Link's checkItemAction and playTextureAnime, J3DFrameCtrl::checkPass
(key frames are found on full passes only), the world systems in the play scene's draw
(site_025B00B0), particles.

Converted, per group (`WWHD_TRUE60_GROUPS=+name,-name`, or a plain list; `all`, `none`):

| Group | Default | Contents |
|---|---|---|
| `loco` | on | Link: WAIT, FREE_WAIT, MOVE, ATN_MOVE, SIDE_STEP, FRONT_ROLL, BACK_JUMP, BACK_JUMP_LAND, AUTO_JUMP, LAND, FALL |
| `sword` | on | Link: CUT_A/F/R/L, CUT_EA/EB (combo finishers), CUT_EX_A/EX_B (parry attacks), CUT_KESA, CUT_TURN (spin attack), CUT_TURN_CHARGE, CUT_TURN_MOVE, CUT_REVERSE |
| `camera` | on | the follow camera (other camera modes stay at 30 Hz) |
| `items` | off | Link: boomerang, hookshot and bow aiming/throwing (not yet gated) |
| `swim`, `sail`, `bk`, `mo2`, `cc`, `ki` | — | reserved, nothing converted |

Everything else (other Link procedures, swimming, the ship, enemies, NPCs, objects) stays at
30 Hz and is drawn interpolated. Link drops to 30 Hz while moving collision carries him.
`WWHD_TRUE60_LINK=list` (decimal/0x hex procedure numbers) overrides the procedure list for tests.

### Gates (measured; scripted runs, 30 fps vs true 60 at every full step)

Test tools (local scripts, not in the repository): a pair script runs one scenario at 30 fps and with true
60 and compares Link dumps, camera, sounds, random numbers and the save-info block. Scenarios start
from save states made with the current build and run on the game's own step clock (Link's executes
after a confirmed state load: a load lands on a different pass from run to run).

| Scenario (8.5 s) | Procedures | Position | Animation frame | Sound starts | Random numbers | Save info |
|---|---|---|---|---|---|---|
| sword: single cut, 4-hit combo, spin attack (charge, move, release) | identical (11 changes) | 0.00 | 0.000 | 548 / 548, identical | 3 of 440 steps differ (from t=3.2 s) | identical |
| locomotion: run, roll, roll into a wall, turn | identical (11 changes) | 0.00 | 0.000 | 522 / 522; 2 random ambient ids differ | drift from t=1.6 s | identical |
| bomb put down, then a cut: hit mid-swing (LARGE_DAMAGE) | identical (8 changes) | 0.00 | 0.000 | 676 / 678; a random voice variant and ambient ids differ | drift from t=3.57 s | identical (life 11, bombs 9) |
| save-state load mid-swing | identical timeline, no crash | — | — | — | — | — |
| scene change (stage re-entered) mid-swing / mid-roll | identical until the change, no crash | 0.00 | — | — | — | identical |

The camera matches to 0.0000 in the sword scenario. The sound differences all come from randomly
chosen sounds and follow from the random-number deviation below.

- **True 60 off (gate 1)**: against the main build (6649e10), 30 fps: Link and camera traces
  identical (2615 lines), frames pixel-identical; interpolation mode: traces identical, frames
  differ in about 300 pixels of water sparkles.
- **Game save (gate 4)**: a save written through the pause menu after the same actions: cking.sav
  differs from the 30 fps one only at 0x1C..0x1F and 0xA8C..0xA97 (save time and play time), which
  also differ between two 30 fps runs; cking_playlog.sav identical.
- **Stability**: a 5-minute soak with random buttons and stick (sword, rolls, bombs, damage,
  swimming), mode switches (off, true 60, interpolation, true 60), a scene change and a state load:
  no crash, NaN or soft-lock.
- After a scene change the new scene starts at a different game step than at 30 fps: loading takes
  real time, and the game counts its frames while it waits.

### Known deviation: random-number sequence

**The converted logic consumes exactly the random numbers of
the 30 fps game, and the game state after every full pass is bit-identical; random numbers drawn
by drawing code are not.**

- **Cause.** Drawing code also draws from the shared generator cM_rnd: every object drawn with a
  point light (dScnKy_env_light_c::settingTevStruct_plightcol_plus) takes one value for the light
  flicker. With true 60 some objects at the edge of view are culled one step earlier by
  fopAcM_cullingCheck than at 30 fps (the 60 Hz rendering leaves state behind that the next full
  pass's culling uses; the inputs we could identify are restored and identical, the remaining one
  was not found). One object fewer drawn takes one value fewer, and from then on the sequence is
  shifted. In the tests it starts within seconds (running: t=1.6 s; after a bomb explosion).
- **What it affects.** Later random outcomes differ from what the 30 fps game would have
  produced, like in any differently played session: item drops, enemy decisions and timing, ambient
  sounds and animals, randomised minigame layouts. Quest and event logic is not random and is not
  affected. Tricks that manipulate the random sequence (speedrun RNG manipulation) may behave
  differently.
- **What it does not affect.** Link's and the camera's positions, procedures, timers, animation,
  hits, and the save data: all bit-identical at every full step (see the gates).
- Interpolation mode (key 6) has the same kind of drift, earlier (t=0.67 s in the same test):
  its half passes draw too.

### Hooked functions and sites

| WWHD | Function | Purpose |
|---|---|---|
| `025DF940` | fpcM_Execute | per-process gate, step length, preview snapshot, random-number hold |
| `025DF904` | fpcM_Draw | drawing process (no interpolation for 60 Hz processes); half-pass draw state of 30 Hz actors put back |
| `0203593C` (interp.cpp) | per-frame function | `pass_begin`: takes the previews back on full passes |
| `0200ECD4`…`0200F8D0` | cLib_addCalc*/chase* | ratio 1-(1-s)^dt, steps × dt (previews) |
| `027F2FC4` | J3DFrameCtrl::update | frame += rate × dt; whole step on full passes |
| `027F2BF8` | J3DFrameCtrl::checkPass | key frames on full passes only |
| `025E3EC8`, `025D67A8`, `025D6800` | morf counter, fopAcM_calcSpeed, fopAcM_posMove | × dt |
| `@023FCEB0`…`@023FD39C` | posMoveFromFootPos | foot speed, gravity, exact-arc position (previews) |
| `@0240D6D8`, `@0240D6F8`, `@0240D6FC` | Link's procedure call | skipped in previews; outputs recorded on full passes |
| `023F695C`…`023FB230`, `023FBCEC`, `023DBDD0`, `023FD4E4`, `023DC7AC` | Link's decision functions | fenced in previews |
| `025E14A8`, `025DFAB8`, `02821448`, `0200E240`, `0253EC0C`, `0253ED80`, `025F0658`, `025B8AF4`, `025B51DC` | creation, emitters, colliders, events, fade, save data | fenced in previews |
| `02593B10` | d_meter update | full passes only |
| `02715310` (interp.cpp) | HD UI manager: screen updates after fpcM_Management (TV pause screen at +0x1EC) | full passes only (issues #64, #74) |
| `025028B8` | dCamera_c::followCamera | marks the camera as following (60 Hz) |
| `024EF968` | dBgS::MoveBgCrrPos | Link riding moving collision: 30 Hz |
| `02018D40` | cM3dGSph::SetC | NaN centre in a camera step: skipped, step undone |

Debug aids: `WWHD_LINK_TRACE`, `WWHD_CAM_TRACE`, `WWHD_ACTOR_DUMP=path:FN|link:SIZE`,
`WWHD_RNG_TRACE`, `WWHD_RND_LOG`, `WWHD_SE_TRACE`, `WWHD_PAD_TRACE`, `WWHD_CULLLOG`, `WWHD_TEVLOG`,
`WWHD_LIGHT_TRACE`, `WWHD_WATCH_LINK` / `WWHD_WATCH_ADDR`, `WWHD_T60_MEMDIFF`, `WWHD_T60_REGIONLOG`,
`WWHD_T60_DRAWWRITE`, `WWHD_T60_PAGEHASH`; test scenarios: `WWHD_TEST_ORIGIN_LOAD`, `WWHD_TEST_POKE`
(e.g. `0.1:*101F84DC+2E:383B` equips sword and shield), `WWHD_TEST_MODES`, `WWHD_TEST_LOAD`,
`WWHD_TEST_SCENECHANGE`, `WWHD_TEST_TOUCH`, `WWHD_SAVEINFO_DUMP`. All are inactive unless set.

### Layouts used (WWHD; generated code)

| Structure | Field | Offset | Evidence |
|---|---|---|---|
| daPy_lk_c | process size | 0x8284 | profile g_profile_PLAYER (101CEC74 method table) |
| daPy_lk_c | mCurProc / proc member pointer | +0x65F0 / +0x65F4 | commonProcInit; execute 0240D6B4 |
| daPy_lk_c | m34C2 / mResetFlg0 / m3700 / m_old_fdata | +0x68DE / +0x3C0 / +0x7308 / +0x65CC | execute 0240D370, 0240D300; posMove 023FDF9C |
| fopAc_ac_c | heap / culling matrix | +0xF4 / +0x348 | fopAcM_cullingCheck 025D6D08 |
| camera process | size | 0xB28 | profile (name 0x1DC) |
| save info | pointer | *101F84DC | dComIfGs_setSelectEquip; status A at +0x20, select equip +0x2E, items +0x5C, item record +0x86 |
| play state | g_dComIfG_gameInfo.play | 1046F0B0 | select items +0x5BBB (X, Y, Z), start/next stage +0x5134/+0x5140 |


### Per-step logic in the other actors (survey)

`tools/true60/actor_survey.py [d_a_]` scans the generated code of every named actor function and
writes `build/true60_survey.tsv`, one line per GameCube source file. It reports:
- calls to the primitives that true60 already scales;
- inline step counters: a 16/32-bit field loaded, ±1, stored back. The offset is from the base
  register, usually `this`; Link's code often uses `this+0x448`. Example: Link's `-0x652A`
  is `m3522` at +0x6972;
- inline exponential smoothing (smooth_sites.py);
- stores to the position/speed fields of `fopAc_ac_c` outside the primitives.

The counter fields cross-check with the timer catalogue where both exist. Example: d_a_bk
(Bokoblin) `m034C` → +0x464 in both.

| Category | Files | Functions | Primitive calls | Inline step counters | Pos/speed stores | Files with none of these |
|---|---|---|---|---|---|---|
| Link (d_a_player_*) | 3 | 804 | 248 | 27 | 338 | 0 |
| NPCs (d_a_npc_*) | 60 | 3223 | 805 | 51 | 627 | 0 |
| Objects (d_a_obj_*) | 142 | 2062 | 363 | 84 | 211 | 61 |
| Enemies, bosses, effects, items (other d_a_*) | 218 | 3363 | 3393 | 335 | 2351 | 34 |
| Tags/triggers (d_a_tag_*) | 20 | 247 | 5 | 7 | 0 | 13 |
| d_camera.cpp (for comparison) | 1 | 87 | 5 | 6 | 6 | — (134 inline smoothing sites) |

What this means for converting them:
- **Smoothing is solved generically.** Actor code smooths almost only through the cLib primitives:
  0 inline smoothing sites in all d_a_* files, against 134 in d_camera.cpp.
- **Inline physics.** The work per actor is its inline physics: `pos += speed`, `speed.y -= g` and
  similar written out instead of calling `fopAcM_posMove`/`calcSpeed` (3,527 stores, part of them
  initialisation in create/init functions).
- **Timer arrays and phase checks.** Examples: `mTimers[4]`, `g_Counter.mCounter0 & 0x3F`,
  per-step random chances.
- **Seagull example.** d_a_kamome has 56 primitive calls, but also about ten inline position/speed
  updates (`pos += speed`, `pos.y += 5`, `speed.y -= 3`, `speed.y -= 0.8`):
  - d_a_kamome.cpp:206–212, 245–249, 575–577, 827–829;
  - a timer array `mTimers[4]` plus `mRiseTimer`;
  - a timer bit test (`mGlobalTimer & 0x3F`);
  - several per-step random chances.

Most active files (score = primitive calls + 5 × counters + pos/speed stores):

| File | Functions | Primitive calls (already scaled) | Inline step counters (WWHD offsets) | Pos/speed stores | Catalogued timers (field, WWHD offset) |
|---|---|---|---|---|---|
| d_a_player_main.cpp | 786 | 246 | s16-0x3B0, s16+0x3FC, s16-0x424, s32+0x458, s16-0x5B60, s16-0x5B68, s16-0x5B6C, s16-0x652A… | 335 | m34D0 0x6916, m34D2 0x6918, m34D6 0x691C, m34DA 0x6920, m3526 0x6976, m355C 0x69… |
| d_a_bpw.cpp | 32 | 225 | s16+0x562, s16+0x568, s16+0x57E, s16+0x59A, s16-0x59A | 131 | — |
| d_a_gm.cpp | 19 | 90 | s16+0x3F6, s16+0x3F8, s16+0x40C, s16+0x40E, s16+0x410, s16+0x412, s16+0x43A, s16+0x43E | 117 | — |
| d_a_bdk.cpp | 27 | 135 | s16+0x3DC, s16-0x40A, s16-0x40C, s16+0x122C, s16-0x1234, s16+0x269C, s16+0x26A0, s16+0x26A… | 64 | — |
| d_a_bk.cpp | 50 | 93 | s32+0x410, s32+0x414, s16-0x424, s16-0x42C, s16-0x44E, s16-0x464, s16+0x4A0, s16-0x95A, s1… | 90 | m034C 0x0464 |
| d_a_bst.cpp | 25 | 161 | s16+0x1308, s16+0x130E, s16-0x133C, s16-0x30B8, s16+0x30D0 | 43 | — |
| d_a_ship.cpp | 77 | 97 | s16-0x662, s16+0x662, s16+0x66E, s16-0x5B6C | 94 | — |
| d_a_mo2.cpp | 43 | 75 | s16+0x594, s32+0x840, s32+0x844, s16-0x856, s16-0x85A, s16-0x85E, s16-0x860, s16-0x882, s1… | 49 | — |
| d_a_mt.cpp | 19 | 75 | s16-0x466, s16-0x580, s16+0x582, s16+0x586, s16+0x588, s16+0x5AC, s16+0xD1E, s16-0x1A18, s… | 63 | — |
| d_a_ph.cpp | 23 | 110 | s16+0x462, s16-0x46C, s16+0x47A, s16-0x48E | 57 | — |
| d_a_bgn.cpp | 24 | 125 | s16-0x284, s16-0x4BC, s16+0xA32, s16-0xAA2, s16-0xD4C, s16+0xD50, s16+0x2296 | 21 | — |
| d_a_npc_md.cpp | 135 | 60 | s16-0x4260, s16-0x4262, s16-0x4266 | 105 | m3144 0x4260, m314A 0x4266 |
| d_a_tn.cpp | 46 | 55 | s16+0x43A, s32+0x4F0, s16-0x502, s16-0x506, s16-0x536, s16-0x54C, s16+0x594, s16+0x8C0, s1… | 62 | — |
| d_a_bgn2.cpp | 15 | 72 | s16-0x62A, s16-0x648, s32+0x2028, s16-0x3058 | 49 | — |
| d_a_npc_so.cpp | 87 | 44 | s32+0xCEC, s32+0xCF0, s32+0xCF4, s32+0xCF8, s32+0xD34 | 70 | — |
| d_a_kb.cpp | 26 | 45 | s16+0x768, s16+0x776, s16+0x780, s16-0x782, s16-0x78C, s16+0x790, s16-0x792 | 58 | — |
| d_a_nz.cpp | 24 | 37 | s16-0x3DA, s16+0x3DC, s16-0x3E8, s16+0x3EC, s16+0x3EE, s16+0x3FE | 69 | — |
| d_a_am2.cpp | 15 | 62 | — | 71 | — |
| d_a_bwd.cpp | 11 | 83 | s32+0x580, s16+0x1B30, s16-0x1B5A, s16+0x3E98 | 23 | — |
| d_a_pw.cpp | 28 | 45 | s16+0x484, s16-0x4A0, s16-0x4B4, s16+0x57E | 57 | — |
| d_a_fganon.cpp | 21 | 59 | s16+0x5B8, s16+0x5BC, s16+0xD88, s16+0xD8A | 37 | — |
| d_a_kamome.cpp | 17 | 56 | s32+0x3BC, s16+0x3CC | 50 | — |
| d_a_btd.cpp | 24 | 85 | s16+0x40A, s16+0x40E, s16-0x41C, s16-0x6208, s16+0x705A | 4 | — |
| d_a_wz.cpp | 18 | 41 | s16+0x4FA, s16-0x510, s16+0x512 | 56 | — |
| d_a_cc.cpp | 24 | 30 | s16+0x40E | 77 | — |
| d_a_kanban.cpp | 10 | 20 | s16+0x3DC | 83 | — |
| d_a_npc_ji1.cpp | 100 | 70 | s32-0xE50, s32-0xE54, s32+0xF90, s32+0xF98, s32+0xF9C | 11 | field_0xC30 0x0E54 |
| d_a_ks.cpp | 14 | 17 | s16-0x43A, s16+0x56C, s16-0x57C, s16+0x57C, s16+0x57E, s16+0x69EE, s32+0xFFFF8840, s32-0xF… | 49 | — |
| d_a_bb.cpp | 23 | 46 | s16-0x49A, s16+0x4C6, s16+0x4C8 | 44 | — |
| d_a_bl.cpp | 21 | 26 | s16+0x422 | 69 | — |

## GX2CopySurface completes before it returns

The game relies on `GX2CopySurface` being finished when the call returns. agl's tile-mode conversion
(027B5EEC, during boot on the Prepare Thread) allocates a temporary surface, copies into it with
`GX2CopySurface`, immediately copies it back with `OSBlockMove` and frees it. When the port only queued
the copy for the render thread, a late copy could land after the heap had reused that memory (for agl's
shader program array), which crashed boot intermittently (SIGBUS at guest address 4 in 027B90AC).

`HLE(gx2, GX2CopySurface)` therefore calls `render_sync` after queueing the copy, unless a display list is
being recorded (display lists run when called). This applies to both backends. It costs about 50 syncs
during boot and none during gameplay (`WWHD_SYNC_STATS=1`, site "CopySurface").

Debug aids, all off by default:
- `WWHD_GX2_DELAY_COPY=ms` (gx2_core.cpp): the render thread waits before each copy; reproduces the
  crash on a build without the sync;
- `WWHD_COPYDBG=1`: logs each GX2CopySurface (thread, lr, images) and each CPU-path execution;
- in true60_test.cpp: `WWHD_BOOTDBG=1`, `WWHD_BOOTDBG_SLOW=ms`, `WWHD_BOOTDBG_PROT=1` (write-protects the
  first program array's page and logs the writing thread) and `WWHD_HEAPLOG=1`.

## Effects interpolation (runtime/src/interp_fx.cpp)

Per-step work that the game runs from drawing code (so on hold passes too, i.e. twice per logic
step with interpolation on) and how it is handled:

| WWHD | What | Hold pass | Logic pass (halfway) |
|---|---|---|---|
| `025AF8A0` `dScnPly_Draw` | calls particle calc, grass/tree/wood/flower calc, `g_Counter.mTimer++` (`101FF560`, right after `025A8148`), Bgsp Move, dSnap | (see rows below) | |
| `0282167C` `JPAEmitterManager::calc(group)` | particles (from `025A81A0` groups 0-6, `025A8148` 7-8, `025A81F8` 9-12) | skipped | particle position `+0x28` and size `+0x9C` halfway, restored at the next pass start |
| `027DF40C` key animator evaluation | btk/brk/... material animations; called from Draw on every pass (btk entry `025E7FC4`, brk entry `025E83FC`, daBg, items, ...) | frame recorded | evaluated at the halfway frame |
| `027F2FC4` `J3DFrameCtrl::update` | frame advance (e.g. `daSalvage_c::_draw` plays its btk/brk in Draw) | skipped | |
| `0246C5E8` sea vertex grid, `0246C7C8` sea material | heights/origin from `execute` (logic only); scroll counter `+0x22C` +1 per Draw | counter held | halfway heights/origin; scroll `(n-0.5)/300` |
| `0256A448` `wave_move` | wave crest sprites, environment Execute | | sprite position/scale/alpha halfway |
| `0254C6C4` / `025C9948` / `02548370` grass/tree/flower calc | sway from `g_Counter.mTimer` | mTimer increment undone | calc with mTimer+1, slots halfway |
| `025D0994` `dWood::Packet_c::calc` | bush sway (72 `Anm_c` at packet `+0x1C8D8`, 0x8C apart, sway matrix `+0`, trunk `+0x30`) | (world block) | animation matrices halfway |
| `0281FE40` `JPABaseEmitter::calc` | per emitter: centre/scale from the static emitter info (`104B5730` `+0xE0` / `+0xEC`) | | particles born this step: half a step back (velocity × scale + emitter movement) |
| rain `02566B88`, snow `02568BD4`, ash `02569408`, spores `02567F68`, fog `0256BB6C`, poison `0256CA54`, sky clouds `0256DDF8`, stars `0256A388` | environment Execute (logic only); packets from `g_env_light` (`10475A68`) `+0xA44/+0xA50/+0xA78/+0xA84/+0xA6C/+0xA94/+0xA60` (GameCube offset + 0x80); record arrays at GameCube offset + 0x84/0x88 | | every float field of records that did not respawn, halfway |
| `0251D864` cloth vertex fill | `dCloth_packet_c` (`+0x98/+0x9C` grid, `+0xB0/+0xB8/+0xC0` pos/nrm/back-nrm buffers [2], `+0x1C0` current) | | halfway between the two buffers after a simulation step |
| `024EDDF8` `dAttention_c::Draw`, `024EC1C8` `dAttDraw_c::draw` | attention arrow: a 4-joint J3D model at the target's attention position, facing the camera (inverse camera rotation); animation (`mDoExt_McaMorf` at `dAttDraw_c +0`, frame control `+0x98`, frame `+0x9C`) played by `dAttention_c::Run` in the play scene's execute (30 Hz) | | interpolation: model blending (viewCalc). True 60: full passes draw it unblended from the halfway target position and frame and the current (60 Hz) camera |

Still run on hold passes from `dScnPly_Draw` (with true60.cpp's site_025B00B0): only drawing
(actor draws, grass/tree/wood/flower/magma draw, attention draw), `MassClear`, the menu particle
calc (held back by the particle hook) and the HD call `0271DF30` (only when `+0xB64 == 2`).

Particle fields (measured, WWHD = GameCube): `+0x8C` axis, `+0x98` scale out, `+0x9C/+0xA0` size,
`+0xAC` alpha, `+0xB0` alpha wave, `+0xB4` loop offset, `+0xB8` prim colour, `+0xBC` env colour,
`+0xC0` rotation angle (u16), `+0xC2` rotation speed. Emitter: `+0x22C` global translation,
`+0x238` global particle scale.

Layouts used:

- `JPAEmitterManager`: `+0x50 + 12*group` emitter list (JSUPtrList; link `+0` object, `+0xC`
  next). `JPABaseEmitter`: `+0x1AC` particles, `+0x1B8` children, `+0x194` tick. `JPABaseParticle`
  matches the GameCube layout up to 0xD0 (`+0x10` offset, `+0x1C` local, `+0x28` global position,
  `+0x34` velocity, `+0x78` age, `+0x8C` draw params (`+0x9C/+0xA0` size), `+0xC8` callback,
  `+0xCC` status); WWHD adds a delay byte at `+0x10C`.
- Key animator: `*(this+0)` time block: `+0` frame, `+4` start, `+8` end, `+0x10` frame-mapping
  function (`027DA9E8` loop, `027DAAA0` clamp); `this+0x28` cached frame.
- `J3DFrameCtrl` (no vtable in WWHD): `+0` rate, `+4` frame, `+8` start, `+0xA` end, `+0xE`
  attribute, `+0xF` state. `mDoExt_baseAnm` starts with it; btk anm at `+0x68`, brk at `+0x10`.
- `daSea_packet_c` (static instance `1046D8D0`): `+0x130` flat inter, `+0xF0` wave info
  (`+0x114` cur scale), `+0x1FC/+0x200` grid min x/z, `+0x20C` height table (65x65 f32),
  `+0x22C` scroll counter, `+0x250` double-buffered vertex buffers (index `+0x4A8`).
  Draw `0246CAEC` (actor Draw) builds the vertices (`0246C5E8`) and the material (`0246C7C8`).
- `dKankyo_wave_Packet`: `WAVE_EFF[300]` at `+0xA0`, 0x38 each (GameCube layout).
- Sway slots: grass `+0x18F0C` (0x38 apart, s16 `+4`), tree `+0x2A9C` (0x84, `+4/+6`), flower
  `+0x35BC` (0x38, `+4`).
- Billboards: J3D billboard shapes get their view-space matrices in viewCalc (`027DE8A0` →
  `027DB590`, view matrix from `104B45F8`), i.e. from the blended camera and world matrices.

## True 60 fps: per-step primitives

The game advances its logic one step per 30 Hz frame. Running logic at 60 steps/s means halving
every per-step rate below (or calling it with half steps). These are the shared primitives that
actors use everywhere. "Evidence" is the matcher's tag (p = model probability). All are in
`build/names.tsv`.

### Smoothing and chasing (`SSystem/SComponent/c_lib.cpp`)

`addCalc` moves `*value` by a fraction (`scale`) of the remaining distance per step, so at 60
steps/s the fraction needs `1-(1-s)^(1/2)`. `chase` moves by a fixed `step` per step, so it
needs half the step.

| GameCube | WWHD | Evidence | Source |
|---|---|---|---|
| `cLib_addCalc` | `0200ECD4` | graph (p=0.78) | src/SSystem/SComponent/c_lib.cpp:22 |
| `cLib_addCalc2` | `0200ED84` | tu (p=0.99) | c_lib.cpp:56 |
| `cLib_addCalc0` | `0200EDC8` | tu (p=0.65) | c_lib.cpp:69 |
| `cLib_addCalcPos` | `0200EE00` | graph (p=0.79) | c_lib.cpp:80 |
| `cLib_addCalcPosXZ` | `0200EF78` | graph (p=0.96) | c_lib.cpp:105 |
| `cLib_addCalcPos2` | `0200F164` | graph (p=0.99) | c_lib.cpp:133 |
| `cLib_addCalcPosXZ2` | `0200F268` | tu (p=0.64) | c_lib.cpp:145 |
| `cLib_addCalcAngleS` | `0200F378` | tu (p=0.99) | c_lib.cpp:160 |
| `cLib_addCalcAngleS2` | `0200F428` | tu (p=1.00) | c_lib.cpp:192 |
| `cLib_addCalcAngleL` | `0200F474` | graph (p=0.67) | c_lib.cpp:205 |
| `cLib_chaseUC` | `0200F4FC` | graph (p=0.88) | c_lib.cpp:235 |
| `cLib_chaseS` | `0200F564` | graph (p=0.99) | c_lib.cpp:259 |
| `cLib_chaseF` | `0200F5C8` | graph (p=0.82); checked by hand against the source | c_lib.cpp:276 |
| `cLib_chasePos` | `0200F62C` | graph (p=0.97) | c_lib.cpp:293 |
| `cLib_chasePosXZ` | `0200F764` | graph (p=0.97) | c_lib.cpp:309 |
| `cLib_chaseAngleS` | `0200F8D0` | graph (p=1.00) | c_lib.cpp:326 |
| `cLib_calcTimer<T>` | one copy per actor TU; named: `<Uc>` `0207A9A0` (+1 copy), `<s>` `02055B64`, `<i>` `0211D2F8` (+1 copy), `<Us>` `0223556C`, `<Sc>` `0220CCB4` (legacy) | dup-graph / graph (p=1.00) / dup-graph / not named / callgraph-legacy | include/SSystem/SComponent/c_lib.h (template: `if (*t != 0) --*t`) |

The c_lib unit keeps the GameCube function order in WWHD (`0200ECD4`–`0200FCD8`), which
cross-checks the names above. Many `cLib_calcTimer` calls are inlined by GHS, so timers also appear
as plain decrements (see the catalogue below).

### Movement and per-step physics

| GameCube | WWHD | Evidence | Source |
|---|---|---|---|
| `fopAcM_calcSpeed` (speedF + gravity → speed, clamps at maxFallSpeed) | `025D67A8` | graph (p=0.85) | src/f_op/f_op_actor_mng.cpp:454 |
| `fopAcM_posMove` (pos += speed) | `025D6800` | graph (p=0.87) | f_op_actor_mng.cpp:469 |
| `fopAcM_posMoveF` (calcSpeed + posMove) | `025D6870` | tu (p=1.00) | f_op_actor_mng.cpp:482 |
| `dBgS_Acch::dBgS_Acch` | `024F0474` | graph (p=0.93) | src/d/d_bg_s_acch.cpp:50 |
| `dBgS_Acch::Set` | `024F06B4` | graph (p=0.99) | d_bg_s_acch.cpp:92 |
| `dBgS_Acch::CrrPos` (per-step ground/wall/roof correction) | `024F08A8` | assert | d_bg_s_acch.cpp:209 |
| `dBgS_Acch::GroundCheckInit` | `024F034C` | graph (p=0.77) | d_bg_s_acch.cpp:109 |
| `dBgS_Acch::GroundCheck` | `024EFF50` | graph (p=0.95) | d_bg_s_acch.cpp:122 |
| `dBgS_Acch::GroundRoofProc` | `024F03A0` | graph (p=0.78) | d_bg_s_acch.cpp:156 |
| `dBgS_Acch::LineCheck` | `024F00CC` | callgraph-legacy | d_bg_s_acch.cpp:175 |
| `dBgS_Acch::CalcWallBmdCyl` | `024F1134` | not named in the final run (earlier runs: graph (p=0.68)) | d_bg_s_acch.cpp:350 |
| `dBgS_Acch::SetGroundUpY` | `024F12A8` | callgraph-legacy | d_bg_s_acch.cpp:378 |
| `daObj::posMoveF_*` (object physics: resist, grade, stream) | `02311AB8`, `023121C4`, `023123C0` | graph (p=0.65) / tu (p=0.65) / graph (p=0.65) | src/d/actor/d_a_obj.cpp |

The fields involved are in fopAc_ac_c and moved by +0x11C in WWHD (see layouts): `speedF`
0x254→**0x370**, `gravity` 0x258→**0x374**, `maxFallSpeed` 0x25C→**0x378**, `speed` (cXyz)
0x220→**0x33C**, `current.pos` 0x1F8→**0x314**, `current.angle` 0x204→**0x320**, `shape_angle`
0x20C→**0x328**, `old` 0x1E4→**0x300**.

### Animation frame advance

| GameCube | WWHD | Evidence | Source |
|---|---|---|---|
| `J3DFrameCtrl::init` | `027F2BC0` | graph (p=0.83) | src/JSystem/J3DGraphAnimator/J3DAnimation.cpp:13 |
| `J3DFrameCtrl::checkPass` | `027F2BF8` | graph (p=0.82) | J3DAnimation.cpp:24 |
| `J3DFrameCtrl::update` (frame += rate, loop/stop modes) | `027F2FC4` | callgraph-legacy | J3DAnimation.cpp:143 |
| `mDoExt_baseAnm::initPlay` | `025E72D4` | tu (p=0.90) | src/m_Do/m_Do_ext.cpp:85 |
| `mDoExt_baseAnm::play` | `025E742C` | graph (p=0.80) | m_Do_ext.cpp:116 |
| `mDoExt_McaMorf::setAnm` | `025E4A98` | tu (p=1.00) | m_Do_ext.cpp:1414 |
| `mDoExt_McaMorf::setMorf` (morf length → per-step morf rate) | `025E4A54` | graph (p=0.74) | m_Do_ext.cpp:1452 |
| `mDoExt_McaMorf::play` (frame advance + morf counter) | `025E535C` | graph (p=1.00) | m_Do_ext.cpp:1463 |
| `mDoExt_McaMorf::calc()` | `025E55A0` | graph (p=0.77) | m_Do_ext.cpp |
| `mDoExt_McaMorf2::setMorf` / `play` | `025E5D1C` / `025E65FC` | tu (p=0.60) / callgraph-legacy | m_Do_ext.cpp:1799 / 1810 |
| `daPy_lk_c::setFrameCtrl` (Link's per-anime rate) | `023DE788` | tu (p=0.79) | src/d/actor/d_a_player_main.cpp:2937 |

Not identified: `mDoExt_McaMorf::calc(u16)` and `mDoExt_McaMorf2::calc(u16)`, the per-joint morf
blend. They are J3DMtxCalc callbacks on GameCube, and WWHD restructured J3D. The candidates
`025E410C` (1448 bytes) and `025E46B4` sit between `mDoExt_MtxCalcOldFrame::initOldFrameMorf`
and `setMorf` and use 0.5/1.5/1e-4 constants (quaternion blend), but are not confirmed. The
J3D math they call (`JMAEulerToQuat`, `JMAQuatLerp`, `PSMTXQuat` on GameCube) lives at
`027E0710`/`027ED3AC`, inside the HD library block.

### Particles

| GameCube | WWHD | Evidence | Source |
|---|---|---|---|
| `JPAEmitterManager::calc` (all emitters, per step) | `0282167C` | tu (p=0.62) | src/JSystem/JParticle/JPAEmitterManager.cpp:84 |
| `JPABaseEmitter::calc` | `0281FE40` | tu (p=0.97) | src/JSystem/JParticle/JPAEmitter.cpp:230 |
| `JPABaseEmitter::calcEmitterInfo` | `0281F688` | graph (p=0.64) | JPAEmitter.cpp:200 |
| `JPABaseEmitter::calcCreatePtcls` (emission rate per step) | `0281F878` | tu (p=0.99) | JPAEmitter.cpp:251 |
| `JPABaseEmitter::calcParticle` | `0281FBD8` | not named in the final run (earlier runs: callgraph-legacy) | JPAEmitter.cpp:319 |
| `JPABaseEmitter::calcChild` | `0281FD14` | not named in the final run (earlier runs: callgraph-legacy) | JPAEmitter.cpp:345 |
| `JPABaseEmitter::calcKey` | `0281F42C` | tu (p=0.96) | JPAEmitter.cpp:370 |
| `JPAEmitterManager::JPAEmitterManager` / `createSimpleEmitterID` | `02820DE4` / `02821448` | assert / assert | JPAEmitterManager.cpp |

### Camera

| GameCube | WWHD | Evidence | Source |
|---|---|---|---|
| `camera_execute` | `024FFA3C` | manual | src/d/d_camera.cpp:5426 |
| `camera_draw` | `024FFC40` | manual | d_camera.cpp:5453 |
| `dCamera_c::Run` (per-step camera update; mode dispatch) | `024FE3E8` | graph (p=0.99) | d_camera.cpp:676 |
| `dCamera_c::NotRun` | `024FF6C0` | graph (p=0.90) | d_camera.cpp:917 |
| `dCamera_c::followCamera` (main smoothing) | `025028B8` | graph (p=1.00) | d_camera.cpp:2618 |
| `dCamera_c::lockonCamera` | `025052E8` | tu (p=0.94) | d_camera.cpp:3291 |
| `dCamera_c::talktoCamera` | `02508A60` | tu (p=1.00) | d_camera.cpp:3677 |
| `dCamera_c::eventCamera` | `024FF164` | strings | d_camera.cpp:4679 |
| `dCamera_c::shakeCamera` / `StartShake` | `024FC108` / `02515298` | strings / callgraph-legacy | d_camera.cpp:4989 / 5118 |

The camera's smoothing goes through the cLib_addCalc* family above and through `d_cam_param.cpp`
(14 of 28 matched).

### Events, demos, cutscene timelines

| GameCube | WWHD | Evidence | Source |
|---|---|---|---|
| `dEvt_control_c::check` / `checkStart` / `order` | `0253FF34` / `0253FD1C` / `0253EC0C` | tu (p=0.89) / graph (p=0.91) / graph (p=0.90) | src/d/d_event.cpp |
| `dEvent_manager_c::runProc` | `02544374` | callgraph-legacy | src/d/d_event_manager.cpp |
| `dDemo_manager_c::create` (`mControl->forward(0)`) | `02528CEC` | callgraph-legacy | src/d/d_demo.cpp:659 |
| `dDemo_manager_c::update` (`mControl->forward(1)`, `mFrame++`) | not identified, probably inlined into its caller in WWHD | | d_demo.cpp:695 |
| `JStudio::stb::TControl::forward` | not identified | | JSystem/JStudio |

### Global counters and the main loop

| GameCube | WWHD | Evidence | Notes |
|---|---|---|---|
| `cCt_Counter` | `0200E6EC` | manual | `g_Counter` is at **`101FF558`**: `mCounter0` +0 (always ++), `mCounter1` +4 (reset or ++), `mTimer` +8. Same logic as c_counter.cpp:12 |
| `fpcM_Management` | `025DF948` | manual | runs every process (actor) step |
| `fapGm_Execute` | `025D42EC` | graph (p=0.74) | game main loop step |
| `dScnPly_Execute` | `025B0314` | vtable | play scene step |

Many actors use `g_Counter.mCounter0` as a frame clock (for example `daBomb_c`, `daBoko_c`,
`dKyr_wind_move`). On 60 steps/s these phase checks (`mCounter0 & 1`, `% N`) change rate too.

## Link (daPy_lk_c) and camera coverage

- `d_a_player_main.cpp`: 783 of 934 GameCube functions matched (84%). Link's procedures come
  mostly from his PTMF procedure tables (`vtable` evidence) plus `tu`/`graph`. Some key ones:
  `execute` `0240CDD0`, `posMove` `023FDA70`, `autoGroundHit` `023FE2F0`, `setNormalSpeedF`
  `02416230`, `setSpeedAndAngleNormal` `0241650C`, `setSpeedAndAngleAtn` `02417538`,
  `setSpeedAndAngleAtnActor` `02416B70`, `setSpeedAndAngleSwim` `0242ECFC`, `commonProcInit`
  `023DFDD8`, `setFrameCtrl` `023DE788`, `setBlendMoveAnime` `023E14A0`, `setWorldMatrix`
  `023FF6A0`, `draw` `02443EC4`. The full list is in `build/names.tsv`.
- Camera: `d_camera.cpp` 87/176, `d_ev_camera.cpp` 28/46, `d_cam_param.cpp` 14/28.

Key daPy_lk_c fields (GameCube → WWHD): `mCurProc` 0x31D8→0x65F0, `mStickDistance` 0x35B0→0x6A08,
`mNormalSpeed` 0x35BC→0x6A14, `mModeFlg` 0x3618→0x6A70, `mpSeAnmFrameCtrl` 0x363C→0x6A94,
`mFrameCtrlUnder[2]` 0x302C→0x5898, `mEquipItem` 0x3560→0x69B0, `mTinkleHoverTimer`
0x354C→0x699C, the timer union `m34D0..m34DA` 0x34D0→0x6916 (+0x3446). The fopAc_ac_c base fields
(speedF, gravity, ...) are listed above.

## Frame-count timers (catalogue)

`tools/decomp/timers.py` lists fields that are decremented by one (load, `addi -1`, store back)
or passed to `cLib_calcTimer`. It covers code reachable from an actor's Execute (method table,
`execute` methods, PTMF procedure tables of the same file) in the GameCube build. Each entry gets
its WWHD offset when the matched WWHD function has the same number of decrement sites.

Current output (`build/timers.tsv`): 510 sites in 164 classes; 131 have a WWHD offset.
Examples:

| Class | Field (GameCube → WWHD) | Where |
|---|---|---|
| daPy_lk_c | `m34D0` 0x34D0→0x6916 (proc timer, 19 procs) | procAutoJump, procCutEA, procGuardSlip, procLadderMove, ... |
| daPy_lk_c | `m34D2` 0x34D2→0x6918 | procFall, procAutoJump, procVomitJump |
| daPy_lk_c | `m34D6` 0x34D6→0x691C, `m34DA` 0x34DA→0x6920 | procTactPlayOriginal, procTactPlay |
| daPy_lk_c | `m3526` 0x3526→0x6976 | changeSlideProc |
| daPy_lk_c | `mTinkleHoverTimer` 0x354C→0x699C | autoGroundHit |
| daPy_lk_c | `m355C` 0x355C→0x69AC, `m355E` 0x355E→0x69AE | checkNextActionHookshotReady / BowReady |
| dCamera_c | `mBlureTimer` 0x590→0x594 | shakeCamera |
| sub_meter_class (HUD) | `mAdjustHp` 0x300A, `mCurrHP` 0x301F, alpha timers 0x2FD8/0x3014 | dMeter_lifeChange, dMeter_heartAlpha (HUD rewritten in HD) |

Limits: timers updated through other patterns (`if (t) t--` through a temporary pointer, or
through the cLib_calcTimer template with the address in a non-argument register) are missed.
Free functions on `xxx_class*` count as that class.

### The shared mini-game timer (issue #105)

The shared TIMER process (letter sorting on Dragon Roost, the Windfall auction, the boat race, the
volcano and Orca training challenges) installs `025C5A8C` (`dTimer_c::_execute`, EU `025C5A4C`) in its
**draw** method slot (`101EE73C + 0x10`), so it runs through `fpcLf_Draw`, not the execute queue.
Interpolation and true 60 hold actor execution on in-between frames but still draw, so the countdown
at `this + 0x10C` ran 2× fast at 60 fps and 4× at 120. `runtime/src/countdown.cpp` runs the callback
on full game steps only and refreshes its text between steps (through the HD UI hook `02715310`) so the
display stays smooth. Actor-local countdowns (`OBJ_TIMER` at `023A232C`, the flight challenge) already
run in execute and were correct. `runtime/tools/countdown_scenario.py` checks timer ticks per game step
in all frame modes (it needs a local checkpoint in a running mini-game; `--check-trace` checks a trace
written with `WWHD_COUNTDOWN_TRACE`).

## Structure layouts (GameCube → WWHD)

`tools/decomp/layout.py` aligns the loads/stores through `this` (or the first argument) of every
matched pair. Each aligned pair votes GameCube offset → WWHD offset. Field names come from the
`/* 0x... */` comments in the decompilation headers.

**fopAc_ac_c** (3,942 pairs; all actors' base class):

| GameCube | WWHD | Shift | Fields |
|---|---|---|---|
| 0x000–0x0C0 | same | 0 | leafdraw_class base; `actor_type` 0xC0 |
| 0x0C4–0x10C | +4 | +0x4 | `actor_tag`; `draw_tag` 0xD8→0xDC; `heap` 0xF0→0xF4; `eventInfo` 0xF4→0xF8; `tevStr` 0x10C→0x110 |
| tevStr | 0xB0 → 0x1C8 bytes | | dKy_tevstr_c grew by 0x118 (HD lighting) |
| 0x1BC–0x290 | +0x11C | +284 | `setID` 0x1BC→0x2D8, `demoActorID` 0x1C0→0x2DC, `actor_status` 0x1C4→0x2E0, `actor_condition` 0x1C8→0x2E4, `parentActorID` 0x1CC→0x2E8, `home` 0x1D0→0x2EC, `old` 0x1E4→0x300, `current` 0x1F8→0x314, `shape_angle` 0x20C→0x328, `scale` 0x214→0x330, `speed` 0x220→0x33C, `cullMtx` 0x22C→0x348, `cullSizeFar` 0x248→0x364, `jntHit` 0x250→0x36C, `speedF` 0x254→0x370, `gravity` 0x258→0x374, `maxFallSpeed` 0x25C→0x378, `eyePos` 0x260→0x37C |
| sizeof | 0x290 → ~0x3AC | | derived actor members start ~0x11C later |

**daPy_lk_c** (485 pairs). The shift grows piecewise through the class: +0x11C (base) →
+0x3A0 (from about 0x3DC: daPy_py_c/early members grew) → +0x15A8 (0x2E98 `mpEquipItemModel`) →
+0x286C (0x2FDC `m_anm_heap_under`) → +0x3414..+0x3458 (0x317C onwards: `mActorKeepEquip`
0x317C→0x6590, `mCurProc` 0x31D8→0x65F0, `mDirection` 0x34B8→0x68D4, `mItemButton`
0x34C9→0x690E, the s16 timers 0x34D0→0x6916, `mEquipItem` 0x3560→0x69B0, `mNormalSpeed`
0x35BC→0x6A14, `mModeFlg` 0x3618→0x6A70). Run `layout.py ... daPy_lk_c` for the full list.

**view_class / camera_process_class** (from `camera_draw`, by hand): +4 from 0xC4 on.
`mNear` 0xC8→0xCC, `mFar` 0xCC→0xD0, `mFovy` 0xD0→0xD4, `mAspect` 0xD4→0xD8, `mLookat` 0xD8→0xDC
(eye 0xDC, center 0xE8, up 0xF4), `mBank` 0xFC→0x100. camera_process_class `mCamera`
(dCamera_c) 0x244→0x248.

**dCamera_c** (43 pairs): 0x000–0x130 unchanged (`mFovy` 0x38, `mPadId` 0x124,
`mpPlayerActor` 0x128, `mLockOnActorId` 0x130); +4 around 0x50C (`mEventFlags` 0x50C→0x510,
`mBlureTimer` 0x590→0x594).

**dBgS_Acch**: unchanged where measured (`m_flags` 0x28, `pm_pos` 0x2C, `m_tbl_size` 0x84,
`m_ground_h` 0x94).

**mDoExt_McaMorf**: `mpModel` 0x50→0x90 (+0x40). **JPABaseEmitter**: `mGlobalRotation`
0x1A8→0x1F0 (+0x48).

**J3DModel / J3DModelData / J3DJoint**: no matched methods. Hexa Drive rewrote J3D for GX2
(devirtualised; most of J3DMatBlock/J3DMaterial/J3DShape has no counterpart found), so their
layouts can't be measured this way.

## Coverage against the GameCube build

`build/coverage.tsv` has the per-file table. Totals (round 2):

- WWHD: 39,698 functions (37,766 from the recompiler's discovery plus 1,932 tail-call-only
  functions). **13,991 are named (35%)**: 12,451 from stages measured at ≥ 97%, 755 `tu-margin`
  (medium) and 785 `callgraph-legacy` (lower). Round 1 had 12,271.
- GameCube: 27,041 functions (weak copies merged); 13,844 matched (51.2%; round 1: 44.8%).
- **No GameCube counterpart (HD-only), ≈16,600 functions**, in long unmatched blocks
  (`build/regions.tsv`):
  - `025F8C38`–`0273A9D4`: new UI (Layout `.bflyt`/`.bflan` panes, GamePad/TV screens, save,
    staff roll, Miiverse, "PictureAccessor"), interleaved with the few GameCube `d_menu_*`,
    `d_metronome` and `d_a_agb` pieces that survived.
  - `0273B2D0`–`027EAC44`: Nintendo `sead`/`agl` (task manager, render layers, bloom, DOF,
    shadows, shader programs).
  - `02843374`–`028E8B04`: nw4f `lyt`/`font`/`snd`, ErrEula/swkbd wrappers, GX2 utilities.
- That leaves ≈23,100 WWHD functions that may have a GameCube counterpart. **≈61% of them are
  named** (round 1: ≈51%). Of the ≈9,100 still unnamed there:
  - ≈4,200 are identical per-file copies of inline functions (destructors, collider virtuals,
    vector operators);
  - ≈5,900 are unique functions, mostly mid-size without literals in `d_*`, `m_Do` and the
    JSystem block.

By area (GameCube functions matched):

| Area | GameCube functions | Round 1 | Round 2 |
|---|---|---|---|
| actors (d_a_*) | 15,374 | 9,593 (62%) | 10,820 (70%) |
| d_* (game) | 4,833 | 1,544 (32%) | 1,751 (36%) |
| JSystem/JAudio | 4,015 | 477 (12%) | 633 (16%) |
| SDK/MSL/runtime (dolphin, TRK, MSL; replaced by Cafe OS) | 1,221 | 43 (4%) | 48 (4%) |
| f_op/f_pc | 621 | 171 (28%) | 216 (35%) |
| m_Do | 515 | 81 (16%) | 97 (19%) |
| SSystem c_* | 462 | 216 (47%) | 279 (60%) |

What blocks 80%:

- **Inline copies** (≈4,200). GHS keeps one per file, and identical bodies belong to different
  functions (for example all trivial destructors). A copy is named only when its own file's
  GameCube object has a copy of a known candidate and the choice is unique. Most collider and
  destructor copies sit in per-file copies of vtables. Those can be identified only through
  their file's constructor, and constructors reference many tables at once.
- **Rewritten or replaced code on the GameCube side**: J3DGraphBase/Loader (0%), JKernel and
  JUtility (≈2%, replaced by sead), the Dolphin SDK/MSL/TRK (gone; Cafe OS), most of
  `d_menu_*`/`d_meter`/`f_op_msg_mng`/JMessage (HD UI). These GameCube functions have no WWHD
  counterpart to find, so the GameCube-side percentage is capped well below 80%.
- **Absolute-score limit**: ≈10,500 unnamed WWHD functions score p < 0.1 against every
  candidate in their window. Their counterpart is outside the window (moved file, or a file with
  no anchor in a block that isn't alphabetical, like the JSystem libraries), already taken by
  a wrapper/inline twin, or absent.
- **JSystem block** (`027EAC44`–`02843374`, 1,963 functions, ≈25% named). Libraries are
  alphabetical inside but not with each other. JAudio, JParticle and JStudio are the parts that
  survived.

Tried without gain: raising the file-window cap from 400 to 1,000 candidates (+0 names, 3× the
runtime).


## Gameplay mods (runtime/src/mods/, Gameplay menu)

All off by default; hooks in `tools/recomp/hooks_mods.txt`. Test switches `WWHD_MOD_*`, traces
`WWHD_MODS_TRACE`, test aids `WWHD_TEST_RSTICK` / `WWHD_RSTICK`, `WWHD_TEST_MOUSE`,
`WWHD_TEST_WHEEL`, `WWHD_TEST_GOTO`, `WWHD_TEST_DOOR_DELETE` (see the sources).

| WWHD | What | Used for |
|---|---|---|
| `0250FDC8` `dCamera_c::manualCamera` | right-stick camera: bezier-shaped stick → `cLib_chaseF` on the turning speed `this+0x154` (+0.25/step) → yaw += speed × 0.92 × f25 (param, 8) → drawn U/V smoothed (≈0.66) | sites `@02510448`/`@025104A0` (speed), `@0251053C` (f28 = vertical rate), `@02510EA0`/`@02510EE4` (V/U smoothing) |
| `025071FC` `dCamera_c::subjectCamera` | first-person camera | mouse → right stick (rate) |
| `0252A684` `dDoor_info_c::getDemoAction`, `021C0078` `daMbdoor_c::getDemoAction` | door event cut (action table: 16 = TALK) | quick doors |
| `101F36CC` | `l_fopOvlpM_overlap[0]` (request: `+0x20` task) | fast scene changes |
| `101F3A1C` | `g_fpcDtTg_Queue` (delete tags: `+8` next, `+0xC` process, `+0x18` timer; `fpcDtTg_Do` `025DDE44`) | quick doors leave deletion out of the extra steps (issue #61, `runtime/src/mods/turbo_steps.h`) |
| `101F3328` | `g_fopAcTg_Queue` (actor tags: `+8` next, `+0xC` actor; `fopAcIt_Executor` `025D51DC`) | test aid `WWHD_TEST_DOOR_DELETE` |
| `101F5088` | game pad state (sead controller): `+0x124` held, `+0x18` pressed, `+0x1C` released, `+0x40` hold counter, `+0x130/0x134` main stick, `+0x138/0x13C` right stick | cleared triggers in extra steps |
| `daPy_lk_c+0x68D9` | `mReadyItemBtn` (0 X, 1 Y, 2 R; from `itemTrigger` `023EAB40`) | left click while aiming |

Measured (Outset, copy of the user's save): the original right-stick camera ramps 0.48, 1.13, 1.84 …
to 7.36°/step over 10 steps and eases out over 4 steps plus a tail; the direct camera turns
7.36°/step × stick × speed from the first step and stops on release (same in interpolation and true
60, 30 steps/s: 220.8°/s at full deflection). Link's house door, A press → control inside: 170
frames; quick doors 78; fast scene changes 166; both 74 (the ≈34-frame wait for the new scene's
archives is not shortened).

## Aspect ratio (runtime/src/aspect.cpp, Graphics menu "Aspect ratio", `WWHD_ASPECT`)

The game draws a 16:9 screen of 1280x720 guest pixels. Other aspect ratios keep the guest sizes and
change three things.

**3D projection and culling** (all from the main camera, `camera_class` / `view_class`):

| WWHD | What | Notes |
|---|---|---|
| `1004AAF0` | `16/9` constant | only read by `camera_execute` (`024FFA94`) and `init_phase2` (`025020D8`); Cemu's resolution pack patches this one (plus `101417D0` and `10165898`, see below) |
| `024FFA3C` | `camera_execute` | `view.mAspect` (+0xD8) = constant, every logic step (`@024FFA98`) |
| `024FFC40` | `camera_draw` | `C_MTXPerspective(proj, mFovy, mAspect, mNear, mFar)` at `024FFD60` |
| `024F8068` | `view_setup` | `mDoLib_clipper::setup(mFovy, mAspect, mNear, far)` at `024F811C` / `024F8168`: the frustum all culling uses (`fopAcM_cullingCheck`, `daBg_Draw`, grass, trees via `J3DUClipper`) |
| `025ADB04` | `dScnName_c::setView` | the file-select scene's own perspective (`025ADB38`) |

Hor+ for wider screens (same vertical field of view), Vert+ for narrower ones (same horizontal
field of view, `fovy' = 2 atan(tan(fovy/2) * (16/9)/A)`). Only standard values are replaced (16/9
or one we stored), so special aspects (demo cameras) pass through. Particles read `view.mAspect`
(JPADrawInfo) and see the new value.

**Render targets** (metal_surfaces.mm): screen-shaped buffers (w/h within 3% of 16:9, 1920x1080
down to 60x33, not the GamePad's 854x480 chain, no arrays/mips) are allocated `A/(16/9)` times wider
(or `(16/9)/A` taller) on top of the internal resolution. Viewports/scissors and texture coordinates
already go through the per-surface `sx`/`sy`, so every full-screen pass lines up. The factor is
switched one swap after the game's projection (the painter draws the previous pass's lists).

**HD layouts (nw4f `nw::lyt`)**, found by the uniform uploads of the lyt shader:

| WWHD | Function | Notes |
|---|---|---|
| `0272DB10` | layout layer projection setup | per `Layout`/`LayoutBack` layer: `sead::OrthoProjection` +0x610, +0x6BC (+-640 x +-360; fields near +0x94, far +0x98, top +0x9C, bottom +0xA0, left +0xA4, right +0xA8, dirty bytes +0/+1), `sead::PerspectiveProjection` +0x768 (fovy 40, aspect 16/9 = `101417D0`, set by `0274E04C`). The layer's DrawInfo (layer+0x548) keeps its own copy of the perspective matrix; changing these objects later has no effect. |
| `02874038` | `nw::lyt::DrawInfo::LoadProjectionMtx` | `GX2SetVertexUniformReg(uProjection, 16, DrawInfo+0)` |
| `02874074` | `DrawInfo::LoadMtxModelView` | 12 floats at DrawInfo+0x70, flag +0xB6 |
| `028766CC` | `nw::lyt::Pane::CalculateMtx` | vtable slot (Pane vtable `101805D4`), recursive over children |
| `02877100` | `nw::lyt::Pane::Draw` | plain panes (layout roots) |
| `028F8250` | `nw::font` text draw | content+4 -> projection, content+8 -> view; uploads proj x view |

Layouts are drawn with a perspective camera (fovy 40, the layout plane at z = -989 so 1 unit = 1
pixel). `nw::lyt::Pane`: +0x00 sibling link, +0x08 vtable, +0x0C parent, +0x10 child count,
+0x14 child list sentinel, +0x1C translate, +0x28 rotate, +0x34 scale, +0x3C size, +0x44 flags,
+0x45 alpha, +0x48 global matrix (3x4), +0x80 name.
The HUD is `Main_00` (root children N_TV_00 / N_DRC_00 / N_Default_00 hearts, N_HeartPos_01,
L_Rupy_00, L_CompassClock_00, L_DungeonKey_00, N_SwimTimePos_00, L_Time_00, L_BossHP_00,
L_EnemyHP_00, L_Arrow_00, L_BatteryVol1_00, L_RupySwordCounter_00) plus `CommandGuide_00`
(buttons, N_All_00 at 509,238) and `PlaceName_00` (N_All_00 at -597,291); layouts are in
`Common/Pack/permanent_2d_<lang>.pack` (SARC of Yaz0 SARCs).

Which screen a layout goes to can't be told on the game side: TV and GamePad layouts share the
layers' DrawInfos, and the lists are recorded on threads that never bind the target (agl binds it
elsewhere). The render thread knows: the hooks tag the projection upload (`OP_SET_PROJ_REGS`) and
each root draw (`OP_LAYOUT_ROOT`) in the GX2 command stream. For TV-shaped targets the projection's
x/y rows are divided by `kx`/`ky` (layouts keep their proportions, centred); the root's screen is
reported back for the next frame's anchoring. While a TV layout's root computes its matrices (dirty
bit 0x10 of +0x44 forced so the offsets are applied), root children more than 300 units from the
centre move out by the extra half width, `L_EnemyHP_00` and `L_CommandA_00` (placed at an actor's
projected position) and roots the game moved off 0,0 are scaled by `kx`, and leaf panes covering the
whole 1280x720 screen (backgrounds, fades) are stretched. Values are changed only for the call.

Not handled: bloom/blur kernels sized in guest texels are `kx` times wider horizontally;
`10165898` (agl perspective, fovy 45, near 0.1) is left at 16:9 (use unknown).
