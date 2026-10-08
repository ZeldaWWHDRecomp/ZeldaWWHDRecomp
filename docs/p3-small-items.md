# P3 runtime changes

Base: GitHub devel `872f17e2efb6733e398178d4ce08934d3fcccc60` (fresh clone, 2026-10-08). Reference ideas:
GreenNaugahyde/ZeldaWWHDRecompAndroid (MPL-2.0), inspected read-only; no fork builds or scripts run.

## Average performance rates

The common overlay accumulates frame and actor-logic counter deltas over elapsed wall time, even
while hidden. It resets on renderer, internal resolution, frame mode or target-rate changes, and
counter rollback. True60 half steps count as logic passes; interpolated draws do not.
Android polls readable kgsl busy/total, GPU devfreq load and CPU/GPU/SoC thermal zones every two
seconds while the performance overlay is visible. Unavailable readings are omitted. Thermal-zone
values use the Linux millidegree Celsius convention. No permission changes.

Validation: standalone `perf_average_test` compiled with Clang C++20 and warnings-as-errors, PASS:
cumulative irregular sampling, every reset key, 120 drawn/60 logic, counter rollback, explicit
reset, synthetic kgsl valid/zero denominator. Android hardware telemetry remains untested. The local screenshot report
`local-evidence/performance-overlay.html` shows MoltenVK at 30 fps on the Dragon Roost approach:
instantaneous 30 FPS, average 28.5 FPS / 28.5 logic steps/s. Screenshot assets are deliberately
excluded from Git under the no-game-assets rule. Android, Linux and Windows CI PASS (links below).

## Run/swim speed

Built-in `move-speed` is disabled by default. Factor 1.25–4 (default 1.5), hold L3 by default;
L3/R3/L/R/ZL/ZR selectable, with host bindings through Controls. Saved settings and mod profiles
include the factor/button. Environment aids: `WWHD_MOD_MOVE_SPEED=1`, `WWHD_MOD_MOVE_FACTOR=2`.
Only PROC_MOVE (6) and PROC_SWIM_MOVE (0x37) qualify. At the existing 023FD39C site, horizontal
movement deltas are multiplied together with true60's dt. Stored velocity, vertical movement and
collision pushes are unchanged. Off uses the original path. The vector argument identifies Link
without depending on true60 being enabled. Site ownership remains in true60_link.cpp.

Validation: movement math across 30/60/120/240 presentations and 30/60 logic rates, both procedures,
factors 1.25/1.5/2/4, off/hold/rebind and other procedures PASS; manager preferences/disable-all PASS.
Actual headless scripted Windfall-dock runs (hold L3, forward stick, factor 2) PASS:

| Mode | Run off / on (units/s) | Swim off / on (units/s) |
| --- | --- | --- |
| 30 fps | 510.052 / 1019.963 | 446.144 / 892.323 |
| true60 | 510.031 / 1019.995 | 446.144 / 892.322 |

`tools/bench/check_move_speed.py` reproduces the checks from local WWHD_LINK_TRACE files. Rates
use full-pass position deltas and the fixed 30-Hz logic clock; true60 previews are checked
separately. Running uses settled speed steps. Swimming uses the first three complete stroke cycles
after settling, since comparing unequal stroke phases or later collisions gives misleading rates.
The factor check allows 0.5%; observed ratios differ from 2 by less than 0.03%. Per-step
horizontal displacement/velocity also matches 1 off and 2 on (tolerance 0.1%); true60 run previews
match 0.5 off and 1 on. Swimming retains the existing true60 framework's 30-Hz procedure cadence.
A separate current-devel runtime without the mod reproduces the disabled run/swim rates within
0.1%, confirming the off path. The synthetic tests additionally cover other presentation rates,
factors and held/rebound buttons. Physical Android input remains untested.

## Crash context and Android sharing

Normal game-thread code refreshes a bounded snapshot once per second: renderer, resolution,
frame mode/target, controller, built-in switches, enabled/active packages and WWHD environment.
Startup context is available for a crash before game initialization. Secret/key/token/password
variables are suppressed. Atomic bytes and revision checks keep handler reads free of locks and
allocations; interrupted snapshots are explicitly reported. Home paths and user names are redacted
in the context, module paths, recovery hints and saved log ring. Guest halt logs are redacted too.
Android offers the newest crash report once on the next start, shares a copy from a narrowly scoped
FileProvider cache directory using a temporary read grant. Sharing needs a player tap.

Validation: redaction test PASS for configured and other Unix/Windows homes, user name and >4 KB
output. Additional regression tests PASS for case-insensitive Windows homes, mixed separators,
other drive letters, account names containing spaces and overlapping home prefixes. Runtime source syntax checks PASS. Forced crash and Android Java/resources build are validated below. Physical-device sharing remains untested.

Follow-up validation: native BOTH runtime links successfully (existing local game archive with
only three hook-mismatched generated units rebuilt privately; no generated material committed).
All six selected CTests PASS: mod_manager, mod_packages, crash_log_module, perf_average,
move_speed, crash_redact. Forced host crash contains startup environment and redacted test HOME;
the assertion rejects an unredacted HOME anywhere in the log. Android Java/resources APK build
`:app:assembleDebug` PASS with Studio JBR (the system default Java 8 was unsuitable). This checks
the Java/share integration, not native Android execution or a physical-device share intent.

## Surface invalidation: measured; index skipped

Inspected both Vulkan surfaces.cpp and Metal metal_main.mm. Both walk the surface collection;
Vulkan already skips dirty surfaces and caches mip ranges, while Metal checks known level ranges.
Existing render-thread profiler `invalidate` timings include the entire operation (and Vulkan's
buffer-cache invalidation), so they bound the possible saving from replacing only the linear walk.

| MoltenVK 30 fps scene | Steady sampled frames | Invalidation ms/frame | Calls/frame | Render CPU ms/frame |
| --- | --- | --- | --- | --- |
| Windfall docks, scripted movement | 720 / 840 / 960 | 0.17 / 0.18 / 0.18 | 191 | 3.48 / 3.75 / 3.27 |
| Dragon Roost approach cinematic | 720 / 840 / 960 | 0.20 / 0.20 / 0.20 | 196 | 5.40 / 5.34 / 6.85 |

The same dock route is in PROC_SWIM_MOVE throughout frames 840–960 (confirmed by the Link trace),
so those later Vulkan samples measure the sea just off Windfall. Metal was measured separately:

| Metal scene | Steady sampled frames | Invalidation ms/frame | Calls/frame | Render CPU ms/frame |
| --- | --- | --- | --- | --- |
| Windfall dock-to-sea route | 720 / 840 / 960 | 0.20 / 0.21 / 0.21 | 191 | 3.11 / 3.23 / 3.03 |
| Sea, camera turned toward Windfall | 1200 | 0.29 | 191 | 4.10 |

Additional town and Dragon Roost workloads, after visual verification of the scene captures:

| MoltenVK 30 fps scene | Sample frames | Invalidation ms/frame | Calls/frame | Render CPU ms/frame | Draws/frame |
| --- | --- | --- | --- | --- | --- |
| Windfall town house exterior | 840 | 0.22 | 191 | 4.31 | 1144 |
| Upper Windfall town, repeated fixed views | 840 | 0.23–0.28 | 192 | 4.28–5.14 | 1373–1408 |
| Dragon Roost Rito aerie, crowd/animated NPC sequence | 840 / 1080 / 1320 / 1680 | 0.17–0.18 | 194–195 | 2.51–3.08 | 564–674 |

The upper-town location was also checked against Link's position (824, 1020, -205008). Rito aerie
is stage Atorizk. Town traversal attempts did not move these restored states, including a Pro
Controller check, so they are reported as fixed views rather than successful walking runs. The
Rito sequence remained in dialogue; it is an animated multi-NPC workload, not free-roaming gameplay.
The separate dock-to-sea route did move and swim, as the displacement traces confirm.

Startup before the state load is excluded. Frame 720 is the first post-load window and may include
cache warm-up; frame 840 and later provide the steady comparison. PNG capture work increased swap
CPU in the windows after frames 900/1080 and is excluded from the additional CPU comparison above.
On the Metal dock-to-sea route, frame 1080 crosses new geometry/shader creation (8.86 ms render
CPU); it is excluded from the steady comparison, though invalidation was only 0.29 ms. All runs
used private caches, headless windows, no audio and copied saves on the shared Mac. Each scenario
has one run, with additional upper-town/controller repetitions, rather than a statistical
cross-device performance study.

**Decision: skip the interval index.** Total invalidation cost, including work an index would not
remove, stayed below 0.30 ms per 30-Hz logic frame in the sampled workloads. Its maximum possible
saving is therefore below 0.9% of a 33.3 ms frame or a CPU core at 30 steps/s. It is around 5–7%
of the render worker's active CPU in some samples, but that worker spends roughly 28–30 ms/frame
waiting here; this is not an observed frame-rate bottleneck. The actual recoverable scan cost is
smaller than the measured whole-operation bound, and an index adds maintenance for mutable base
and mip ranges. The absolute benefit is negligible in these measurements, so the existing behavior
is preserved under the prompt's explicit skip rule. Android/mobile CPU costs and other locations
are unmeasured; on-device profiling could justify revisiting the index later.

## Branch and CI

Four separate commits on `p3-small-items`: average overlay `69ae042`, speed mod `c6f5295`,
crash context/share `6462d9b`, and this profiling/report commit. All use the required Lukas S author
address. No main/devel pushes, merges, issues, PRs or comments.

CI for the implementation/report checkpoint bf02786: [Android](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37812381500),
[Linux](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37812381450),
[Windows](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37812381514): all PASS.
Report checkpoint 126a277 also passed [Android](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37813855769),
[Linux](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37813855813),
and [Windows](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/runs/37813855764).
The final crash-context commit additionally closes those path-redaction gaps; the standalone
redaction regression executable passes with warnings treated as errors. Final branch CI is
available in the repository Actions view. Android CI builds native placeholder guest code
and a release APK, then checks it with the artifact guard. Native Android execution, physical
controller input, sysfs availability and the share chooser still require a device.
Local derived traces/test summaries and the requested overlay screenshot are retained under
excluded `local-evidence`. All own build outputs, generated code, archives, dependencies,
shader caches, test executables and disposable Android outputs were deleted after verification.
No files outside the two clones were deleted.
