#!/bin/zsh
# run_latency.sh EXE OUT FPS [VAR=value ...]
# One scripted, headless latency run (README.md): boots EXE like cliff_check.sh (portable state slot CLIFF_SLOT (4) of
# CLIFF_STATE loaded at logic step 3400, fixed seed, step clock; copies of CLIFF_SHADER, the state folder and the
# user settings in OUT.iso), switches frame interpolation on at FPS (60, 120; 30 = off) 100 steps after the load, and
# presses BUTTONS (hex, default 4000 = B) with the left stick up (STICK=1, default: at the cliff Link reacts to the
# stick (he starts to walk), not to B) PRESSES times (default 40) every PERIOD ms (default 1500) from
# START_STEPS (default 300) logic steps after the load. Vulkan renderer. Extra VAR=value go to the game (e.g.
# WWHD_DRAWOFF=2). Result: OUT/latency.txt (one line per press) and the "[latency]" lines in OUT/log.
set -u
exe=${1:A} out=${2:A} fps=$3; shift 3
here=${0:A:h}; root=${here:h:h:h}
iso=$out.iso
slot=${CLIFF_SLOT:-4}
presses=${PRESSES:-40} period=${PERIOD:-1500} buttons=${BUTTONS:-4000} start=${START_STEPS:-300}
[ -e $out ] && { echo "$out exists"; exit 1; }
for v in CLIFF_STATE CLIFF_SHADER; do [ -n "${(P)v:-}" ] || { echo "$v not set"; exit 2; }; done
case $fps in 30) interp=(WWHD_INTERP=0) ;; 60|120|240) interp=(WWHD_INTERP=0 WWHD_INTERP_FPS=$fps WWHD_INTERP_AT_STEP=3500) ;;
  *) echo "FPS: 30, 60, 120 or 240"; exit 2 ;; esac
rm -rf $iso; mkdir -p $iso/user
cp $CLIFF_SHADER $iso/shader.bin && cp -R $CLIFF_STATE $iso/state || exit 2
# vanilla settings: an empty user folder (never the player's own mods and settings)
# game time to the end: the load (3400 steps from boot are before the origin), the start, the presses, a margin
end=$(( (start + 30) / 30 + presses * period / 1000 + 6 ))
cd $root
DIGEST= ORIGIN=3460 SHADER_CACHE=$iso/shader.bin VK_SHADER_CACHE=$iso/vk.bin VK_PIPELINE_CACHE_DIR=$iso/vkp \
  timeout $(( 300 + end * 4 )) tools/gametest/run_scenario.sh $exe ${CLIFF_GAME:-game} ${CLIFF_SAVE:-../gametest/saves/savage} $out $end \
  WWHD_USER_DIR=$iso/user WWHD_STATE_DIR=$iso/state WWHD_STATE_LOAD_AT=3400:$slot WWHD_TEST_FIXED_SEED=1 WWHD_TEST_STEP_CLOCK=1 \
  WWHD_RENDERER_RUNTIME=vulkan $interp \
  WWHD_LATENCY_TEST=$buttons:$period:80:$presses WWHD_LATENCY_START_AT=$((3400 + start)) WWHD_LATENCY_LOG=$out/latency.txt WWHD_LATENCY_STICK=${STICK:-1} \
  "$@" > /dev/null 2>&1
rm -rf $iso
python3 -I $here/summarize.py $out
