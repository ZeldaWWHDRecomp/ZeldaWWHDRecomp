# Android on-device setup and dual displays

## Status

The implementation is available for lead review. Local synthetic emulator checks
cover embedded translation, Android-hosted compilation and loading, update/recovery,
and dual-display output/input/lifecycle. Remote CI and all real-phone checks remain
pending. Synthetic results do not establish full-game resource requirements or
physical GPU behavior.

See [device checklist](android-device-checklist.md) for rhemfur's acceptance checks
and [fork assessment](android-fork-reuse.md) for reference-only upstream findings.
The implementation uses this project's recompiler; no second recompiler is added.

## Shared setup pipeline

`tools/android/setup_adapter.py` calls the existing installer's extraction,
translation, compilation and linking operations. `tools/recomp/recomp.py` and
`tools/installer/setup.py` retain their real-user validation behavior. The adapter
supplies Android native process execution and durable checkpoints instead of the
desktop install entry point, which chooses desktop toolchains and deletes work.
The PC-produced Android build/import route remains available.

Android embeds official CPython 3.14.8 through a small JNI bridge in the dedicated
`:setup` process. Isolated interpreter configuration disables environment/site
loading and bytecode writes. Python is initialized once; calls are serialized.
`prepare_python.py` verifies pinned official archives and packages the standard
library, required native dependencies, shared scripts and build maps.

Chaquopy offers Gradle/Java integration; Briefcase generates application projects;
Buildozer/python-for-android supplies a broader application build system. Direct
CPython fits the existing SDL/Gradle host without replacing it. This is a design
assessment, not an alternatives benchmark. Python and dependency license notices
are included in packaging.

## Compiler execution and delivery

`build_toolchain.py` and `android-toolchain.yml` build Android-hosted clang/lld/ar
from LLVM 20.1.8 revision `87f0227cb60147a26a1eeb4fb06e3b505e9c7261`
with NDK 30.0.16248370. Host TableGen runs on CI; the delivered compiler runs on
Android API 33+, rather than being a desktop NDK compiler. Packages include target
headers/sysroot/libraries, notices, content hashes and versioned metadata.

Apps targeting Android 10+ cannot execute downloaded binaries from writable app
home. The supported design installs native PIE compiler drivers through APK native
library packaging and unpacks verified data assets privately. It retains the target
SDK and needs neither root nor an unrelated prebuilt compiler. Preferred first-setup
compiler download is not implemented; supported signed executable delivery would
need a separate design. CI artifact publication and release checksum review remain
lead responsibilities. The local pinned-source arm64 compiler executes and links
loadable synthetic libraries; remote x86_64 CI has not yet established acceptance.

## Phone flow and recovery

The launcher selects folder/WUA/WUD/WUX inputs and required keys with SAF. URI grants
are persisted; document streams are copied without assuming filesystem paths.
Bounded private key input travels through stdin and is excluded from logs and
checkpoint identities. Selection/import errors offer retry or reselection.

`ondevice_setup.py` drives extract → translate → compile → link → activate. Versioned
job state, exact inventories/hashes and atomic completion records distinguish valid
outputs from partial writes. Incomplete stages restart; completed verified objects
and generations are reused. Native child cancellation kills/reaps the owned process
group and removes pending outputs. A failed replacement preserves the old install.

The foreground setup service provides progress, notifications, manual pause and
thermal/battery policy with hysteresis. Compilation defaults to one job and is
bounded. Wake locks are released while paused. Translation uses Python monitoring
checkpoints without modifying the translator; native tools poll cancellation.
Some native I/O can delay pause acknowledgement. UI wording distinguishes a stage
restart from reuse of completed work. Import reserves 1 GiB, not a full-game size
estimate; later stages enforce available-space checks too.

Fingerprints cover ABI/runtime SDK, translation inputs/options, recompiler/helpers,
installer and toolchain. Updates trigger rebuilding; runtime-only changes can reuse
C. Activation atomically pairs the validated library and assets; failed rebuilds
preserve saves and valid installations. Python/compiler resources are verified
against trusted APK archives before reuse. Only then are recognized obsolete cache
generations reclaimed; current resources and unrelated names are preserved.

## Display design

`GamePadDisplay` tracks real logical displays and owns a Presentation surface.
AndroidX window/folding information handles separating hinges on a single logical
display; unfolding is not assumed to create a second display. External displays take
priority. TV starts on the main display and GamePad on the secondary; persistent
swap/scaling/filter settings retain desktop semantics.

The secondary Vulkan surface has its own swapchain and an asynchronous worker.
Where required, a separate logical device/queue avoids secondary presentation
blocking the primary queue. One bounded Android Hardware Buffer snapshot transfers
existing GamePad rendering between devices using external ownership and sync FDs.
Production frames are not read back through the CPU and simulation is not duplicated.

Acquisition uses zero timeout. Busy/hidden/unavailable secondary output skips frames;
ordinary rendering avoids secondary global-idle waits. Primary completion is polled
before handing off readiness; secondary fence waits and teardown stay on its worker.
One outstanding snapshot bounds buffering. A shared physical GPU still has contention
and copy/blit costs; separate queues/devices cannot eliminate those costs.

Generation/identity guards discard obsolete surface callbacks. Resize, fold, rotate,
activity recreation, dismissal, unplug and Vulkan out-of-date/surface-loss recover
only affected resources. Startup window rejection retries are bounded. Missing
secondary output restores accessible single-display content and cancels touch;
requested dual output returns when available. Touch maps orientation, viewport,
scaling and letterboxing to GamePad coordinates and cancels on role/surface changes.

## Synthetic fixture isolation

Production setup accepts only known game builds. Unit tests and debug app probes
register the exact authored RPX digest through `debug_fixture_build.py`; they do not
change installer validation or add synthetic entries to production build maps.
The installer gets a private lookup. The embedded translator's shared lookup is
replaced only during the serialized debug translation call and restored in `finally`.
Other hashes retain normal rejection and known real builds retain their lookup.

`prepare_python.py --service-fixture` explicitly includes debug registration,
embedding/service smoke entry points and the authored translation fixture. Default
packages omit them. Extraction fixtures require this debug option. Release Gradle
`verifyReleasePython` rejects these resources, including the registration module.
The release guard inspects nested `python-source.zip` and rejects test resources;
debug CI alone explicitly opts in with `--allow-android-test-fixtures`.
Never use that exception to approve a release artifact.

## Automated coverage and evidence

- Android Python tests exercise extraction, translation, compilation/linking,
  activation, corruption, interrupted writes, storage faults, pause/restart,
  fingerprints, failed updates and resource sampling. Installer tests independently
  cover real-user build validation. JVM tests cover resource verification/cleanup,
  thermal/battery/recovery policy and hinge layout.
- Renderer host tests cover touch, display fallback, presentation-worker behavior
  and queue selection. Emulator probes check distinct synthetic pictures, injected
  touch, role swaps, secondary removal/recreation, primary progress, rotation,
  folds, dismissal, surface loss and activity recreation.
- `python_smoke.py` compares complete generated inventories and exact C/header bytes
  against desktop output. `runtime_smoke.py` additionally compiles with the actual
  Android-hosted compiler, asserts loaded output and can launch the phone-linked
  runtime with isolated-device lifecycle coverage.
- CI workflows configure these probes on synthetic data. Workflow definitions are
  not a claim that remote CI has passed; the lead runs CI after reviewing commits.

Recorded local API 36 arm64 SwiftShader synthetic results include:

| Probe | Result and scope |
| --- | --- |
| Desktop/Android translation | Exact 14,580 C/header bytes and inventory matched |
| Phone-linked runtime + isolated lifecycle | 100 observations passed |
| Production-layout isolated timing + lifecycle | 114 observations passed |
| Latest cache cleanup + retained active runtime | Cleanup/recovery passed; 37 display observations passed |
| Verified cache reclaim | Python 96,825,344 bytes; compiler 215,482,368 bytes |
| Larger authored compiler resource probe | One job; 31 complete/two incomplete samples; three complete native-child samples |

These probes used different recorded APKs; do not conflate their exact binaries.
The timing probe recorded disabled/enabled medians 23.8893/28.0299 ms and p99
33.2887/57.778 ms, within its recorded thresholds. Earlier noisy timing failures
were not erased. CPU cadence on a loaded software emulator is not a causal physical
GPU-overhead or scanout measurement.

The resource probe measured APK 157,288,749 bytes, installed APK/native allocation
345,772,032 bytes, and sampled aggregate PSS maximum 300,340,224 bytes. First service
build took 16.1810 seconds; completed-generation reuse took 11.4534 seconds. These
are authored synthetic workload results, not game estimates. Sequential PSS reads
can miss peaks; RSS sums double-count shared pages. Kernel worker/largest-child
high-water values are separate and must not be added as simultaneous memory.

Detailed scalar evidence/logs remain local outside git. Full-dump peak temporary
storage, peak memory and clean compiler-build duration are unmeasured. API 33
emulator compiler/load passed, but that emulator's Vulkan driver lacks required
dynamic rendering; rendering there remains unsupported/unverified.

## Validation after the devel port

On the fresh clone of branch base `0934835`, local checks pass:

- 78 Android Python tests; 44 installer tests run with two skips.
- All five JVM suites and four renderer host test binaries.
- Fresh arm64 native/extractor build and Gradle debug/release APK builds.
- Embedded app extraction, exact 2,017-byte C/header inventory parity,
  translation pause/retry, native compile/link pause/retry and full-runtime load.
- Separate debug foreground-service compile/load/activation and fresh-service reuse.
- Release assembly rejects debug registration resources. Fixture-free release
  assembly and the default release guard pass; the actual release APK contains the
  build map and excludes registration, smoke modules and runtime placeholders.

The app probes used API 36 arm64 SwiftShader with two cores and 2 GiB RAM, official
CPython 3.14.8 and the cached pinned-source compiler described above. Embedded
extraction/translation/compile/load checks took 32.6668 seconds; this is synthetic
work, not a full dump. Debug APK SHA-256:
`12b83b01bfd15366996e8764225efc93889b58150a39490053d0528aa9838748`.
The earlier 14,580-byte fixture measurement predates devel's strict hook checking;
the current fixture omits real-game hooks outside its authored text. No output
normalization is used. Detailed reports remain local outside git.

## Reproduction and remaining gates

Run host suites:

```sh
python3 -m unittest discover -s tools/android -p 'test_*.py'
python3 -m unittest discover -s tools/installer -p 'test_*.py'
```

Use `android.yml` for JVM/renderer checks and release packaging,
`android-toolchain.yml` for pinned-source compiler and setup/update/recovery probes,
and `android-display-smoke.yml` for secondary-display lifecycle/timing probes.
Smoke tools expose `--help` for APK/serial/output arguments. Physical captures use
`resource_sampler.py` and the device checklist without sharing inputs or keys.

Pending: lead CI/artifact review; full user-owned dump setup/update; physical heat,
battery, background termination and URI-provider behavior; Ayn Thor, separating
hinges, external/mismatched-refresh displays, sleep/wake and real-driver recovery.
Immediate reboot before SAF grant persistence settles remains a specific recovery
case to validate. Keep these checks pending until evidence is supplied.
