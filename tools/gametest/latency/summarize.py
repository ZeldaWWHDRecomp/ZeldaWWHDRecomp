#!/usr/bin/env python3
# summarize.py RUN... : press -> present latency of latency runs (run_latency.sh / the phone's latency.txt), per run
# and pooled: count, median, p90, min, max (ms), the medians of the parts, misses. --json for one object per run.
import json, statistics, sys

def pct(v, q):
    v = sorted(v)
    return v[min(len(v) - 1, int(q * (len(v) - 1) + 0.5))] if v else float('nan')

def load(path):
    rows, miss, done = [], 0, False
    for line in open(path, errors='replace'):
        x = line.split()
        if not x: continue
        if x[0] == 'press' and len(x) >= 12:
            press, read, react, swap_ns, present = (int(x[i]) for i in (2, 3, 5, 8, 9))
            rows.append({'lat': (present - press) / 1e6, 'read': (read - press) / 1e6, 'react': (react - read) / 1e6,
                         'swap': (swap_ns - react) / 1e6, 'present': (present - swap_ns) / 1e6,
                         'steps': int(x[6]) - int(x[4])})
        elif x[0] == 'miss': miss += 1
        elif x[0] == 'done': done = True
    return rows, miss, done

def line(name, rows, miss, done):
    lat = [r['lat'] for r in rows]
    med = lambda k: statistics.median(r[k] for r in rows) if rows else float('nan')
    return ('%s: n=%d miss=%d%s press->present median %.2f p90 %.2f min %.2f max %.2f ms | medians: press->read %.2f '
            'read->react %.2f react->swap %.2f swap->present %.2f' % (name, len(rows), miss, '' if done else ' (not done)',
            pct(lat, .5), pct(lat, .9), min(lat, default=float('nan')), max(lat, default=float('nan')),
            med('read'), med('react'), med('swap'), med('present')))

args = [a for a in sys.argv[1:] if not a.startswith('--')]
pool = []
for a in args:
    path = a if a.endswith('.txt') else a + '/latency.txt'
    try: rows, miss, done = load(path)
    except OSError: print('%s: no latency.txt' % a); continue
    pool += rows
    if '--json' in sys.argv:
        lat = [r['lat'] for r in rows]
        print(json.dumps({'run': a, 'n': len(rows), 'miss': miss, 'done': done, 'median': pct(lat, .5), 'p90': pct(lat, .9)}))
    else: print(line(a, rows, miss, done))
if len(args) > 1 and '--json' not in sys.argv: print(line('pooled', pool, 0, True))
