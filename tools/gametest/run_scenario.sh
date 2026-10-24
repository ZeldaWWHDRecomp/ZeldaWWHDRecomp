#!/bin/zsh
# run_scenario.sh EXE GAME SAVE OUT END [VAR=value ...]
# One scripted, headless run of the game for comparisons (compare.py): boots EXE with the game files in
# GAME and a copy of the save folder SAVE, goes from the title screen into the game (A every 2 s from TV
# frame 1200 to 3200), starts the scenario clock at TV frame ORIGIN (default 3400) and stops END seconds
# (game time) later. Traces go to OUT: Link (trace.txt), camera (cam.txt), random numbers (rng.txt), sound
# starts (se.txt), a hash of every process after each execute (digest.txt), the save-info block at the end
# (si.bin), the log (log), and perf.txt (process CPU time, wall time, frames).
# Extra VAR=value arguments go to the game's environment (e.g. WWHD_TEST_STICK=1-4:0:1 to walk, or the
# decomp build's WWHD_DECOMP=none). Muted, hidden windows, no host input, own shader cache in OUT/..; the
# game is always stopped when this script ends.
set -u
zmodload zsh/datetime
exe=${1:A} game=${2:A} save=${3:A} out=${4:A} end=$5; shift 5
origin=${ORIGIN:-3400}
press=$(python3 -c "print(','.join('%d-%d:8000' % (f, f + 8) for f in range(1200, 3201, 60)))")
# a boot that never reaches the scenario (an intermittent start-up hang) is retried: BOOT_TRIES (3)
for attempt in $(seq 1 ${BOOT_TRIES:-3}); do
  rm -rf $out; mkdir -p $out; cp -R $save $out/save
  cd $out
  # bytes the game never initialises (they differ from run to run): Link, camera, warp actor
  skip=A8:64F0:98,6B:3D0:30,A8:6D9C:6,A8:713C:6,A8:71DC:6,1DC:508:8,A8:48C:168
  # the player's live shader caches stay untouched: every cache (Metal and Vulkan) points next to the run
  env WWHD_HIDDEN_WINDOWS=1 WWHD_RENDERER_RUNTIME=metal WWHD_NO_HOST_INPUT=1 WWHD_NO_AUDIO=1 WWHD_NO_GAMEPAD=1 \
    WWHD_SHADER_CACHE=${SHADER_CACHE:-$out/../shadercache.bin} WWHD_STATE_DIR=$out/states \
    WWHD_VK_SHADER_CACHE=${VK_SHADER_CACHE:-$out/../vkshaders.bin} WWHD_VK_PIPELINE_CACHE_DIR=${VK_PIPELINE_CACHE_DIR:-$out/../vkpipelines} \
    WWHD_PRESS=${PRESS:-$press} WWHD_TEST_ORIGIN=$origin WWHD_TEST_END=$end \
    WWHD_LINK_TRACE=trace.txt WWHD_CAM_TRACE=cam.txt WWHD_SAVEINFO_DUMP=si.bin WWHD_SE_TRACE=se.txt WWHD_RNG_TRACE=rng.txt \
    WWHD_PROC_DIGEST=${DIGEST-digest.txt} WWHD_PROC_DIGEST_SKIP=$skip "$@" \
    $exe --game $game --save save > log 2>&1 &
  pid=$!
  trap 'kill -9 $pid 2>/dev/null' EXIT INT TERM HUP
  # CPU time of the game process (user+sys, seconds)
  cpu_now() { ps -o time= -p $pid 2>/dev/null | awk -F'[:.]' '{ if (NF==3) print $1*60+$2+$3/100; else print $1*3600+$2*60+$3+$4/100 }'; }
  t0=$EPOCHREALTIME cpu0= t1=
  limit=$(( (${BOOT_LIMIT:-150} + end * 3) * 10 ))
  for i in $(seq 1 $limit); do
    kill -0 $pid 2>/dev/null || break
    [ -f test_done ] && break
    # the scenario's CPU time and wall time: from its origin to its end
    if [ -z "$cpu0" ] && grep -q '\[test\] origin' log 2>/dev/null; then cpu0=$(cpu_now); t1=$EPOCHREALTIME; fi
    sleep 0.1
  done
  cpu=$(cpu_now) t2=$EPOCHREALTIME
  kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null
  # total: from the start of the process; scenario: from the origin to the end (END seconds of game time)
  printf "cpu_total_s %s cpu_scenario_s %s wall_total_s %.1f wall_scenario_s %s done %s\n" "${cpu:-"?"}" \
    "$([ -n "$cpu0" ] && echo "$cpu - $cpu0" | bc || echo "?")" "$((t2 - t0))" \
    "$([ -n "$t1" ] && printf %.2f $((t2 - t1)) || echo "?")" "$([ -f test_done ] && echo y || echo n)" > perf.txt
  grep -q '\[test\] origin' $out/log && break
  cp $out/log $out/../$(basename $out).bootfail$attempt.log
done
rm -rf save states
echo "$(basename $out): done=$([ -f test_done ] && echo y || echo n) $(grep -m1 '\[decomp\] decomp build' log)"
grep -iE "halt|crash|assert|fatal" log | head -3
