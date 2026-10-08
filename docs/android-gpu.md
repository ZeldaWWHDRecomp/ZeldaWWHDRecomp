# Android GPU support

Fresh GitHub devel clone, branch `android-gpu`, 2026-10-08. Reference ideas:
GreenNaugahyde/ZeldaWWHDRecompAndroid (MPL-2.0), inspected read-only. Its renderer code and
scripts were neither built nor run. This implementation uses the current upstream renderer.

## BC upload decoding

Native BC remains the upload path when textureCompressionBC and the required optimal-tiling
sample/filter/transfer features are available. Otherwise BC1–BC5 blocks are detiled using the
existing guest layout, decoded by a compute shader into a storage buffer, and copied into RGBA8
images. BC1–3 sRGB images retain sRGB sampling; signed BC4/5 use RGBA8_SNORM. Guest format/block
geometry stays compressed for address ranges, mip offsets, invalidation and upload-cache keys.
The compute output follows the renderer's existing deferred buffer retirement and image barriers.
No storage-image sRGB support is required. Graphics queues must also support compute for fallback.

The documented integer decode convention expands RGB565 endpoints by bit replication and
interpolates unsigned colour/alpha with integer division (floor). Signed alpha clamps -128 to
-127 and divides toward zero. BC1's three-colour mode has a transparent fourth selector;
BC2/3 always use four colours. CPU and GLSL implementations are independent implementations
of these same rules. Native hardware interpolation may differ by a quantization unit.

Debug switches:

- `WWHD_VK_FORCE_BC_DECODE=1`: force fallback even on BC-capable desktop GPUs.
- `WWHD_VK_BC_VERIFY=1`: drain each decoded upload and compare every output byte with the CPU
  reference, logging the first mismatch and failing the upload. Deliberately expensive.
- `WWHD_VK_BC_TIMING=1`: log CPU recording/upload duration; when verification is enabled this
  includes GPU completion and CPU comparison. These are not GPU timestamp measurements.

| Check | Result |
| --- | --- |
| CPU BC reference tests | PASS: BC1–5, signed channels, endpoint/selectors, tails, malformed lengths |
| Compute synthetic tests | PASS: 7 format variants, 3 shapes including layers/tails, 12 seeds, 252 dispatches |
| Production surface smoke | PASS: 10 linear/sRGB/signed formats, 2 mip levels, 2 layers, cache reuse and invalidation |
| Native and forced `--renderer-smoke`, MoltenVK with validation | PASS; no validation errors; existing 3D-image maintenance9 forward-compatibility warnings |
| Scripted native/forced frame comparison | PASS: two 1280×720 Windfall frames; details below |
| Upload-cost measurements | PASS: CPU recording and verification costs measured below |
| Mali/PowerVR device execution | Untested: no physical device available |

Scripted MoltenVK comparison uses the same restored Windfall state, 30 fps, no inputs, private
caches, copied saves, headless/no audio. Preselected tolerance: RGB mean absolute error ≤1/255,
and ≥99% of pixels with maximum RGB channel error ≤3/255. Endpoint rounding can differ from
native compressed filtering, and nonlinear material/edge operations amplify a small number of
errors. The exact CPU/GPU fallback comparison remains byte-exact.

| TV frame | RGB mean error (byte units) | Pixels within 3 | Maximum channel error |
| --- | --- | --- | --- |
| 900 | 0.108692 | 99.9252% | 67 |
| 960 | 0.107773 | 99.9224% | 41 |

There were zero GPU/CPU mismatches across 2,197 decoded mip uploads (78.9 MiB compressed input).
With verification disabled, CPU recording cost per mip was median 18.2 µs / p95 53.5 µs after
excluding the first upload; total 58.1 ms across 2,196 uploads. The first upload cost 54.5 ms,
including shader/pipeline creation. After state restoration, 943 uploads cost 25.0 ms total,
median 17.7 µs / p95 50.4 µs. Verification's GPU drains and CPU comparisons raised the median to
301.4 µs / p95 2253.4 µs. These timings measure CPU upload work, not isolated GPU execution.
Steady frame time was 33.78 ms native, 33.54 ms forced+verify and 33.54 ms forced without verify;
render CPU was 3.41 / 3.25 / 3.31 ms. One shared-machine sample per variant cannot establish a
speedup or a precise steady GPU overhead. Startup/restore costs and the forced allocation traffic
remain relevant on mobile devices.

## Custom Android Vulkan drivers

The Graphics settings list installed drivers, show the active name/version, and provide install,
select and remove actions. Installation uses Android's document picker and a bounded private
cache copy; no broad storage permission. Selection applies on restart. The package parser accepts
AdrenoTools schemaVersion 1 metadata and root-level arm64 ELF shared libraries, validates minimum
API, and reuses the existing ZIP reader's traversal/symlink/size/CRC checks. Each successful install
gets a unique identity, so two versions with identical names cannot share their pipeline cache.
Removing an inactive driver deletes its default cache; the active driver requires a system-driver
restart before removal. Explicit debug cache overrides are keyed by their requested path inside the same private
driver directory, so removal deletes them too. Desktop override paths keep their existing behavior.

libadrenotools is pinned to `8fae8ce254dfc1344527e05301e43f37dea2df80`, including its upstream
liblinkernsbypass submodule. Both BSD-2-Clause notices ship in APK assets/licenses. Four upstream
hook libraries are packaged alongside the app's native libraries; legacy native-library extraction
supplies their real directory to libadrenotools. The renderer passes the returned
vkGetInstanceProcAddr into its existing loader; no volk dependency. Android surface creation uses
the selected driver's vkCreateAndroidSurfaceKHR, including recreated windows.

A persistent probing marker is written before loading the selected library. It clears after
120 rendered TV frames. An unfinished marker on the next launch clears selection and displays a
system-driver fallback message, covering both crashes and hangs terminated by the user/OS.
Immediate load failures also fall back. The driver remains loaded for process lifetime. Pipeline
caches use installed identity plus the existing Vulkan device/driver UUID validation. TU_DEBUG is
left at the user's existing value: no global gmem policy is imposed without device measurements.

Compared with the fork, this reuses our loader, surface lifecycle, ZIP parser and deferred upload
buffers, uses a storage-buffer compute output for sRGB portability, and keeps host-testable driver
state separate from Android JNI. It does not copy the fork's renderer or introduce volk.

| Check | Result |
| --- | --- |
| Synthetic ZIP and state-machine CTest | PASS: metadata/ELF/traversal/API rejection; selection; 119/120-frame probe; interrupted probe; load failure; cache removal |
| Android arm64 native build (stub-generated game entry points) | PASS |
| Android debug APK Java/resources/native packaging | PASS with Studio JBR; release artifact guard PASS (23 files checked) |
| GitHub branch CI | 09eb00b: Android/Linux/Windows PASS (final implementation plus measurements) |
| Snapdragon installation, custom-driver loading and surface/presentation | Untested; requires physical device |
| Real-driver crash/hang recovery and cache behavior | Untested; state machine tested on host only |

rhemfur can validate a real Snapdragon device after merge. Physical-device follow-up should cover
system/Turnip selection, package install/remove, 120-frame success and termination before frame
120, restart fallback notice, rotation/window recreation and driver-specific pipeline caches.
No game files, generated game code, saves or screenshots are committed.

CI evidence: [initial Android](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37810597403),
[initial Linux including forced fallback smoke](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37810597404),
[initial Windows](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37810597394).
Follow-up cache handling: [Android](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37811265631),
[Windows](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37811265640),
[Linux](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37811265630).
Local numeric comparison/timing records are retained under excluded `local-evidence/gpu-tests`.

Final validated report/code head `09eb00b`: [Android](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37812410658),
[Linux](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37812411070),
[Windows](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37812410746), all PASS.
Implementation commits: `74c8476` (decoder/custom drivers), `925a1d3` (complete per-driver cache
cleanup); `09eb00b` records gameplay comparisons and measurements. This final documentation
update records the CI results. All authors use Lukas S <lukasschaupp@gmail.com>.
All local generated code, test executables, archives, dependency builds and Android build products
were removed after verification. Only excluded derived validation records remain. No main/devel
pushes, merges or GitHub posts were made.
