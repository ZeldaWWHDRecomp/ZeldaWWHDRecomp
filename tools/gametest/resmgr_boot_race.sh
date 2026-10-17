#!/bin/zsh
# resmgr_boot_race.sh EXE GAME OUT [RUNS]
# Regression test for the ResMgr boot race (docs/decomp-notes.md, "ResMgr pack requests finish before the
# load task continues"; the signal 11 at guest address 00000014 in 0275D21C reported on PR #206).
# Boots EXE headless and muted with slow game file reads (the title scene's archives are then requested
# while the permanent packs load) and ResMgrThread paused at the reported crash point:
#   control: WWHD_RESMGR_NO_WAIT=1 (the old behaviour) must lose the race and crash (in 0275D21C, or the
#            title's bridge asserts when the load task went on even earlier), else the test no longer
#            reaches the race and proves nothing;
#   fixed:   the default must wait for ResMgrThread every time and never crash.
# A boot whose title archives were not requested while the packs loaded (the machine read them fast
# anyway) proves nothing either way and is repeated, up to 3 times. RUNS boots each (default 2). Exit
# status 0 when both hold. Runs in OUT with its own user folder, save and shader cache; every game
# process is stopped when its boot ends.
set -u
exe=${1:A} game=${2:A} out=${3:A} runs=${4:-2}
mkdir -p $out/user $out/save
cd $out  # crash reports (captures/) land here
boot() {  # boot LOG [VAR=value ...]: up to 3 tries until a "from pack" request happened
  local log=$1 try; shift
  for try in 1 2 3; do
    boot_once $log "$@"
    grep -q "from the packs" $log && return
  done
}
boot_once() {
  local log=$1; shift
  env WWHD_USER_DIR=$out/user WWHD_HIDDEN_WINDOWS=1 WWHD_NO_AUDIO=1 WWHD_NO_GAMEPAD=1 WWHD_NO_HOST_INPUT=1 \
    WWHD_SHADER_CACHE=$out/shadercache.bin WWHD_EXIT_AT_FRAME=1500 WWHD_INTERP=1 WWHD_RESMGR_LOG=1 \
    WWHD_TEST_FS_MBPS=10 WWHD_TEST_RESMGR_MID_DELAY_MS=20 "$@" \
    $exe --game $game --save $out/save > $log 2>&1 &
  local pid=$!
  trap "kill -9 $pid 2>/dev/null" EXIT INT TERM HUP
  local j; for j in $(seq 1 1800); do kill -0 $pid 2>/dev/null || break; sleep 0.1; done
  kill -9 $pid 2>/dev/null
  wait $pid 2>/dev/null
  trap - EXIT INT TERM HUP
}
fail=0
for i in $(seq 1 $runs); do
  boot $out/control$i.log WWHD_RESMGR_NO_WAIT=1
  if grep -q "went on while" $out/control$i.log && grep -q "^CRASH" $out/control$i.log; then
    echo "control $i: race lost and crashed (expected): $(grep -A1 '^CRASH' $out/control$i.log | tail -1 | grep -o '\[game function [^]]*\]' || grep -m1 '^Source File' $out/control$i.log)"
  else
    echo "control $i: no crash: the test did not reach the race"; fail=1
  fi
  boot $out/fixed$i.log
  if grep -q "^CRASH" $out/fixed$i.log; then
    echo "fixed $i: CRASH"; grep -m3 -A2 "^CRASH" $out/fixed$i.log; fail=1
  elif ! grep -q "load task waits" $out/fixed$i.log; then
    echo "fixed $i: no wait seen: the test did not reach the race"; fail=1
  else
    echo "fixed $i: waited for ResMgrThread, no crash"
  fi
done
exit $fail
