# HUD cost fixture

Build and install like the other guest examples. With `draw` false (the default),
the mod registers no HUD callback. With `draw` true it records exactly 200 filled,
alpha-blended rectangles on TV per actor logic pass. Both variants keep the same
guest module and Link hook loaded. Options take effect only after restart.

Use separate isolated manager profiles with `config.hud-cost.draw` false/true.
The existing `tools/bench/run_bench.py` accepts those manager directories through
two `--variant` environment sets and interleaves them with `--runs 3`. Report
render-thread CPU milliseconds per frame, medians, IQRs and paired differences.
This fixture measures 200 rectangles; it does not establish texture-upload or
large-text performance.

The planned campaign is three paired baseline/HUD runs on Metal and three on
Vulkan: twelve measured runs total, plus the runner's warm-up for each renderer.
Start timing only below a one-minute load of 12 with no other game or benchmark.
Use copied saves, private caches, no audio, four build jobs, and at least 15 GiB
free disk. Keep measurements and private frame dumps out of packages and commits.
