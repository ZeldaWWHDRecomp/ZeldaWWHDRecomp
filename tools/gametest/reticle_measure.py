#!/usr/bin/env python3
"""Measure local reticle traces and captured PNGs (requires Pillow).

Projection coordinates describe generated draw passes. PNG components describe
actual captured pixels. These are separate measurements: this tool does not
assume a render-thread log line identifies the same frame as a projection.
The ROI must be visually checked to contain the aiming dot, without red scenery.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

TRACE = re.compile(
    r'\[reticle\] step (\d+) pass (\d+) t (\S+) packet ([0-9A-Fa-f]+) '
    r'slot (-?\d+) world (\S+) (\S+) (\S+) screen (\S+) (\S+) (\S+)')


def read_trace(text):
    rows = []
    for line in text.splitlines():
        m = TRACE.search(line)
        if not m:
            continue
        values = m.groups()
        row = dict(step=int(values[0]), draw_pass=int(values[1]), t=float(values[2]),
                   packet=values[3].upper(), slot=int(values[4]),
                   world=list(map(float, values[5:8])), screen=list(map(float, values[8:11])))
        if not all(math.isfinite(v) for v in [row['t'], *row['world'], *row['screen']]):
            raise ValueError('nonfinite reticle projection: ' + line)
        rows.append(row)
    return rows


def distribution(values):
    if not values:
        return dict(count=0, max=None, median=None, p95=None)
    ordered = sorted(values)
    return dict(count=len(values), max=max(values), median=statistics.median(values),
                p95=ordered[max(0, math.ceil(len(ordered) * .95) - 1)])


def projection_metrics(rows):
    streams = {}
    for row in rows:
        streams.setdefault((row['packet'], row['slot']), []).append(dict(row))
    result = []
    for (packet, slot), samples in sorted(streams.items()):
        samples.sort(key=lambda row: row['draw_pass'])
        jumps, hold_jumps, boundary_jumps, steps = [], [], [], {}
        previous = None
        for row in samples:
            row['jump_px'] = None
            if previous and row['draw_pass'] == previous['draw_pass'] + 1:
                jump = math.hypot(row['screen'][0] - previous['screen'][0],
                                  row['screen'][1] - previous['screen'][1])
                row['jump_px'] = jump
                jumps.append(jump)
                (hold_jumps if row['step'] == previous['step'] else boundary_jumps).append(jump)
            previous = row
            steps.setdefault(row['step'], []).append(row)
        per_step = []
        for step, group in sorted(steps.items()):
            x = [row['screen'][0] for row in group]
            y = [row['screen'][1] for row in group]
            per_step.append(dict(step=step, fractions=[row['t'] for row in group],
                                 positions=[row['screen'] for row in group],
                                 x_range_px=max(x) - min(x), y_range_px=max(y) - min(y)))
        result.append(dict(packet=packet, slot=slot, samples=samples, steps=per_step,
                           consecutive_draw_jumps_px=distribution(jumps),
                           within_logic_step_jumps_px=distribution(hold_jumps),
                           logic_boundary_jumps_px=distribution(boundary_jumps)))
    return result


def red_components(image, roi=(.35, .30, .65, .70), min_area=3):
    """8-connected red-dominant components, in full-image pixel coordinates.

    Weighted centres use redness above the larger of G/B. Width/height retain
    connected trails; two disconnected dots remain two separate components.
    Yellow lock-on markers need a different colour fixture; they are excluded.
    """
    image = image.convert('RGB')
    width, height = image.size
    x0, y0 = int(roi[0] * width), int(roi[1] * height)
    x1, y1 = int(roi[2] * width), int(roi[3] * height)
    if not 0 <= x0 < x1 <= width or not 0 <= y0 < y1 <= height:
        raise ValueError('ROI must be a nonempty rectangle inside the image')
    weights = {}
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = image.getpixel((x, y))
            if r >= 100 and r >= 1.7 * g and r >= 1.7 * b:
                weights[(x, y)] = r - max(g, b)
    remaining = set(weights)
    components = []
    while remaining:
        seed = remaining.pop()
        stack, pixels = [seed], []
        while stack:
            x, y = stack.pop()
            pixels.append((x, y))
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    p = x + dx, y + dy
                    if p in remaining:
                        remaining.remove(p)
                        stack.append(p)
        if len(pixels) < min_area:
            continue
        total = sum(weights[p] for p in pixels)
        xs, ys = zip(*pixels)
        components.append(dict(area_px=len(pixels),
                               centre_px=[sum(p[k] * weights[p] for p in pixels) / total for k in (0, 1)],
                               bbox_px=[min(xs), min(ys), max(xs) + 1, max(ys) + 1],
                               width_px=max(xs) - min(xs) + 1, height_px=max(ys) - min(ys) + 1))
    return sorted(components, key=lambda c: (-c['area_px'], *c['centre_px']))


def frame_number(path):
    m = re.search(r'(\d+)(?:_tv)?\.png$', path.name)
    return int(m[1]) if m else -1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path, help='private run directory containing log and captured TV PNGs')
    parser.add_argument('--roi', type=float, nargs=4, default=(.35, .30, .65, .70),
                        metavar=('LEFT', 'TOP', 'RIGHT', 'BOTTOM'))
    parser.add_argument('--min-area', type=int, default=3)
    parser.add_argument('--out', type=Path, required=True, help='new JSON result; never overwrites evidence')
    args = parser.parse_args()
    if args.min_area < 1:
        parser.error('--min-area must be positive')
    from PIL import Image
    rows = read_trace((args.run / 'log').read_text(errors='replace'))
    frames = []
    for path in sorted(args.run.glob('*.png'), key=frame_number):
        if path.name.endswith('_drc.png'):
            continue
        with Image.open(path) as image:
            components = red_components(image, args.roi, args.min_area)
            size = list(image.size)
        frames.append(dict(file=path.name, capture_index=frame_number(path), size=size,
                           sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                           red_component_count=len(components), components=components))
    result = dict(run=str(args.run), roi=list(args.roi), projection_count=len(rows),
                  projection_streams=projection_metrics(rows), frames=frames,
                  interpretation='Colour components require visual confirmation. Projection passes and PNG frames are not implicitly aligned.')
    with args.out.open('x') as output:
        json.dump(result, output, indent=2, allow_nan=False)
        output.write('\n')
    print(f'{len(rows)} projections, {len(frames)} captured TV frames -> {args.out}')


if __name__ == '__main__':
    main()
