#!/usr/bin/env python3
"""Check actual WWHD_LINK_TRACE displacement from paired 30/true60 hold-to-boost runs.

Usage: check_move_speed.py EVIDENCE_DIR [--factor 2] [--stock TRACE]
EVIDENCE_DIR contains speed-30/{off,on}_01/link.trace and speed-true60 equivalents.
Inputs and traces stay local; this script contains no game data. Rates use the fixed 30-Hz
logic clock, excluding true60 preview positions. Swimming averages the first three complete
stroke cycles after settling, avoiding unequal stroke phases and subsequent collisions.
"""
import argparse
import itertools
import json
import math
from pathlib import Path
import statistics


def read(path):
    rows = [list(map(float, line.split())) for line in Path(path).read_text().splitlines()
            if len(line.split()) == 18]
    assert rows, f"No Link trace rows in {path}"
    return rows


def horizontal(row):
    return math.hypot(row[10], row[12])


def distance(a, b):
    return math.hypot(b[6] - a[6], b[8] - a[8])


def measure(rows):
    full = [r for r in rows if r[3] == 1]
    land = [(a, b) for a, b in zip(full, full[1:])
            if a[5] == b[5] == 6 and b[1] - a[1] == 1 and a[9] >= 16.9 and b[9] >= 16.9]
    assert len(land) >= 10, "Not enough settled running steps"
    groups = [list(g) for proc, g in itertools.groupby(full, key=lambda r: r[5]) if proc == 55]
    assert groups, "No swimming"
    swim = max(groups, key=len)
    speed = [horizontal(r) for r in swim]
    lows = [i for i in range(15, len(swim) - 1) if speed[i] < speed[i - 1] and speed[i] < speed[i + 1]]
    assert len(lows) >= 4, "Need three complete swimming stroke cycles after settling"
    swim = swim[lows[0]:lows[3] + 1]
    pairs = list(zip(swim, swim[1:]))
    assert all(b[1] - a[1] == 1 for a, b in pairs), "Swimming logic steps are not contiguous"
    normalized = {}
    for proc in (6, 55):
        ratios = [distance(a, b) / horizontal(b) for a, b in zip(full, full[1:])
                  if a[5] == b[5] == proc and b[1] - a[1] == 1
                  and a[9] >= 16.9 and b[9] >= 16.9 and horizontal(b) > 1]
        assert len(ratios) >= 10
        normalized[str(proc)] = statistics.median(ratios)
    return {
        "run_units_per_second": statistics.mean(distance(a, b) * 30 for a, b in land),
        "run_steps": len(land),
        "swim_units_per_second": sum(distance(a, b) for a, b in pairs) * 30 / len(pairs),
        "swim_steps": len(pairs),
        "median_step_distance_over_velocity": normalized,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path)
    parser.add_argument("--factor", type=float, default=2)
    parser.add_argument("--stock", type=Path)
    args = parser.parse_args()
    result = {}
    for mode in ("30", "true60"):
        result[mode] = {}
        for state in ("off", "on"):
            rows = read(args.evidence / ("speed-" + mode) / (state + "_01") / "link.trace")
            measured = measure(rows)
            expected = 1 if state == "off" else args.factor
            for ratio in measured["median_step_distance_over_velocity"].values():
                assert abs(ratio / expected - 1) < .001, (mode, state, "once per step", ratio)
            if mode == "true60":
                halves = [distance(a, b) / horizontal(b) for a, b in zip(rows, rows[1:])
                          if a[5] == b[5] == 6 and a[3] != b[3] and b[2] > a[2]
                          and a[9] >= 16.9 and b[9] >= 16.9 and horizontal(b) > 1]
                assert len(halves) >= 10
                measured["preview_distance_over_velocity"] = statistics.median(halves)
                assert abs(statistics.median(halves) / (.5 * expected) - 1) < .001
            result[mode][state] = measured
        for key in ("run_units_per_second", "swim_units_per_second"):
            ratio = result[mode]["on"][key] / result[mode]["off"][key]
            assert abs(ratio / args.factor - 1) < .005, (mode, key, ratio)
        if mode == "true60":
            for state in ("off", "on"):
                for key in ("run_units_per_second", "swim_units_per_second"):
                    assert abs(result[mode][state][key] / result["30"][state][key] - 1) < .001
    if args.stock:
        result["stock"] = measure(read(args.stock))
        for key in ("run_units_per_second", "swim_units_per_second"):
            assert abs(result["stock"][key] / result["30"]["off"][key] - 1) < .001
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
