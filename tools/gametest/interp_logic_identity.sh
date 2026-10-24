#!/bin/zsh
# interp_logic_identity.sh EXE GAME SAVE STATE OUT [VAR=value ...]
# Frame interpolation must only change what is drawn between logic steps: this check plays the same
# scripted route at 30 fps (interpolation off), 60 fps and 120 fps (unpaced, so every step gets all its
# in-between frames) and compares the game logic step by step with compare.py: Link, camera, cM_rnd
# seeds, sound starts, a hash of every process after each execute, the save-info block at the end. The
# 60 and 120 fps runs must be identical to the 30 fps run. With BASE=other-exe, the 30 fps run of that
# build is compared too (a change must not alter the logic of the build before it).
#
# SAVE: a save folder (copied), STATE: a save state file (copied into slot 1: a .wwstate portable state
# or a full state). Each run boots
# with interpolation off, loads the state at script frame LOAD (default 3400) with the fixed seed
# (WWHD_TEST_FIXED_SEED), and starts the scenario 30 logic steps after the load (WWHD_TEST_ORIGIN_LOAD);
# the 60/120 fps runs switch interpolation on at the scenario's start (WWHD_TEST_MODE=1@0). The route
# (ROUTE_STICK, ROUTE_RSTICK, ROUTE_PRESS: WWHD_TEST_STICK/RSTICK/PRESS syntax, scenario seconds) and
# its length END (seconds) have defaults: walking, turning the camera, sword swings, a roll, Z-targeting.
# FPS="30 60 120" chooses the runs (240 works too). Headless, muted, hidden windows, no host input, an
# own shader cache per OUT (SHADER_CACHE=file to share one; PREBUILD=file seeds each run's prebuilt shaders); the game is always stopped when a run ends. Run folders keep only the traces
# and the log. Exit status 0 when every comparison is identical.
set -u
zmodload zsh/datetime
exe=${1:A} game=${2:A} save=${3:A} state=${4:A} out=${5:A}; shift 5
here=${0:A:h}
end=${END:-24}
load=${LOAD:-3400}
stick=${ROUTE_STICK:-0.5-3.5:0:1,4-5.5:1:0.3,6-8:-0.7:0.7,12-14:0:-1,15-16.5:0.6:0.8,19-21:-1:0}
rstick=${ROUTE_RSTICK:-8.5-10:1:0,16.5-17.5:-1:0.4}
press=${ROUTE_PRESS:-2-2.15:4000,3-3.15:8000,5-5.15:4000,10.5-12:0080,11-11.15:4000,11.5-11.65:4000,13-13.15:8000,17.8-18:4000,18.5-19.5:4000,21.5-21.65:8000,22-22.15:4000}
mkdir -p $out
boot=$(python3 -c "print(','.join('%d-%d:8000' % (f, f + 8) for f in range(1200, $load - 200, 60)))")
if [[ $state == *.wwstate ]]; then slotfile=slot1.wwstate; else slotfile=slot1.bin; fi

run() {  # run EXE NAME FPS [VAR=value ...]
  local x=$1 name=$2 fps=$3; shift 3
  local d=$out/$name
  for attempt in $(seq 1 ${BOOT_TRIES:-3}); do
    rm -rf $d; mkdir -p $d/states $d/user
    cp -R $save $d/save
    cp -c $state $d/states/$slotfile 2>/dev/null || cp $state $d/states/$slotfile
    [ -n "${PREBUILD:-}" ] && { mkdir -p $d/user/shader-prebuild/live; cp -c $PREBUILD $d/user/shader-prebuild/live/shaders.bin 2>/dev/null || cp $PREBUILD $d/user/shader-prebuild/live/shaders.bin; }
    local mode=()
    [ $fps -gt 30 ] && mode=(WWHD_INTERP=0 WWHD_INTERP_FPS=$fps WWHD_INTERP_PACED=0 WWHD_DISPLAY_HZ=0 WWHD_TEST_MODE=1@0)
    [ $fps -le 30 ] && mode=(WWHD_INTERP=0)
    ( cd $d && env WWHD_HIDDEN=1 WWHD_HIDDEN_WINDOWS=1 WWHD_NO_HOST_INPUT=1 WWHD_NO_AUDIO=1 WWHD_NO_GAMEPAD=1 WWHD_LAST_SESSION=0 \
        WWHD_SETTINGS=$d/settings.ini WWHD_DISPLAY_SETTINGS=$d/display.plist WWHD_CONTROLS=$d/controls.json \
        WWHD_SHADER_CACHE=${SHADER_CACHE:-$out/shadercache.bin} WWHD_USER_DIR=$d/user WWHD_STATE_DIR=$d/states \
        WWHD_TEST_FIXED_SEED=1 WWHD_PRESS=$boot WWHD_STATE_LOAD_AT=$load:1 WWHD_TEST_ORIGIN=1 WWHD_TEST_ORIGIN_LOAD=30 WWHD_TEST_END=$end \
        WWHD_TEST_STICK=$stick WWHD_TEST_RSTICK=$rstick WWHD_TEST_PRESS=$press \
        WWHD_LINK_TRACE=trace.txt WWHD_CAM_TRACE=cam.txt WWHD_SAVEINFO_DUMP=si.bin WWHD_SE_TRACE=se.txt WWHD_RNG_TRACE=rng.txt \
        WWHD_PROC_DIGEST=digest.txt WWHD_PROC_DIGEST_SKIP=$skip WWHD_PROC_DIGEST_ANM_CACHE=$anm \
        $mode "$@" $x --game $game --save save > log 2>&1 ) &
    local pid=$!
    trap "kill -9 $pid 2>/dev/null" EXIT INT TERM HUP
    local limit=$(( (${BOOT_LIMIT:-240} + end * 4) * 10 ))
    for i in $(seq 1 $limit); do
      kill -0 $pid 2>/dev/null || break
      [ -f $d/test_done ] && break
      sleep 0.1
    done
    pkill -9 -P $pid 2>/dev/null; kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
    rm -rf $d/save $d/states $d/user
    grep -q '\[test\] origin' $d/log && [ -f $d/test_done ] && { echo "$name: done"; return 0; }
    cp $d/log $out/$name.bootfail$attempt.log
  done
  echo "$name: the scenario did not run to its end (logs: $out/$name.bootfail*.log)"
  return 1
}

# process hashes: run-to-run noise (as run_scenario.sh) and, with DRAW_SKIP=1 (the default), state that
# drawing writes into the processes: the actor's tevStr (+0x110, 0x1C8 bytes: lights, colours and their
# per-draw easing, written by settingTevStruct in the actor's draw), the skull flag's render packet
# (00AE +0x3AC, 0x1344 bytes up to its cloth arrays: draw-list links, GPU uniforms and transforms,
# double-buffered per draw, its position matrix from the camera). The 60/120 fps runs draw more often
# (and with the blended camera). Link's eye and mouth texture controllers (+0x66C, +0x6C4, +0x71C: the frames his
# draw sets from m3530/m3532 on every draw); the King of Red Lions' HD sea-shadow animation (00A5 +0x5BC,
# played by its shadow draw), its wake's particle track callback (00A5 +0xD0DC: positions its particle
# draw stores) and the same callback's points in the harbour boats (ikada 0044 +0x508: dPa_trackEcallBack
# mPos, written and read only by its draw); its sail's render packet (00AB +0x3BC, 0x2100 bytes); the water pillars' tevStr
# (01C1 +0x204); and every material animator's evaluation cache (the frame drawing last
# evaluated, WWHD_PROC_DIGEST_ANM_CACHE). The logic does not read these. DRAW_SKIP=0 hashes them too.
skip0=A8:64F0:98,6B:3D0:30,A8:6D9C:6,A8:713C:6,A8:71DC:6,1DC:508:8,A8:48C:168
skip=$skip0
[ "${DRAW_SKIP:-1}" = 1 ] && skip=$skip0,AE:3AC:1344,A8:66C:4:58:3,A5:5BC:3C,A5:D0DC:50,44:508:24,AB:3BC:2100,1C1:204:1C8,*:110:1C8
anm=0
[ "${DRAW_SKIP:-1}" = 1 ] && anm=1
bad=0
for f in ${=FPS:-30 60 120}; do run $exe new$f $f "$@" || bad=1; done
# (the base build may not know the * skip: both 30 fps runs of that comparison hash everything)
if [ -n "${BASE:-}" ]; then
  full=$skip0
  skip=$full anm=0 run ${BASE:A} base30 30 "$@" || bad=1
  skip=$full anm=0 run $exe new30full 30 "$@" || bad=1
fi
for f in ${=FPS:-30 60 120}; do
  [ $f = 30 ] && continue
  echo "== 30 fps vs $f fps"
  python3 $here/compare.py $out/new30 $out/new$f $end || bad=1
done
if [ -n "${BASE:-}" ]; then
  echo "== base build vs this build, 30 fps"
  python3 $here/compare.py $out/base30 $out/new30full $end || bad=1
fi
echo "logic identity: $([ $bad = 0 ] && echo PASS || echo FAIL)"
exit $bad
