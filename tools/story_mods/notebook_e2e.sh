#!/bin/zsh
# Headless end-to-end run of the story notebook (docs/mod-sdk-v2.md, "The story notebook"):
# the story-notebook package (from the mod repository) on -> its letter waits in the Rito postbox -> Link (warped in front of
# Windfall's postbox) takes it and the notebook -> the Delivery Bag shows the notebook -> a touch opens
# its page -> A opens the quest, A shows it on the chart, B, B close -> the MAP screen's sea chart.
#
# usage: STORY_NOTEBOOK_PKG=<mod repo>/story-notebook notebook_e2e.sh BUILD_DIR SAVE_DIR OUT_DIR
#   STORY_NOTEBOOK_PKG  the package's folder with its manifest.json and a built mod.elf
#   BUILD_DIR  a configured build (BUILD_DIR/wwhd), SAVE_DIR a COPY-able save with file 1 on Windfall's
#   pier (the A presses at TV frames 1200..3200 load file 1), OUT_DIR is created (log, frame dumps).
# SHADERS=file: a shader cache to use (default: a new one in OUT_DIR). Checks the log; frame dumps of the page (present) and the GamePad map are left in OUT_DIR.
# Machine rules: one game at a time, own PID only, no audio, hidden windows.
set -eu
B=$1 SAVE=$2 OUT=$3
REPO=${0:A:h:h:h}
MOD=${STORY_NOTEBOOK_PKG:?the story-notebook package folder from the mod repository}
[ -f $MOD/mod.elf ] || { echo "build $MOD/mod.elf first"; exit 2; }
rm -rf $OUT; mkdir -p $OUT/save $OUT/states $OUT/manager/Mods/story-notebook
cp -R $SAVE $OUT/save/user
cp -R $MOD/. $OUT/manager/Mods/story-notebook/  # the whole package: its icon and model files too
echo '{"active": "Default", "format_version": 1, "profiles": {"Default": {"enabled": {"story-notebook": true}}}}' > $OUT/manager/profiles.json
echo "{\"format_version\": 1, \"python\": [\"python3\"], \"compiler\": [\"clang\"], \"builder\": \"$REPO/tools/guestmod/build_guest_mod.py\", \"include\": \"$REPO/runtime/include\"}" > $OUT/guest-sdk.json
PRESS=$(python3 -c "
p=['%d-%d:8000'%(f,f+8) for f in range(1200,3200,60)]           # load file 1
p+=['%d-%d:8000'%(f,f+6) for f in range(3480,4100,40)]          # talk to the postbox, read, take the notebook
p+=['4480-4486:8000','4550-4556:8000','4620-4626:4000','4680-4686:4000']  # open quest, track, back, close
print(','.join(p))")
cd $OUT
env -i HOME="$HOME" PATH="/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin" \
  WWHD_NO_AUDIO=1 WWHD_NO_GAMEPAD=1 WWHD_NO_HOST_INPUT=1 WWHD_HIDDEN_WINDOWS=1 WWHD_CODE_MODS=1 \
  WWHD_MOD_MANAGER_DIR="$OUT/manager" WWHD_TEST_TRUST_NATIVE_MODS=story-notebook WWHD_GUEST_BUILD_CONFIG="$OUT/guest-sdk.json" \
  WWHD_SETTINGS="$OUT/settings.ini" WWHD_DISPLAY_SETTINGS="$OUT/display.plist" WWHD_CONTROLS="$OUT/controls.json" \
  WWHD_SHADER_CACHE="${SHADERS:-$OUT/shaders.bin}" XDG_CONFIG_HOME="$OUT/config" WWHD_STATE_DIR="$OUT/states" WWHD_SIM_SCREEN=1280x720 \
  WWHD_MAIL_TRACE=1 WWHD_CHART_TRACE=1 WWHD_NOTEBOOK_TRACE=1 WWHD_TEST_ORIGIN=1 WWHD_PRESS=$PRESS \
  WWHD_LINK_WARP=3390:873.4:89.3:-198620.4:18205 WWHD_TEST_DRC_MODE=4200:gamepad \
  WWHD_TEST_DRC_TOUCH=4300-4306:0.539:0.8125,4420-4424:0.667:0.427,4760-4764:0.375:0.06 \
  WWHD_DUMP_FRAMES=4450,4520,4590,4650,4900 WWHD_DUMP_PRESENT=1 \
  $B/wwhd --game $REPO/game --save $OUT/save > log 2>&1 &
pid=$!
for i in {1..900}; do kill -0 $pid 2>/dev/null || break; [ -f frame_4900_drc.png ] && break; sleep 1; done
sleep 3; kill $pid 2>/dev/null || true; sleep 1; kill -9 $pid 2>/dev/null || true
fail=0
for want in 'letter "lantern" state 2' 'the story notebook is in this save file' 'quest "lantern" started by its letter' \
            'the delivery bag shows the notebook' 'page opened' 'tracked: shown on the sea chart' 'page closed' \
            '\[chart\] menu [0-9A-F]*: pane [0-9] at'; do
  if grep -q "$want" log; then echo "ok   $want"; else echo "FAIL $want"; fail=1; fi
done
exit $fail
