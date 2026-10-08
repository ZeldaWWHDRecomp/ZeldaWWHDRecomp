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
| Scripted native/forced frame comparison | Pending |
| Upload-cost measurements | Pending |
| Mali/PowerVR device execution | Untested: no physical device available |

## Custom Android Vulkan drivers

The Graphics settings list installed drivers, show the active name/version, and provide install,
select and remove actions. Installation uses Android's document picker and a bounded private
cache copy; no broad storage permission. Selection applies on restart. The package parser accepts
AdrenoTools schemaVersion 1 metadata and root-level arm64 ELF shared libraries, validates minimum
API, and reuses the existing ZIP reader's traversal/symlink/size/CRC checks. Each successful install
gets a unique identity, so two versions with identical names cannot share their pipeline cache.
Removing an inactive driver deletes its default cache; the active driver requires a system-driver
restart before removal. Explicit debug cache overrides are isolated by identity too.

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
| GitHub branch CI | Pending |
| Snapdragon installation, custom-driver loading and surface/presentation | Untested; requires physical device |
| Real-driver crash/hang recovery and cache behavior | Untested; state machine tested on host only |

rhemfur can validate a real Snapdragon device after merge. Physical-device follow-up should cover
system/Turnip selection, package install/remove, 120-frame success and termination before frame
120, restart fallback notice, rotation/window recreation and driver-specific pipeline caches.
No game files, generated game code, saves or screenshots are committed.
