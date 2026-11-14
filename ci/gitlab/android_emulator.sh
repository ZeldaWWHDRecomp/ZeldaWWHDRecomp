#!/bin/bash
# GitLab stand-in for reactivecircus/android-emulator-runner (called by ci/gitlab/gh_steps.py): boots an
# x86_64 emulator on a self-hosted Linux runner with KVM, runs the step's script, stops the emulator.
# GitLab's hosted Linux runners offer no KVM, so these steps run only with WWHD_ANDROID_EMULATOR=1
# on a runner tagged wwhd-linux-kvm.
#
#   android_emulator.sh API ARCH TARGET AVD_NAME DISK_SIZE EMULATOR_OPTIONS SCRIPT
set -euo pipefail
api=$1 arch=$2 target=$3 avd=${4:-test} disk=$5 options=$6 script=$7
[ -w /dev/kvm ] || { echo "no usable /dev/kvm on this runner"; exit 1; }
sdk="$ANDROID_HOME/cmdline-tools/latest/bin"
image="system-images;android-$api;$target;$arch"
"$sdk/sdkmanager" --install "$image" emulator platform-tools > /dev/null
export PATH="$ANDROID_HOME/emulator:$ANDROID_HOME/platform-tools:$PATH"
echo no | "$sdk/avdmanager" create avd --force -n "$avd" -k "$image" > /dev/null
if [ -n "$disk" ]; then
  echo "disk.dataPartition.size=$disk" >> "$HOME/.android/avd/$avd.avd/config.ini"
fi
# shellcheck disable=SC2086  # the options are a word list, as in the GitHub action
emulator -avd "$avd" -port 5554 $options > "$RUNNER_TEMP/emulator-$avd.log" 2>&1 &
emu=$!
stop() {
  adb -s emulator-5554 emu kill > /dev/null 2>&1 || true
  for _ in $(seq 60); do kill -0 "$emu" 2> /dev/null || return 0; sleep 1; done
  kill "$emu" 2> /dev/null || true
}
trap stop EXIT
timeout 600 adb -s emulator-5554 wait-for-device
for _ in $(seq 300); do
  [ "$(adb -s emulator-5554 shell getprop sys.boot_completed 2> /dev/null | tr -d '\r')" = 1 ] && break
  sleep 2
done
[ "$(adb -s emulator-5554 shell getprop sys.boot_completed | tr -d '\r')" = 1 ] || { tail -50 "$RUNNER_TEMP/emulator-$avd.log"; exit 1; }
# disable-animations: true
for s in window_animation_scale transition_animation_scale animator_duration_scale; do
  adb -s emulator-5554 shell settings put global "$s" 0
done
bash -eo pipefail -c "$script"
