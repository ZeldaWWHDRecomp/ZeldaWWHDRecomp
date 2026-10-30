"""Analyze an on-device WWHD_VK_STUTTER_TRACE without mixing menus into gameplay.

Frame bounds are explicit: --start-frame is an excluded boundary, --end-frame
is included. A "frame" event records the interval ending at that frame; excluding
the start boundary prevents its interval from including the preceding menu/load.
Percentiles use nearest rank. Event durations can overlap and are not frame time.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import re
from typing import Iterable


HEADER = {'kind', 'frame', 'key', 'start_ns', 'end_ns', 'thread', 'result', 'cpu_ns'}


def distribution(values: Iterable[float]) -> dict:
    ordered = sorted(values)
    if not ordered:
        return {'count': 0}

    def percentile(fraction):
        return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]

    return {
        'count': len(ordered),
        'total_ms': sum(ordered),
        'mean_ms': sum(ordered) / len(ordered),
        'min_ms': ordered[0],
        'p50_ms': percentile(.50),
        'p95_ms': percentile(.95),
        'p99_ms': percentile(.99),
        'max_ms': ordered[-1],
    }


def report_snapshot(path: Path) -> dict:
    """Extract named CPU/GPU metrics; keep the report for attribution and context.

    A report is a cumulative snapshot, not necessarily scoped to the CSV window.
    Its averages must not be substituted for the selected frame distribution.
    """
    raw = path.read_text(encoding='utf-8-sig')
    number = r'(\d+(?:\.\d+)?)'
    result = {'source': path.name, 'raw_text': raw, 'metrics': {}}
    match = re.search(
        rf'Output FPS {number} \(average {number}\), render interval {number} ms '
        rf'\(worst {number}\), average logic steps/s {number}', raw)
    if match:
        result['metrics'].update(zip(
            ('output_fps', 'average_output_fps', 'render_interval_ms',
             'worst_interval_ms', 'average_logic_steps_per_second'),
            map(float, match.groups())))
    for caption, key in (('Game CPU', 'game_cpu'), ('Render CPU', 'render_cpu'),
                         ('GPU interval', 'gpu_interval')):
        match = re.search(rf'{re.escape(caption)}: {number} ms \(avg {number}\)', raw)
        if match:
            result['metrics'][key] = {'current_ms': float(match[1]), 'average_ms': float(match[2])}
    for caption, key in (('Dynamic resolution:', 'dynamic_resolution'),
                         ('Shader compile:', 'shader_compilation')):
        match = next((line for line in raw.splitlines() if line.startswith(caption)), None)
        if match is not None:
            result[key] = match
    return result


def analyze(trace: Path, start_frame: int, end_frame: int,
            reports: Iterable[Path] = (), label: str | None = None) -> dict:
    if start_frame < 0 or end_frame <= start_frame:
        raise ValueError('Use 0 <= start-frame < end-frame')
    selected_frames = []
    kinds = {}
    rows = selected_rows = 0
    with trace.open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames is None or not HEADER.issubset(reader.fieldnames):
            raise ValueError('Expected WWHD_VK_STUTTER_TRACE CSV columns: ' + ','.join(sorted(HEADER)))
        for row in reader:
            rows += 1
            try:
                frame = int(row['frame'])
                begin, end = int(row['start_ns']), int(row['end_ns'])
                cpu, status = int(row['cpu_ns']), int(row['result'])
                if min(frame, begin, end) < 0 or end < begin or cpu < -1:
                    raise ValueError('negative counter, reversed interval or invalid CPU duration')
                kind, key = row['kind'], row['key']
                if not kind or key is None:
                    raise ValueError('missing event kind or key')
            except (TypeError, ValueError, KeyError) as error:
                raise ValueError(f'Invalid trace row at CSV line {reader.line_num}: {error}') from error
            if not start_frame < frame <= end_frame:
                continue
            selected_rows += 1
            duration = (end - begin) / 1_000_000
            group = kinds.setdefault(kind, {'durations': [], 'cpu': [], 'keys': set(), 'nonzero_results': 0})
            group['durations'].append(duration)
            if cpu >= 0:
                group['cpu'].append(cpu / 1_000_000)
            group['keys'].add(key)
            group['nonzero_results'] += status != 0
            if kind == 'frame':
                if end == begin:
                    raise ValueError(f'Zero-length frame interval at CSV line {reader.line_num}')
                selected_frames.append((frame, duration))
    if not selected_frames:
        raise ValueError('No frame events in the selected gameplay window')

    intervals = [duration for _, duration in selected_frames]
    frame_stats = distribution(intervals)
    frame_stats['updated_frames_per_second'] = len(intervals) * 1000 / frame_stats['total_ms']
    frame_stats['intervals_ge_50ms'] = sum(value >= 50 for value in intervals)
    frame_stats['intervals_ge_100ms'] = sum(value >= 100 for value in intervals)
    frame_stats['percent_intervals_ge_50ms'] = frame_stats['intervals_ge_50ms'] * 100 / len(intervals)
    frame_stats['percent_intervals_ge_100ms'] = frame_stats['intervals_ge_100ms'] * 100 / len(intervals)
    events = {}
    for kind, group in sorted(kinds.items()):
        if kind == 'frame':
            continue
        events[kind] = distribution(group['durations'])
        events[kind]['cpu'] = distribution(group['cpu'])
        events[kind]['unique_keys'] = len(group['keys'])
        events[kind]['nonzero_results'] = group['nonzero_results']
    compile_kinds = [kind for kind in events if any(token in kind for token in ('shader', 'spirv', 'pipeline', 'translation'))]
    return {
        'schema_version': 1,
        'label': label or trace.stem,
        'trace': trace.name,
        'window': {
            'start_frame_exclusive': start_frame, 'end_frame_inclusive': end_frame,
            'first_observed_frame': min(frame for frame, _ in selected_frames),
            'last_observed_frame': max(frame for frame, _ in selected_frames),
            'rows_in_trace': rows, 'rows_selected': selected_rows,
        },
        'percentile_method': 'nearest_rank',
        'frame_intervals': frame_stats,
        'events_by_kind': events,
        'compilation_event_kinds': compile_kinds,
        'reports': [report_snapshot(Path(path)) for path in reports],
        'notes': [
            'Frame bounds select start < frame <= end; the excluded start boundary avoids the preceding menu/load interval.',
            'frame measures time between updated game frames, not display refreshes or repeats of the previous picture.',
            'Shader/pipeline/translation events are attributed by their recorded frame; background work may finish outside the window.',
            'Event durations overlap across threads and must not be summed as frame time or percentages of total elapsed time.',
            'Report averages may include earlier frames and are retained as snapshots, separate from the selected CSV statistics.',
            'GPU interval can include stalls and is not GPU utilization. Nonzero event results are not necessarily errors.',
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--start-frame', type=int, required=True, help='excluded boundary after boot/menu')
    parser.add_argument('--end-frame', type=int, required=True, help='included last gameplay frame')
    parser.add_argument('--report', type=Path, action='append', default=[], help='native performance snapshot; repeatable')
    parser.add_argument('--label', help='measurement label, e.g. baseline-warm or fast-warm')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = analyze(args.trace, args.start_frame, args.end_frame, args.report, args.label)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False, allow_nan=False) + '\n', encoding='utf-8')
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')
    frame = result['frame_intervals']
    print(f"{result['label']}: {frame['count']} intervals, {frame['updated_frames_per_second']:.2f} updated FPS, "
          f"p50/p95/p99 {frame['p50_ms']:.2f}/{frame['p95_ms']:.2f}/{frame['p99_ms']:.2f} ms; "
          f">=50ms {frame['intervals_ge_50ms']}, >=100ms {frame['intervals_ge_100ms']}. JSON: {args.output}")


if __name__ == '__main__':
    main()
