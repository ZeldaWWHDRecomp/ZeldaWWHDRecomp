# How this port works, and how it differs from emulating with Cemu

Cemu and this port run the same game binary (`cking.rpx`) on a Mac. Cemu emulates the Wii U well
enough to run the original code as if it were on the console. This port translates the game's code
into a native macOS program ahead of time and replaces everything around it (operating system,
graphics, audio, input) with native implementations of the APIs the game calls.

## At a glance

| | Cemu (emulator) | This port (static recompilation) |
|---|---|---|
| CPU code | PowerPC translated at run time (JIT/interpreter) | translated once, ahead of time, PowerPC → C → native ARM64 (`clang -O3`) |
| Scope | thousands of Wii U titles | one game; the recompiler output is specific to this binary |
| Cafe OS | high-level emulation of the system libraries | high-level emulation of the libraries this game imports, as native code |
| Graphics | GX2 writes PM4 command packets; an emulated GPU command processor parses them, tracks state, and renders via Vulkan/OpenGL/Metal | the GX2 API itself is implemented on Metal; no command packets, no command-processor emulation |
| Shaders | Latte microcode → GLSL/SPIR-V/MSL (Cemu decompiler) | same decompiler (vendored), MSL only, persistent recipe cache |
| Threads | emulated cores with a cycle-quantum scheduler on host fibers | each guest thread is a host thread; a per-core scheduler reproduces Cafe OS priority rules |
| Memory | emulated address space | guest memory mapped at a fixed host address; loads/stores are byte-swapped host accesses |
| Audio / input / text entry | emulated AX DSP + host audio; configurable controllers; ImGui keyboard | AX mixing to CoreAudio; keyboard + GameController as a GamePad; native macOS text field |
| Distribution | emulator + your game dump | recompiled program + your game dump (game files are never redistributed) |

## The CPU: static recompilation

`tools/recomp/` disassembles the game's executable and turns every PowerPC function into a C
function (`build/gen/code_*.c`):

- **Function discovery.** Call targets, relocation references (vtables, function pointers) and jump
  tables are found statically; each function becomes `void f_XXXXXXXX(Cpu* c)`.
- **Registers.** Guest registers live in a `Cpu` struct passed to every function. It's marked
  `__restrict` so the C compiler can keep them in host registers.
- **Control flow.** Branches become `goto`s, calls become C calls, and tail calls are guaranteed with
  `musttail`. Indirect branches go through a dispatch table indexed by guest address.
- **Memory.** Guest address `ea` lives at `0x200000000000 + ea` on the host. Loads and stores are
  plain memory accesses plus a byte swap (the Wii U is big-endian).
- **One set of hooks for every regional build.** Functions are named by their address in the USA
  build, which is the canonical id; translating another regional build shifts the hooks' and the
  runtime's addresses through that build's map, so the generated symbols stay the same
  ([builds.md](builds.md)).
- **Exact semantics where it matters.**
  - Paired-single math, the GQR quantization registers, and Espresso's `fres`/`frsqrte` tables.
  - Single-precision rounding, with FMA contraction disabled.
  - `lwarx`/`stwcx.` map to compare-and-swap.
  - `sync`/`lwsync`/`eieio` emit real memory barriers, because Apple Silicon reorders memory
    accesses between cores more aggressively than the Espresso.

The result is ordinary native code. The game's main thread needs about 4 ms of CPU per frame on an
M3 (the console budget is 33 ms), and there is no JIT warm-up or translation stutter.

## The operating system: native implementations of Cafe OS

The game imports a few hundred system functions. The ones it uses are reimplemented natively
(`runtime/src/hle/`, `runtime/src/threads.cpp`):

- **Threads.** Every guest thread is a host thread, but guest code runs only while that thread holds
  its emulated core. That reproduces Cafe OS behaviour:
  - one thread per core at a time;
  - strict priorities, with equal priorities taking turns;
  - preemption at function entry when a higher-priority thread becomes ready.

  The game relies on this. Letting same-core threads run truly in parallel corrupted shared data.
- **Synchronization and services.** Mutexes, events, message queues and alarms are host objects keyed
  by their guest address. Memory heaps run on host-side allocators, and the file system maps to the
  extracted game files and a local `save/` folder.
- **Audio.** AX voices are decoded and mixed natively (ADPCM/PCM, resampling, filters, the game's own
  final-mix callbacks) and played through CoreAudio.
- **System applets.** The on-screen keyboard is a macOS text field, and the error viewer logs errors
  and confirms them automatically. Online services report "unavailable".

## Graphics: native GX2 instead of GPU emulation

This is the biggest structural difference.

**On a Wii U (and in Cemu)**, GX2 is a library that encodes PM4 command packets into a ring buffer;
the Latte GPU's command processor executes them. Cemu emulates that command processor: it parses the
packets, rebuilds GPU register state, and translates draws to a host graphics API.

**Here**, the GX2 functions themselves are implemented:

1. GX2 calls update a register file with the same layout as the Latte GPU (so state is
   bit-compatible with the decompiler) and append compact commands of our own to a queue. Display
   lists recorded by the game use the same encoding. Struct arguments are copied into the command,
   just as GX2 encodes them into packets.
2. A render thread turns the commands into Metal work, like the GPU running asynchronously to the
   CPU. Concretely, it:
   - picks and caches pipeline, depth/stencil and sampler states from the registers;
   - detiles textures from guest memory with Cemu's AddrLib, and uploads them again when the CPU
     changes them: the host pages of every uploaded texture (all mip levels) are write-protected, so
     the first write to one faults once and marks it (`runtime/src/write_watch.h`); the next use of
     the texture hashes all of its bytes and re-uploads it if they differ. The game does not announce
     every such write (GX2Invalidate mostly comes in its "everything" form), and a sampled check
     showed changed textures up to 64 frames late;
   - tracks which memory the GPU has written;
   - renders into Metal textures, including texture-array targets for shadow cascades.
3. Shaders (Latte microcode) are translated to Metal Shading Language with Cemu's decompiler. Every
   translated shader and pipeline is recorded in `~/Library/Caches/wwhd/shaders.bin`; later launches
   replay it at startup, compiling in the background and on first use.
4. Presentation models the display: 60 Hz vsync, the game's swap interval (30 fps), and an sRGB TV
   buffer. The TV and GamePad screens are separate windows.

Skipping the command-processor layer removes work: there are no packets to encode and parse.
It also makes the renderer's state easy to inspect. The cost is that only the GX2 functions this
game calls are implemented, and anything the hardware does implicitly has to be modelled
explicitly. Examples are flips waiting for the GPU, aliasing render targets in the same memory,
and depth buffers sampled as textures.

## What is shared with Cemu

Cemu (MPL-2.0) is the reference for hardware behaviour and supplies vendored components in
`runtime/third_party/cemu`:

- the Latte shader decompiler (MSL backend), fetch-shader and GS copy-shader parsers;
- the address library (tiling/detiling);
- register definitions and a few GX2 structure layouts and initialisation routines.

Everything else is specific to this port: the recompiler, the CPU runtime, the OS layer, the
scheduler, the GX2 implementation, and the Metal renderer.

## Trade-offs

- **For this port:**
  - native speed with no JIT;
  - deterministic code layout (debuggable with standard tools: crash handler, `sample`, function
    traces);
  - full control over threading and presentation;
  - integrates like a regular Mac app.
- **For Cemu:**
  - runs the whole Wii U library;
  - handles hardware paths this game never uses;
  - years of compatibility work;
  - game-agnostic.

  A static recompilation has to be redone for each game, and its OS/GX2 layers only cover what that
  game uses.
- **Accuracy model.** Both emulate behaviour, not timing cycles. Cemu approximates timing with
  instruction quanta; this port models timing at the API level (vsync, flips, GPU completion,
  thread priorities).
