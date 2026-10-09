# Android setup and dual-display implementation status

## Scope and current evidence

For the consolidated requirement audit, exact evidence scopes and remaining lead/device gates, see [Android acceptance handoff](android-acceptance-handoff.md).

The two root-level Android task prompts define the acceptance criteria. This file records implementation evidence and remaining work; it does not narrow those criteria. Sections record successive implementation milestones; later sections supersede earlier status statements and test counts. Current translation checkpoints reuse C across runtime-only updates, and actual synthetic APK replacement/rebuild/failure recovery has passed locally. Remote CI, full-game measurements and all physical-device checks remain pending.

`tools/recomp/android_fixture.py` constructs a game-free ELF/RPX containing three authored PowerPC instructions, invokes the existing `Recompiler`, emits a native harness asserting return value 42, and records raw C/header hashes, output size and translation elapsed time. `--reference` checks both file inventory and exact bytes. The module needs no subprocess support, so embedded Python can invoke it directly. Android CI now creates the desktop baseline, checks different Python hash seeds, compiles and executes the harness, and retains the baseline for the eventual emulator comparison.

Locally on macOS arm64 with CPython 3.14.6, the fixture translated with no unhandled instructions, emitted 14,580 C/header bytes (including the harness and current hook declarations), compiled with Apple clang and returned success. Two independent translations matched byte for byte. These are small synthetic desktop measurements, not phone performance measurements.

The Vulkan presentation path now uses a zero acquisition timeout for the GamePad swapchain and skips unavailable images. Before reusing an asynchronous acquire semaphore, it polls its previous submission fence and skips secondary presentation if that submission is still pending. No wait is submitted against an unsignalled semaphore after a timeout. The modified renderer passed a syntax check using the existing desktop build's actual compile flags. This removes two secondary-display wait points; it does not yet prove independent presentation, since submission-ring pressure, queue presentation and swapchain recreation still share device/queue resources.

## Setup constraints to resolve next

The extraction adapter now invokes unchanged `archive_info`/`disc_info` and `extract_game` through a limited native process bridge. Input and extractor identities select an isolated generation; completion records the exact output inventory and hashes. Interrupted extraction restarts, verified completion is reused, and failed replacements preserve earlier generations. Keys travel only through bounded private stdin, without entering checkpoint identities. Native execution now streams separate stdout/stderr, supports cancellation, and kills/reaps the child process group on cancellation or callback failure. Four extraction fault tests cover reuse/corruption, failed replacement, pause/retry and log-open cleanup; four pipe-adapter tests cover concurrent draining, abandoned readers, spawn errors and bounded key input. All 34 Android host tests pass.

The arm64 emulator verified private stdin, separated and streamed output, cancellation, callback exceptions and a child closing stdin early. Exact desktop translation parity still passed (14,580 fixture bytes); startup/resource preparation and checks took 9.984 seconds with process VmHWM 174,348 kB. These are synthetic emulator measurements. The Android extractor itself builds locally, but APK packaging and synthetic archive execution are now verified as described below. SAF input selection and launcher integration are implemented in the section below; their full acceptance coverage remains pending; the initial foreground service and thermal/battery policy now have the emulator coverage described below.

`tools/android/setup_adapter.py` now wraps the unchanged installer's `recompile()` function, replacing only its module-local subprocess bridge with an in-process invocation of the unchanged `recomp.py`. It first uses the shared supported-RPX validation. A durable translation identity includes the input RPX, installer, translator/helpers/hooks and app-supplied port revision. Completed output is reused only when its exact file inventory and every SHA-256 match; a changed revision creates a new generation and retains the previous one. Completion metadata is atomically replaced and synced. Partial translation is discarded and retried without deleting prior completed generations. Events report running/completed/reused/paused states, size and elapsed time.

Eight game-free translation tests passed, including exact desktop C parity, checkpoint reuse after a fresh worker, corrupt output/metadata and stale-file recovery, revision invalidation, pause before/after a stage, failed-translation cleanup/retry, and unsupported-dump rejection. Android CI runs these tests. The debug embedded-Python probe now packages and invokes this adapter in the app's dedicated setup process. The production file picker and resumable importer are implemented below, with full picker/provider acceptance still pending. Progress controls and the foreground worker are implemented below. Translation and linking pause at command/stage boundaries; compilation additionally pauses between objects, as described below. Extraction checkpoints are implemented; full picker/provider and update acceptance coverage remain missing; the service refreshes release inputs and recovery hooks request rebuilds. The adapter deliberately avoids the desktop `install()` entry point that deletes work and chooses desktop launchers/toolchains.

The adapter's `compile()` now calls unchanged `setup.compile_gamecode()` with a module-local native process bridge. Each object is written to a unique temporary path, synced, atomically published, and paired with a digest checkpoint. Restart reuses only objects whose content matches their checkpoint. The build identity includes generated sources/headers, explicitly supplied SDK header roots, flags, command/environment, installer, port revision, and the validated compiler/sysroot bundle identity. Changed inputs create another generation and retain old objects. Pausing prevents new tool launches while letting active compiler invocations finish and checkpoint their objects. Five additional tests use actual desktop clang output to verify parallel compilation/restart reuse, single-object corruption recovery, compiler failure/retry, pause/resume, toolchain changes, and header/flag invalidation. All thirteen adapter tests pass locally. Android-hosted object compilation, linking, loading and checkpoint reuse passed the local synthetic emulator probe described below.

`link()` now wraps unchanged `setup.link_game()` with native archive/link execution and a separate durable generation. Its identity includes ordered object hashes, SDK/runtime inputs, toolchain identity, linker/archive commands, flags and environment. A successful candidate is synced, made read-only, atomically published and hashed; reuse checks the actual library bytes. Failed linking preserves earlier generations. Three additional desktop tests compile the real synthetic translation into a loadable library, call its harness to verify the translated return value 42, check restart reuse and corruption recovery, and verify a failed update leaves the previous library intact. The sixteen adapter tests pass locally. This returns a candidate library for the Android host. Full-runtime loading, validated active-generation selection and explicit rollback are now implemented and exercised below; automatic rebuild and crash-triggered rollback orchestration remain pending.

- The phone now relinks the runtime objects with translated game code into a complete SDL library, preserving the desktop linking model. Runtime SDK delivery and its versioned Java/JNI host contract are described below; the PC-produced APK remains supported.
- `setup.py` invokes Python and native extractor/compiler subprocesses, detects Android as Linux, selects desktop toolchains, and deletes `work/` before rebuilding. A host adapter must supply Android execution/toolchain behavior and durable checkpoints while leaving both shared scripts unchanged. Do not claim the existing installer runs on Android unchanged without this adapter.
- Official CPython embedding is now exercised in the debug app, as described below. Production packaging still needs resource pruning, external-library license notices and release validation.
- Apps targeting Android 10 or later cannot directly execute binaries from writable app-home storage. An ordinary checksum-verified compiler download followed by `execve` there will not work. Use a CI-built APK-packaged compiler first, then investigate supported signed delivery for the preferred first-setup download. Do not lower the target SDK or require root to sidestep this constraint. Source: [Android execution restrictions](https://developer.android.com/about/versions/10/behavior-changes-10#execute-permission).
- Still required: successful remote CI builds of Android-hosted clang/lld, complete file-picker/provider acceptance coverage, automatic update/reboot acceptance coverage and real-phone validation. Local synthetic emulator compile/link/load, extraction checkpoints and foreground pause/recovery checks are recorded below; remote CI remains unrun.

## Embedded Python evidence

`tools/android/prepare_python.py` downloads official CPython 3.14.8 Android packages from python.org with fixed SHA-256 values for arm64 and x86_64. It compiles our small JNI embedding bridge with the pinned NDK, packages libpython and its external shared libraries, and stores the standard library and unchanged installer/recompiler modules in APK assets. `EmbeddedPython.java` extracts resources into a content-addressed app-private generation and invokes a serialized native worker. Python uses isolated configuration with environment/site loading disabled. This follows the [official CPython embedding guidance](https://docs.python.org/3/using/android.html); it does not download an Android-hosted compiler.

Alternatives were evaluated for fit with the existing SDL/Gradle app: [Chaquopy](https://chaquo.com/chaquopy/doc/current/) supplies Gradle and Java integration and remains a fallback if maintaining the bridge becomes costly. [Briefcase](https://briefcase.beeware.org/en/latest/) generates complete native application projects, while [Buildozer](https://buildozer.readthedocs.io/en/latest/) automates Python application packaging with python-for-android. Direct CPython was selected for the initial implementation because our translation needs the standard library and the app already owns its native/Gradle build. This is an integration assessment, not a benchmark of those alternatives.

The debug-only `PythonSmokeActivity` runs in `:setup`. It translates the synthetic RPX, invokes unchanged `setup.recompile()` through the adapter, verifies its output, and creates a fresh adapter instance to prove completed translation is reused. `tools/android/python_smoke.py` reads actual generated files from the emulator and compares the complete C/header inventory and raw bytes against desktop output. It records translation/startup time and process RSS/high-water memory, and refuses real phones. The emulator CI workflow now builds the x86_64 Python bundle and runs this probe after the dual-display test; remote execution remains unverified.

The embedding registers a `wwhd_native.run` built-in module for native tool processes. It uses `posix_spawn` with an absolute executable path, explicit environment support, bounded combined stdout/stderr capture, and process-group termination on timeout. The Python GIL is released during process I/O/waiting, and close-on-exec pipes isolate concurrent tool invocations. The emulator probe checks Unicode environment values, stdout/stderr capture, nonzero status, missing-executable errors, timeout cancellation and two parallel worker calls. It now executes the APK-installed compiler/archive/linker through unchanged installer operations. Extraction still requires an Android-hosted extractor and a stdin/progress bridge.

Local arm64 API 36.1 results: exact parity for all 14,580 C/header bytes, installer checkpoint reuse verified, 3.10 seconds for activity startup/resource preparation plus both translation probes, 0.00104 seconds for the tiny direct translation, and 183,284 kB process memory high-water mark (includes Android/Java and Python). The prepared Python package occupied approximately 28.3 MB before APK compression. These synthetic measurements do not estimate a full game's translation or phone requirements. The APK built successfully and passed the public-content guard.

To reproduce, run `python3 tools/android/prepare_python.py --abi arm64-v8a --ndk "$ANDROID_HOME/ndk/30.0.16248370" --out build/embedded-python`, build Gradle with `-PwwhdPythonDir="$PWD/build/embedded-python/package"`, then invoke `python3 tools/android/python_smoke.py --serial emulator-5554 --apk android/app/build/outputs/apk/debug/app-debug.apk --output build/python-smoke-result.json`. Select matching ABI properties for an x86_64 emulator. The normal PC-based APK build remains available without the Python property.

## Dual-display work remaining

The Android app now creates a `Presentation`/`SurfaceView` on an available secondary presentation display. It starts/stops with the activity, enumerates displays deterministically, invalidates old callbacks with generation tokens, and publishes retained `ANativeWindow` references to a mutex-protected renderer mailbox. The render thread creates a separate Vulkan surface and swapchain, checks queue support, renders the GamePad there, and restores the requested single-screen composition when the display is removed. Surface dimensions come from the secondary view. This has now been exercised on a local emulator; real-device validation remains pending.

Second-screen touch uses a single active pointer, cancels on view/surface loss, rejects stale generations and maps through the actual fitted renderer viewport. It rejects initial touches in black bars, clamps drags and rejects non-finite coordinates. The mapping and repeated window-removal/fallback/restoration tests pass through CTest and are wired into Android CI. A late-created secondary surface now receives the semaphore pair required by capture and waiting presentation paths. The complete Java source set passes `:app:compileDebugJavaWithJavac` against API 36, and the modified renderer passes an arm64 Android API 33 NDK syntax check.

The complete Android native placeholder build succeeded using NDK 30.0.16248370 (`GEN_DIR=build/gen-android-dualscreen-stub`, isolated `OUT=build/android-dualscreen-check`, `JOBS=4`). It linked `libmain.so`, and `assembleDebug` produced an APK which passed `tools/release/guard.py`. The stripped synthetic `libmain.so` is 6,791,448 bytes; this is not a full game's library size. The dual-display implementation and its CTest tests were committed separately as `030502d8`.

The debug-only `DisplaySmokeActivity` starts a game-free native display loop: changing red TV and blue GamePad images are presented through the real Vulkan swapchains. It records successful queue presentations and GamePad input in an atomically replaced app-private metrics file. `tools/android/display_smoke.py` controls an emulator overlay display, injects touch on its actual logical display ID, removes the display while touch is held, asserts touch cancellation and continued primary presentations, and reconnects it. It preserves/restores emulator display and fullscreen-confirmation settings, and emits JSON observations, relevant logcat output and a screenshot. It refuses to run on a real phone. The debug test launcher is excluded from release APKs.

The runner passed on the local arm64 API 36.1 emulator with Goldfish/GFXStream SwiftShader Vulkan 1.3.0. In a recorded run, both displays presented 13 frames before the first assertion; touch mapped to exactly (0.5, 0.5); after removal the primary advanced from 51 to 62 presentations while the secondary remained at 45; reconnect advanced both. These are functionality observations, not frame-time/performance guarantees. The native placeholder library was rebuilt from this isolated worktree, the debug APK built, and the public-content guard and both mapping/fallback tests passed. `.github/workflows/android-display-smoke.yml` now wires the same test to an x86_64 API 35 emulator; that remote CI job has not run yet. `ABI=x86_64` and Gradle's `-PwwhdAbi=x86_64` select that test target, with arm64 remaining the default.

Android now persists a TV/GamePad display-swap setting and uses the desktop scaling settings for either physical display. The renderer latches the requested roles once per frame, cancels held touch when roles change, and routes touch to the physical display showing the GamePad. Removing the secondary display restores single-screen composition while retaining the user's swap preference for reconnect.

The expanded local emulator test passed swap cancellation, rejection of GamePad touch on the TV display, primary-display GamePad touch and release, all three scaling selections, removal during held primary touch, reconnect, and settings persistence across process restart. Screenshots visibly confirm red TV/blue GamePad before the swap and blue GamePad/red TV afterward; the runner waits for ten presentations on both surfaces before the swapped capture. It restores the prior app settings after testing. The native build and debug APK build passed, as did mapping/fallback tests. Remote CI remains unverified.

The runner also replaces the secondary logical display with 480x800 at 240 dpi, then restores 800x480 at 160 dpi. A local run confirmed both presentation counters advanced by at least twenty after each replacement, and Vulkan logs confirmed swapchains adopted both extents. This exercises secondary surface replacement and density/orientation changes; it does not establish foldable layout support or primary activity recreation.

Android now enables `VK_EXT_swapchain_maintenance1` when the driver exposes its feature and instance dependencies. Secondary replacement then detaches old views, acquire/render semaphores, swapchain and surface/window ownership into a bounded four-generation retirement queue. Graphics submission serials/fences and three reusable presentation fences protect their distinct lifetimes; all readiness checks are nonblocking. A replaced surface stays alive until older resize generations referring to it retire too. A full retirement queue retains only the latest pending surface callback, cancels touch, falls back to the main display and retries after resources retire. Secondary swapchain recreation and initial render-finished semaphore allocation no longer call device idle on this path. Drivers lacking the extension retain the explicitly logged idle compatibility path. Vulkan's [presentation semaphore guidance](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html) explains why graphics submission fences alone cannot establish presentation completion.

The API 36 arm64 GFXStream/SwiftShader emulator passed `display_smoke.py --require-fence-retirement`: ordinary unplug/reconnect, portrait/landscape replacement, swap/scaling/touch, four deliberately retained generations, fallback at the resource bound, latest-surface recovery and settings persistence across restart. The diagnostic holds retirement collection, not the real compositor; it tests the resource bound and recovery without claiming a physically stalled display. During the hold the primary advanced from 223 to 278 presentations; releasing it advanced to 283, the secondary resumed from 191 to 192, and retired generations increased from three to seven with zero remaining. Every observation reported zero secondary idle waits and at most four pending generations. `build/secondary-retirement-result.json` and its log contain the observations. The complete native placeholder build, minimal debug APK/content guard (17 files), and host touch/fallback tests passed. Remote CI and physical devices remain unverified.

On Android devices exposing maintenance presentation fences and at least two queues in the selected graphics family, secondary acquisition, shader drawing and presentation run on one persistent worker using a dedicated queue. The worker owns a command pool/buffer, descriptor pool and drawing fence. Requests contain copied handles and immutable draw parameters; the renderer polls completion without waiting. Drivers without the feature/queue combination retain the logged shared-queue compatibility path, which does not establish independent presentation.

The main queue copies the selected TV/GamePad scan image into one private snapshot before primary presentation. Its ordinary graphics submission signals both primary presentation and snapshot readiness, avoiding a second graphics submission. If primary presentation is unavailable, the end-of-frame flush submits the copy and signals readiness instead. The worker waits on snapshot readiness and secondary acquisition before drawing. The next successful acquisition requires completion of the previous worker draw, so snapshot replacement/overwrite, command-pool reset and descriptor reuse cannot race that draw. An acquired image retains its acquire-semaphore slot until drawing consumes it; pending surface events defer replacement until the old acquired image is consumed. Per-image render-finished semaphores and maintenance presentation fences protect presentation reuse and retirement independently. Explicit device drains join the worker and acquisition completion; ordinary primary swapchain recreation waits only on the graphics queue.

`present_worker_test` covers nonblocking polling during a held operation, single-request ownership, unread-result protection, exception propagation and reuse. The emulator runner exercises touch, display swap/scaling, primary swapchain recreation while secondary presentation is held, held acquisition, removal/reconnection during either hold, four retained generations and settings persistence. The holds occupy the worker before WSI calls; they do not reproduce a physically stalled compositor. A reviewed swapped screenshot shows the blue GamePad picture on primary and red TV on secondary.

The full local API 36 arm64 GFXStream/SwiftShader run passed `display_smoke.py --require-fence-retirement --require-present-worker --measure-timing` (`build/secondary-batched-snapshot-result.json`). Primary interval medians were 17.26 ms disabled, 19.20 ms enabled, 17.00 ms held and 17.02 ms disconnected. Their p99 values were 25.77, 36.22, 25.26 and 24.28 ms, respectively; the largest enabled interval was 42.70 ms. Each profile contains at least 180 intervals. The predefined limits are disabled median × 1.25 + 2 ms, disabled p99 × 1.5 + 10 ms and a 250 ms worst interval. Logs confirm one graphics submission per frame. Native build, APK installation, host worker test and Python syntax checks pass. Remote CI remains unrun.

These results measure synthetic CPU submission cadence, with a fixed 16 ms loop delay and authored 64×36 color patterns; they do not prove physical scanout latency, zero rendering overhead or full-game performance independence. GPU timestamps use monotonic lifetime counters independent of periodic log resets and cover primary graphics-queue submission intervals, excluding independent worker drawing; they are not GPU busy time. `--disable-gpu-timestamps` supports comparisons without optional query instrumentation. Earlier presentation-only and separate-snapshot-submission implementations failed the timing gate; batching the copy into primary submission produced the passing result above.

Orderly shutdown now drains both queues and acquisition completion, joins the worker, retires its private snapshot and destroys its command/descriptor pools, drawing/acquisition fences and snapshot-ready semaphore. The debug session supports an orderly exit, and the runner requires the release marker before reporting success. The local emulator passed all 37 functional observations and the orderly-exit check (`build/secondary-worker-teardown-result.json`); the log records session completion followed by worker-resource release. Native build, host worker test, Python syntax and APK content guard (17 files) pass. This check exercises the explicit cleanup path; it is not validation-layer or whole-renderer leak analysis.

The Java host now listens to pinned Jetpack WindowManager 1.5.1 folding information. A separating hinge in one logical display creates two SurfaceViews: TV at the left/top and GamePad at the right/bottom. An external presentation display takes priority. Both hosts share surface-generation guards, pointer ownership and the existing native secondary swapchain; there is no CPU frame readback. Window-coordinate hinge bounds are converted to content coordinates, and the hinge gap is excluded from both panes. Absent, partial, stale or unusable hinge bounds restore the full primary surface. Flat flexible screens use the single-display layout; a physical separating hinge can remain dual-pane when flat, following [FoldingFeature semantics](https://developer.android.com/reference/androidx/window/layout/FoldingFeature). Listener removal and a lifecycle token reject callbacks from an earlier resumed session.

Host geometry tests cover horizontal and vertical hinges, zero-width folds, inset offsets, incomplete spans, invalid dimensions and coordinate overflow. The debug-only DisplaySmokeActivity can inject a synthetic hinge to exercise actual SurfaceViews and Vulkan swapchains on the existing emulator; this does not test hardware posture detection. Real foldable posture delivery, spanning behavior, primary rotation and device performance remain pending. Local `build/fold-host-smoke.json` passed 48 observations with `--exercise-folds --require-fence-retirement --require-present-worker`: horizontal panes were 2400x530 each with a 20-pixel gap, vertical panes were 1190x1080 each, and every unfold restored 2400x1080. Center GamePad touch, touch cancellation, both presentation counters, external-display restoration and orderly worker teardown passed. Screenshots were inspected for distinct red TV/blue GamePad panes. That first run used posture intents and therefore also exercised pause/resume; it did not isolate live pane changes or establish pane timing thresholds. The fixture now receives posture broadcasts in the resumed debug activity, avoiding Activity launches. `build/fold-live-smoke.json` passed 66 observations: live fold/unfold with held touch, swaps and centered GamePad input in both roles, hinge-gap touch rejection, external-display priority (full 2400x1080 primary plus 800x480 secondary), and unplug restoration of vertical 1190x1080 panes. Both presentation counters advanced after restoration; unfold canceled touch and restored the full primary surface. The build, Python syntax check and APK guard also passed. This remains synthetic posture coverage and makes no new timing claim. `assembleDebug`, the host geometry tests, Python syntax check and APK content guard (80 files) passed. CI now includes geometry tests and the synthetic pane smoke; remote CI has not run.

Still required: validation-layer coverage, per-screen lifecycle retry, additional lifecycle unit tests, physical fold-posture acceptance, compatibility-driver performance/retirement coverage, desktop runtime regression checks and physical-device/full-game timing with a stalled secondary consumer. Physical checks remain assigned to rhemfur in `docs/android-device-checklist.md`.

## Android extractor delivery and execution

`tools/android/package_extractor.py` packages the project's Android CMake-built `wwhd-extract` as an Android-installed PIE executable. It requires ABI/API 33, the pinned NDK toolchain, the Android ELF interpreter and bundled pinned zstd. It strips debug data, records source/binary hashes and size, and includes project/zstd licenses. Gradle's optional `wwhdExtractorDir` enables native executable extraction and preserves its checksum. `AndroidExtractor.prepare()` checks architecture, executable mapping and installed binary checksum before supplying its path. Spawned extraction processes explicitly resolve APK-installed libc++ via `LD_LIBRARY_PATH`; they cannot use the Java process's linker namespace.

The test-only archive generator reuses the existing C++ ZArchive writer and emits a separate expected file-hash inventory. It contains an authored synthetic RPX, metadata and 1.3 MB of authored content, including stored and compressed blocks. A local arm64 emulator run passed extraction through unchanged installer functions, exact file-inventory/content hashes, progress streaming, pause with partial-output cleanup, checkpoint reuse without any second tool execution, and damaged-archive rejection while retaining the completed generation. Translation of that extracted RPX remained byte-identical to desktop. Extraction produced 1,300,208 bytes in three files in 0.108 seconds; startup/resource preparation plus the entire probe took 11.284 seconds. App process VmHWM was 183,840 kB (extractor subprocess peaks excluded). These synthetic emulator measurements do not predict a real game's phone requirements.

All 38 Android host tests pass, including extractor packaging rejection for a wrong ABI/API, NDK/toolchain and system zstd. The APK release-content guard passed. CI builds the extractor for both ABIs and requires the synthetic extraction probe before the existing x86_64 compile/link/activate/display tests; remote CI has not run. Native Android WUD/WUX execution and key handling still need emulator coverage; existing desktop extractor tests cover those formats separately. Production SAF import and the complete setup flow remain open. Foreground scheduling, thermal/battery controls and update/reboot hooks are implemented below, with their verification limits.

## Compiler source-build evidence

`tools/android/build_toolchain.py` and `.github/workflows/android-toolchain.yml` now define builds for Android-hosted arm64 and x86_64 clang/lld/llvm-ar. LLVM is locked to the peeled `llvmorg-20.1.8` commit `87f0227cb60147a26a1eeb4fb06e3b505e9c7261`; the script rejects a different or modified source checkout and an NDK version other than 30.0.16248370. Host TableGen tools are built first, then the actual compiler is cross-built against Android API 33 with static libc++. Packaging includes target sysroot, compiler headers/runtime libraries, license notices, per-file hashes, build time and installed size; CI archives it and generates an archive checksum.

The remote compiler workflow has not run yet. A local arm64 build from the pinned LLVM source completed successfully and its compiler-enabled APK passed the synthetic emulator probe. The installed compiler package contains 351,545,226 bytes. The recorded build_seconds field currently measures the last resumed packaging invocation (18.97 seconds), not a clean compiler build; a clean-build elapsed-time measurement remains pending. The produced archives are CI artifacts, not downloadable app setup assets. A release must pin the reviewed archive checksum inside the app, and first-setup download delivery remains pending. The APK-installed compiler execution design described below is validated locally. Build design references: [LLVM cross compilation](https://llvm.org/docs/HowToCrossCompileLLVM.html) and [LLVM CMake options](https://llvm.org/docs/CMake.html).

`tools/android/package_toolchain.py` now validates the source-build package's full inventory/digests, pinned LLVM/NDK revisions, ABI/API, and Android ELF64 PIE interpreter before preparing APK resources. It renames the three tools to installer-extracted `lib*.so` executable paths and archives compiler data separately. Five packaging tests check byte preservation, changed/extra files, wrong revisions, wrong ABI/desktop interpreter, and symlink rejection using authored ELF metadata fixtures. Those fixtures are never executed and do not establish compiler execution. The compiler CI workflow now prepares and archives this APK payload after a successful source build.

Gradle's optional `wwhdToolchainDir` includes this payload and enables native-library extraction for compiler-enabled APKs, preserving the compiler bytes against additional stripping. The existing PC APK mode keeps direct-mapped libraries. `AndroidToolchain.prepare()` checks installed native executable hashes and permissions, checks the data archive hash, extracts data by compiler identity, and returns explicit compiler, linker, resource and sysroot paths. The compiler-enabled APK installation and execution path passed locally on the arm64 API 36.1 emulator. The first link exposed omitted per-architecture NDK runtime directories; packaging now includes those directories, including libunwind.a, with a regression test. Android documents the extraction packaging setting under [extractNativeLibs](https://developer.android.com/guide/topics/manifest/application-element#extractNativeLibs).

## Pending device checks for rhemfur

- On-device setup from a privately supplied supported dump: stage times, peak RSS/storage, toolchain footprint, compile concurrency, heat and battery drain. Record model, OS, ABI, available RAM/storage and app/toolchain revisions.
- Pause/resume and recovery after backgrounding, process termination, reboot, low battery, thermal throttling, failed downloads and low storage; ensure no keys enter logs.
- Update-triggered rebuild, failed rebuild rollback, and save preservation.
- Ayn Thor, foldable and external display where available: TV/GamePad routing and swap settings, touch corners and letterboxing, rotation, repeated fold/unfold or disconnect/reconnect, sleep/wake, mismatched refresh rates and stalled secondary presentation.
- Capture main-display frame times with secondary output disabled/enabled/unavailable, along with both display resolutions and refresh rates. Confirm pictures keep updating and single-display fallback restores access to the GamePad.

All phone checks remain pending. Do not upload dumps, keys or generated game code with results.

## Android-hosted synthetic compiler evidence

The local compiler-enabled smoke test passed with official embedded CPython 3.14.8 and our pinned source-built LLVM 20.1.8 compiler on the arm64 API 36.1 emulator. It compares every generated C/header byte and inventory against desktop output (14,580 bytes), compiles four translation units with concurrency two through unchanged setup.py, archives and links with our Android-hosted tools, loads the resulting 8,024-byte library and asserts the synthetic guest returns 42. A new adapter then reuses all four verified objects and the linked library with a process runner that rejects any compiler invocation.

Recorded synthetic times: startup/resource preparation and probe 6.97 seconds, compilation 0.255 seconds, linking 0.116 seconds. The setup process reported VmHWM 185,188 kB and VmRSS 171,812 kB; these exclude compiler subprocess memory and are not a whole-job peak. Full-dump performance and real-phone measurements remain pending. At that milestone, all 22 Android Python tests passed locally; that later milestone had 26 tests; the current host suite has 43. The compiler workflow now runs this probe on x86_64; remote execution remains pending.

The 13 GB LLVM build intermediates and source checkout were removed after packaging, retaining the validated compiler and APK payload for further tests. Emulator-generated Python/toolchain caches were cleared to recover storage. Replacing the large test APK required removing the previous installation with package uninstall -k (retaining app data), then reinstalling the same signed APK. This is test-environment maintenance, not the production update flow.

## Foreground worker and pause controls

`SetupService` now runs the existing durable Python job in the dedicated setup process. It refreshes verified installed tools and the release pipeline identity, bounds compilation to one job, reports durable host status and progress through a foreground notification, and validates the candidate before atomically selecting its paired library/assets. Manual controls have their own durable flag, so automatic policy changes cannot clear a user pause. A partial wake lock is held during active work, renewed with a timeout and released on pause/failure/completion. Paused foreground work waits without a wake lock and automatically resumes when sensor conditions permit. Progress/error controls are available in `SetupActivity`; this is now the launcher screen, with picker/import behavior described below.

The initial policy pauses at severe thermal status or battery temperature at least 42°C. Heat resumes only at light-or-lower status and at most 39°C. Unplugged battery pauses at 20% or below and resumes at 30% or charging. Unknown unplugged battery status waits for valid telemetry. Manual pause takes priority while the policy continues tracking its other limits. These are initial configurable-in-code defaults, not thresholds validated on a real phone. A standalone Java test checks transitions and hysteresis; 43 Android Python host tests also pass, including a fresh worker honoring the manual control file.

The manifest declares the special-use foreground service, its explanation and required permissions; API 33 uses the ordinary foreground call and newer versions specify the service type. Android documents this type for valid foreground work outside other types and requires a declared explanation: [foreground service types](https://developer.android.com/develop/background-work/services/fgs/service-types). Boot/package-replacement receivers resume interrupted retained jobs or request a rebuild after changed release inputs, preserving manual pause. Opening progress also checks interrupted/update states. These start sites follow the [documented foreground start exemptions](https://developer.android.com/develop/background-work/services/fgs/restrictions-bg-start). An explicit Android force-stop prevents background restart until the user opens the app, as usual.

The arm64 emulator service smoke passed with the UI closed: paused foreground status, no paused wake lock, killed setup-process recovery with a new PID and fresh persisted status, thermal/battery pauses and hysteresis, automatic resume, active wake-lock acquisition, manual pause after preparation, and an unsupported authored input producing the unchanged installer's useful error. Failure released foreground/wake resources and left active/previous build records unchanged. The test restores battery/thermal overrides and setup selection, and removes its synthetic job. The progress screenshot was visually checked. CI runs the policy test and this service test in addition to prior full-runtime/display checks; remote CI remains unrun. Successful activation through this service, actual reboot/update receiver execution, full SAF import, notification-permission variants and real-phone heat/battery behavior still require coverage.

For a compiler-enabled debug APK, reproduce with `python3 tools/android/setup_service_smoke.py --serial emulator-5554 --output build/setup-service-result.json`. The service probe deliberately uses unsupported authored bytes and does not bypass production game validation. It is separate from the existing full synthetic build/activation probe. Local native APK build, release-content guard and all host checks passed. The compiler CI removes its LLVM source/native/target intermediates after archiving validated packages and requests an 8 GB emulator data partition for the combined test. Disposable local native/assets copies and redundant unpacked compiler data were removed; the verified APK compiler payload and original source-build metadata remain available locally.

## Runtime SDK and activation evidence

The active selection now optionally binds a library to its private extracted asset tree and RPX digest. Activation rejects incomplete or redirected trees; selection rejects a changed RPX, an outside path or incomplete asset metadata. `AndroidGame.selected()` returns one immutable library/asset snapshot under the cross-process metadata lock. `WwhdActivity` retains that snapshot through native loading and SDL argument creation, passing `--game` to the existing runtime. Concurrent activation cannot pair an already selected library with a newer asset tree. Explicit rollback restores both paths together. Saves keep their existing external app-storage path. Existing selections without paired assets retain the earlier optional PC/manual behavior.

`tools/android/ondevice_setup.py` coordinates extraction, translation, compilation and linking using the shared adapter. Its versioned private job manifest names validated host-supplied tools and privately imported inputs. A nonblocking file lock prevents two workers from mutating the same job. Atomically synced state reports progress, pause, errors and a candidate ready for host activation; it does not claim activation succeeded. A pause file stops work at the adapter's safe checkpoints. Failed or paused updates retain the last ready candidate and earlier build generations. Compilation concurrency is restricted to one or two. Keys are read through the shared parser and never enter result/state data. Four host tests use actual shared translation and clang linking to verify executable output, restart reuse, pause, failed-update recovery, version/path/concurrency rejection and worker exclusion. All 43 Android host tests pass. full SAF picker/provider and automatic-rebuild acceptance tests remain required; the service, policy and recovery hooks are described above.

The arm64 emulator also ran this job coordinator with the full SDK and Android-hosted compiler, loaded its resulting library and asserted translated return value 42. A fresh worker reused all stages without invoking tools; manual pause retained the ready candidate and resumed from checkpoints. A real clang invocation with a deliberately invalid option failed the update, preserved the previous ready candidate, and recovered by reusing the original verified job. The fixture overrides game validation and adds authored guest/runtime stubs only in the debug probe; production job execution has neither override. A full activation/display run selected the job-produced library and paired assets through both SDL launches, including rollback, asset-path/digest rejection, touch, swap, scaling and secondary removal/replacement. That run took 17.675 seconds for Python startup/resources and all Python/activation checks, with app-process VmHWM 189,636 kB; it excludes the separate display-test duration and tool-subprocess memory peaks. The final updated failure probe passed separately. These remain synthetic emulator checks. Native APK build, release-content guard (35 files) and all 43 host tests passed; remote CI and real-phone validation remain pending.

`tools/android/package_runtime.py` reads the actual Android Ninja link command, preserves runtime object/library order and strong HLE definitions, and substitutes only the user's game archive and output. It excludes `libgamecode.a` entirely. Runtime objects, libraries and public headers form a content-hashed SDK manifest. Desktop packaging supplies its existing game compilation flag reader; Android removes host target/sysroot/output/dependency paths and debug information, retains exact FP and signed-char flags, and requests 16 KiB ELF segment alignment. The SDK contains 19,958,296 installed bytes and its archive is 5,628,044 bytes in the local arm64 build. Both the SDK archive (94 files) and the compiler-enabled APK passed the public-content guard.

`prepare_python.py --runtime-sdk build/runtime-sdk` includes this SDK in the embedded source resources. Resource ZIPs use fixed metadata so unchanged source bytes retain their identity across packaging runs. The debug fixture adds authored placeholder hook/direct-call definitions for runtime references absent from the three-instruction synthetic input; it does not alter generated C for the parity comparison or introduce another translator. The on-device link resolves the complete runtime and a synthetic test entry point. Five objects compile, the complete 8,230,200-byte library loads, its SDL_main resolves, and the translated guest code returns 42 using real runtime globals. A restarted adapter reuses all objects and the linked library without invoking a tool.

`AndroidGame` verifies a completed, read-only library inside private on-device storage, eagerly resolves dependencies and SDL_main, checks its digest again, then atomically publishes the active selection. File and parent-directory synchronization persist metadata. A shared file lock protects AtomicFile readers/writers across the game and setup processes. A valid prior record is retained for explicit rollback; corrupt current metadata cannot overwrite valid rollback history. Saves remain outside this selection store; paired asset paths are now selected atomically as described above.

The record fingerprints the Python distribution, compiler, runtime SDK and source pipeline. `needsRebuild()` detects changes. A previous runtime with the same architecture and versioned Java/JNI host API can still launch while its replacement is being prepared. `host_api` is currently 1 and must be bumped in packaging and the Java host when their native interface becomes incompatible; incompatible records are rejected. Service/recovery hooks now request rebuilds; complete update/reboot acceptance and rollback after a game-start failure remain pending. The dedicated setup worker is responsible for supplying the completed candidate from its matching validated build stages.

`WwhdActivity` loads the selected complete library and gives SDL its absolute path. Without a valid on-device selection it retains the existing PC-built libmain.so path. Complete picker/provider and phone update/reboot acceptance tests are still required; this is not yet a complete phone setup flow.

The debug activation probe checks failed native loading, interrupted AtomicFile writes, outdated input detection with continued compatible selection, explicit rollback, rejection of incompatible host API and corrupt digests, recovery from corrupt JSON, and preservation of valid rollback history. `runtime_smoke.py` backs up/restores existing active/previous metadata and runs the full dual-display test through the newly activated library, including both SDL process starts. It restores app/display settings and removes its disposable synthetic generation. It refuses real phones. Three runtime recipe tests check ordered relocation and exclusion of the game archive, incomplete/duplicate game inputs, and unknown inputs; another regression test checks stable resource identity. All 26 Android Python tests pass locally.

To reproduce on an emulator, first build the native placeholder runtime with android/build_native.sh. Package its build directory using `python3 tools/android/package_runtime.py --build build/native-check/game --ndk "$ANDROID_HOME/ndk/30.0.16248370" --abi arm64-v8a --out build/runtime-sdk` (output must be new), prepare embedded Python with `--runtime-sdk build/runtime-sdk`, and build Gradle with both Gradle properties documented above (`wwhdPythonDir` and `wwhdToolchainDir`). Run `python3 tools/android/runtime_smoke.py --serial emulator-5554 --apk android/app/build/outputs/apk/debug/app-debug.apk --output build/runtime-activation-result.json`. The compiler workflow builds compiler-enabled synthetic APKs and SDKs for both ABIs and runs activation/SDL tests on x86_64; remote execution remains pending.

The final local activation run took 9.10 seconds for startup/resource preparation, translation, compile/link/load and activation checks. Compilation took 0.475 seconds and linking 0.239 seconds. The setup process reported VmHWM 186,828 kB and VmRSS 184,964 kB (compiler subprocess peaks excluded). Both SDL starts logged the selected private library. The swapped screenshot visibly shows the blue GamePad on the primary and red TV on the secondary. App/display settings and pre-existing selection metadata were restored, and the synthetic generation was removed. These remain tiny synthetic emulator measurements, not full-game or real-phone performance results.


## SAF selection and private import

`SetupActivity` is now the launcher and offers extracted-folder, WUA and WUD/WUX selection through Android's document picker. Disc images require separate disc/common key file selections. It takes read-only persisted URI grants and stores a versioned `source.json` in the private, no-backup job directory. Activity recreation does not discard the selection. Changing input is allowed only before work, after completion/failure, or after a manual pause has reached its checkpoint. The optional PC route remains available through Play current build; APKs without embedded setup resources explain that route and disable phone setup controls.

`SetupImport` runs in the foreground setup worker before the unchanged shared pipeline. It enumerates document trees without assuming filesystem paths, streams into a separate private staging directory, fsyncs and renames completed files, and saves SHA-256 receipts for game inputs. Restart validates completed files and recopies corrupt outputs. Incomplete streams restart from the beginning; no incomplete file or job manifest is published. Files named like temporary outputs cannot collide with staging. Unknown sizes are supported; known size/mtime changes fail with a retry error. Traversal, duplicate names, excessive nesting and cycles are rejected. Empty directories are retained. Keys are bounded to 4096 bytes, copied atomically and never hashed into receipts or progress; the unchanged installer parses them. A completed private input snapshot survives provider changes and is reused for updates.

Import checks for the copy size plus a 1 GiB working reserve, checks available space during streaming, and reports copied MiB/file counts. This reserve is not a measured full-game storage requirement; extraction/translation/compile storage measurements remain pending. The screen explains private-copy overhead and checkpoint behavior. URI grants can be revoked or documents removed; opening then fails rather than treating an incomplete copy as valid. Android's [SAF guidance](https://developer.android.com/training/data-storage/shared/documents-files) describes persisted grants and their limitations. The picker currently requires a persistable read grant and reports a failure if a provider cannot supply it.

The emulator debug provider serves authored documents through non-seekable pipes. `setup_import_smoke.py` passed pause/resume at file boundaries and during writes, verified-file reuse, digest corruption recovery, unknown-size streams, temporary-name collisions, short provider output rejection, unsafe names and bounded private key handling. A full-toolchain APK on the local emulator with 828 MiB free rejected import with the expected storage error. The success probe used a smaller debug APK to recover over 1 GiB free without weakening production checks. The latest full compiler-enabled APK also passed the existing service recovery/policy smoke, the release-content guard (35 files), and all 43 host tests; the updated launcher screenshot was visually checked. Disposable Gradle native/assets copies and the temporary duplicate APK were removed. The compiler workflow runs the same import probe on its 8 GiB emulator. Debug provider/harness code is absent from release source sets. Actual DocumentsUI/grant results are recorded below. Cloud/removable providers, selection changes during lifecycle transitions and a complete supported-dump setup remain unverified; these tests do not establish the full phone flow.

Physical-device acceptance is tracked separately in [the checklist for rhemfur](android-device-checklist.md); every device item remains pending until measured evidence is returned.


## DocumentsUI grant acceptance

`picker_smoke.py` uses the actual system DocumentsUI and the external-storage documents provider, rather than fabricated Activity results or same-UID fixture grants. It creates tiny authored inputs under an isolated Documents folder, selects an extracted tree, WUA, WUD/WUX and separate key documents through the production setup screen, and checks the app's persisted read grants and document readability. The local API 36 arm64 emulator passed those selections, missing-key Start gating, and folder/image grant checks after force-stop and reopening. The debug observer does not create grants or bypass production input validation. Cleanup releases only the test's grants, removes its authored files/jobs, restores the previous setup pointer and checks active/previous metadata were unchanged. This does not yet establish a supported-dump build through the service.

The compiler workflow now runs this DocumentsUI probe after the runtime, service and import probes, with a real emulator reboot requested. The final receiver-based observer run passed on the API 36 arm64 emulator: a changed kernel boot ID, readable image/key grants after a framework reboot with the explicit twelve-second settling delay, and real Back cancellation preserving the selected job. The observer is a debug-only receiver so reading grants does not open/close test activities or disturb window focus. The selected-keys screenshot was visually checked. Remote CI remains unrun. For local reproduction use `python3 tools/android/picker_smoke.py --serial emulator-5554 --reboot --output build/picker-result.json`; the selectors currently require the emulator's English system UI. APK build, 43 host tests and release-content guard (35 files) passed, and disposable Gradle native/assets copies were removed again.


The zero-settling-time reboot probe exposed a real limitation: after a framework reboot, the newest common-key read grant was missing even though the app had observed it as persisted before reboot. The failing report is retained locally as `build/picker-immediate-reboot-result.json`. AOSP's [`schedulePersistUriGrants`](https://raw.githubusercontent.com/aosp-mirror/platform_frameworks_base/master/services/core/java/com/android/server/uri/UriGrantsManagerService.java) schedules disk persistence after ten seconds; this is consistent with the observed loss, not proof that every Android build behaves identically. The ordinary reboot probe explicitly allows twelve seconds and records that delay; use `--grant-settle-seconds 0` to reproduce the immediate-reboot case. The test verifies a changed kernel boot ID, and retries the observed transient “No root for primary” provider-startup error for at most thirty seconds. It never retries a missing grant into a pass.

The setup screen now offers same-job access restoration before private import is complete. It accepts only the original dump URI, takes a new read grant and leaves the selected job, import inventory and private copies unchanged. Lost disc/common key grants are identified separately and can be reselected before import completes; Start stays disabled until both key grants are present. Feedback is published as one text update only when changed, avoiding continuous accessibility events during progress polling. The DocumentsUI test revokes its own authored grants, checks wrong-document rejection and same-job restoration, and compares source metadata and private preservation sentinels byte for byte. These sentinels are not importer completion receipts; the separate import test covers verified-file reuse. The extended API 36 arm64 emulator test has passed folder/image grant restoration, wrong-document rejection, lost-common-key Start gating and same-job key reselection. Restored grants were readable after a changed-kernel-ID reboot with the explicit twelve-second settling delay, and cleanup restored prior selection and released all five test grants. The final combined run passed these recovery cases, settled reboot, real picker cancellation, release of all five test grants and restoration of previous selection metadata. Its report is `build/access-recovery-result.json`. The runner retries only the observed transient null accessibility root within a bounded interval during window transitions. The runner removes each old UI snapshot before requesting a fresh hierarchy, so an Android automation failure cannot silently reuse a stale dialog.

After a disc image has been imported, a failed job or a manual pause now exposes
both key pickers again. Selecting a correction queues a versioned URI record;
the worker copies only that key into private staging at its next safe start,
fsyncs and atomically replaces the private key, then removes the request. Failed
or paused copying keeps the previous key and pending request. Termination after
rename but before request removal safely repeats the bounded copy. The dump,
its receipts, the other key and job identity remain unchanged. Key bytes and
digests are absent from request/progress records. Completed jobs and active
workers do not accept key replacement.

The API 36 arm64 importer smoke passed oversized-replacement rejection,
interruption/retry and pending-request replay, asserting no extra dump open and
byte-identical input manifests/receipts. The actual DocumentsUI test also covers
selection on an already imported job and pending correction across app closure.
The focused API 36 arm64 run passed all five observations, restored prior setup
metadata and released all four authored grants
(`build/key-correction-picker-scrolled.json`). The earlier complete run passed
its existing access-recovery cases but failed to locate the replacement below
the grid viewport; it is retained as a failed run, not counted as a full pass.
Its `--key-correction-only` option runs that case directly; the default CI run
retains all existing picker/access cases. The runner scrolls visible file lists
when a requested file is below the viewport and retains failure screenshots.
All six durable worker tests passed, including an invalid-key failure followed
by same-job retry that delivers corrected parsed key bytes to an authored
extraction boundary. Actual disc decryption is not exercised by that boundary.
These synthetic tests establish correction mechanics, not successful decryption
of a privately supplied WUD/WUX. A successful settled-grant reboot test must not
be presented as immediate-reboot or power-loss acceptance. Cloud/removable
storage and a complete supported-dump run remain pending as well.

The optional PC release variant also built without the phone setup payload (`assembleRelease`), passed the content guard (16 files), and excluded the debug harness classes. Its synthetic APK measured 10,578,912 bytes; this is a packaging check, not a playable user-game build or a phone setup size estimate.

### Package-update recovery decisions

Automatic recovery now shares `SetupRecoveryPolicy` between the launcher and
boot/package-replacement receiver. It resumes interrupted preparation, import,
compilation, activation and policy-paused jobs, respects a durable manual pause,
and rebuilds a completed job when packaged inputs change. A merely selected
dump and a failed job require an explicit Start/retry action. Previously the
package-replacement branch could start any selected dump because a missing
active build also reported `needsRebuild`.

The host policy matrix passed 88 combinations. On the API 36 arm64 emulator,
`tools/android/setup_update_smoke.py` passed actual `adb install -r` replacement
checks: selected stayed idle, manually paused completed stayed idle, completed
stale automatically dispatched setup. The last case uses synthetic completed
control metadata and a minimal debug APK: its expected missing compiler asset
failure proves dispatch only, not compilation or activation. Active/previous
metadata stayed byte-identical and the prior setup pointer was restored. This
check is included in emulator CI; remote execution remains pending.

`setup_rebuild_smoke.py` additionally passed five observations on the API 36.1
arm64 emulator using three actual APK replacements and our Android-hosted
compiler. `package_update_fixture.py` builds a distinct authored runtime object
with the pinned NDK for each SDK, updates its verified inventory and identity,
and makes the third revision deliberately fail linking. The first runtime
executed revision 1; package replacement automatically compiled, linked,
executed and activated revision 2 without a manual Start. Generated C/header
inventories stayed byte-identical. Revision 3 failed on its deliberately
undefined symbol; active, previous and ready records, both existing libraries
and an authored external save stayed unchanged. The selected revision 2 then
loaded and executed successfully under the failed-update APK. Cleanup restored
all original metadata. `build/update-acceptance/rebuild-result.json` records
5.05 seconds for the prepared initial build, 6.72 seconds for successful update
including installation, and 6.57 seconds for the failed update including
installation. These are tiny synthetic fixture measurements, not game estimates.

The fixture service factory is opted into only by the debug Application and a
matching private authored-job marker; release continues using the validating
SetupService. Four fixture packaging tests passed, and all three APK content
guards passed. Compiler CI includes the replacement test; remote execution,
full-game update acceptance and physical-device behavior remain pending.

Translation checkpoints now fingerprint the imported RPX, unchanged installer,
RPX reader, translator Python/hook files and Android invocation adapter rather
than the whole port label. A runtime-only update can reuse validated C, while
changed hook inputs produce a new generation and preserve the old one. The
17 adapter tests passed, including a real shared-translator hook-change case
and a runtime-only update that forbids invoking translation again. The APK
replacement runner requires exactly one retained translation generation and
identical complete checkpoint records, including metrics. Its refreshed API 36.1
arm64 run passed all five observations: unchanged translation checkpoint,
automatic changed-runtime activation, deliberately failed linking, preserved
saves/active/previous/ready records, and execution of the retained runtime.
`build/update-reuse-acceptance/rebuild-result.json` records 72.66 seconds for
first-use preparation/build, 27.81 seconds for successful replacement/rebuild,
and 17.91 seconds for failed replacement/rebuild, with installations included
in the latter two. These are synthetic results from a freshly booted disposable
2-core, 2 GiB emulator, not full-game estimates. All three refreshed APKs passed
the 98-file content guard. Compilation and linking still use conservative
port/SDK invalidation.

### Synthetic build through the foreground service

A debug-only `SyntheticSetupService` now inherits production service scheduling,
compiler preparation, durable control records, completed-candidate identity
checks and AndroidGame activation. Its separate `service_fixture` Python module
accepts only the fixed authored synthetic RPX digest, invokes the unchanged
shared translator, and adds authored runtime stubs plus a return-value harness.
It checks the compiled result returns 42 before production native validation
and paired library/assets publication. Production SetupService still invokes
only validating `ondevice_setup`; no job flag enables synthetic validation.
The module is packaged only with explicit `prepare_python.py --service-fixture`,
and `preReleaseBuild` rejects a source bundle containing it. Both rejection and
acceptance without that module were verified locally.

`setup_build_smoke.py` passed on API 36 arm64 emulator using our own pinned
compiler and a fresh runtime SDK. The first run completed in 6.83 seconds. A
further run with prepared tool/Python resources completed a new synthetic job
in 4.20 seconds; a fresh service process reused its linked library in 2.74
seconds with the selection and library mtime unchanged. Records are in
`build/service-build-resume-smoke.json`. The check preserves and restores the
prior setup, active and previous metadata, and removes its authored job. The
APK guard checked 94 files, the debug build and Python packaging tests passed.
Compiler CI now runs this service probe; remote CI remains unrun.

The probe starts with an already private authored folder, so it does not prove
SAF-to-supported-game service completion, full-game update acceptance,
full-game time/storage/RAM, or physical thermal behavior.
The tiny fixture's elapsed times include host ADB/UI overhead and must not be
used as game estimates. One reinstall hit the emulator's data-space limit;
`--reuse-installed` resumed only after verifying the installed base APK's exact
SHA-256 against the supplied APK, avoiding another installation staging copy.

### Primary rotation and Activity recreation

`display_smoke.py --exercise-primary-rotation --exercise-folds
--require-fence-retirement --require-present-worker` passed 73 observations
on the API 36 arm64 emulator. The debug Activity requested portrait and
landscape; primary swapchains adopted 1080x2400 and then 2400x1080. Both
presentation counters advanced by at least ten after each turn, centered
GamePad touch mapped correctly in the swapped primary view, and release cleared
touch. The complete functional suite then passed live fold panes and shutdown.
CI now requests the primary rotation checks. This run did not measure timing.

The cold-boot attempt exposed retained InputDispatcher injection state: logs
reported an invalid DOWN because pointer 0 was already down after an interrupted
test. The runner now releases the primary injected pointer at startup and in
cleanup; it also collapses SystemUI before primary touch checks. Assertions
remain unchanged.

The first full `Activity.recreate()` probe failed because SDL's destruction path
quit the native engine and its creation guard terminated the process. The app now
opts into retaining a live native session during configuration replacement.
The replacement Activity creates fresh views, clipboard bindings and surfaces,
while preserving the native thread, controller/audio state and selected build
snapshot. It skips JNI initialization because that replaces native lifecycle
synchronization objects used by the running engine. Finishing the Activity still
uses the ordinary native shutdown path.

`tools/android/activity_recreation_smoke.py` exercises external normal/swapped
output and synthetic horizontal fold panes. It requires the same process and
native thread, exactly one SDL_main entry, restored pane dimensions, advancing
presentation counters on both surfaces, cancellation of held touch and correct
center touch after replacement. The debug-only recreation trigger is:

```sh
adb shell am broadcast -a org.wwhdrecomp.wwhd.SMOKE_FOLD \
  -p org.wwhdrecomp.wwhd --ez recreate true
```

The API 36 arm64 emulator passed all 21 recreation observations in
`build/retained-activity-smoke.json`, including held-touch cancellation and
center touch after all three replacements. The broader display regression
passed 73 observations with primary rotation, live folds, surface retirement
and orderly shutdown (`build/retained-session-display-regression.json`).
The test is included in display CI;
remote CI execution remains pending. This
synthetic renderer test does not establish real-game state, audio/controller
continuity, physical fold-posture delivery or recovery after process death.
Those checks remain on the device checklist.


### Worker and native-child resource measurements

The durable worker now records `resources_before`, `resources_latest` and
completed-stage scalar metrics in private `state.json`. Kernel `getrusage`
provides peak RSS separately for the worker process (including its Android VM
and embedded Python) and the largest reaped child, plus cumulative CPU times.
Values are normalized to bytes. They are process-lifetime maxima, may include
previous jobs in the same process and must not be added to claim concurrent
aggregate memory. A terminated worker retains only its last atomic snapshot.
No input names, keys or compiler command lines enter these resource fields.

The refreshed `setup_build_smoke.py` passed on the API 36.1 arm64 disposable
emulator (2 cores, 2 GiB, own Android-hosted compiler). The real synthetic
foreground build, activation and fresh-process checkpoint reuse passed.
`build/resource-acceptance/build-result.json` records worker peak RSS of
133,812,224 bytes and largest reaped-child peak RSS of 81,453,056 bytes.
Translation took 0.214 seconds, compilation 2.948 seconds and linking 1.027
seconds; generated output was 38,135 bytes, objects 112,432 bytes and the linked
full-runtime fixture 8,283,368 bytes. End-to-end first-use preparation/build took
57.17 seconds; fresh-service reuse took 8.04 seconds. These are synthetic
measurements, not supported-game estimates or aggregate memory measurements.
The APK guard checked 98 files, and all 50 Android Python tests passed. CI's
service-build and update runners retain these metrics and reject missing
worker/native-child measurements. Whole-run aggregate memory and peak temporary
storage still require the separate sampling procedure in the device checklist.


### Low-storage checkpoints and retry

The Android adapter checks the same 1 GiB working reserve used by import and
shared extraction before new translation, each uncached object, and fresh
archive/link commands. This is a minimum free-space floor, not a full-game peak
storage estimate. Verified translation/object/library checkpoints remain usable
below the floor. A new build stops with a free-space/retry message and retains
completed work; the unchanged installer already checks extraction's reported
output size plus the reserve. Temporary atomic metadata writes are now removed
on failure while preserving the prior record if publication has not occurred.

All 57 Android Python tests passed (`build/update-acceptance/storage-all-tests.log`).
The added host checks inject low available space before extraction, translation,
compilation, linking and a durable worker update, plus ENOSPC during an object
sync and atomic metadata sync. They verify previous extraction/library/ready
preservation, executable old/new synthetic results, no pending-file acceptance,
cleanup and recompilation of only the missing object on retry. These are injected
host faults using the shared translator and real host compiler, not evidence of
an actually full Android volume. Emulator/phone exhaustion during native writes
and full-game peak temporary storage remain pending.


### Secondary-window dismissal recovery

An independently dismissed Android Presentation now invalidates its surface and
touch generation immediately, then schedules one recovery attempt after 500 ms.
It no longer relies on an unrelated display event to restore the GamePad window.
Stop, close and replacement cancel the pending attempt; its generation guard
rejects stale work. An InvalidDisplayException ends the attempt and leaves
recovery to a later real display event rather than repeatedly showing an invalid
window. Ordinary host-driven dismissal does not schedule recovery.

`display_smoke.py --exercise-dismissal --exercise-primary-rotation --exercise-folds
--require-fence-retirement --require-present-worker` passed 76 observations on
the API 36.1 arm64 emulator. The new checks dismiss the actual Presentation with
a held GamePad touch, observe single-display fallback and continued main frame
progress, then require automatic recreation on the same logical display and
working centered touch. Existing role swaps, removal/reconnect, blocked-secondary
worker progress, primary rotation, fold/unfold and settings restart checks passed.
The report is `build/dismissal-acceptance/display-extended-result.json`.

The first broader run passed the new dismissal checks but reached the synthetic
renderer's two-minute lifetime during folding; its normal fixture exit left the
last metrics unchanged and the host transition check failed. That failed report
is retained as `build/dismissal-acceptance/display-result.json`. The fixture now
allows four minutes; per-transition timeouts and performance thresholds are
unchanged. Native build, debug APK build/content guard (80 files), Python syntax,
workflow YAML and FoldLayout checks passed. This run did not request performance
measurement; physical-display behavior and remote CI remain pending. Display CI
now includes the dismissal probe.


### Pinned Python download recovery

The build-time CPython packager now streams into a bounded-memory partial file,
checks the pinned SHA-256 before publishing the archive, fsyncs valid bytes and
atomically renames the cache entry. Interrupted, mismatched and failed-sync
transfers remove their partial file. A damaged existing cache is discarded so a
later retry can fetch valid bytes; an intact verified cache needs no network.
Interrupted downloads restart rather than attempting HTTP range resume. This
runs before replacing extracted/package resources, preserving existing output
when download validation fails.

All 61 Android Python tests passed (`build/update-acceptance/download-all-tests.log`).
The new authored download tests cover valid-cache reuse, stale partial cleanup,
checksum rejection/retry, damaged-cache replacement, interrupted streaming and
ENOSPC at sync. The retained real arm64 CPython archive also matched its pinned
checksum through the new helper with network access forbidden by the check.
No new upstream download was needed. This is build-time Python packaging
coverage; the own-CI compiler's APK execution design remains as documented above.


### Durable Python and compiler-data preparation

Python and compiler-data installation now sync every extracted file, persist
nested directory entries before writing the completion marker, sync the marker's
directory, rename the prepared generation and sync its parent directory. Both
callers remove their pending tree on failed preparation. Existing other resource
generations remain available while new content is staged. Process death still
leaves an untrusted `.pending` tree that the next attempt removes and restarts.

The refreshed full-service probe passed on a fresh API 36.1 arm64 emulator using
our installed compiler and embedded Python. First-use preparation plus synthetic
compile/link/activation took 87.18 seconds; a fresh service reused the completed
build in 7.86 seconds. The report is
`build/resource-durability-acceptance/build-result.json`. The APK build and 98-file
content guard passed. This verifies Android's normal file/directory sync and
publication path, not a power-loss guarantee or an interruption injected during
resource extraction. Those failure points and physical-filesystem behavior
remain pending; existing resource hashes and completion gating still determine
which generation can be reused. The initial compile rejected O_DIRECTORY because
it is absent from the public Java OsConstants API; the final implementation uses
public O_RDONLY/O_CLOEXEC flags and passed the real emulator run.


### Process death during Python resource unpacking

`resource_recovery_smoke.py` now exercises the actual embedded-Python preparation
path on an emulator. It verifies the installed APK digest, derives the exact
resource generation from its bundled archive hashes, and renames any existing
valid cache aside for restoration. After observing a real Python file in staging
without a completion marker, it identifies and force-stops the setup process,
confirms that process has exited, and plants an authored incomplete-tree sentinel.
Retry must discard that tree, complete resource publication, execute the embedded
synthetic probe and reproduce the bundled shared-script hashes. Cleanup restores
the previous cache and verifies setup/active/previous metadata byte for byte.
Pre-existing incomplete staging or an invalid cache is rejected before ownership
is taken; this test never runs on a phone.

Two API 36.1 arm64 runs passed, with retry durations of 20.88 and 20.60 seconds.
The reviewed report is
`build/resource-recovery-acceptance/recovery-reviewed-result.json`. A separate
negative guard check rejected pre-existing staging and preserved its authored
sentinel and the valid cache; its expected rejection is recorded in
`build/resource-recovery-acceptance/guard-result.json`, not counted as a successful
recovery run. Baseline embedded translation also passed exact desktop C/header
comparison before the interruption probe. APK build/content guard (98 files),
Python syntax and workflow YAML checks passed. Compiler CI runs the probe after
the synthetic service build and retains its report; remote execution is pending.
This proves process-death recovery during Python unpacking, not power-loss
behavior or an interruption during compiler-data unpacking. Those remain pending.


### Process death during compiler-data unpacking

The same resource probe now supports `--resource compiler`. It checks the pinned
compiler identity and bundled data checksum, preserves the existing data cache,
and terminates the live setup process after observing the first unpacked file
without completion. Retry must remove the injected incomplete-tree sentinel,
reproduce three bundled file samples (metadata, an Android header and a sysroot
library), then use the recovered own-CI compiler to compile/link/load the full
synthetic runtime. These are sample byte checks plus real tool execution, not a
claim of independently rehashing every installed compiler-data file.

The API 36.1 arm64 emulator run passed; retry plus synthetic runtime verification
took 63.57 seconds. The report is
`build/compiler-resource-recovery-acceptance/recovery-result.json`. The generalized
probe's default Python mode also passed again in 19.66 seconds, with restored
cache and unchanged setup/active/previous metadata in both cases. The preceding
baseline passed Android-hosted compilation, full-runtime loading and exact
desktop C/header parity. APK guard (98 files), Python syntax and workflow YAML
checks passed. Compiler CI runs both resource modes and retains both reports;
remote execution and physical power-loss/filesystem behavior remain pending.

### Pause checkpoints during resource preparation

The foreground setup service now passes its durable manual/thermal/battery pause
check into compiler executable verification, bundled archive hashing, Python and
compiler-data extraction, and recursive directory syncing. Hashing and extraction
check between 64 KiB reads, extraction also checks each ZIP entry, and directory
syncing checks each directory. A pause closes streams and removes the current
incomplete staging tree; resume restarts that resource stage. The final completion
marker/rename publication finishes without an intervening pause checkpoint, so a
published generation remains reusable. Filesystem operations and staging cleanup
can still delay a pause on slow storage; this is not a hard latency guarantee.
The shared installer and recompiler remain unchanged. The debug synthetic service
uses the same preparation callback path as the release service.

`tools/android/resource_pause_smoke.py` requests the normal service Pause action
after observing an actual unpacked file without completion. It requires the same
live setup PID, manual paused state within 15 seconds, no incomplete/published
resource generation, and no setup wake lock. Resume must complete and activate
the same job's real synthetic compile/link/load. Three bundled file samples are
checked, and prior setup metadata, the opt-in fixture marker and matching resource
cache are restored. Pre-existing incomplete staging is rejected before ownership.
Compiler CI runs both Python and compiler modes and retains their reports.

Local API 36.1 arm64 emulator runs passed in
`build/resource-pause-acceptance/compiler-result.json` and `python-result.json`.
Compiler pause took 1.78 seconds; resume through activation took 95.61 seconds,
including cold Python preparation. Python pause took 0.88 seconds; resume through
activation took 11.68 seconds with compiler data already prepared. These are
synthetic emulator measurements, not full-game or phone estimates. A separate
negative guard check preserved a pre-existing staging sentinel; its expected
rejection report is `guard-result.json`, not a successful pause run. APK build,
98-file content guard, all 61 Python unit tests, Python syntax and workflow YAML
checks passed. The disposable AVD, APK and duplicate build intermediates were
removed. Remote CI and physical thermal/battery/slow-storage checks remain pending
for rhemfur in the device checklist.

### Minimum supported Android API and initial battery state

The app's minimum framework API is 33, while prior local compiler/runtime tests
used API 36.1. The compiler workflow now runs setup/compiler loading on an API 33
x86_64 emulator before the existing API 35 full-runtime/display acceptance. It
reuses the same source-built compiler-enabled APK, runs the service policy/recovery
probe and completes a synthetic foreground-service build with fresh-service reuse.
The API 33 AVD and system image are removed before the API 35 stage, after checking
that the emulator has exited. The upstream [emulator action's cleanup](https://github.com/ReactiveCircus/android-emulator-runner/blob/v2/src/main.ts)
stops its emulator before returning; the extra process check guards disk cleanup.
Remote execution remains pending.

`runtime_smoke.py --expected-api 33 --compiler-only --require-extractor` records
API, ABI, build fingerprint and model, and rejects an unexpected API before app
mutation. Compiler-only reports explicitly scope the result to compilation,
library loading and activation, with `rendering: not_tested`. The ordinary probe
still requires SDL launch and display checks. This distinction is necessary for
the local API 33 Google APIs arm64 image revision 17: its Vulkan feature query did
not expose the required dynamic-rendering support, so the full display probe
failed before producing frames. That failed result is retained as
`build/api33-acceptance/unsupported-renderer-runtime-smoke-result.json` and the
corresponding display log. API 33 rendering on a compatible GPU/driver remains in
rhemfur's checklist; a compiler-only pass is not renderer acceptance.

The API 33 run found a production startup problem: a fresh setup service could
remain in `battery_unknown` with the platform reporting 100% and charging. The
service now consumes the sticky battery Intent returned by registration before
its first policy tick, using the same parsing as later broadcasts. Before the
fix, the full-service build timed out while paused; afterward it compiled,
linked, loaded, activated and reused the authored synthetic build without a
battery-change command. The policy thresholds and unknown-battery fallback are
unchanged.

API 33 also denies a `run-as` shell's signal to the app worker under its SELinux
policy. The debug recovery harness now checks ActivityManager for the exact
expected setup PID, matching app UID and `:setup` process name, then terminates it
from the app domain. This preserves a real abrupt worker death and sticky-service
restart without force-stopping the package. A negative check rejected the main
app PID and confirmed that process survived. The control remains debug-only.

Local API 33 reports in `build/api33-acceptance/` passed compiler execution,
source-built extraction, exact desktop C/header bytes and inventory, full synthetic
runtime loading/return-value execution and activation. The final foreground
service build took 3.24 seconds with resources already prepared; fresh-service
reuse took 2.10 seconds. Its worker peak RSS was 143,224,832 bytes and largest
reaped native-child peak RSS was 83,648,512 bytes. These process-lifetime maxima
are separate and non-additive, and the timings exclude cold resource preparation.
All nine service observations passed, including sticky recovery, manual pause,
thermal/battery hysteresis, automatic resume and wake-lock release. Reports also
retain the initial missing local extraction-fixture payload failure, the original
run-as signal denial and the before-fix battery timeout rather than counting them
as passes. Test hardware was an arm64 emulator with two virtual cores, 2 GiB RAM,
an 8 GiB data partition and software graphics; these are synthetic measurements.

The final APK also passed all nine service policy/recovery observations on API
36.1 and a synthetic foreground-service build in 6.14 seconds, with
fresh-service reuse in 2.98 seconds. Reports are retained in
`build/api36-policy-regression/`. Android APK builds and the 98-file content guard,
all 61 Android Python tests, 39 installer tests (two skipped), Python syntax,
workflow YAML and cleanup-script syntax checks passed. The API 33 image initially
expanded to 8.2 GiB and reduced free disk below 15 GiB; the emulator was not started
until zero-filled ranges were reclaimed and a full SHA-256 confirmed identical
image bytes, restoring over 19 GiB free. Both disposable AVDs, the downloaded API
33 system image, APK and duplicate intermediates were removed after the runs. The
original API 36.1 system image/AVD was retained. Real-phone tests and remote CI
remain pending; no renderer pass is claimed for the API 33 image.

### Recover Android hosts after secondary Vulkan surface loss

Previously `VK_ERROR_SURFACE_LOST_KHR` disabled secondary output until another
Android surface/display event arrived. A loss without such an event could leave
the secondary host showing its last image indefinitely. Acquisition and
presentation loss now invalidate the active native touch generation and viewports,
cancel touch, enable the main-screen fallback, and queue a host-recovery request
through `WwhdActivity`. Java checks the failed generation and active lifecycle
before replacing the affected Presentation or embedded fold SurfaceView. The
existing single delayed recovery slot is cancelled by close/stop/replacement;
obsolete requests cannot replace a newer host. Vulkan ownership and retirement
stay on the renderer/worker threads, and the JNI call queues UI work without
waiting for it. Persistent failures may retry on successive host generations;
an invalid active generation suppresses repeated requests while its replacement
is pending. Java queues a 500 ms delayed attempt; a real layout/display event may
recover earlier. Acquisition stays disabled until a surface is published again.

`display_smoke.py --exercise-surface-loss` injects secondary acquisition and
presentation rejection separately. Acquisition injection returns before acquiring
an image. Presentation injection first performs the real queue present, retaining
its semaphore wait and completion fence, then substitutes a surface-lost result
for a successful call. This exercises recovery without manufacturing an
unsignalled presentation fence or leaving an acquired image unconsumed. The
[Vulkan queue-present contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html)
also enqueues the synchronization operations when the presentation engine rejects
a real request as surface-lost. These are authored fault tests, not a claim that
the emulator driver generated that error itself.

The API 36.1 arm64 run passed 98 observations in
`build/surface-loss-acceptance/display-result.json`: both losses caused fallback
and touch cancellation; the same logical display received a replacement host,
both pictures advanced and touch worked again. An obsolete recovery request kept
the current host and held touch intact. Existing dismissal, rotation, folding,
blocked-worker, retirement-bound and settings checks passed too. A focused second
run in `fold-result.json` passed 82 observations, adding acquisition/presentation
loss inside embedded vertical fold panes, restored dimensions and touch, and
obsolete-request rejection for those hosts. Both runs reported zero secondary
idle waits and at most four retired generations pending.

The first run also passed the unchanged frame-time regression gates with at least
180 intervals per profile. Primary interval medians were 23.12 ms disabled,
26.71 ms enabled, 20.44 ms with the worker held and 18.89 ms disconnected; p99
values were 38.98, 57.90, 36.68 and 30.95 ms. The largest enabled interval was
82.07 ms. The observed enabled-minus-disabled median difference was 3.59 ms;
this includes shared GPU/host work and is not a causal measurement of GPU cost
alone. These are synthetic CPU submission-cadence measurements with software
Vulkan, a 16 ms loop delay, two virtual cores and 2 GiB emulator RAM, not physical
scanout or full-game timing. Dedicated-queue/present-fence capability was required.
The shared-queue compatibility path still needs device measurements and provides
less isolation from host presentation calls, which Vulkan permits to block.

Native Android build, minimal APK build and 80-file content guard passed, as did
host present-worker/touch/fallback tests, the Java fold-layout test, Python syntax
and workflow YAML checks. Display CI now enables surface-loss coverage, including
fold panes; remote execution, validation-layer evidence and real-driver surface
loss remain pending. The disposable AVD, APK, unstripped library and duplicate
build intermediates were removed, retaining reports and the reusable stripped
native library. No changes were made to the shared installer or recompiler.

### Extractor cancellation with an abandoned diagnostic reader

The Android subprocess adapter now signals worker completion with an event,
independent of its bounded output queues. Previously, cancellation could reap the
native extractor but leave its Python worker blocked while appending an end
marker to a full, unread stderr queue. A failed progress consumer would then wait
forever for that worker. Readers still drain queued diagnostics before observing
completion, and stdout/wait still propagate the native failure. The shared desktop
installer and translator are unchanged.

A deterministic regression fills stderr, abandons progress, and requires worker
termination before starting the diagnostic reader. It fails against the previous
implementation and passes with the fix. All 62 Android Python tests pass; the
installer suite runs 39 tests with two skips. This is host adapter coverage; no new
phone or emulator acceptance result is claimed for this change.

### Pausing during setup checksum validation

Dump fingerprints and extraction, translation, object and SDK checkpoint
validation now check the setup pause policy between reads of at most 1 MiB.
Previously these hashes could read a full game dump or cached tree before a
manual, heat or battery pause took effect. A pause closes the current file and
leaves existing checkpoints intact; retry starts that checksum again. Tree
validation also checks before beginning its inventory. Directory enumeration and
individual filesystem operations have no hard latency guarantee; the unchanged
translator's execution still pauses at its documented stage boundary.

Host tests verify cancellation after the first bounded read, file closure, and
pausing a multi-megabyte dump before any native extractor starts, followed by a
successful retry. All 64 Android Python tests pass. This change does not claim a
new physical-phone or emulator acceptance run.

### Interrupting the unchanged translator on a pause request

The Android adapter now polls its existing manual/thermal/battery pause policy
on a short-lived thread during translation. Python monitoring remains disabled
until a pause is requested. The watcher then enables line events for its own
reserved monitoring tool; a callback raises `Paused` only in translator/RPX code
on the owning setup thread. It disarms before raising, joins the watcher and
releases the monitoring slot on every exit. Other tools retain their slots and
event settings. This uses CPython's documented
[execution monitoring API](https://docs.python.org/3.14/library/sys.monitoring.html).
Neither `tools/recomp/recomp.py` nor the shared installer changes.

An interrupted translation unwinds and removes its pending generation; retry
starts translation again. Completed checkpoints remain reusable. The watcher
polls every 100 ms, but scheduling, filesystem operations and native Python
operations can delay interruption, so this is not a hard real-time guarantee.
Linking still pauses after its current stage and compilation between files.
The setup UI now describes this behavior.

The host regression requests a pause while the real `ppc2c.translate` invocation
is active, using a test-only 200 ms delay. The old adapter continues through
three instruction calls; the new adapter interrupts at the first, removes all
pending output, restores the installer runner, frees its monitoring slot and
stops its watcher. Retry matches direct desktop translation. All 65 Android
Python tests pass.

The same real-translator pause/retry probe is now part of `embedding_smoke` and
required by `python_smoke.py`, so existing CI emulator translation runs cover it.
Local API 36.1 arm64 / official embedded Python 3.14.8 acceptance passed:
`build/translation-pause-acceptance/result.json`. It includes exact desktop
C/header parity, native process bridge checks and completed checkpoint reuse.
Cold startup plus the fixture took 20.56 seconds. This run used a Python-only
debug APK: compiler, activation, full-game heat behavior and physical-phone
pause latency are outside this new result's scope.

### Secondary queues from another graphics family

Android queue selection now prefers a second queue in the primary graphics
family, then considers other graphics families that can present. Transfer-only,
compute-only, non-presenting and empty families are excluded. The device requests
one queue from each selected family when the families differ. Present-fence
retirement remains required for this worker path.

The worker's command pool uses the selected family. Its private snapshot and the
secondary swapchain use concurrent sharing across primary/secondary families
when necessary: ordinary worker drawing uses the secondary family, while the
explicit capture/waiting path can still draw on the primary family. All other
images retain their previous sharing mode. Same-family devices continue using
exclusive sharing. This follows Vulkan's
[image sharing rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkSharingMode.html)
and [command-pool family requirement](https://docs.vulkan.org/refpages/latest/refpages/source/VkCommandPoolCreateInfo.html).
Startup logs and debug display metrics report both selected family IDs.

The queue-selection host test covers 288 count/capability configurations, empty
and invalid primary selections, and skipping a transfer-only family before a
usable third family. CMake/CTest and Android CI include this test. Native Android
compilation and the APK content guard pass. Cross-family GPU execution and its
sharing cost require hardware coverage; a family-0 emulator run cannot prove
that path. Devices exposing only one usable queue, or lacking present-fence
retirement, still have an unresolved main-thread isolation gap. This change does
not claim that all hardware meets the independent-presentation requirement.

### Recovering surface loss while rebuilding a secondary swapchain

The display stress run exposed a real surface-loss return during presentation-mode
enumeration. Previously the generic swapchain exception handler hid secondary
output until another Android event. Vulkan failures now preserve their result
code while remaining runtime errors. Secondary swapchain capability, format,
presentation-mode and creation failures with `VK_ERROR_SURFACE_LOST_KHR` request
the same generation-checked Java host recovery as acquisition/presentation loss.
`VK_ERROR_OUT_OF_DATE_KHR` leaves a resize retry pending. Format-query errors are
checked explicitly; a truncated `VK_INCOMPLETE` format list remains usable.

The authored `lose_query` command rejects a secondary capability query before
issuing GPU work. Existing surface-loss acceptance now covers this fault for
external output and embedded fold panes, including touch cancellation, primary
progress, recreated secondary output and obsolete recovery rejection.

Two failed local stress reports are retained under
`build/secondary-family-acceptance/failed-before-query-recovery/` and
`failed-waiting-for-full-queue/`. The stress harness previously advanced after
retirement count changed even while a replacement still had zero drawable size.
It now waits for each of the first three replacements to draw at the requested
size before changing the next display. At the four-generation limit, secondary
output may pause until retirement resumes; the test still requires the exact
four-entry cap, continuing primary frames and recovery to the newest surface.
No frame-time thresholds or resource bounds were relaxed.

The final API 36.1 arm64 SwiftShader regression passed **114 observations** in
`build/secondary-family-acceptance/result.json`: external/folded
acquire/present/query loss, stale recovery rejection, touch, scaling, swapped
roles, removal/reconnect, primary rotation, dismissal, bounded retirement,
settings persistence and orderly worker teardown. Both queue families were 0;
this proves same-family regression coverage, not cross-family GPU execution.
The exact APK checksum is recorded in the report. Secondary idle waits stayed
at zero and pending retirement never exceeded four.

Primary interval measurements (milliseconds), with the unchanged regression
gates and at least 180 samples per profile:

| Secondary state | Median | p99 | Maximum |
| --- | ---: | ---: | ---: |
| Disabled | 20.0117 | 28.2694 | 28.3960 |
| Enabled | 23.9740 | 36.0167 | 36.1059 |
| Presentation held | 21.2166 | 30.6238 | 40.3140 |
| Disconnected | 20.6524 | 28.1849 | 31.3315 |

The enabled-minus-disabled median difference is 3.96 ms in this synthetic
64×36-pattern loop. It includes shared host/GPU work and is not an isolated
GPU-cost measurement, physical scanout timing or full-game result. The report
retains refresh rate, resolutions, settings, GPU/device and measurement scope.
Physical-device performance and the single-queue isolation gap remain open.

### Cancelling active compiler and linker commands on setup pause

The Android adapter now supplies its existing pause policy to the native process
bridge for compiler, archive and linker commands. Once the callback acknowledges
a pause it stays latched until that command returns, even if Resume arrives while
the bridge kills/reaps its process group. An acknowledged native interruption
becomes `Paused`; an unrelated `InterruptedError` remains a failure. The bridge
already kills the spawned process group and waits for its child before returning.
The shared desktop installer and translator remain unchanged.

Each compiler command now writes inside its own UUID-named pending directory.
Real Android cancellation exposed a clang-owned randomly named `.tmp` file that
was left beside the requested output by the old single-file cleanup. Removing
the private directory cleans both the requested object and compiler-owned
intermediates without touching another parallel worker. Startup removes abandoned
command directories from process death before reusing verified objects. The
linker already has a private staging directory. Neither cancelled command
publishes an object marker or replacement library; verified objects and the
previous library remain available.

Host coverage checks partial object plus compiler temporary cleanup, acknowledged
pause followed immediately by Resume, retention/reuse of verified objects,
cleanup after process death, unrelated native interruption, and partial linker
output with preservation of the previous library and successful retry. All 69
Android Python tests pass. Installer checks run 39 tests with two skips. The setup
UI describes command interruption and checkpoint reuse.

The embedded compiler probe adds an authored 8,000-function test unit, requests
pause on the third native poll, retries the interrupted compilation, and verifies
checkpoint reuse plus retention of the original objects. The linker probe cancels
a real process on its first native poll after `posix_spawn`, preserves the prior
library, retries and executes the translated return-42 function. Its tiny synthetic
link can finish before a second 100 ms poll: this test does not establish mid-link
progress. Probe durations include validation and cleanup, not isolated pause
latency. Full-game mid-link interruption and phone heat/battery timing remain on
the device checklist. Existing compiler CI runs now require both probe flags.

Failed evidence is retained in
`build/native-pause-acceptance/failed-clang-intermediate/` and
`failed-link-finished-before-pause/`; neither is a passing result.

Final API 36.1 arm64 / embedded Python 3.14.8 acceptance passed in
`build/native-pause-acceptance/result.json`, with the exact APK checksum and
system fingerprint. Compiler and linker pause probes took 0.266 s and 0.180 s
respectively, measured from stage entry through unwind, including validation
and cleanup. The warm-resource fixture completed in 15.77 s and also passed
unchanged desktop C/header parity, translated return-42 execution, complete
runtime loading, checkpoint reuse and the durable job's pause/update recovery.
The cached runtime SDK predates the latest Vulkan changes; this result proves
setup/tool cancellation and loading, not new dual-screen renderer behavior.


### Two-device Android GPU image bridge (prerequisite)

`AndroidSharedImage` imports one RGBA8 `AHardwareBuffer` allocation into two
logical Vulkan devices on the same physical device. Each device uses its own
dispatch table, image, allocation and semaphores. The bridge exports a sync FD
after a submitted GPU signal and temporarily imports it into the other device's
semaphore. Callers must provide image layout and EXTERNAL queue-family ownership
transfers, prevent semaphore reuse while pending, and drain uses before teardown.
The allocation is never CPU mapped or copied. This is preparation for giving
single-queue hardware a separate logical-device secondary presentation worker;
the live secondary swapchain has not yet been moved to that device.

The authored `shared_image_probe` debug command creates two separate logical
devices, each requesting only queue zero, and transfers three 64×36 GPU-cleared
patterns through the shared allocation. A consumer-only coherent assertion
buffer verifies every resulting byte within one UNORM rounding step. Explicit
fence/device waits and CPU readback belong to this diagnostic, not a normal
presentation path. Its Python runner rejects a mismatched installed APK, reports
probe failures, and requires primary frame progress after the probe.

The local API 36 arm64 emulator passed (`build/shared-image-acceptance/result.json`)
with APK SHA-256
`0d52cf6d82296f53d7015581af8507dff41ccb4c10d6491bcfebe2a0ef4b8528`.
A repeat run also passed (`repeat.json`), with primary frames advancing from 14
to 25. Three GPU rounds completed in 0.250 seconds including command publication and
log polling; primary frames advanced from 15 to 28. This duration is diagnostic
wall time, not GPU latency or secondary presentation overhead. An earlier
opaque-FD design failed capability checks on this emulator; its failed evidence
remains in `unsupported-opaque-fd/`. Sync-FD hand-offs passed instead.

The Android display CI workflow now runs this prerequisite probe and uploads its
JSON/log. That API 35 x86_64 workflow has not been run locally or remotely in this
worker. Passing image sharing does not establish isolated WSI, stalled-compositor
recovery, single-queue primary frame independence, physical-device support or
full-game performance. Those requirements remain open until the separate-device
swapchain and bounded asynchronous snapshot flow are integrated and measured.


### Presentation shader resources on a separate device

`IndependentPresenter` now owns a separate descriptor layout, pipeline layout,
samplers, format-keyed pipelines and Vulkan dispatch table. It uses the same GLSL,
FXAA, scaling parameters and draw recorder as ordinary presentation. Creating or
recording its resources does not replace the main device's function pointers or
pass the main device's pipeline cache to another device. Its owner supplies the
imported source view, target image/view, command buffer and descriptor pool,
serializes use, and drains GPU references before destroying the presenter. The
ordinary presentation path also uses the per-device dispatch for resource
creation and recording; primary pipeline cache accounting remains on that path.

The shared-image diagnostic now samples the imported AHardwareBuffer image with
this presenter on the consumer device, renders into an ordinary RGBA8 target,
and checks that target's bytes. It selects smooth, sharp and integer scaling on
the three rounds, and FXAA on the last. Uniform patterns verify the transfer and
shader draw, not visual filter quality at nonunit scales. The runner requires a
new shader-probe success marker so an older copy-only APK cannot satisfy the
extended test.

Local API 36 arm64 evidence (`build/independent-presenter-acceptance/result.json`)
passes all three shader-rendered rounds with distinct queue-zero logical devices.
APK SHA-256 is
`77db8721a321ceb90c7a7a6f7c8a76e5757de8e5b4bc9d940a9f0e749df2f4d3`;
final probe wall time was 0.248 seconds and primary frames advanced from 11 to 16.
Native compilation, minimal debug APK build and the 80-file APK guard passed.
This remains offscreen prerequisite evidence. The live secondary swapchain,
its WSI semaphores and retirement resources still belong to the main device;
secondary-device swapchain integration and its independence measurements remain
unfinished. Remote API 35 x86_64 CI and physical validation remain unrun.


The first broader regression attempt used a custom low-density emulator and
failed at primary touch injection (`failed-letterboxed-input/`). Its screenshot
shows the landscape app surface centered vertically in the physical portrait
display. The runner injected at surface-local coordinates without that window
offset. The failure is retained; it is not a passing touch result. The retry uses
the standard AVD density of 420 (primary extent 2400×1080). Robust physical input
injection for letterboxed windows remains an emulator-harness limitation.


At standard density, the complete secondary-display regression passed all 114
observations (`display-result.json`), including external/fold role swaps and
touch, acquisition/presentation/query surface loss, dismissal, blocked worker and
acquisition, rotation, retirement pressure, settings restart and orderly worker
teardown. Ordinary secondary idle waits stayed at zero; retained generations
reached the unchanged limit of four. All four timing profiles met the unchanged
180-sample minimum and relative/absolute frame-time gates:

| Secondary state | Primary median ms | Primary p99 ms | Primary maximum ms |
| --- | ---: | ---: | ---: |
| disabled | 22.110 | 39.603 | 41.222 |
| enabled | 25.329 | 63.897 | 72.202 |
| held | 21.293 | 47.521 | 59.089 |
| disconnected | 20.517 | 30.971 | 31.747 |

These are synthetic primary CPU submission intervals with shared host/GPU costs,
not physical scanout or isolated second-device swapchain performance. The Android
host suite passed 69 tests. The owned emulator was stopped and its disposable
AVD, APK copies and unstripped library were removed after verification.


### Separate-device secondary WSI integration — performance gate still failing

Android now selects a bounded separate-logical-device presentation path when the
existing dedicated-queue/present-fence path is unavailable and hardware-buffer,
external-semaphore-FD and foreign-family extensions are advertised. The main
device enables those dependencies, while the secondary device requests only
queue zero of the main graphics family. A debug Activity extra can force this
path on a multiqueue emulator; its metrics require `primary_requested_queues=1`
and `secondary_isolated_device=true`. Main and secondary device/queue handles
are checked for distinctness. Unsupported initialization retains the logged
compatibility fallback; physical capability coverage remains pending.

The secondary worker owns its device dispatch, Android surface, swapchain,
views, pipelines, command/descriptor pools and fences. It acquires with timeout
zero and a fence, waits for acquisition on that worker, then consumes the shared
snapshot. Drawing completion is also waited on the worker before presenting
with zero WSI wait semaphores. This avoids retaining a binary semaphore payload
in a stalled presentation engine and makes ordinary replacement independent of
swapchain-maintenance presentation fences. Replacement drains only the secondary
queue on its worker, destroys old application resources, and creates the newest
mailbox surface. Ordinary frames do not wait on this worker or drain the main
device. Explicit shutdown joins it and drains its device.

This lifetime scheme follows the Vulkan contracts for
[fence-only acquisition](https://docs.vulkan.org/refpages/latest/refpages/source/vkAcquireNextImageKHR.html),
[available writes at presentation](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html),
and [swapchain destruction](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroySwapchainKHR.html).
Outstanding application image operations must complete before destruction;
implementation-owned image memory may remain with the presentation engine.
Application counters bound owned swapchains and shared snapshots at one each;
they do not measure implementation-internal retention or total GPU memory.

The main queue copies RGBA8 UNORM scan images directly into one imported hardware
buffer; other formats use a normalization blit. It releases external ownership
and signals snapshot readiness in its ordinary submission. The renderer polls
that submission's fence without waiting before dispatching the worker hand-off.
A retired/reused submission serial proves original completion as well. Sync-FD
export/import runs on the worker. The worker releases the snapshot, waits its
own drawing fence and returns the payload before publishing completion. Thus
any later primary wait uses an already-completed secondary signal. While busy,
the renderer skips another snapshot. Surface events coalesce to the newest
retained native-window reference; acquired images are consumed before replacement.

The first local API 36 arm64 run passed WSI loss, touch, swapping, scaling,
blocked presentation/acquisition and primary-resize checks, but failed the
unchanged timing gate. Its primary median was 17.141 ms disabled versus 29.409 ms
enabled (`failed-inline-fd-and-blit/` under
`build/isolated-secondary-acceptance/`). Moving FD hand-off off the render thread
and using a direct matching-format copy reduced the enabled median to 24.280 ms,
but still failed the 23.418 ms gate (`failed-early-fd-export/`). Polling primary
completion before FD export did not fix the regression: disabled 17.171 ms,
enabled 25.346 ms, gate 23.464 ms (`failed-polled-fd-export/`). Held/disconnected
medians remained about 17.1 ms. These are retained failures, not acceptance.
Primary queue-submit CPU cost remains elevated during enabled output. No remote
CI success or physical performance claim is made for this path.

The emulator advertises `VK_KHR_external_memory_fd`, in addition to Android
hardware buffers. Ordinary opaque-FD GPU memory sharing is a possible next
experiment to investigate the hardware-buffer/driver cost; it has not been
implemented or verified here. The current live path still uses hardware buffers.
The original dedicated-queue path remains available on devices that support it.


The final local functional-only run passed all 100 observations
(`functional-result.json`, identity in `functional-identity.json`), with APK
SHA-256 `3c3bbcdb5934e3f65b5fc0d66861b2389b51e959722a337ee3c1502c2429f737`. It forced one primary queue and a distinct secondary
queue-zero logical device. Touch, role swaps, all three filters, external and
fold WSI-loss recovery, dismissal, primary rotation, blocked acquisition and
presentation, five consecutive surface-size replacements, settings restart and
orderly worker teardown passed. Application-owned swapchains and shared
snapshots stayed at one each, and ordinary main-device secondary idle waits
stayed at zero. Native builds, the 80-file APK guard and 69 Android host tests
passed. This functional result does not override the failed timing reports.

An earlier functional run exposed repeated recreation of stable fold swapchains:
Android returned SUBOPTIMAL for intentionally oriented panes. Matching the
existing Android path, the isolated path now recreates on real host events or
OUT_OF_DATE, while accepting SUBOPTIMAL. New tests require at least 60 primary
frames and secondary progress on each stable fold pane without another local
replacement wait. The final run passed those checks; local replacement waits
peaked at 58 across the full lifecycle test, versus 257 before the fix. The
pre-fix evidence is retained in `pre-suboptimal-fix/`. A missing creation log
caused an earlier harness failure (`failed-missing-swapchain-log/`); the worker
now logs actual successful swapchain dimensions. Explicit drains now apply their
consumed worker result through the same state-update handler as ordinary polls.

The forced functional test command is `tools/android/display_smoke.py` with
`--require-isolated-device --require-present-worker --exercise-primary-rotation
--exercise-folds --exercise-dismissal --exercise-surface-loss`, plus the usual
serial/APK/output arguments. Add `--measure-timing` to enforce the unchanged
performance gate, which failed at this milestone; later passing evidence is
recorded below. This worker has not run remote CI for
the new path. Its owned emulator and disposable AVD/APK/unstripped library were
removed after testing; reusable native objects and stripped libraries remain.


### External-memory alternative capability check

`shared_image_smoke.py --external-memory-capabilities` now runs a read-only
query command and writes structured image/buffer support alongside the exact
installed APK hash and emulator identity. Successful completion of this mode
means the queries ran and primary frames continued; it does not mean sharing is
supported. It changes no live presentation allocation strategy.

API 36.1 arm64 GFXStream/SwiftShader advertises external-memory FD extensions,
but RGBA8 optimal opaque-FD and DMA-BUF image queries returned
`VK_ERROR_FORMAT_NOT_SUPPORTED` for usages 23 (transfer, sampling, color target),
7 (transfer and sampling), and 3 (transfer only). Transfer/storage-buffer queries
returned zero features and compatible handle masks for opaque FD, DMA-BUF, and
Android hardware buffers. These tested alternatives therefore cannot replace
AHB images on this emulator. The unexecuted experimental FD allocator was
removed; hardware support still requires independent capability and GPU tests.

Final diagnostic and unchanged three-round AHB consumer-shader regression both
passed with APK SHA-256
`22b6583c1fd7ee5dcd041dc1fa661c16d7f92d63adbad237850f6068d66bb12b`.
Evidence is retained in `build/opaque-memory-acceptance/final-capabilities.json`
and `final-ahb-regression.json`, with companion logs. AHB probe duration was
0.240 seconds and primary presents advanced 14 to 20. Native/Gradle builds and
release guard (80 files) passed. No new secondary-frame timing acceptance is
claimed; the earlier failing gate remains open. Next investigation should
measure the existing worker/driver synchronization costs rather than assuming
an advertised external-memory extension provides a usable alternative.


### Separate-device worker stage timings

The display fixture accepts `secondary_profile1`/`secondary_profile0` to enable
opt-in CPU wall-time counters for acquisition, its fence wait, ready-FD transfer,
command recording, submission, draw-fence wait, returned-FD transfer, presentation,
and primary snapshot recording. They are disabled by default. `display_smoke.py
--measure-timing` records named lifetime counters and phase deltas alongside the
unchanged cadence gate. Counters include driver blocking and CPU scheduling;
they do not measure GPU busy time. Live counter snapshots can straddle a call.
The instrumentation currently covers the separate-device path only.

The first stage-profile run failed its cadence gate (disabled median 26.657 ms,
enabled 41.278 ms). Enabled calls averaged 9.705 ms in secondary submission,
4.540 ms in acquisition, 4.514 ms waiting its fence, 1.039 ms in recording,
0.028 ms in ready-FD transfer, and 0.073 ms in returned-FD transfer. Concurrent
host verification jobs occupied several cores, and held-worker cadence also
slowed, so these figures cannot establish uncontended costs or isolate a cause.
Evidence is `build/secondary-profile-acceptance/profile.json` plus logs and
`host-load.txt`; its failure remains retained.

Shared images now request transfer and sampling usage without color-attachment
usage: they receive a GPU copy/clear and are sampled by the consumer, never used
as a framebuffer attachment. Three-round shader/readback assertions passed with
APK SHA-256 `a1ba56eb056cf92ad4fde9798a0b7174483413933529c252c7337db466b6c1ca`
in 0.267 seconds, primary presents 17 to 24. The final named-counter run with
that APK reached every timing phase and failed the unchanged p99 gate: disabled
median/p99 23.546/34.718 ms; enabled 26.322/65.856 ms, p99 limit 62.077 ms; held
median 17.745 ms; disconnected 17.775 ms. Enabled acquisition/submission averaged
3.724/3.308 ms, versus ready/returned FD transfers 0.019/0.006 ms. Variable host
load prevents attributing the difference to usage flags. No performance fix or
acceptance is claimed. Final evidence is `narrow-usage-profile.json`,
`narrow-usage-ahb.json`, and `narrow-usage-identity.json` in the same directory.
Native/Gradle builds, 69 Android host tests, and release guard (80 files) passed.


### Acquisition-semaphore experiment (rejected)

A local experiment replaced fence-only secondary acquisition and its worker-side
host wait with a binary acquisition semaphore waited by the secondary draw
submission. The draw fence completed both waits before reuse, and presentation
still waited on no semaphores. All 100 functional observations passed, including
fold panes, surface-loss injection, blocked output, replacement, touch, rotation,
settings restart and orderly worker release. This was functional evidence only;
no validation-layer run was performed.

The unchanged cadence gate failed: disabled median/p99 19.016/30.043 ms; enabled
22.885/56.922 ms, against p99 limit 55.065 ms. Held/disconnected medians were
17.916/17.636 ms. Removing the explicit acquisition wait did not remove driver
cost: acquisition averaged 15.127 ms, submission 3.984 ms, ready/returned FD
transfers 0.011/0.008 ms, and recording 0.329 ms per completed call. Concurrent
host verification jobs remained active. These are CPU wall times under variable
load, not controlled comparative GPU measurements. The experiment does not
establish a performance improvement and was reverted; the live path retains
fence-only acquisition. Do not treat the functional pass as cadence acceptance.

Evidence is in `build/secondary-acquire-semaphore-acceptance/`: `functional-retry.json`
(100 observations, passed), `timing.json` (failed), companion logs,
`identity.json`, and `host-load.txt`. APK SHA-256 was
`6a8f4c047b8228157b0b22fe7bd4d3d83b289f631591e99647e001a61f5ebe31`.
The first fresh-emulator launch timed out before any secondary swapchain existed;
`functional.json` and logs retain that failure. The identical-APK retry passed;
startup stability is not inferred from that retry, and the missing initial
surface publication remains unclassified. Native/Gradle builds, 69 Android host
tests and the 80-file release guard passed for the experiment. Subsequent
investigation should consider shared-image ownership/layout costs; moving the
acquisition wait alone did not satisfy the performance requirement.


### Same-APK layout comparison and passing default-path gate

`PresentImageDraw` now carries its sampled source layout, defaulting to the
existing shader-read layout. The Android debug fixture accepts `shared_general`
(`display_smoke.py --shared-general --require-isolated-device`) to keep the
shared GPU image in GENERAL for copies and sampling. Ownership release/acquire
barriers and sync-FD hand-offs are retained. The source descriptor matches the
actual layout; ordinary presentation and the production isolated path retain
transfer/shader-read layouts. GENERAL is a diagnostic option, not a selected
performance fix. `shared_image_smoke.py --general-layout` verifies its three
consumer-rendered GPU rounds, while the default probe verifies the existing
layouts. Both passed with the same APK.

The display runner now verifies the installed APK hash against `--apk`, writes
that identity into both successful and failed reports, and records host CPU
count and load averages around each timing phase. This makes later comparisons
inspectable without assuming host scheduling was stable.

Local API 36.1 arm64 GFXStream/SwiftShader evidence, all with APK SHA-256
`b927e3f1b567dde111b2569a95fb3b0bb51e410806b9fd4147dce66ab267f735`:

- Default layout standalone timing passed: disabled median/p99 17.450/25.543 ms,
  enabled 21.416/44.404 ms; limits 23.812/48.315 ms.
- GENERAL layout timing passed: disabled 20.588/35.860 ms, enabled
  25.526/61.244 ms; limits 27.735/63.789 ms. It does not demonstrate an advantage
  over the default under variable load. GENERAL also passed all 100 functional
  lifecycle observations.
- The full default-path command (forced single requested primary queue, separate
  logical device, timing, rotation, folds, dismissal, and WSI loss) passed all
  114 observations plus orderly resource release: disabled 23.889/33.289 ms,
  enabled 28.030/57.778 ms; limits 31.862/59.933 ms. Held/disconnected medians
  24.651/23.793 ms; enabled worst 61.878 ms. Application swapchain/snapshot counts
  remained bounded and ordinary main-device secondary idle waits stayed zero.
  The swapped screenshot was inspected: blue GamePad main, red TV external.

Artifacts are in `build/shared-general-acceptance/`: `pixels-optimal.json`,
`pixels-general.json`, `functional-general.json`, `timing-optimal.json`,
`timing-general.json`, and `default-combined.json`, with logs/screenshots.
Earlier failures remain retained; these passes are synthetic emulator cadence
acceptance for the current default path, not zero shared-GPU cost, uncontended
performance, full-game performance, physical scanout, or a validation-layer run.
The results do not isolate a causal improvement from the earlier usage-flag
change. Physical measurements remain in the device checklist.

The display CI workflow now runs the forced isolated-device full test with its
unchanged timing gate, plus the GENERAL GPU pixel probe, and uploads their
JSON/log/screenshot artifacts. Workflow YAML parsed locally; native/Gradle builds,
69 Android host tests and the 80-file release guard passed. The updated API 35
x86_64 GitHub workflow has not run remotely; changes remain local for lead review.


### Validate completed resource caches before reuse

Completed Python and compiler-data caches previously trusted the presence of a
marker after publication. They now require its exact one-byte value and compare
cached contents and file/directory inventory against the already-checksummed APK
archives on every preparation. Missing, changed, extra, or symbolic-link entries
invalidate reuse. Content comparison uses two 64 KiB buffers plus a path
inventory, with pause checkpoints while reading and traversing. Pausing this
read-only validation preserves the existing generation. Invalid caches rebuild
through the existing synced pending-directory publication; active install paths
are separate. Resource cleanup now unlinks symbolic links without following them.
Neither the shared installer nor recompiler changed.

`ResourceVerifierTest` exercises valid reuse, marker contents, same-length damage,
truncation, extension, missing/extra entries, escaping paths, symlinks and a
mid-file pause/retry. `resource_recovery_smoke.py --damage-complete` damages a
completed cache while retaining its valid marker and adds an unbundled file.
The probe requires repaired APK sample bytes and removal of the extra entry.
It also checks existing active/previous selection metadata, referenced libraries
and RPX files, and an authored file in the actual save directory remain unchanged.
The original cache is restored after the authored test. CI adds the Java test
and both Python/compiler completed-cache repair probes with JSON artifacts.

API 36.1 arm64 emulator evidence in `build/resource-cache-acceptance/`, with APK
SHA-256 `4e92758bce1df50f9b0fba5de4e761cd0f17de82afe204b60843c8ebfc359c98`:

- `active-synthetic-install.json`: desktop/Android generated inventory and bytes
  match; the Android-hosted compiler links, loads and activates the full-runtime
  synthetic fixture. Its cached runtime SDK predates the recent Vulkan work;
  this is resource/setup acceptance, not current dual-display integration proof.
- `python-repair-final.json`: completed-cache repair passed, 10.975 seconds.
- `compiler-repair-final.json`: repair plus synthetic compile/link/load passed,
  18.188 seconds. Both final repair probes preserved the existing library/RPX,
  selection metadata and authored save.
- `python-interruption-regression.json`: actual setup-process termination during
  unpacking, clean retry and preservation checks passed, 11.843 seconds.
- `warm-cache.json` and `warm-native.json`: healthy cache validation, translation,
  compile/link/load and exact desktop C parity passed in 15.964 seconds, with
  both cache marker inodes/timestamps unchanged (no redundant publication).

Retry durations include activity launch, repair, fixture work and assertions;
they are not isolated verification/cancellation latencies. `environment.json`
records the emulator fingerprint and measured sizes: APK 150,851,731 bytes,
compiler-data allocation 210,424 KiB and Python/source allocation 94,300 KiB.
Those cache allocations exclude APK/native executables. The synthetic activation
probe reported setup-process VmHWM 184,700 kB; it is not aggregate process-tree
peak memory or full-game setup usage. No new peak-storage measurement is claimed.
The initial compiler probe hit a save-helper permission failure before damaging
the cache (`compiler-repair.json`); the helper now uses the established emulator
shell save path and its successful retry remains separate.

Gradle build, 69 Android host tests, the Java resource-verifier test, Python syntax,
workflow YAML parsing and release guard (98 files) passed. Remote CI and physical
storage/thermal/battery behavior remain pending. The owned emulator and disposable
APK/AVD/packaging copies were cleaned after testing; compiler/Python packages and
reusable native objects remain.

### Secondary window startup retry acceptance

`GamePadDisplay` retries a valid secondary display whose Presentation window
creation throws `InvalidDisplayException`: four total attempts, with 500, 1000
and 1500 ms delays. Ordinary layout callbacks cannot bypass the pending timer or
exhausted budget. A relevant display event, a different selected display, or a
new activity session permits another budget. Closing/stopping/replacing the host
cancels the timer; generation checks reject obsolete recovery callbacks. While
unavailable, native single-display fallback continues updating.

The debug-only activity can author window rejection before `show()` via
`invalid_display_shows`; production activity intents do not expose this probe.
`tools/android/display_startup_smoke.py` installs and verifies the exact APK,
waits for actual overlay device removal/addition, and restores overlay settings.
The display CI workflow runs it and retains its report/log. Remote CI is pending.

Local API 36.1 arm64 emulator, standard 1080x2400 density 420 primary, 800x480
secondary, SwiftShader, two emulator cores, 2048 MiB RAM:
`build/display-startup-acceptance/startup.json` passed seven observations using APK
SHA-256 `2ed49bc708935adbf367305014f061274deeae54c8d1c9d154acf0ff90a696b2`.
Zero, one and three authored rejected shows reached dual presentation. Persistent
failure stopped at four warnings while primary frames advanced from 78 to 200;
a remove/reconnect then consumed the final authored rejection and recovered at
primary frame 290 with ten secondary presentations. This verifies bounded
window-creation failure handling, not real driver/service rejection. It does not
establish the cause of the earlier unclassified fresh-emulator startup timeout.
Physical startup/resume window-service races remain in the device checklist.

The same APK passed the full forced-isolated lifecycle smoke, 100 observations
(`lifecycle.json`): rotation, folds, display/touch swaps, secondary acquisition
and presentation holds, dismissal, injected surface loss and orderly release.
Application-owned secondary swapchains and shared snapshots each stayed at most
one. This regression run did not repeat frame-time acceptance; the prior default
combined timing results remain separate. Gradle, 69 Android host tests, Python
syntax, workflow YAML parsing and the release guard (80 files) passed.
The owned test emulator was stopped and removed after its PID exited; disposable
APK/packaging copies were removed while retaining reusable native/toolchain data.

### Current runtime SDK and on-device dual-display integration

Earlier resource/setup probes used a cached runtime SDK that preceded the
isolated Vulkan presenter. Rebuilt `build/native-check/game` and packaged its
current object/link inventory with `package_runtime.py`, then repackaged official
Python and the shared sources with that SDK. No second recompiler is involved;
`git diff bloom -- tools/recomp/recomp.py tools/installer/setup.py` was empty.

`runtime_smoke.py` now accepts `--require-isolated-device --exercise-lifecycle`.
It verifies the installed APK bytes and, when supplied, the APK runtime SDK
identity against the phone-linked library's reported identity. Its display probe
requires both SDL launches to load the activated library and paired asset tree.
Restored active/previous selection metadata is checked byte for byte, including
absence of previously missing metadata. Errors are retained in the JSON report.
The API 35 compiler CI invocation enables these combined checks; the API 33
compiler-only invocation retains its explicit rendering limitation. Remote runs
remain pending; no workflow was dispatched or changes pushed.

Local API 36.1 arm64, two emulator cores, 2048 MiB RAM, standard 1080x2400 density
420 main display and 800x480 secondary with SwiftShader. The fresh compiler APK
was 150,910,155 bytes, SHA-256
`1079510a1ebe9850feb507b8020ca438b1560bd30e3804d1c2dc4defef2a5d8a`.
`build/integrated-runtime-acceptance/package.json` records the package provenance.
Runtime SDK identity
`abdb66749a12f0836a68406734f8a36d9004bb97ee2ea6746e887873ec773721`
matched the phone-compiled library; its archive was 5,716,428 bytes and unpacked
payload 20,301,628 bytes.

The first-use embedded fixture passed extraction, exact desktop generated
C/header inventory and byte comparison (14,580 bytes), compilation, loading and
activation in 49.536 seconds including resource preparation and interruption
probes. Direct tiny-fixture translation took 0.003607 seconds. The initial
five-object compilation took 0.831 seconds and linking 0.551 seconds, producing
112,432 object bytes and an 8,388,288-byte full runtime library. These individual
events exclude later authored pause/retry probes in the same run. Process VmHWM
was 178,632 kB, excluding native children and aggregate peak memory. These are
synthetic emulator measurements; they do not establish full-dump phone costs.

`build/integrated-runtime-acceptance/runtime.json` passed compilation/activation
and all 100 forced-isolated display observations using that same phone-linked
library: secondary holds, failed acquisition, unplug/replug, roles/scaling/touch,
rotation, folds, dismissal, surface loss and restart. Both SDL launches selected
the active compiled library and paired assets; original active/previous metadata
was restored. All observations reported the isolated device, at most one
secondary swapchain and shared snapshot, and zero ordinary primary-device
secondary-idle waits. This run did not repeat frame-time thresholds; previous
separate default timing acceptance remains the timing evidence.

`warm.json` passed the updated wrapper's exact installed-APK and SDK-identity
assertions and byte-checked metadata restoration. Its compiler-only fixture took
20.244 seconds with prepared caches; rendering is explicitly `not_tested` in
that report and is covered by the separate integrated run above. Native build,
Gradle, 69 Android host tests, Python syntax, workflow YAML parsing and the public
content guard (98 files) passed. Full-game phone validation, aggregate memory and
peak temporary storage sampling, physical heat/battery behavior, and remote CI
remain pending. No full-task completion claim is made.
The owned emulator exited before its disposable AVD was removed. APK/packaging
copies, unpacked upstream Python and the unstripped linked library were cleaned;
current SDK/Python/compiler packages, stripped library and native objects remain.

### Sampled setup storage and aggregate process memory

`tools/android/resource_sampler.py` discovers the app's processes plus all native
process descendants from `ps`, collects each PID's PSS/RSS with `dumpsys meminfo`,
and measures app-private allocated blocks with `run-as du` and shared `/data`
available blocks with `df`. It reads stage labels only; input names, command
arguments, source paths and raw process output are excluded from its JSON. A
missing PID or changed process set makes that memory sample incomplete and
excludes it from the complete-sample maxima. Reads are sequential, not
simultaneous. Sampling can miss short-lived children and peaks, and itself adds
work. RSS sums double-count shared pages; shared-volume free-space changes can
include unrelated system activity. Kernel worker/child high-water values remain
separate and are never added to claim concurrent aggregate memory.

The standalone CLI records APK hash and device fingerprint and reads an existing
debug setup job without installing or controlling the app. The device checklist
contains its command. A two-second paused-job capture passed validity checks with
five complete memory samples (`cli-paused.json`); this only verifies the capture
command, not full-game resource use. It exits on completion/failure or the capture
window; a paused job remains observable until resumed or the window ends.

`setup_build_smoke.py --sample-resources` captures the first synthetic service
build, before its fresh-process reuse check. The CI compiler workflow enables it.
The debug fixture adds an explicitly authored 8192-function translation unit and
asserts an exported function result through the loaded library. That unit exists
to make a real compiler process observable; its extra memory/time/code size is
not representative of the recompiled game. The shared recompiler and installer
are unchanged. Release packaging rejects this debug service fixture.

Local API 36.1 arm64, two emulator cores, 2048 MiB RAM, official Python 3.14.8,
pinned own-built LLVM, one compiler job:
`build/resource-sampling-acceptance/probe.json` passed full service activation and
fresh-process reuse, with the same current runtime SDK identity as the previous
integration run. APK SHA-256
`5eec4ce2cdb1b2759fade8fdb819fc20202617bbe6bc0b8398f77f0c0382fb6c`;
APK 157,288,749 bytes, installed APK/native allocation 345,772,032 bytes.
Preparation/build took 16.181 seconds; fresh-process checkpoint reuse 11.453
seconds. The resource-observation fixture generated 740,572 bytes, compiled six
objects (1,857,496 bytes) in 1.992 seconds and linked a 9,664,224-byte library in
0.250 seconds. These are synthetic measurements with the sampling overhead.

There were 31 complete and two incomplete memory samples, including three
complete samples with a native compiler child. The requested post-sample delay
was 0.25 seconds; maximum observed start gap was 1.136 seconds and maximum sample
duration 0.882 seconds. Maximum sampled sequential process sums were 300,340,224
PSS bytes and 476,073,984 RSS bytes. The kernel reported worker lifetime peak RSS
167,321,600 bytes and largest reaped-child lifetime peak RSS 356,208,640 bytes.
Those kernel maxima demonstrate why the sampled observations must not be called
exact concurrent peaks. Full-run phone aggregate memory remains pending.

Private allocation began at 312,471,552 bytes and reached 421,801,984 bytes, which
was also its final pre-cleanup allocation; minimum sampled shared `/data` space
was 4,356,091,904 bytes. This was an update/warm-cache measurement, not a clean
install footprint. `cache-allocation.txt` shows two Python cache generations of
94,536 and 94,540 KiB after the source-only update, plus the unchanged 210,424 KiB
compiler cache. Obsolete Python cache cleanup is a concrete remaining issue;
the older cache is not needed to launch the independently linked active library.

The earlier small-fixture cold run (`build.json`, APK provenance in
`initial-package.json`) passed in 14.750 seconds with a complete compiler-child
sample: sampled private peak 321,384,448 bytes, final 321,245,184 bytes, observed
excess above final 139,264 bytes. That excess is not an exact temporary-storage
peak. A tiny warm run missed every child and correctly failed the capture gate
(`warm.json`); the larger authored unit addresses this repeatability problem.
Initial fresh-emulator harness attempts failed before installation because the
runner assumed the package already existed; it now handles absent packages and
always retains failure reports when cleanup runs. Those logs remain separate.

Gradle, 74 Android host tests, Python syntax, workflow YAML parsing and the APK
content guard (98 files) passed. The source fixture changes are debug-only;
remote CI and privately supplied dump/phone measurements remain pending.
The owned emulator was stopped and its PID confirmed exited before removing the
AVD. Disposable APK/Gradle copies and unpacked upstream Python were cleaned;
resource reports, current packages and reusable native objects remain.

### Obsolete Python and compiler preparation-cache cleanup

The update-related cache growth found in the preceding storage capture is fixed.
After verifying or atomically preparing the current resource generation,
`EmbeddedPython` and `AndroidToolchain` remove obsolete generations from their
respective preparation-cache roots and sync the parent directory. Active game
libraries, job/checkpoint directories, paired assets and saves live elsewhere.
Python tasks are serialized in the dedicated setup process; APK resources are
fixed for that process, and APK replacement terminates it before the new version
uses its resources. The active linked runtime does not need an obsolete Python
or compiler preparation tree to launch.

`ResourceCacheCleanup` recognizes only hash names of the selected generation's
width (128 digits for Python, 64 for compiler data), plus their `.pending` trees.
It preserves the current generation and unrecognized sibling names. The current
cache must have a valid marker, and callers perform the full trusted-archive
content/inventory verification before invoking cleanup. It rejects a symbolic
cache parent and unlinks obsolete symlinks without following them. A checkpoint
before processing each entry allows service pause/stop to interrupt cleanup; retry
can finish a partially deleted obsolete tree while retaining the current cache.
Preparation can temporarily retain the previous generation until the new one
is verified; this change does not eliminate that transient storage cost.

The stdlib-only Java `ResourceCacheCleanupTest` passes both hash-width cases,
current/foreign/unknown sibling preservation, invalid-current rejection,
symbolic parent/root/nested links, idempotence and an actual mid-delete pause
with partial obsolete-tree removal followed by successful retry. Both Android
build and compiler workflows run this test. Compiler CI also runs the emulator
cleanup probes and retains their JSON. Remote execution remains pending.

`resource_recovery_smoke.py --obsolete-generations` copies a real prepared cache
into an authored obsolete slot, creates an obsolete incomplete tree and an
unrecognized sibling, then requires real app cleanup. It checks reclaimed
allocation, original marker inode/timestamp preservation (healthy cache reuse),
trusted bundled-file samples, and preservation of current/previous install
files, selection metadata and an authored save. It restores only its own probe
state and leaves the original selected cache intact.

Local API 36.1 arm64 emulator, two cores, 2048 MiB RAM, SwiftShader, exact APK
SHA-256 `072ee3d22193edeaaca4d02cca11ca9b27dc796a3c78fda66767a03c3e115416`:

- `build/cache-cleanup-acceptance/python-cleanup.json` passed, reclaiming
  96,825,344 allocated bytes; the complete probe took 8.688 seconds.
- `compiler-cleanup.json` passed, reclaiming 215,482,368 allocated bytes; the
  complete probe took 15.344 seconds and compiled/linked/loaded the full runtime
  fixture. These elapsed values include verification and fixture work, not
  isolated deletion latency.
- Both probes kept the healthy current marker unchanged, preserved the unknown
  sibling, and preserved two active-install files, metadata and the authored save.
- `python-interruption.json` and `compiler-interruption.json` passed actual setup
  process termination during unpacking and fresh retry, with retry times 10.602
  and 24.644 seconds. These are unpacking regressions, not process-death injection
  during cleanup; mid-delete interruption/retry is covered by the Java test.
- `active-runtime-display.json` passed 37 observations after cleanup and recovery.
  Both SDL launches selected the retained on-device compiled library and paired
  assets; TV/GamePad output, touch, role swaps, display replacement and saved
  settings remained usable. This is an active-library regression, not a new
  forced-isolated lifecycle or frame-time acceptance run.

`environment.json` records emulator and APK provenance. Gradle, the content guard
(98 files), 74 Android host tests, Java resource verification/cleanup tests,
Python syntax and both workflow YAML parses passed. Physical update/cleanup
interruption and full-dump storage behavior remain in rhemfur's checklist.
The owned emulator exited before its AVD and disposable APK/Gradle copies were
removed. Reports, current compiler/Python/SDK packages and reusable native
objects remain; no heavyweight compiler rebuild was needed for this change.
