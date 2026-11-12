#!/bin/zsh
# walk_doors_check.sh EXE OUT [VAR=value ...]
# Quick doors' "Walk through" mode at a door out of a room (issue #529: the Tower of the Gods map and key rooms):
# one headless, muted run (run_scenario.sh) of the save copy WALK_DOORS_SAVE that loads the portable state
# WALK_DOORS_STATE (Link just inside a room, facing away from its door) at step 3420, then holds the stick back
# (WALK_DOORS_STICK, default 3-9 s) so that Link walks out through the door, and runs 20 s of game time.
# PASS (exit 0): the door opened and shut by itself (walk-through), no event or cutscene runs in the last 5 s, and
# Link got at least 300 units away from where he started. FAIL (exit 1) otherwise.
# Before the fix, the map room's door (state: Siren room 3 point 0) let the hall's statue pads start the gate and
# water-spout scenes with Link still in the room, and the second one never ended: Link stood frozen.
#
# Isolated: its own WWHD_USER_DIR (empty: default settings), a clone of WALK_DOORS_SHADER (optional), its own Vulkan
# caches and save copy, all in OUT.iso and OUT.run, removed at the end.
set -u
exe=${1:A} out=${2:A}; shift 2
here=${0:A:h}; root=${here:h:h}
for v in WALK_DOORS_SAVE WALK_DOORS_STATE; do [ -n "${(P)v:-}" ] || { echo "$v not set"; exit 2; }; done
[ -e $out ] && { echo "$out exists"; exit 1; }
iso=$out.iso
mkdir -p $iso/user $iso/state $out
cp ${WALK_DOORS_STATE:A} $iso/state/slot4.wwstate
shader=$iso/shader.bin
[ -n "${WALK_DOORS_SHADER:-}" ] && cp -c ${WALK_DOORS_SHADER:A} $shader
SHADER_CACHE=$shader VK_SHADER_CACHE=$iso/vk.bin VK_PIPELINE_CACHE_DIR=$iso/vkp DIGEST= BOOT_TRIES=1 BOOT_LIMIT=300 \
  $here/run_scenario.sh $exe ${WALK_DOORS_GAME:-$root/game} ${WALK_DOORS_SAVE:A} $out.run 20 \
  WWHD_USER_DIR=$iso/user WWHD_STATE_DIR=$iso/state WWHD_TEST_FIXED_SEED=1 WWHD_TEST_STEP_CLOCK=1 \
  WWHD_MOD_QUICK_DOORS=1 WWHD_MOD_DOOR_MODE=walk WWHD_WALK_DOORS_LOG=1 WWHD_PORTABLE_LOAD_AT=3420:4 \
  WWHD_TEST_STICK=${WALK_DOORS_STICK:-3-9:0:-1} WWHD_EVENT_TIMELINE=$out/events.txt "$@" > $out/runner.txt 2>&1
for f in log trace.txt perf.txt; do [ -e $out.run/$f ] && mv $out.run/$f $out/; done
rm -rf -- $out.run $iso
python3 - $out <<'EOF'
import sys, re, math
out = sys.argv[1]
log = open(out + '/log', errors='replace').read()
opened = re.findall(r'\[doors\] door1[02] (\w+) opening', log)
shut = re.findall(r'\[doors\] door1[02] (\w+) shut', log)
events = [l.split() for l in open(out + '/events.txt')]
tail = events[-150:]
busy = sum(1 for e in tail if e[1] != '0')
# Link's place just before the stick (the state is loaded by then: step 3485 = 2.8 s after the origin) and at the end
pos = [l.split() for l in open(out + '/trace.txt')]
pos = [(int(p[0]), float(p[6]), float(p[8])) for p in pos if len(p) > 8 and p[0].isdigit()]
before = next(((x, z) for s, x, z in pos if s >= 3485), (0.0, 0.0))
moved = math.hypot(pos[-1][1] - before[0], pos[-1][2] - before[1]) if pos else 0.0
ok = bool(opened) and bool(set(opened) & set(shut)) and busy == 0 and moved >= 300 and len(tail) == 150
print('%s: walk-through opened %s, shut %s; event steps in the last 5 s: %d of %d; Link moved %.0f -> %s' % (
    out, sorted(set(opened)), sorted(set(shut)), busy, len(tail), moved, 'PASS' if ok else 'FAIL'))
sys.exit(0 if ok else 1)
EOF
