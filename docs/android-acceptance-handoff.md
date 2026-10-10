# Android acceptance handoff

Local implementation is ready for lead review on `android-ondevice-dualscreen`.
This is not release acceptance: remote compiler/emulator CI and the physical-device
checks remain pending. Nothing has been pushed or dispatched by this worker.
The source requirements remain the two root-level Android prompt files.

## Requirement audit

| Requirement | Implementation and evidence | Outstanding scope |
| --- | --- | --- |
| One unchanged recompiler/setup; PC alternative | Android host adapters call shared installer operations. `git diff bloom -- tools/recomp/recomp.py tools/installer/setup.py` is empty. Existing PC packaging/import remains available. | Full private-game PC/phone end-to-end validation. |
| Embedded Python and identical translation | Official CPython 3.14.8 packaging, dependency/license verification and alternatives are documented in `android-implementation.md`. Latest synthetic bootstrap compares complete generated inventory and raw bytes (14,580 bytes). | Full-game resource measurements. |
| Own pinned Android-hosted clang/lld | `build_toolchain.py`, `android-toolchain.yml`, pinned LLVM revision and NDK, sysroot/licenses/checksums; local arm64 compiler executes inside app and loads asserted synthetic output. | Lead must run CI, review and publish versioned artifacts. x86_64 remote execution remains unverified. |
| Supported executable delivery | Compiler PIE drivers installed as APK native libraries; verified data assets unpack privately. Target SDK is retained. Writable app-home execution is forbidden on supported Android. | Preferred first-setup compiler download is not implemented; signed executable delivery needs a separate supported design. Current APK delivery is the documented constraint resolution. |
| Phone selection/extract/translate/compile/activate | SAF input/key handling, unchanged pipeline adapters, foreground setup service, versioned verified checkpoints and atomic paired library/assets activation. Synthetic extraction, actual phone-side compilation/load and active-runtime display smoke passed locally. | Real user-owned input formats, providers and complete gameplay are pending on devices. No public game fixtures. |
| Progress, recovery, pauses and resource bounds | Durable job manifests, storage reserve, cancellation, manual/thermal/battery policy, bounded compiler jobs, wake-lock release and restart wording. Host fault tests and emulator interruption/pause/recovery probes are recorded in implementation notes. | Sustained physical heat/battery/background behavior; immediate unsettled SAF grant reboot case. |
| Automatic update/rebuild, preservation and cleanup | Fingerprints cover translation/toolchain/runtime inputs; synthetic APK update/failure recovery preserves active build and authored saves. Verified obsolete resource caches are reclaimed; current cache and unknown siblings preserved. | Real-game automatic update and full storage requirements. |
| Synthetic emulator CI | Workflows run embedded translation, exact desktop parity, Android-hosted compilation/load, update and recovery probes. Local arm64 execution passed. | Workflow definitions are coverage, not proof that remote CI passed. Lead review/push is required by worker policy. |
| Measurements | Resource sampler captures setup descendants, complete/missing samples, APK/private allocation and stage timings. Synthetic resource probe passed with observable compiler children. | Sampled maxima are not exact peaks. Full-dump peak memory/storage and clean toolchain-build elapsed time remain unmeasured. |
| Secondary display and Vulkan ownership | Android logical-display/hinge hosts, independent secondary Vulkan device/swapchain worker, bounded snapshot handoff and nonblocking acquisition; shared simulation, no production CPU frame readback. | Physical GPU/driver coverage and shared-GPU contention cost. |
| Touch, scaling, roles and lifecycle | Desktop scaling/settings, viewport touch mapping and cancellation, generation guards, fallback/recovery. Unit coverage plus synthetic distinct pictures, touch, swaps, rotation/recreation/folds, dismissal and surface-loss probes. | Ayn Thor, real hinge topology, external display/refresh/sleep and physical letterbox/gesture accuracy. |
| Main-display independence and performance | Forced isolated-device combined probe passed 114 observations including timing and lifecycle. Phone-linked current runtime separately passed 100 lifecycle observations. Bounded ownership and ordinary secondary idle counters checked. | Emulator cadence does not isolate physical shared-GPU or scanout cost; historical timing failures remain documented. |
| Hardware handoff | `android-device-checklist.md` provides device/configuration, full setup, recovery, update, touch/lifecycle and repeatable frame/resource capture checks. | All physical checks remain explicitly pending for rhemfur. |

## Evidence to retain

Local evidence is deliberately outside source control under `build/`:

- `cache-cleanup-acceptance/`: latest APK SHA-256
  `072ee3d22193edeaaca4d02cca11ca9b27dc796a3c78fda66767a03c3e115416`;
  bootstrap, verified cache cleanup, actual unpack process termination/recovery,
  and 37 active phone-linked runtime display observations passed. Python and
  compiler cleanup reclaimed 96,825,344 and 215,482,368 allocated bytes respectively.
- `integrated-runtime-acceptance/`: current runtime SDK phone-side compile/load
  plus forced isolated-device lifecycle, 100 observations passed. Its APK differs
  from the latest cleanup APK; do not conflate the exact binaries.
- `shared-general-acceptance/default-combined.json`: production layout, 114
  observations passed. Median frame intervals disabled/enabled 23.8893/28.0299 ms;
  p99 33.2887/57.778 ms, within recorded thresholds. These are emulator observations.
- `resource-sampling-acceptance/probe.json`: authored larger synthetic translation
  unit, one compiler job, 31 complete and two incomplete memory samples, three
  complete samples containing native children. Sampled aggregate PSS maximum
  300,340,224 bytes. Sequential sampling can miss peaks; this is not a game estimate.

Latest handoff check: `python3 -m unittest discover -s tools/android -p 'test_*.py'`
passed all 74 tests. `python3 -m unittest discover -s tools/installer -p 'test_*.py'`
ran 39 tests successfully with two skips. Earlier APK/native/JVM/guard commands and exact result scope
are recorded in `android-implementation.md`; hardware and remote results must not
be inferred from them.

## Lead acceptance steps

1. Review local commits and the delivery/executable constraint above; run the
   configured Android/toolchain/display workflows through the authorized lead.
2. Retain workflow artifacts/checksums and investigate failures before publishing
   compiler-enabled releases. Do not substitute an unrelated compiler.
3. Give rhemfur the device checklist and collect privately owned full-dump/device
   metrics. Keep missing checks pending and exclude keys, game code and content
   from shared artifacts.

No owned emulator is left running. Retain the small evidence reports and reusable
compiler/Python/runtime packages; do not recreate the deleted LLVM build tree
unless an actual compiler change requires it.
