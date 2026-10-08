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
