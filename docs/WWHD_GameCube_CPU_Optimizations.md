# GameCube-informed HD CPU optimization (2026-10-06)

## What transfers from the decompilation

The GameCube decompilation provides a useful model of resource ownership, not
drop-in GX2 rendering code. In
[J3DModel::entryModelData](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphAnimator/J3DModel.cpp),
model vertex data is attached separately from allocated shape/material packets
and draw/normal matrices. The same file also handles CPU deformation, so treating
every vertex buffer as immutable would be incorrect.

[J3DShape::drawFast](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphBase/J3DShape.cpp)
loads vertex arrays and draws multiple matrix groups. Geometry ownership is not
the same as the identity of the latest draw or vertex binding. Separately,
[J3DDrawBuffer::entryMatSort](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphBase/J3DDrawBuffer.cpp)
groups compatible material packets, and
[J3DMatPacket::draw](https://github.com/zeldaret/tww/blob/main/src/JSystem/J3DGraphBase/J3DPacket.cpp)
loads shared material state before iterating shapes. These are useful boundaries
for investigating repeated HD preparation, not proof that draws can be merged or
that HD shadows use the GameCube implementation.

The port already identifies HD J3D model calculation, view calculation and UBO
updates in `runtime/src/interp.cpp`. Its weather interpolation explicitly records
retained GameCube effect layouts with a larger HD packet base. The existing HD
Vulkan vertex path, however, uploads fresh geometry by default. Its optional
reuse cache remembers only two source keys per binding and compares against
mapped GPU memory.

## Geometry snapshot implementation

`WWHD_VK_GEOMETRY_SNAPSHOTS=1` selects an independent bounded host-backed geometry
cache before the old vertex snapshot path. It can reuse one geometry source
across nonconsecutive shapes and vertex bindings, including a shorter unchanged
prefix of a larger captured payload.

- 64 buckets with four entries each; at most 256 KiB of payload retained per entry
  (64 MiB maximum payload size; vector capacity and metadata add overhead).
- Every candidate compares all requested fresh guest bytes with ordinary host
  memory, including deformed geometry and interior/last-byte mutations.
- A miss captures host bytes before allocating and uploads that same capture.
- GPU slice reuse requires the same device and submission generation and a
  sufficiently large prior upload. Host bytes may survive retirement; slices
  cannot. Old uploads remain immutable for already queued draws.
- Unbounded vertex requests, oversized payloads, null/empty inputs and wrapped
  guest ranges retain fresh-upload behavior. Vertex-copy-window mode uses its
  existing separate path.

The cache changes upload preparation, not guest simulation, draw order,
material/texture processing, topology, shadows, or presentation synchronization.
It remains opt-in pending broader scene coverage.

## Verification

The isolated PR branch based on upstream `5070881` also passes a fresh native
Clang Release build, all nine CTest checks (including `geometry_snapshots`), the
standalone geometry test with undefined-behavior trap instrumentation, and the
actual-device renderer smoke with synchronization validation requested. The
geometry readback fixture passes through ten submissions with no validation
errors. The saved-scene timings and paced gameplay validation below belong to
the earlier local experimental build, not this isolated upstream build.

The host test covers every-byte mutation, prefix growth/shrinkage, nonconsecutive
sources, generation/device changes, unreadable GPU mappings, failed/throwing
factories, mutation during allocation, range/size fallback and bounded eviction.
The GPU smoke fixture verifies original and changed snapshots through readback
over ten asynchronous submissions, wrapping the submission ring.

These measurements used a local experimental build with other renderer changes
held constant in both variants, including direct endian index uploads. They
estimate the incremental cache effect in that build; they are not measurements
of otherwise-clean upstream `main`. Only the geometry cache and its tests are
included in this PR.

Timing uses the existing saved Outset fixture, AVX2 Release executable, warmed
caches, 4K rendering, full-resolution AO,
6,210 draws/frame, 2,048-draw batching with cap two, direct endian index uploads,
and identical uncapped diagnostic mode. Uncapped throughput is not normal-game
FPS. Measurements cover frames 2,401–3,600 after restore at frame 1,200.

Eight same-executable runs used off/on/on/off followed by on/off/off/on order.
All measured windows held 6,210 draws and 49 passes/frame, with no shader or
pipeline creation during measurement. Each trial contributed 1,200 frames.

| Trial | Fresh uploads CPU (ms/frame) | Geometry cache CPU (ms/frame) |
| --- | ---: | ---: |
| A | 11.2500 | 9.2839 |
| B | 8.9063 | 9.9610 |
| C | 9.5313 | 9.0756 |
| D | 10.7553 | 9.0757 |
| Mean | 10.1107 | 9.3491 |

The mean renderer CPU reduction is 0.7617 ms/frame (7.5%), with three of four
adjacent comparisons improving. Control variation is substantial, and B goes
in the opposite direction; these samples do not establish a universal gain.
Uploads fall consistently from 30.26 to 15.52 MiB/frame (48.7%). Pooled diagnostic
throughput is 66.03 versus 67.02 swaps/second (1.5% higher). This is a CPU-work
improvement estimate in this fixture, not a pacing fix or proof of gameplay FPS.

All eight 4K RGB screenshots are either identical or differ in the previously
observed 1,607 water pixels within `(1511,1225)–(3840,1289)`. Both variants occur
with reuse disabled and enabled. An enabled capture was visually inspected.

The first preliminary control, `gc-geometry-off-a`, is excluded: the harness
rejected the renderer's updated presentation log format, and the smoke fixture
was subsequently added to the executable. The eight accepted runs all use the
same final executable. The harness now verifies immediate presentation on both
screens with either supported log format.

- Executable SHA-256: `7F7F0513052EBA4FD7872ECCD7359A2B647DEB3310E7F5CABF5F2B3792DFE6BE`.
- State SHA-256: `2E35F15E439E553FC48FC68CD554376205BE7BAFDB557A9CF17E3B084A16E420`.
- Local artifacts: `build/windows/state-benchmarks/gc-geometry-*` and
  `gc-geometry-summary.json`; GPU smoke logs under `build/gamecube-geometry`.

In the local experimental build, the native Release build and all ten CTest
checks pass. The standalone geometry
test also passes Clang's undefined-behavior instrumentation in trap mode
(`-fsanitize=undefined -fsanitize-trap=undefined`), which avoids the installed
Windows sanitizer runtime's linker incompatibility. The actual-device
renderer smoke passed with Khronos validation and synchronization validation
requested, including exact geometry readback through ten asynchronous
submissions. It reported no validation errors; existing layer-discovery and
unused-fragment-output warnings remain. A separate paced gameplay run with
Khronos synchronization/full submit-time validation requested completed 2,520
frames, including save-state restoration, with zero validation errors or sync
hazards. Its 23 warnings concern layer discovery and unused fragment outputs.
Artifacts are under `gc-geometry-validation`; its timing is excluded from the
performance results above.

To enable the experiment with the rebuilt native executable:

```powershell
$env:WWHD_VK_GEOMETRY_SNAPSHOTS = '1'
./build/windows-avx2/wwhd.exe --game '<your game directory>'
```

Unset the variable or set it to `0` to use the previous path. No new rendering
default was changed. Direct endian index uploads were held enabled equally in
every trial to measure the additional geometry-cache effect.

## Further targets supported by the comparison

1. Attribute HD shape submissions to models/material groups, then look for
   repeated support-uniform and texture preparation within a group. Preserve
   guest byte validation and per-draw attachment/feedback dependencies.
2. Trace the HD equivalents of model material updates and matrix-group drawing
   before attempting native replacements. Preserve deformation and exact guest
   floating-point behavior; renderer CPU savings require reducing submissions or
   preparation, not merely executing guest routines faster.
3. Investigate shadow caster eligibility separately from main-camera visibility.
   The measured 3,426 shadow draws are a lead, not evidence that those draws are
   redundant. Offscreen actors can cast visible shadows.
