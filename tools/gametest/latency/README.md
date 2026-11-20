# Input-to-screen latency test

Measures, in milliseconds, how long a button press takes to reach the screen: from the moment the press is
available to the game's pad reads to the moment the first frame showing Link's reaction is handed to the display.
Works in every build (normal and decomp) with the Vulkan renderer, on the Mac and on Android.

## In the game (runtime/src/latency_test.h)

`WWHD_LATENCY_TEST=bits:period_ms:hold_ms:count` turns it on (off by default; nothing changes without it). For
example, `4000:1500:80:40` presses B 40 times, one press every 1.5 s, held for 80 ms.

- The presses start `WWHD_LATENCY_START_STEPS` of Link's steps after the last state load (default 150).
- Each press gets a jitter of 0 to 40 ms, so the presses fall on every phase of the frame rhythm.
- `WWHD_LATENCY_STICK=1` also holds the left stick up during a press (Link starts to walk: a reaction in every
  scene where he stands; at the cliff state B does nothing).
- `WWHD_LATENCY_START_AT=n` starts at logic step n since boot instead (portable-state runs).
- A press waits while Link has not returned to the procedure he had before the first press (counted as `busy`).

`WWHD_LATENCY_LOG=file` writes one line per press. All times are host steady-clock ns:

| point | meaning |
|---|---|
| press | the time the button becomes visible to the game's pad reads (the time real input would arrive) |
| read | the first fresh `VPADRead` that returned it, and the logic step count at that read |
| react | Link's first execute after the read whose procedure (`daPy_lk_c::mCurProc`) changed: the game state shows the reaction, for example a sword swing |
| swap | the game's `GX2SwapScanBuffers` that ends that pass: the first frame with the reaction |
| present | that frame's TV present call returned (`vkQueuePresentKHR`; a frame held for even delivery counts when its present is issued). With hidden windows (scripted Mac runs) there is no present: the end of the renderer's swap on the render thread counts instead |

- If Link does not react within 45 logic steps (an event, a menu), the press is written as a `miss` line.
- The log gets a `[latency]` summary line every 10 presses and a `[latency] done:` line at the end.
- Limit: the present is the moment the frame is handed to the display system. The compositor and the panel come on
  top of it and are the same for every setting compared.

## Mac: scripted run

```sh
source ~/projects/zelda_wwhd-work/wt-entry-runs/cliff/env.sh   # CLIFF_STATE, CLIFF_SHADER (copies are used)
tools/gametest/latency/run_latency.sh build/fast/wwhd runs/lat-120-off 120
tools/gametest/latency/run_latency.sh build/fast/wwhd runs/lat-120-c2 120 WWHD_DRAWOFF=2
python3 tools/gametest/latency/summarize.py runs/lat-120-off runs/lat-120-c2
```

- The setup is the same as `cliff_check.sh`: the cliff state is loaded at logic step 3400.
- Frame interpolation is switched on at the given rate 100 steps later (`30`: off).
- The run uses the Vulkan renderer and hidden windows.
- `PRESSES`, `PERIOD` and `BUTTONS` (hex) change the presses.
- Wrap each run with `~/projects/zelda_wwhd-work/tools/gameslot.sh` on the shared Mac.

## Phone

- Set the same variables in the app's environment, with `WWHD_LATENCY_LOG` in the app's files folder.
- Pull the file after the run and read it with `summarize.py FILE...`.

## Output

- `summarize.py RUN...` prints one line per run: the number of presses and misses, the press-to-present median, p90,
  min and max in ms, and the medians of the parts (press to read, read to reaction, reaction to swap, swap to
  present).
- With several runs, a pooled line follows.
- `--json` prints one object per run instead.
