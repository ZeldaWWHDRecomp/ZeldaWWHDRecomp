# Mod SDK v2: PowerPC guest mods (design study and prototype)

Status: **prototype** on branch `sdk2-guest-mods`, off by default. The Native SDK v1
(`runtime/include/wwhd_mod.h`, [mod-manager.md](mod-manager.md)) stays supported and unchanged.

Code mods for this port are written in C (or C++) against mod headers and compiled for the
console CPU: 32-bit big-endian PowerPC, the game's ABI. The package is the same on every
platform. On install, the port translates the mod's PowerPC code to C with its own translator
and compiles it with the player's local compiler (the one setup already uses for the game
code) into a loadable module. Mods hook or replace game functions **at runtime**: the game
code is not translated or compiled again.

This follows the model of Zelda64Recomp / N64Recomp mods (see [Prior art](#prior-art)).

## Contents

- [Prior art](#prior-art)
- [The port today](#the-port-today)
- [Runtime hooking: options and costs](#runtime-hooking-options-and-costs)
- [Mod toolchain and package](#mod-toolchain-and-package)
- [Install-time translation](#install-time-translation)
- [Headers for modders (legal)](#headers-for-modders-legal)
- [Host services](#host-services)
- [Trust](#trust)
- [Mod manager integration](#mod-manager-integration)
- [Prototype: what exists and how to run it](#prototype-what-exists-and-how-to-run-it)
- [Measurements](#measurements)
- [Open decisions](#open-decisions)
- [Road to a production version](#road-to-a-production-version)

## Prior art

Checked on 2026-10-08 in the public sources of
[N64Recomp](https://github.com/N64Recomp/N64Recomp) (`ffb39cd`),
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) (`cdf5abb`),
[Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) (`b65c482`) and
[MMRecompModTemplate](https://github.com/Zelda64Recomp/MMRecompModTemplate) (`0c3e82c`).
Only ideas are described here; no code was copied.

| Topic | Zelda64Recomp / N64Recomp |
| --- | --- |
| Package | `.nrm` = zip of `mod.json` (id, version, authors, `minimum_recomp_version`, dependencies, optional dependencies, `config_schema`, `native_libraries`), `mod_syms.bin` (sections, functions, relocations, imports/exports, replacements, hooks, events) and `mod_binary.bin` (section bytes). Built by `RecompModTool` from a linked ELF (N64Recomp `RecompModTool/main.cpp`, `src/mod_symbols.cpp`). |
| Toolchain | Stock clang + ld.lld for MIPS (`-target mips -mips2 -mabi=32 -O2 -G0 ...`, linked with `--emit-relocs` at 0x81000000; the template's `Makefile` / `mod.ld`). Apple clang lacks the target, so macOS modders use Homebrew LLVM. |
| Marking | `RECOMP_PATCH`, `RECOMP_HOOK("fn")`, `RECOMP_HOOK_RETURN`, `RECOMP_EXPORT`, `RECOMP_IMPORT`, `RECOMP_CALLBACK` are only `section(...)` attributes; the tool reads the sections (template `include/modding.h`). Patch/hook targets are checked by name against the game's reference symbols at build time. |
| Load-time recompilation | A **live recompiler** (sljit JIT) turns the mod's MIPS code into host code at every launch, on all platforms (`LiveRecomp/`). An offline C path (`OfflineModRecomp`, `.offline.nrm` + a native library) exists for debugging only. No cache. |
| Replacement | The runtime **overwrites the first bytes of the native game function** with an absolute jump into the mod (x86-64 `movabs/jmp`, ARM64 `ldr/br`), making the page writable meanwhile (N64ModernRuntime `librecomp/src/mods.cpp`, `patch_func`). Zero cost for functions that are not replaced. |
| Hooks | Per (function, entry/return) a hook slot. The function is **regenerated from the original ROM code** by the live recompiler with `run_hook(slot)` calls inserted and jump-patched in. Needs the original code and a JIT at runtime. Return hooks can read the return value. |
| Conflicts | Two mods replacing one function: error. Replacing a function the port itself patched needs `RECOMP_FORCE_PATCH`. Any number of hooks. |
| Calls | Mod → game: relocations against reference symbols, resolved to direct native calls. Function pointers: every mod function registered at its guest address. |
| Versioning | `minimum_recomp_version`, `game_id`, dependency minimum versions, native library `recomp_api_version == 1`. |
| Events | `RECOMP_DECLARE_EVENT` / `RECOMP_CALLBACK`: callbacks in mod order, register context restored between them. The port declares base events. |
| Native libraries | "extlib": `.dll/.so/.dylib` named in the manifest, loaded next to the mod, functions callable as imports `(rdram, ctx)`. Full process privileges. |
| Config | Typed schema (enum, number, string, ranges), read with `recomp_get_config_*`, stored per mod. |
| Memory | Mod sections copied into emulated RAM from 0x81000000, guard gaps, then the mod heap. |
| Trust | No trust prompt or sandbox found; native libraries run unrestricted. |

The two ideas that do **not** carry over: jump-patching native code (our game code is in the
signed, locally linked executable; macOS on Apple silicon forbids writable code pages of a
signed image, and the hardened runtime of a release would forbid it entirely) and hooking by
regeneration (we have no in-process recompiler or JIT; our "recompiler" is Python + the local
C compiler, used offline at setup). The rest (section-marked hooks, name or address based
imports, conflict rules, typed config, events, function registration for pointers) does.

## The port today

- **Translation** (`tools/recomp/recomp.py`, `ppc2c.py`): each guest function becomes a C
  function `void f_XXXXXXXX(Cpu* c)`; the 39,713 functions of `cking.rpx` go into 78
  `code_NNN.c` files (about 170 MB of C).
- **Calls**: direct `bl` → a direct C call `f_X(c)`; branches to other functions → `MUSTTAIL`
  tail calls; computed calls (`bctrl`, `blrl`, vtables, process method tables) and unknown
  targets → `c->pc = target; ppc_dispatch(c)`, a lookup in a flat 16 MiB-range table
  (`runtime/src/core.cpp`, `dispatch::lookup`, one load) indexed by `(addr - 0x02000000) / 4`.
- **Port hooks** (`tools/recomp/hooks*.txt`): for a listed address the generator emits
  `f_X(c) { hook_X(c); }` and renames the game code to `f_X_orig`; the runtime defines
  `hook_X` (frame interpolation, true 60, gameplay mods). `@ADDR` lines add instruction-level
  `site_X(c)` calls. Changing the list requires translating and compiling the game again.
- **Local build** (`tools/installer/setup.py`): setup runs `recomp.py`, compiles the 80
  sources with the pinned toolchain (`xcrun clang` from Apple's Command Line Tools on macOS,
  llvm-mingw on Windows, zig on Linux; flags from `sdk/manifest.json`, i.e.
  `-O3 -ffp-contract=off -fno-strict-aliasing`), archives them and links them with the
  prebuilt runtime into the executable. On an M-series Mac this takes about 1.5 minutes at
  4 jobs (about 5 CPU minutes).
- **Native mods v1**: `kind: native` packages, `dlopen`/`LoadLibrary` of a prebuilt library
  per platform, a C ABI with frame callbacks and guest-memory access, one-time trust
  confirmation.

## Runtime hooking: options and costs

| Option | Cost per call of an unhooked function | Code size | Hookable | Notes |
| --- | --- | --- | --- | --- |
| **(a) flag check at every function entry** (recommended, prototyped) | 1 load + test + not-taken branch (arm64: `adrp; add; ldrb; cbnz`) | +3.2 MB text (+7.8 %) for the cold paths, +40 KB flags, +318 KB table | every function, also through pointers and tail calls | no change of the call graph; inlining unaffected |
| (b) all direct calls through a function table | an indirect call instead of a direct one (load + `blr`), no inlining across calls | small | every function | indirect branches strain the predictor at ~40k targets; blocks clang's inlining and musttail optimizations |
| (c) only a published "hookable" subset (like `hooks.txt`) | zero for the rest | tiny | the list only | a new hook point needs a port update (and the game rebuilt on the player's machine) |
| (d) patch native code at load (N64Recomp) | zero | zero | every function, but hooks need regeneration | not possible on macOS arm64 for a signed executable; Windows/Linux need writable code pages; hooks would need a JIT |

(a) is the recommendation: every game function is hookable with a cost at the level of the
two checks every function already has (`PPC_ENTER`: trace switch and core preemption), and no
part of the game must be rebuilt when mods change. (c) remains a fallback if the measured cost
were too high (it is not, see [Measurements](#measurements)); (a) and (c) can also be combined
(check only in functions that are not on a "hot leaf" list).

### Mechanism (prototype)

`recomp.py --mod-hooks` (or `WWHD_RECOMP_MOD_HOOKS=1`) emits at the start of every function body:

```c
void f_0200EDC8(Cpu* __restrict c) {
    PPC_ENTER(0x0200EDC8u);
    PPC_MOD_HOOK(515, 0x0200EDC8u);   /* ordinal in g_recomp_funcs, address */
    ...
```

`PPC_MOD_HOOK` (`runtime/include/ppc.h`) tests `g_mod_hook_flags[ordinal]`, one byte per
function (generated in `table.c`). A set flag diverts to `ppc_mod_run`
(`runtime/src/mods/guest_mods.cpp`), which runs the entry hooks, the replacement or the
original, then the return hooks. To run the original, the runtime sets `c->mod_skip` to the
function address and calls the function body (`g_mod_bodies[ordinal]`); the check consumes
the skip. `Cpu::mod_skip` uses what was padding, so `sizeof(Cpu)` and save states are
unchanged.

Calling convention of hooks is the game's: entry and return hooks receive the function's
arguments (r3–r10 and f1–f8 are restored before every hook), a return hook leaves the return
value (r3, r4, f1) as the function produced it, a replacement is the function. r1/r2/r13 are
preserved by every callee (ABI). Return hooks run in reverse load order.

### Port hooks, frame interpolation and true 60

The check sits in the game code (`f_X`, or `f_X_orig` behind a port hook), so the port's
own hooks stay **outermost**: interpolation and true 60 decide first whether and how a game
function runs, and mods hook the game's code below them.

- A mod hooking a logic function runs once per logic step, also at 60/120/240 fps
  interpolation (hold passes skip the logic, so they skip the mod too).
- A mod hooking a drawing function runs once per displayed frame.
- With true 60, the port scales the arguments of the `cLib_addCalc*` family by the step
  length before the game code runs; a mod replacing such a function receives the scaled
  arguments, so its replacement stays 60 Hz correct. Mods that count steps should use a
  host service for the step length (planned: `wwhd_logic_dt`).
- A mod replacement of a function the port hooks itself (all `hooks*.txt` entries) is
  allowed but should be marked in the mod (like N64Recomp's force patch) so the manager can
  warn. Not enforced in the prototype.

## Mod toolchain and package

- **Compiler**: clang with the PowerPC target and ld.lld, free on all three platforms
  (LLVM releases; Homebrew `llvm` + `lld` on macOS; Apple's clang has no PowerPC target). Tested:
  Homebrew clang 20.1.8 and ld.lld 21. Only clang/lld is supported for mod authors.
- **Flags** (see `runtime/guest/include/wwhd_guest.h`, `examples/guest-mods/Makefile`):
  `--target=powerpc-unknown-eabi -mcpu=750 -O2 -ffreestanding -fno-builtin -nostdlib
  -fno-jump-tables -ffunction-sections -fdata-sections`. `-mcpu=750` keeps to instructions
  of the game's CPU family (no AltiVec, no `isel`); clang does not use small-data (r2/r13)
  addressing for this target, so the game's r2/r13 stay untouched.
- **Output**: one relocatable ELF per mod (`ld.lld -m elf32ppc -r *.o -o mod.elf`), not linked to an
  address. Relocations and undefined symbols are resolved on install.
- **References to the game** by address, so no symbol database is needed:
  `WWHD_GAME_FUNC(0x0200ED84, void, cLib_addCalc2, (f32*, f32, f32, f32))` declares a game
  function (`__wwhd_game_0x0200ED84` as the symbol), `WWHD_GAME_ORIGINAL(...)` the game's own
  code below all mods, `WWHD_GAME_DATA(addr, type)` a variable. A later SDK can add
  name-based symbols on top (a generated `game_symbols.txt`; see the legal section).
- **Hooks** are descriptors in section `.wwhd_hooks` (`WWHD_REPLACE`, `WWHD_HOOK`,
  `WWHD_HOOK_RETURN`), read by the translator.
- **Memory**: the mod's code, data and bss live in guest memory at a base the mod manager
  assigns, in a region the game and the runtime do not use (`0x7F000000`–`0x80000000`,
  16 MiB). Game code can therefore use pointers into mod data, and mod functions are
  registered in the dispatch table at their guest addresses (function pointers handed to the
  game work). A mod heap (guest `malloc`) is a planned host service.
- **ABI**: the game's: arguments r3–r10 / f1–f8, results r3/r4/f1, stack r1, small-data
  bases r2/r13 owned by the game, big-endian. Varargs follow the SVR4 rules (cr6).
- **Package** (`manifest.json`, the v1 manifest format plus):
  `"kind": "guest", "guest": {"api_version": 1, "elf": "mod.elf"}`. Options, dependencies
  and conflicts as for v1.

## Install-time translation

`tools/guestmod/build_guest_mod.py PACKAGE --out CACHE [--base ADDR] [--cc CC] [--json]`:

1. reads the manifest and the ELF (`tools/guestmod/guestmod.py`): lays the allocatable
   sections out at the base (code, read-only data, data, bss), applies all relocations
   (`ADDR32`, `ADDR16_LO/HI/HA`, `REL24`, `REL14`, `REL32`; anything else, e.g. small-data
   relocations, is an error), resolves undefined symbols (`__wwhd_game_*`, `__wwhd_orig_*`,
   `__wwhd_gdata_*`, otherwise a host service by name);
2. finds the functions (symbols, call targets, address-taken code, cross-function branch
   targets) and translates every instruction with **the game's translator** (`ppc2c.py`);
   unsupported instructions are an install error that names them;
3. writes one C file: the functions, the relocated initial memory image, the function table,
   the hook table and the list of host services; it exports `wwhd_guest_module_v1`
   (`runtime/include/wwhd_guest_abi.h`) and **imports nothing from the executable** (all
   runtime entry points come through a host table), so a module is a plain shared library on
   every platform and does not depend on how the game code was built;
4. compiles it with the local compiler (`-O2 -ffp-contract=off -fno-strict-aliasing -fPIC
   -shared`) into `CACHE/<key>/<id>.dylib|.so|.dll`.

The cache key covers the ELF, the base, the translator version, the module ABI version and
the compiler with its flags. A cached module is reused; a port update that changes the
translator or the ABI changes the key and the module is rebuilt on the next start (a few
hundred milliseconds per mod). A game rebuild alone does not invalidate modules; the runtime
re-validates hook targets on load (every target must be a function entry of this build).

### Manager startup and local tools (phase 1)

Guest packages use the same one-time code trust dialog as native packages. The
fingerprint is the SHA-256 of `mod.elf`, so rebuilding a module does not ask again.
The manager freezes the enabled guest set and options during initialization.
After memory and dispatch initialization, before guest threads start, it inspects,
allocates, builds and loads that set in dependency order. Failures appear in each
package's details in the Mods tab. Later enable, disable, profile and option changes
need a restart; an active guest module remains resident until the process exits.

Memory assignments are 64 KiB aligned and persisted in `profiles.json` under
`guest_regions`. Valid assignments remain stable, including those of disabled
installed mods. Growing a mod may move it and rebuild its module; removing a mod
releases its assignment. Overlapping or invalid saved assignments are repaired.
A guest startup dependency must already be active (content/another guest mod), or
be a settings preset; a native plugin loaded later during gameplay cannot supply
a startup dependency.

Setup writes `guest-sdk.json` in the game data directory with its Python command,
compiler argument vector, translator path and SDK headers. Keep the release tools
and local compiler installed. If they are missing, the Mods tab asks you to run
setup again. Development builds may select an equivalent JSON file with
`WWHD_GUEST_BUILD_CONFIG`. The bridge runs argument vectors directly, without a
shell; `--cc-json '["compiler", "arguments"]'` preserves paths containing spaces.
The original `--cc` interface remains supported for catalogue integrations.

On Windows, the standard LLVM installer and llvm-mingw do not include the PowerPC
backend. Modders should use the MSYS2 CLANG64 clang/lld packages (an all-target build),
while players' host modules still compile with setup's pinned llvm-mingw. Pass
`-m elf32ppc` to lld to select ELF output on Windows, where MSYS2 defaults to PE. The
[MSYS2 LLVM package recipe](https://github.com/msys2/MINGW-packages/blob/master/mingw-w64-llvm/PKGBUILD)
selects all targets for its clang build. CI verifies the actual PowerPC compilation.

### Build-step contract additions (phase 1)

`build_guest_mod.py PACKAGE --inspect --base ADDR --json` reports the relocated
`memory_size`, the 64 KiB rounded `allocation_size`, `base`, package `id` and
`elf_sha256` without invoking a compiler. The manager uses this to allocate the
mod region before building. Bases must be 64 KiB aligned in the mod region.
The existing build command and last-line JSON contract remain supported; successful
build and cache-hit responses now also contain these memory and ELF metadata fields.

Cache keys include the actual guest translator, `ppc2c.py`, build script and runtime
ABI/header bytes, in addition to their version strings, ELF, base, package ID,
compiler command/version and flags. This also invalidates development caches when
the translator or CPU layout changes without a release version bump. ELF paths are
relative to the package; parent paths, absolute paths and symlinks are rejected.

The `guestmods` CI workflow compiles the examples with Apple CLT, pinned llvm-mingw
and pinned zig selected by setup. The modder-side PowerPC compiler remains clang
with lld. These tests use synthetic mod code and require no game files.

Errors for the player are short (`--json`: `{"ok": false, "error": "..."}`), for example
"instructions the translator does not support: …", "the mod needs guest API 2; this game
supports 1", "the local compiler could not build the mod (see …/build.log)". Load-time
errors (hook target is not a game function, function already replaced by another mod, a
host service missing in this version, memory overlap) leave the mod unloaded and are logged.

## Headers for modders (public sources)

The SDK generator reads a fresh, clean HTTPS clone of the public
[HD decompilation](https://github.com/ZeldaWWHDDecomp/wwhd). It never executes code
from that checkout. The generated files retain its CC0 notice and source revision.
Private decompilation branches and recompiled game output are not inputs.
[The public GameCube decompilation](https://github.com/zeldaret/tww) remains useful
for names and semantics; its layouts must not be substituted for HD layouts.

```sh
git clone https://github.com/ZeldaWWHDDecomp/wwhd.git build/public-wwhd
git -C build/public-wwhd checkout 47e1dbc3886cfd8233859dffd73efc41a04a9130
python3 tools/guestmod/regenerate_sdk.py --public-clone build/public-wwhd
# CI performs this check without modifying the committed files:
python3 tools/guestmod/regenerate_sdk.py --public-clone build/public-wwhd --check
```

`wwhd/functions.h` gives every public verified function a named hook address.
Ambiguous names retain an address suffix. `wwhd/bindings.h` declares callable
functions with supported signatures; object pointers are opaque `void*`, and
names use a `wwhd_` prefix. Unsupported signatures are reported rather than guessed.
The JSON inventory retains their original public declarations for further curation.
The current public revision resolves every verified declaration (22,853 unique callable
bindings). Public `Pair32` and six-byte vector returns explicitly use two integer registers;
the SDK exposes them as `wwhd_gpr_pair`, with `WWHD_RESULT_R3` and `WWHD_RESULT_R4` accessors,
rather than declaring a C struct return with a hidden result pointer. For `SxyzResult`,
r3 contains x/y and the upper 16 bits of r4 contain z; its lower 16 bits are not part of
the value. The generator validates these public ABI adapters and curated enum definitions
before emitting their declarations. A synthetic translated-module execution test checks
the PowerPC register order with each desktop host compiler.
These addresses target USA version 0. Functions absent from the public decomp
remain hookable by address when hook checks are compiled in.

`wwhd/data.h` names the public save/resource pointer slots, matrix stack, zero
vector and item table bases/strides. It contains no initialized game data.

Curated `actor.h`, `link.h`, `camera.h`, `items.h` and `messages.h` provide partial
HD views. Named scalar fields have compile-time offset checks; unknown compound
fields remain accessible through the byte view. Source qualifications about
inferred fields still apply. `save.h` exposes the documented status prefix and
save-info pointer rather than assuming a fixed live save address. The views require
a 32-bit guest target, and compile as C or C++ with clang.

### European installs

Modders target USA addresses in a single PowerPC ELF package. At inspection and build,
the port translates hook targets, game calls, original calls, function pointers and
`WWHD_GAME_DATA` relocations through the installed game's `tools/recomp/builds` map.
The running executable selects the build; USA and EU modules use separate cache keys,
which also fingerprint the address map. Changed or unmapped functions are refused with
an error naming their public SDK function when available and its USA address.

Use `WWHD_GAME_DATA(address, type)` for constant game data addresses and the generated
table accessors for indexed data. Raw integer-to-pointer casts embed USA addresses and
are unsupported for portable mods. EU refuses packages built with the older SDK, whose
data addresses had no relocations; their authors must rebuild against this SDK.
Object layouts must also be valid for both builds; an address map does not translate
member offsets or game object contents. The standalone builder accepts `--build EU`
for testing; the manager supplies this automatically.

## Host services

Services use the game ABI and resolve by name when the module loads. ABI 2 marks each
imported service call with its originating mod instruction address in `Cpu::pc`.
This identifies the owning package even for mod functions called through game function
pointers or nested cross-mod calls. Old modules must rebuild; the cache hashes the ABI
header and translator sources, so this happens automatically.

| Service | Phase 1 behavior |
| --- | --- |
| `wwhd_log`, `_int`, `_hex`, `_float` | Log lines tagged with the calling mod ID. |
| `wwhd_config_int`, `_bool`, `_float`, `_string` | Typed values from the manager's startup snapshot; numeric/bool calls use their fallback on missing or wrong-type values. `_float` returns a double. Strings include enum options and copy into a caller-owned guest buffer. |
| `wwhd_malloc`, `wwhd_free` | Per-mod 16-byte-aligned guest heap. `guest.heap_size` chooses bytes (default 256 KiB, maximum 8 MiB); null on exhaustion. Metadata stays in guest memory, so restoring it restores allocation state. |
| `wwhd_input_read` | Read-only VPAD-style buttons, sticks and touch in `wwhd_input_state`. |
| `wwhd_file_read`, `wwhd_file_write` | Flat filenames in `ModManager/Data/<id>`, at most 1 MiB per call. No directory components, symlinks, hardlinks or Windows device names. Write replaces the file. Both return bytes transferred, or -1 on failure. |
| `wwhd_logic_dt`, `wwhd_logic_step` | Seconds in the current logic step (true-60 scaling included), and the full logic-step counter. |
| `memcpy`, `memmove`, `memset` | Compiler-generated struct copies and explicit guest-memory operations. |

Option and enabled-set changes take effect on restart. `WWHD_GUEST_OPT_*` and the
prototype's unchecked `WWHD_GUEST_MODS` direct-library loading are retired; install
packages through the mod manager and its code trust dialog.

The HUD service is phase 2. Its proposed interface is renderer-independent submission
of text, rectangles and mod-owned RGBA images, with opaque per-mod resource handles,
explicit guest buffer lengths, frame-scoped draw lists and cleanup at shutdown. The
host would copy pixels/text before returning and render the lists through Metal/Vulkan
at the overlay stage. No guest pointers would be retained by a renderer. Audio streams,
events and additional compiler helpers also remain future work.

## Phase 2 interfaces and integration

The following HUD interface is a proposal, not an available phase 1 import:

```c
typedef u32 wwhd_hud_list;
typedef u32 wwhd_hud_image;
wwhd_hud_list wwhd_hud_begin(u32 screen); /* TV or GamePad; zero on failure */
void wwhd_hud_rect(wwhd_hud_list list, f32 x, f32 y, f32 w, f32 h, u32 rgba);
void wwhd_hud_text(wwhd_hud_list list, f32 x, f32 y, f32 size,
                   u32 rgba, const char* utf8, u32 bytes);
wwhd_hud_image wwhd_hud_image_rgba(const void* pixels, u32 width, u32 height, u32 stride);
void wwhd_hud_image_draw(wwhd_hud_list list, wwhd_hud_image image,
                        f32 x, f32 y, f32 w, f32 h, u32 rgba);
void wwhd_hud_commit(wwhd_hud_list list);
void wwhd_hud_image_release(wwhd_hud_image image);
```

Coordinates use a documented 1280×720 logical canvas with an aspect-preserving safe
rectangle; the runtime supplies the actual screen rectangle. Commands draw in submission
order. Text uses a runtime font, bounded UTF-8 lengths and explicit sizes. Images are
uncompressed RGBA8 with checked stride, dimensions and byte length; resource and draw-list
quotas are per mod. Handles are owned by the calling mod and cannot name another mod's
resources. Calls synchronously copy guest data; no guest pointers cross to renderer threads.

A commit atomically publishes an immutable list for subsequent rendered frames. A logic
hook can update it once per step while interpolation reuses it between steps. Publishing
an empty list hides the panel. Released images remain alive until queued render work has
finished. Metal and Vulkan draw at the same overlay stage; GamePad drawing is explicit.
Host resources are not guest memory: state-load notification must let mods rebuild handles
and publish a fresh list after a full-state restore. Shutdown releases every owned handle.
These rules also resolve the minimap/dragon panel overlap through configurable placement,
rather than hard-coded renderer patches.

Audio streams should follow the same ownership and bounded-copy model: open a stream with
an explicit sample rate/channel count, queue bounded interleaved PCM buffers, query queue
space, and close it. A runtime mixer resamples as needed, obeys mute/volume settings and
rejects invalid handles or oversized queues. It must support cancellation and state-load
reset without retaining guest buffers. This supplies synthesized mod music; access to the
game's own effects remains through game functions. No HUD or audio-stream code is built in
phase 1.

Catalogue integration can already call `build_guest_mod.py` through its documented
inspect/build JSON contract. Follow-up work is to connect catalogue progress/errors and
use the manager's persisted allocation, selected host compiler, trust fingerprint and
cache directory. The catalogue must not allocate independently or trust a previously built
native module without checking its ELF. Dependency resolution remains the manager's job.

Android needs a separate plan for host module compilation, executable code loading,
package storage/document URIs and lifecycle handling. Desktop compile-test results do not
prove Android support. Until that work and device tests are complete, Android guest mods
remain unsupported. Further compiler helpers, events and cross-mod exports should have
versioned contracts and focused tests before being added.

## Porting the existing minimap and dragon prototypes

These are porting plans, not completed ports. Keep their existing gameplay limitations
visible and preserve local asset preparation: no maps, models or game sounds belong in
distributed mod packages.

For **gc-minimap**, replace the built-in switch with a guest package and manager options.
A return hook on the public Link execute target captures position, heading and stage/event
visibility once per logic step. The generated actor views and public game accessors replace
host `ppc_ptr` reads. Keep sector/map-coordinate math in guest code; host mutexes disappear
when state stays in guest globals. Move the renderer-specific panel to the phase 2 HUD
interface, including the frame, heading marker and arrows. Preserve the existing rule of
hiding sectors without verified bounds. Stage/event fields not yet curated into the SDK
need public-source declarations and offset checks before the port uses them.

The cache builder still runs locally against the player's own files. Put its results in the
mod's own data folder, with flat filenames; split files larger than the phase 1 1 MiB
per-call limit. A future catalogue preparation step can manage this. A guest package cannot
read the prototype's arbitrary external cache path through the phase 1 file service.
Verify no state reads or resource uploads when disabled, and compare the panel on both
renderers once the HUD interface exists. Full visual parity is therefore phase 2 work.

For **dragon**, rewrite `Cpu*`/host-memory wrappers as typed PowerPC hooks and replacements.
Use public names for Link execute, camera follow, Valoo lifecycle, resources, song handling
and save-slot operations. Preserve the port's outer climb/true-60 hooks. Entry/return hooks
cannot change argument registers or the result, so operations that redirect arguments or
suppress the original need a replacement plus `WWHD_GAME_ORIGINAL`, scoped to the mod's own
actors. Replacement conflicts must remain explicit. Generic public audio trampolines need
signature curation before using them as semantic song APIs.

Move ride/quest state and tagged actor bookkeeping into guest globals or the mod heap;
use `wwhd_logic_dt()` rather than assuming 30 steps/s. Use the input service and typed
options. Store per-slot quest progress with flat per-mod filenames, retaining explicit
new-game/reset behavior. Full states restore guest quest/heap state, but external progress
files are not rewound: do not immediately overwrite restored state by rereading a newer
file. Save-slot copy behavior and state-load notification need explicit follow-up tests.
Use game resource/effect functions for locally available models, animations and effects.
The letter/flight panels await HUD support, and synthesized melody mixing awaits the audio
stream interface. Test cancellation, ordinary story actors, boat/leaf recovery, save slots,
true-60 timing and simultaneous minimap placement before claiming parity. Existing route,
collision and presentation limitations remain separate from the SDK port.

## Trust

A translated guest mod is compiled to native code in the game process. Two properties limit
it compared with a v1 native library: its loads and stores go to the 4 GiB guest window, and
it reaches the host only through the services above and the game's own functions (including
the runtime's emulated system calls, e.g. the game's file access). That is a mitigation, not
a sandbox: a mod can still crash the game, corrupt the save the game writes, and any bug in
the runtime's emulated system calls is reachable. Recommendation: the same one-time trust
confirmation as v1 native mods, fingerprinting `mod.elf` (the platform-independent code)
instead of a library; a rebuilt module of the same ELF does not ask again. The install step
itself is safe to run on untrusted packages: the generated C contains only numbers and
checked identifiers.

## Mod manager integration

The catalogue's `build_guest_mod` setup step (browse/install/setup flow) runs, per enabled
guest package and after every port update:

```
python3 tools/guestmod/build_guest_mod.py <ModManager>/Mods/<id> --out <ModManager>/GuestBuild \
        --base <assigned base> --cc "<sdk/manifest.json toolchain cc>" --include <sdk>/include --json
```

- The manager assigns each installed guest mod a 64 KiB-aligned base in the guest mod region
  (from its memory size, stored in `profiles.json`) and rebuilds when it changes.
- The last stdout line is JSON: `ok`, `module`, `cached`, `error`. The manager shows `error`
  in the package details and keeps the package unloaded.
- On start, the runtime loads the modules of the enabled guest packages before any guest code
  runs (the prototype takes `WWHD_GUEST_MODS=path,...`). Enabling, disabling and changing the
  load order need a restart in the first version (hooks are installed before the game
  threads start).
- Requires game code built with `--mod-hooks`; setup passes it by default once the decision is
  made. Without it the runtime logs that guest mods are unavailable.

## Players: enabling PowerPC code mods

Code-mod support starts **off**. Content replacements, settings presets, Cemu packs and
Native SDK v1 mods do not need it. In **Settings → Mods**, choose **Enable code mods
(PowerPC mods)**; the macOS Gameplay menu has the same option. Confirm the game-code
rebuild, which normally takes a few minutes. The dialog shows translation, compilation
progress and any errors. Cancel leaves the previous build selected. Keep the complete
release folder and the compiler downloaded by setup: rebuilding uses those local tools.

When setup finishes, choose **Restart now** or restart later. The new build becomes active
at restart. Turning support off follows the same rebuild and restart flow. Setup caches
completed variants separately using a fingerprint that includes the hooks option, game,
compiler and packaged build inputs; returning to an intact cached variant avoids compilation.

You can install a guest package while support is off. The manager offers to enable support;
enabling a guest package also shows the existing native-code trust confirmation. A mod
accepted through that offer is queued only after a successful rebuild and becomes active
on the next launch. The running build's generated hook marker is checked independently
of the saved setting, so changing a preference alone cannot make an unsupported build load
PowerPC code mods. Existing enabled guest packages remain installed when support is off.

See [performance and limits](#performance-and-limits) for rebuild timings and the performance gate.
The opt-in real-game driver is `tools/guestmod/test_code_mods_e2e.py --help`.

For automated runs, `WWHD_CODE_MODS=0` or `1` overrides the preference. Setup accepts
`--code-mods 0` or `1` (default off). The runtime override does not compile game code or
invent hook support: launch a variant built with the matching option. Generated C contains
no SDK hook checks or registration metadata with support off.

## Writing and installing a mod

Guest mods currently target desktop builds. Android still builds, but guest packages are
unsupported there. A player's build needs game code translated with `--mod-hooks`;
this flag remains opt-in pending a quiet-window performance rerun.

### Install the modder toolchain

Use clang with the PowerPC backend and lld. The player's host compiler is separate:
setup already installs or selects Apple CLT, llvm-mingw or zig for module compilation.
Keep the downloaded host compiler for guest builds. If removed, run setup again to
restore it. Portable releases store guest-build paths relative to `guest-sdk.json` so
the release folder can move; system tools keep their external paths.
Modders do not need devkitPPC, and this SDK does not bundle a modder compiler.

- macOS: `brew install llvm lld`. Use `$(brew --prefix llvm)/bin/clang` and
  `$(brew --prefix lld)/bin/ld.lld`; Apple's system clang lacks the required target.
- Windows: install MSYS2, open its CLANG64 shell, then run
  `pacman -S mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-lld make`.
  Use that shell's `clang` and `ld.lld`. The official Windows LLVM and llvm-mingw
  packages may omit the PowerPC backend; llvm-mingw is the player's host compiler.
- Debian/Ubuntu Linux: `sudo apt install clang lld make`. Other distributions provide
  equivalent LLVM packages. Check that `clang --print-targets` lists PowerPC.

### Write and build

Include `wwhd_guest.h` and the generated `wwhd` headers. Hook targets use
`WWHD_ADDR_<public_name>`; callable declarations use `wwhd_<public_name>` where the
name is unique. Ambiguous names have an address suffix. Entry hooks receive the game's
arguments. Return hooks receive those arguments again and preserve the game result.
Only one replacement may own a target; a conflict reports both package IDs.

```c
#include "wwhd_guest.h"
#include "wwhd/functions.h"

WWHD_HOOK(WWHD_ADDR_daPy_Execute, on_link_step, (void* link)) {
    static u32 steps;
    if (++steps == 1) wwhd_log("Link's first logic step");
}
```

Compile from the repository root (substitute your LLVM executable paths):

```sh
clang --target=powerpc-unknown-eabi -mcpu=750 -O2 -ffreestanding \
  -fno-builtin -nostdlib -fno-jump-tables -ffunction-sections -fdata-sections \
  -Iruntime/guest/include -c mod.c -o mod.o
ld.lld -m elf32ppc -r mod.o -o mod.elf
```

A release SDK ships modder headers in `sdk/guest/include`; use that directory instead
of `runtime/guest/include` when compiling outside a source checkout.

The relocatable ELF is identical across desktop platforms. Do not link it to a fixed
address. Use `WWHD_GAME_ORIGINAL` with the generated target address to call below all
mod hooks. The port's own interpolation and true-60 hooks remain outside mod hooks.
Use `wwhd_logic_dt()` for time-based behavior, and avoid interpreting rendered frames
as logic steps. The examples demonstrate entry/return hooks and original calls.

### Package and install

Place this manifest beside `mod.elf` (replace the metadata for your mod):

```json
{
  "format_version": 1,
  "game_id": "wwhd-usa",
  "id": "hello-link",
  "name": "Hello Link",
  "version": "1.0.0",
  "kind": "guest",
  "guest": {"api_version": 1, "elf": "mod.elf", "heap_size": 262144}
}
```

Choose the folder in **Mods → Installed packages → Choose folder → Install package**.
Alternatively, from inside the package folder run
`python3 -m zipfile -c hello-link.wwhdmod manifest.json mod.elf` and choose that package.
Distribute only the manifest, your ELF and your own permitted resources.

Enable the installed package, accept the same trust confirmation used for native mods,
and restart. Confirmation is tied to the ELF hash; changing the ELF asks again.
The manager assigns memory, translates the ELF and builds a cached native module at
startup. Build/load failures appear in the Mods tab and leave that package unloaded.
Changing enabled mods or options takes effect on the next restart. Typed options use
the normal manager manifest schema; read them with `wwhd_config_*` rather than environment
variables. Files are restricted to flat names in the package's own data directory.

For updates, disable the package, restart, then reinstall the same ID. Configurations
remain associated with that ID. A translator, ABI or compiler change invalidates the
module cache automatically; it does not require redistributing the ELF.

## Save states

Full states capture the complete guest mod region (`0x7F000000`–`0x80000000`),
including module data and guest heap metadata. Zero chunks remain sparse. Restoring a
full state restores allocations as well as the bytes in those allocations.

Both full states and portable `.wwstate` files record the loaded guest mods by ID and
version. Loading a state with a different set displays a warning and continues; ordering
alone does not cause a warning. Older states without this metadata count as an empty mod
set. Portable states restore game progress and position, rather than mod memory.

Guest modules remain selected at startup. Loading a state does not install, build, enable
or disable mods. Use the same mod versions that created a state when reproducing gameplay.

## Prototype: what exists and how to run it

| Part | File |
| --- | --- |
| generator option `--mod-hooks` | `tools/recomp/recomp.py` |
| check macro, `Cpu::mod_skip` | `runtime/include/ppc.h` |
| module ABI | `runtime/include/wwhd_guest_abi.h` |
| loader, hook chains, host services | `runtime/src/mods/guest_mods.cpp` |
| translator, install-time build | `tools/guestmod/guestmod.py`, `tools/guestmod/build_guest_mod.py` |
| SDK header | `runtime/guest/include/wwhd_guest.h` |
| tests | `tools/guestmod/test_guestmod.py` (needs a PowerPC clang: `WWHD_PPC_CLANG`, `WWHD_PPC_LLD`) |
| examples | `examples/guest-mods/heart-ticker` (entry + return hook of Link's per-step function, calls a game function; hearts tick down a quarter at a time to half and refill), `examples/guest-mods/addcalc-replace` (replaces `cLib_addCalc2` by an equivalent implementation; every other call goes to the game's original) |

```sh
python3 tools/recomp/recomp.py game/code/cking.rpx build/gen --mod-hooks
cmake --build build/cmake                                   # as usual
make -C examples/guest-mods CLANG=/opt/homebrew/opt/llvm/bin/clang LLD=/opt/homebrew/opt/lld/bin/ld.lld
python3 tools/guestmod/build_guest_mod.py examples/guest-mods/heart-ticker --out build/guestcache --base 0x7F000000
python3 tools/guestmod/build_guest_mod.py examples/guest-mods/addcalc-replace --out build/guestcache --base 0x7F100000
```

These commands describe the prototype build. Install the packages through the manager
as described above; direct `WWHD_GUEST_MODS` loading has been retired.

Historically verified end to end on macOS arm64 (headless scripted run, copy of a save): both modules
load, the heart display changes by quarter hearts during gameplay, the replacement handles
half of the ≈ 600,000 `cLib_addCalc2` calls of the run with no visible difference, the mod's
call of a game function goes through the other mod's replacement, and the return hook sees
Link's actor pointer.

## Performance and limits

Code-mod support starts off. With support off, generation emits no hook checks or
registration metadata. Enabling support rebuilds game code and takes effect after restart.
Completed variants are cached. Local cold rebuilds took about 88–114 seconds with four
compiler jobs; cached selection took about 0.08–0.12 seconds. Timing varies by machine.

The historical no-mod A/B collected 15 interleaved pairs per renderer:

| Renderer | Metric | Baseline median / IQR | Hooks median / IQR | Median cost |
| --- | --- | --- | --- | --- |
| Metal | Frame ms | 6.0650 / 0.1033 | 6.0894 / 0.1191 | +0.40% |
| Metal | Logic CPU ms | 3.8680 / 0.0520 | 3.9360 / 0.1170 | +1.76% |
| Vulkan | Frame ms | 4.6961 / 0.0740 | 4.6824 / 0.1454 | -0.29% |
| Vulkan | Logic CPU ms | 3.8300 / 0.0950 | 3.8860 / 0.1060 | +1.46% |

**The IQR exceeds the measured difference for every metric.** These historical
measurements do not establish the overhead of the current hook implementation.
A quiet-machine rerun is required before enabling support by default; the limit is
2% median overhead on both renderers. `tools/bench/run_bench.py` supports separate
`--variant-binary` executables, interleaved `--runs 15`, inclusive quartiles and
per-pair differences in its JSON report. Failed warm-up stops the campaign.

Android guest mods are not supported in phase 1. Live Windows/Linux rebuild and restart
UI, forced-crash recovery and a genuinely full disk still need platform validation.
Modules use native code and require the same trust confirmation as native mods.

## Fixed phase 1 decisions

Headers use public sources only. Every game function is hookable. Modders use clang
and lld; no compiler is bundled for them. Guest packages use the native trust dialog,
bound to the ELF hash. Full states include mod memory, and both state formats warn
without blocking when mod IDs or versions differ. Enabling and disabling takes effect
only after restart. Hook checks remain off by default until the quiet-machine
Metal and Vulkan A/B gate demonstrates at most 2% median overhead.

## Remaining work

Phase 2 covers the [HUD/audio interfaces](#phase-2-interfaces-and-integration),
state-load notifications, broader translator coverage and ELF fuzzing, cross-mod exports,
and catalogue integration. The [minimap and dragon porting plan](#porting-the-existing-minimap-and-dragon-prototypes)
identifies their required services and reusable game-side hooks. Runtime mod-set changes
continue to require a restart.

The broad generated declarations remain committed so modders can discover public names
without running the generator. A smaller curated set would suffice for the current two
examples, but would limit other mods; curation can be evaluated separately.
