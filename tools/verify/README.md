# tools/verify: verified decompilation of WWHD

Readable C++ for WWHD (`cking.rpx`) functions whose **behaviour is checked against the
recompiled original** (`build/gen`), function by function, without running the game.

- Our own code lives here: the harness, unit generator, recording taps (`runtime/src/verify_tap.cpp`)
  and the ABI layer (`include/gabi.h`).
- The verified source is derived from Nintendo's code. It lives in `wwhd_src/` (not for
  publication, see `wwhd_src/README.md`).
- Never commit `build/` (recompiled code, the RPX image dump, recordings).

## Quick start

```sh
WWHD_RECOMP_PLAIN=1 python3 tools/recomp/recomp.py game/code/cking.rpx build/gen   # once, plain form
python3 tools/verify/verify.py d_a_kamome -n 3000 -rec ../rec1      # build + run a unit
python3 tools/verify/mutate.py d_a_kamome --n 1000 --max 500        # how sensitive is it?
python3 tools/verify/wdis.py 021861D4                               # disassembly with names
```

Output, one line per function:

```
addr      function                    generated   recorded coverage  first difference
021861D4  daKamome_Execute            10000/10000   120/120  337/409  ok
```

- `generated`: generated inputs that passed / run.
- `recorded`: recorded calls that passed. `(-k)` counts recordings the original's replay could
  not reproduce; those are skipped.
- `coverage`: basic blocks of the original reached.

Harness options:

| Option | Meaning |
|---|---|
| `-n N` | generated inputs per function |
| `-seed S` | seed for the generated inputs |
| `-rec DIR` | recorded inputs from `DIR/ADDR/*.tap` |
| `-only ADDR` | test one function only |
| `-v`, `-v -v`, `-v -v -v` | failures; also uncovered blocks; also full memory diffs |
| `-trace SEED` | print every access and call of one generated input |
| `-noclobber` | mocked callees never modify memory |
| `-strictnan` | compare NaN payloads bit for bit |

## What "verified" means

For each input, the original and the candidate run from the same state. They must agree on:

1. **The return value**, as the candidate's C++ signature types it: r3 (masked to its width) or f1.
2. **The net memory effect**: every byte whose final value differs from its initial value. The
   scratch stack below the entry SP is excluded; the caller's frame above it is included.
3. **The call sequence**, call by call:
   - the target (direct, through a function pointer, or an import);
   - the argument registers the callee reads;
   - stack arguments;
   - the contents of stack objects passed by pointer (pointers inside them are compared
     relative to the object).

The original is the game's recompiled code, extracted from `build/gen` and compiled with
`include/vm_gen.h`, which routes every load, store and call through the harness. Every other
WWHD function is **not executed**. Calls to it are recorded and answered by:

- a mock (generated inputs);
- the recorded result and memory effects (recorded inputs);
- or the real code, for pure helpers listed as `real ADDR`.

So each function is tested in isolation, and the test is independent of whether its callees
have been decompiled.

### Memory model (`src/harness.cpp`)

- Guest memory is sparse and materialised lazily. The first access to a byte takes its value
  from, in order:
  1. the recorded snapshot or the `field` specs;
  2. the RPX image (`.rodata`/`.data`/`.text`, dumped by `mkimage.py`);
  3. zero for the stack below the entry SP;
  4. a deterministic typed random word: 0, small ints, small per-byte values, floats, pointers
     into the random heap, s16 pairs, -1 patterns, random bits.
- Any pointer the code follows is therefore valid, and both sides see the same contents.
- Writes are tracked per byte.
- **Clobbering**: on half of the generated inputs, every mocked call starts a new *epoch*. In
  each epoch every heap/global word may have been rewritten (1 in 4, deterministic per address
  and epoch). A candidate that caches a field across a call, reloads one the original kept, or
  moves a store across a call therefore fails, because a real callee could have changed that
  memory. The other half of the inputs keeps memory stable, so deep paths stay reachable.

### Callee facts (`funcdb.py`, `mkunit.py`)

Derived from the generated C of the whole program:

| Fact | Used for | How it is derived |
|---|---|---|
| Argument registers a callee reads | which registers are compared at the call | live-in analysis, interprocedural; varargs prologues and paired-single `ps1` reads excluded; the GameCube signature is added, never subtracted, because a matcher name can be wrong |
| Volatile registers a callee may write | which registers a mock writes | may-def analysis. GHS allocates registers across calls, so callers keep live values in "volatile" registers the callee does not touch; a mock must not clobber them |
| Shape of the r3 result (zero/sign-extended width, constant) | mock results of the same shape | GHS callers do not re-extend a `u8` result |
| Stack arguments | stack comparison | from the GameCube signature |
| Pointee sizes | stack-object comparison | from the signature; constructors' `this` storage is not compared (it is not an input) |

### Inputs

- **Generated:** argument registers typed from the signature, memory filled lazily as above.
  Steering lives in the unit file:
  - `field FUNC|* rN+OFF|ABS TYPE LO HI`: value ranges for fields and globals, with the edges
    drawn more often;
  - `ret ADDR LO HI`: result domains of callees (e.g. `dComIfG_resLoad` 0..5, `fopAc_IsActor` 0..1).
  
  Generated floats come partly from a dictionary of the `.rodata` constants the original reads,
  so `<` vs `<=` boundaries are hit.
- **Recorded:** real calls captured in the running game.
  1. `mktap.py ADDR...` adds a git-ignored `tools/recomp/hooks_tap.txt` and re-runs the
     recompiler.
  2. It then writes `build/gen/code_tap.c`: instrumented copies `tap_ADDR` whose own loads,
     stores and calls are logged.
  3. With `WWHD_TAP=dir` set (optionally `WWHD_TAP_N`, `WWHD_TAP_EVERY`, `WWHD_TAP_AFTER`), each
     call is written to `dir/ADDR/n.tap`. Recording is off by default, and `mktap.py --clean`
     restores the normal build.
  4. The harness rebuilds each recording into a replay:
     - the snapshot is the first value read at each address;
     - callee effects are values F reads that changed since it last saw them; they are attached
       to the preceding call;
     - first reads of F's own frame after a call are callee outputs.
  5. Before a recording is used, the original's replay must reproduce it: the same calls, the
     same writes, the same result, and no reads outside the snapshot.

## Writing candidate source (`include/gabi.h`)

```cpp
struct daMtoge_c : fopAc_ac_c {          // WWHD layout, be<T> fields, explicit padding
    /* 0x3B4 */ gptr<J3DModel> mpModel;
    /* 0x3C0 */ be<f32> mHeightOffset;
};
WWHD_OFFSET(daMtoge_c, mHeightOffset, 0x3C0);

/* 021E01B8 */
BOOL daMtoge_actionUp(daMtoge_c* i_this) {
    WWHD_FUNC(0x021E01B8, BOOL, i_this);   // first statement: address, return type, arguments
    cLib_chaseF(&i_this->speedF, 30.0f, 4.0f);
    ...
}
VERIFY(0x021E01B8, daMtoge_actionUp);
```

### API

- **Structures:** `T*` is a token for a guest address (`PPC_MEM_BASE + EA`).
  - Fields are `be<T>` (big-endian, accessed through the harness). Pointers are `gptr<T>`
    (32-bit). `gabi::at<T>(ea)` and `gabi::ea(p)` convert.
  - Layout structs are never copied (`be` has no copy constructor).
- **Calls to other WWHD functions:** `gabi::call<R>(addr, args...)` follows the EABI:
  - integers and pointers in r3..r10, then on the stack;
  - floats in f1..f8;
  - result from r3 or f1, typed by `R`.
  
  Bindings with real names live in `wwhd_src/include/bindings.h`.
- **Calls between decompiled functions** are written as natural C++ calls. `WWHD_FUNC` turns a
  nested call into a guest call to that function's address, so every function is tested alone.
  A native build can route the address to the implementation.
- **Locals whose address is passed to guest code** use `gabi::Local<T>`, guest stack storage.

### Rules learned in the pilot (each one was a real failure first)

**Floats** (the recompiler's semantics, `tools/recomp/ppc2c.py`):
- `f32` arithmetic is IEEE single, which is exactly fadds/fmuls/fdivs.
- Where GHS contracted `a*b+c` into `fmadds`, write `gabi::fmadds(a, b, c)`. The recompiler
  computes it in double and rounds once more to single, which is neither a fused nor an
  unfused single result. Compile with `-ffp-contract=off`.
- Watch the operand order of fused terms: `x*x + z*z` became `fmadds(x, x, z*z)`.
- Float-to-integer conversion is `gabi::ftoi` (fctiwz: truncating, saturating, NaN → INT_MIN).
  A float passed to an `s16` parameter is `(s16)gabi::ftoi(f)`; C++ conversion is undefined
  out of range.
- A float loaded into an FPR (lfs) quiets a signalling NaN; `be<f32>` reads model that.
  - GHS copies a `cXyz` struct with integer loads (bit-exact, `cXyz::copy`), but goes through
    FPRs when it computes with it.
  - Matrix copies load all twelve values, then store them (`mtx_copy`); this matters when the
    source and destination overlap.
- NaN-vs-NaN results are unspecified: which payload survives depends on the host compiler's
  operand order, in the original too. The harness treats two NaNs as equal. With `-strictnan`,
  2 of 10000 kamome_bgcheck inputs differ only in NaN payloads.
- GHS branches on the negated comparison, so with NaNs `a <= b` is not `!(a > b)`. Write the
  form the code tests.

**Ordering**:
- Under clobbering, a field must be loaded where the original loads it relative to calls.
- C++ argument evaluation order is unspecified, so sequence explicitly:
  `p = i_this->mpMorf; env = dKy_getEnvlight(); f(env, model(p))`.
- Stores before or after a call must stay on the same side of it.

**GHS / HD artefacts that appear in source**:
- **Constructors allocate when `this == NULL`:** `new dBgW()` is `dBgW::dBgW(NULL)`.
- **HD classes have a virtual destructor:** an actor's vtable pointer is at +0xB4, written by
  `fopAcM_ct`. Inline sub-object constructors write their vtables.
- **Function-local statics are initialised on first use:** a guard word plus memcpy from `.data`
  (`l_action` in `daMtoge_Execute`).
- **String literals are not pooled:** each use of `"Kamome"` has its own address.
- **Resource names are `sead::SafeString` temporaries** `{const char*, vtable}`, with one
  SafeString vtable per translation unit.
- **GameCube globals became singletons behind accessors:** `dComIfGp_get()` (0x025200D4) and
  `dKy_getEnvlight()` (0x02555D0C); save info is at `*(0x101F84DC) + 0x20`.
- **Address-of a model's base matrix is null-preserving** (`J3DModel_getBaseTRMtx`).

**Matcher names are hypotheses.** A binding's types are checked by the harness. A wrong name
(e.g. `02555D0C` named `setLightTevColorType`, which is the env-light accessor) shows up as a
call/argument mismatch.

## Workflow for one actor (scales to many in parallel)

1. **Pick the unit.** One GameCube translation unit. List the WWHD functions of its range:
   `build/names.tsv` plus the unnamed functions in between.
2. **Layout.**
   - Start from the GameCube header shifted by the fopAc_ac_c delta (+0x11C) and fix it from
     the disassembly (`wdis.py`).
   - Put the layout in `wwhd_src/include/...`, with `WWHD_OFFSET` asserts.
3. **Port** each GameCube function. Add bindings for its callees (address + types), with
   `WWHD_FUNC`/`VERIFY`.
4. **Unit file** `tools/verify/units/<unit>.txt`:
   - `src` lines;
   - `field`/`ret` steering until the coverage column is close to full;
   - `callee ADDR rN=SIZE` for stack objects (`rN=255`: output storage, not compared).
5. **Run** `verify.py <unit> -n 3000`.
   - A failure prints the first difference and the input (`-trace SEED` for a generated one).
   - Fix the candidate until all generated inputs pass.
6. **Record:**
   - `mktap.py --unit <unit>`;
   - build the game in a scratch clone;
   - run a scripted session with `WWHD_TAP=dir`;
   - `verify.py <unit> -rec dir`.
7. **Mutation test.** `mutate.py <unit>` should leave only equivalent mutants (swapped
   independent stores and the like). A non-equivalent survivor means an input gap: add steering.
8. Commit the source (`wwhd_src/`) and the unit file.

**Parallelising.** Functions are verified one at a time and callees are always mocked, so
functions of one actor can be written by different people or agents at once.
- Give each worker its own source file and unit name (separate build directories).
- Workers declare local bindings for the functions they call, then merge. Calls between
  files go by address anyway.
- Shared headers and the harness need one owner.
- In the pilot, kamome's two largest functions were written by two agents in parallel while
  the harness was being extended.

## Limitations

- **Generated inputs** explore, but rare paths need steering. Block coverage is a weak proxy;
  use `mutate.py`.
  - Some uncovered blocks are infeasible: GHS duplicated dispatch tails, so a tail is only
    reached with a fixed value.
  - Mocks know callees only through types and register facts. A path that needs a callee's
    *real* behaviour (e.g. a search callback that finds something) needs `ret` steering or
    recorded inputs.
- **Recorded inputs** cover only what the session reached:
  - the title screen and the Outset pier for kamome;
  - nothing yet for d_a_mtoge (Forsaken Fortress).
  
  First reads after a call outside F's own frame are taken as initial state. Memory changed
  between two reads by another thread makes a recording unusable (counted, skipped).
- **Global state:**
  - in generated mode every global is random unless steered;
  - in recorded mode it is exactly what the function read.
  - Functions whose behaviour depends on a global being consistent with another (lists,
    heaps) need recorded inputs or `real` helpers.
- **Uninitialised stack temporaries** are zero on both sides. A candidate that forgets to
  initialise a local passed by pointer is only caught if the original wrote a non-zero value.
- **Varargs callees** compare only the arguments the candidate declares (plus the named ones).
- **Not checked:** timing, which thread runs the code, cache/sync instructions, the exact stack
  frame layout, and the order of loads between calls (unobservable).
- **Equivalence is to the recompiled code** (the recompiler's model of Espresso), not to the
  console:
  - `fmadds` is double-then-single rounding;
  - SNaN handling follows host conversions.

## Pilot results

Two actors, one object and one enemy-like NPC. Wall-clock times are for an agent working with
these tools, measured from commits and file timestamps.

### d_a_mtoge: Forsaken Fortress spikes

16 functions verified, each with 10,000 generated inputs. There are no recordings: the spikes
only appear in Forsaken Fortress.
- Not done: four compiler-generated functions in the range (`__sinit`, two deleting destructors,
  an empty virtual).
- Mutation test (`mutate.py d_a_mtoge --n 1000`): 111 mutants compiled, 107 killed. The 4
  survivors are equivalent (each swaps two independent stores), so every non-equivalent mutant
  was killed.

| Category | Count | Functions |
|---|---|---|
| Adopted unchanged apart from layout/bindings | 13 | calcMtx, CreateHeap, CheckCreateHeap¹, actionWait¹, getSwbit, actionHind/Up/Arrival/Down, Draw, IsDelete, Delete, CreateInit, daMtoge_Create |
| Adopted with edits | 2 | `daMtoge_Execute` (HD: function-local action table initialised at runtime), `create` (fopAcM_ct with the HD vtable) |

¹ Not named by the matcher; identified while porting the translation unit.

### d_a_kamome: seagull

All 20 functions of the translation unit verified:
- 10,000 generated inputs each;
- 270 recorded calls each where the function ran (two sessions: title screen and Outset pier);
- `Create`/`Delete` 20 and 8 recorded calls.

| Category | Count | Functions |
|---|---|---|
| Adopted unchanged apart from layout/bindings | 6 | anm_init, kamome_bgcheck, IsDelete, Delete, createHeap, ground_pos_move² |
| Adopted with small edits (fmadds, ftoi, evaluation order, HD null checks) | 5 | search_esa, s_a_i_sub, ko_s_sub, h_s_sub, Create |
| Differs materially in HD | 7 | s_a_d_sub (extra bait-state test), nodeCallBack (HD J3D matrix block + dirty flags), Draw (no blob shadow; USA mbNoDraw), kamome_pos_move (debug-register target), daKamome_setMtx (no mRot; HD joint callback storage), daKamome_Execute (8 inlined functions; Aryll's gull lands only near the ground), kamome_auto_move (smaller wander radius, water check on landing, keep-above-ground timer) |
| Newly decompiled (compiler-generated, no GameCube source) | 2 | `__sinit_d_a_kamome_cpp`, kamomeHIO_c deleting destructor |

² Needs `gabi::ftoi` for float→s16 arguments.

The fork agents estimate that, by statements, about 85% of Execute's GameCube source and about
75% of auto_move's carried over unchanged apart from layout.

Mutation test (`mutate.py d_a_kamome --n 400 --max 400 --rec ../rec1`, a sample including the
inlined helpers): 347 mutants compiled, 296 killed. Of the 51 survivors:

| Kind | Count | Meaning |
|---|---|---|
| Equivalent | ≈ 40 | Swaps of independent stores, zeroing a stack slot that is zero anyway, and a clamp whose `<`/`<=` boundary gives the same value |
| Input gaps | ≈ 11–17 | Mostly rarely reached states of kamome_auto_move (a dropped `anm_init`, a dropped timer store, a literal in a landing state), the radius loop of search_esa and one boundary in setMtx. More `field`/`ret` steering or longer recording sessions would close them |

Bounds: at least 95% of the non-equivalent mutants were killed, 100% in d_a_mtoge.

Effort, wall clock:

| Step | Time |
|---|---|
| Harness from scratch | about 35 min, including reading the project |
| d_a_mtoge port (16 functions) | about 10 min, plus harness fixes it exposed |
| Recording infrastructure + game build + first session | about 15 min |
| kamome's 18 small and medium functions | about 15 min |
| kamome_auto_move (4.7 KB) | 7 min (parallel agent) |
| daKamome_Execute (10.8 KB) | 14 min (parallel agent) |

Per function, after the setup: about 1 min for small functions, and 5–15 min for large ones
with heavy inlining.
