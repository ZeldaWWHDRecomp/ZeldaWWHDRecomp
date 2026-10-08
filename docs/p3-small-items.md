# P3 runtime changes

Base: current GitHub devel (fresh clone, 2026-10-08). Reference ideas:
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
reset, synthetic kgsl valid/zero denominator. Android hardware telemetry and overlay screenshot
remain untested/pending. CI pending.

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
These are synthetic displacement tests, not measured Link displacement in the game. Actual scripted
30/true60 land/swim runs remain pending; no in-game correctness claim from the math test alone.

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
output. Runtime source syntax checks PASS. Forced crash and Android build/share validation pending.

Follow-up validation: native BOTH runtime links successfully (existing local game archive with
only three hook-mismatched generated units rebuilt privately; no generated material committed).
All six selected CTests PASS: mod_manager, mod_packages, crash_log_module, perf_average,
move_speed, crash_redact. Forced host crash contains startup environment and redacted test HOME;
the assertion rejects an unredacted HOME anywhere in the log. Android Java/resources APK build
`:app:assembleDebug` PASS with Studio JBR (the system default Java 8 was unsuitable). This checks
the Java/share integration, not native Android execution or a physical-device share intent.
