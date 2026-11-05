#!/usr/bin/env python3
"""Replay Sploosh shots from a local full state; never modifies the source state/save.

sploosh_scenario.py BINARY GAME SAVE_FILE STATE OUT --mode 30|60|120
  --shots '5:5,4:4,3:6,1:6,1:5,3:5,3:7,2:2,3:2,4:2,5:2'

The fixture must be an active board, cursor (0,0); supply cells from its own target
layout. Boot at 30 Hz, load after startup, then change mode on the load-relative
scenario clock. Outputs per-step board outcomes, marker matrix CSV, and board
crops around every shot. Requires Pillow for crops. Full states and
captures are local game data: do not commit them.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time


def plan(shots, reward_seconds=40):
    events = []
    fires = []
    step = 30
    x = y = 0
    for tx, ty in shots:
        while (x, y) != (tx, ty):
            bits = 0
            if x != tx:
                bits |= 0x400 if x < tx else 0x800
                x += 1 if x < tx else -1
            if y != ty:
                bits |= 0x200 if y < ty else 0x100
                y += 1 if y < ty else -1
            events.append((step, bits))
            step += 9
        step += 15
        events.append((step, 0x8000))
        fires.append(step)
        step += 45
    # Advance the victory/reward dialogue, choosing No at any replay prompt.
    # Down is pressed before A, so finishing a reward never buys another game.
    for at in range(step + 90, step + 90 + reward_seconds * 30, 60):
        events.append((at, 0x100))
        events.append((at + 9, 0x8000))
    return ','.join(f'{s/30:.6f}-{(s+3)/30:.6f}:{b:04X}' for s, b in events), fires, step + 210 + max(0, reward_seconds * 30 - 30)


def board_outcomes(path):
    data = path.read_bytes()
    pos = 0
    changes = []
    previous = None
    while pos + 28 <= len(data):
        magic, step, full, dt, actor, size = struct.unpack_from('<4sQIfII', data, pos)
        assert magic == b'ADMP'
        pos += 28
        a = data[pos:pos+size]
        if len(a) != size:
            break  # process may end between buffered writes
        pos += size
        state = dict(state=struct.unpack_from('>i', a, 0x6DC)[0], cursor=list(a[0x584:0x586]),
                     shots_left=a[0x615], shots_used=a[0x616], ships_left=a[0x614],
                     hits=list(i for i, c in enumerate(a[0x598:0x5D8]) if c == 3),
                     misses=list(i for i, c in enumerate(a[0x598:0x5D8]) if c == 1))
        if state != previous:
            changes.append(dict(step=step, full=full, **state))
            previous = state
    return changes


def check_trace(path):
    groups = {}
    for row in csv.reader(path.open()):
        if len(row) != 18:
            raise ValueError("incomplete marker matrix row")
        key = (int(row[0]), row[3], int(row[5]))
        groups.setdefault(key, []).append((float(row[1]), row[2], tuple(map(float, row[6:]))))
    checked = errors = reassigned = 0
    maximum = 0.0
    for rows in groups.values():
        exact = next((m for t, why, m in rows if why == 'exact'), None)
        if exact is None:
            continue
        for t, why, matrix in rows:
            if t >= 1:
                continue
            checked += 1
            reassigned += why == 'reassigned'
            delta = max(abs(a-b) for a, b in zip(matrix, exact))
            maximum = max(maximum, delta)
            errors += delta > 1e-5
    return dict(checked=checked, off_cell=errors, reassigned=reassigned, max_matrix_delta=maximum)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for arg in ('binary', 'game', 'save', 'state', 'out'):
        ap.add_argument(arg, type=Path)
    ap.add_argument('--mode', choices=('30', '60', '120'), required=True)
    ap.add_argument('--shots', required=True)
    ap.add_argument('--renderer', choices=('metal', 'vulkan'),
                    default='metal' if sys.platform == 'darwin' else 'vulkan')
    ap.add_argument('--expect-sharp', action='store_true')
    ap.add_argument('--reward-seconds', type=int, default=40)
    a = ap.parse_args()
    for name in ('binary', 'game', 'save', 'state', 'out'):
        setattr(a, name, getattr(a, name).resolve())
    a.out.mkdir(parents=True, exist_ok=False)
    (a.out/'save/user').mkdir(parents=True)
    (a.out/'states').mkdir()
    shutil.copy2(a.save, a.out/'save/user/cking.sav')
    # A clone/copy here is intentional: the source is never passed to a game writer.
    if sys.platform == 'darwin':
        subprocess.run(['cp', '-c', str(a.state), str(a.out/'states/slot3.bin')], check=True)
    else:
        shutil.copy2(a.state, a.out/'states/slot3.bin')
    shots = [tuple(map(int, s.split(':'))) for s in a.shots.split(',')]
    if not shots or not all(0 <= x < 8 and 0 <= y < 8 for x, y in shots):
        ap.error("shots must contain board coordinates between 0 and 7")
    if a.reward_seconds < 0:
        ap.error("reward-seconds must be nonnegative")
    presses, fires, end = plan(shots, a.reward_seconds)
    factor = 4 if a.mode == '120' else 2 if a.mode != '30' else 1
    captures = sorted({30 + s*factor + d for s in fires for d in range(-8, 17)})
    env = dict(os.environ, WWHD_HIDDEN_WINDOWS='1', WWHD_NO_HOST_INPUT='1', WWHD_NO_GAMEPAD='1',
               WWHD_NO_AUDIO='1', WWHD_RENDERER_RUNTIME=a.renderer, WWHD_DISPLAY_HZ='0',
               WWHD_INTERP='0', WWHD_INTERP_FPS='120' if a.mode == '120' else '60',
               WWHD_INTERP_PACED='0', WWHD_STATE_DIR='states', WWHD_SHADER_CACHE='../shaders.bin',
               WWHD_VK_SHADER_CACHE='../vulkan-cache',
               WWHD_PRESS=','.join(f'{f}-{f+8}:8000' for f in range(120, 721, 60)),
               WWHD_STATE_LOAD_AT='1000:3', WWHD_TEST_ORIGIN_LOAD='30',
               WWHD_TEST_MODE=('0' if a.mode == '30' else '1')+'@0',
               WWHD_TEST_PRESS=presses, WWHD_TEST_END=str(end/30),
               WWHD_DUMP_LOAD_FRAMES=','.join(map(str, captures)), WWHD_SAVEINFO_DUMP='saveinfo.bin',
               WWHD_ACTOR_DUMP='board.bin:021C5634:6E8', WWHD_SE_TRACE='sounds.txt', WWHD_INTERP_MODEL_TRACE='1000000',
               WWHD_INTERP_MODEL_TRACE_JOINTS='1', WWHD_INTERP_MODEL_TRACE_ALL='1',
               WWHD_INTERP_MODEL_TRACE_SPLOOSH='1', WWHD_INTERP_MODEL_TRACE_FILE='markers.csv')
    # Optional fixture metadata gives the existing read-only memory watch the
    # counter address in this state. Heap addresses are fixture-specific.
    metadata = a.state.with_suffix('.fixture.json')
    fixture = json.loads(metadata.read_text()) if metadata.exists() else {}
    if 'counter_address' in fixture:
        env.update(WWHD_RNG_TRACE='rng.txt', WWHD_MEM_WATCH=f"{int(fixture['counter_address']):X}:8")
    with (a.out/'log').open('w') as log:
        p = subprocess.Popen([str(a.binary), '--game', str(a.game), '--save', 'save'],
                             cwd=a.out, env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 300
        while p.poll() is None and time.monotonic() < deadline and not (a.out/'test_done').exists():
            time.sleep(1)
        if not (a.out/'test_done').exists():
            raise RuntimeError('scenario did not finish; inspect log')
    finally:
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(5)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
    outcomes = board_outcomes(a.out/'board.bin')
    trace = check_trace(a.out/'markers.csv') if (a.out/'markers.csv').exists() else {}
    counters = []
    if (a.out/'rng.txt').exists():
        for line in (a.out/'rng.txt').read_text().splitlines():
            fields = line.split()
            if len(fields) < 6:
                continue
            value = tuple(struct.unpack('>II', bytes.fromhex(fields[4])))
            if not counters or counters[-1]['value'] != value:
                counters.append(dict(step=int(fields[0]), value=value))
    result = dict(mode=a.mode, renderer=a.renderer, fires=fires, outcomes=outcomes,
                  trace=trace, counters=counters)
    (a.out/'results.json').write_text(json.dumps(result, indent=2))
    from PIL import Image
    for path in a.out.glob('load_frame_*.png'):
        if '_drc' not in path.stem and '_board' not in path.stem:
            with Image.open(path) as im:
                box = tuple(round(v * size) for v, size in zip((435/1280, 195/720, 835/1280, 585/720), (im.width, im.height, im.width, im.height)))
                im.crop(box).save(path.with_name(path.stem+'_board.png'))
    print(json.dumps(dict(mode=a.mode, trace=trace, final=outcomes[-1] if outcomes else None)), flush=True)
    if a.expect_sharp and any(not (a.out/f'load_frame_{frame}_board.png').exists() for frame in captures):
        raise SystemExit('capture regression: missing shot frames')
    if a.expect_sharp and a.mode != '30' and (not trace.get('checked') or trace['off_cell']):
        raise SystemExit('marker matrix regression: off-cell or missing in-between draws')


if __name__ == '__main__':
    main()
