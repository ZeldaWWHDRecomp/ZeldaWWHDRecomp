#!/bin/zsh
# cliff_check.sh EXE OUT [VAR=value ...]
# Link's trace at the cliff still scene against two runs of the normal build: one scripted, headless run
# (run_scenario.sh, 16 s of game time) that loads the portable state slot CLIFF_SLOT (4) from CLIFF_STATE and
# keeps the last 3000 trace lines up to the scenario's end step. PASS: the run agrees with one of CLIFF_REFS (two run folders, space-separated)
# on >= 2990 of the last 3000 lines (best alignment within +-15).
#
# Fully isolated, so that two checks side by side cannot see each other: each run gets its own copy of the shader
# cache (CLIFF_SHADER), of the state folder, its own WWHD_USER_DIR (a copy of the default config folder's
# controls.json, display.plist and ModManager; the folder itself is only read), its own Vulkan caches and its own
# save copy. The copies live in OUT.iso and are removed at the end.
#
# The whole script runs on the game's clock: WWHD_TEST_STEP_CLOCK=1 counts the title-screen presses, the load at
# 3400 and the origin in logic steps, not TV frames (CLIFF_CLOCK=step, default). On TV frames (CLIFF_CLOCK=tv, the
# old timing; binaries before the step clock), a build that misses steps under host load gets the presses and the
# load early in game time and then plays on from a different state: a fake divergence (2026-11-06: every one of 18
# decomp runs whose load started at mTimer 3112/3111 failed; the normal build too under a heavier load). Only the
# load on the step clock is not enough: the presses would still land early (seen: mTimer 3120 at logic step 3400).
# Whatever the clock, the run is refused (verdict BADSTART, exit 3) unless the log's "portable state: loading the
# new stage" line shows the start every normal-build run has: mTimer CLIFF_EXPECT_TIMER (3113) and the old stage's
# cM_rnd seeds CLIFF_EXPECT_WERE ("4292 10047 23570": a fingerprint of everything before the load).
set -u
exe=${1:A} out=${2:A}; shift 2
here=${0:A:h}; root=${here:h:h}
iso=$out.iso
slot=${CLIFF_SLOT:-4} expect=${CLIFF_EXPECT_TIMER:-3113} were=${CLIFF_EXPECT_WERE:-4292 10047 23570}
case ${CLIFF_CLOCK:-step} in step) clock=1 ;; tv) clock=0 ;; *) echo "CLIFF_CLOCK: step or tv"; exit 2 ;; esac
[ -e $out ] && { echo "$out exists"; exit 1; }
for v in CLIFF_STATE CLIFF_SHADER CLIFF_REFS; do [ -n "${(P)v:-}" ] || { echo "$v not set"; exit 2; }; done
rm -rf $iso; mkdir -p $iso/user
cp $CLIFF_SHADER $iso/shader.bin && cp -R $CLIFF_STATE $iso/state || exit 2
U="$HOME/Library/Application Support/WWHD"
for f in controls.json display.plist ModManager; do [ -e "$U/$f" ] && cp -R "$U/$f" $iso/user/; done
cd $root
DIGEST= ORIGIN=3460 SHADER_CACHE=$iso/shader.bin VK_SHADER_CACHE=$iso/vk.bin VK_PIPELINE_CACHE_DIR=$iso/vkp \
  timeout 900 tools/gametest/run_scenario.sh $exe ${CLIFF_GAME:-game} ${CLIFF_SAVE:-../gametest/saves/savage} $out 16 \
  WWHD_USER_DIR=$iso/user WWHD_STATE_DIR=$iso/state WWHD_STATE_LOAD_AT=3400:$slot WWHD_TEST_FIXED_SEED=1 WWHD_TEST_STEP_CLOCK=$clock \
  WWHD_DUMP_FRAMES=999999 WWHD_INTERP=0 WWHD_INTERP_AT_STEP=3460 "$@" > /dev/null 2>&1
rm -rf $iso
[ -f $out/log ] && head -c 300000 $out/log > $out/log.cap && mv $out/log.cap $out/log
python3 -I - $out $expect "$were" ${=CLIFF_REFS} <<'PY'
import re, sys
out, expect, were, refs = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4:]
# the trace up to the scenario's end step (origin + 16 s * 30): the harness stops the game when it sees test_done, and
# how many steps it still runs before that (up to ~35 under host load) is not the game's doing
def load(r):
    o = re.search(r"\[test\] origin at .*?logic step (\d+)", open(r + "/log", errors="replace").read())
    end = int(o.group(1)) + 16 * 30 if o else 1 << 62
    rows = [l.split() for l in open(r + "/trace.txt")]
    return [x[3:] for x in rows if len(x) > 3 and x[1].isdigit() and int(x[1]) <= end][-3000:]
log = open(out + "/log", errors="replace").read()
m = re.search(r"portable state: loading the new stage: cM_rnd seeds [\d ]+\(were ([\d ]+)\).*?mTimer (\d+)", log)
start, w = (int(m.group(2)), m.group(1).strip()) if m else (None, None)
try:
    b = load(out)
except OSError:
    print("cliff: no trace -> FAIL (load start mTimer %s)" % start); sys.exit(1)
best = 0
for ref in refs:
    a = load(ref)
    for off in range(-15, 16):
        best = max(best, sum(1 for i in range(len(a)) if 0 <= i + off < len(b) and a[i] == b[i + off]))
if start != expect or w != were:
    print("cliff trace: %d of 3000 lines as the normal build -> BADSTART (the load started at mTimer %s, old-stage seeds "
          "%s; expected %d, %s: not a valid comparison)" % (best, start, w, expect, were)); sys.exit(3)
print("cliff trace: %d of 3000 lines as the normal build -> %s (load start mTimer %d)" % (best, "PASS" if best >= 2990 else "FAIL", start))
sys.exit(0 if best >= 2990 else 1)
PY
