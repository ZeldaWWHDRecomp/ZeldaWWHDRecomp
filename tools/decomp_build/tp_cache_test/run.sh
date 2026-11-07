#!/bin/sh
# run.sh DECOMP_DIR [ROUNDS] [android]: the thread-pointer table's churn test (gabi_tp_cache.h, WWHD_DECOMP_TP_CACHE).
# Many short threads (a quarter detached; thread pointers reused), SIGUSR1 during their start and exit, and a fork
# child; every table hit is checked against the thread's real thread-local block. Exit 1 on any stale hit.
# With "android": builds for arm64 (NDK in ANDROID_NDK) and prints the adb commands to run it in /data/local/tmp.
set -e
here=$(cd "$(dirname "$0")" && pwd); inc="$1/tools/verify/include"; rounds=${2:-1000}
out=${TMPDIR:-/tmp}/wwhd-tpc; mkdir -p "$out"
if [ "$3" = android ]; then
  cxx="$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android33-clang++"
  "$cxx" -std=c++20 -O2 -fPIC -shared -w -static-libstdc++ -I"$inc" "$here/tpc_lib.cpp" -o "$out/libtpc.so"
  "$cxx" -std=c++20 -O2 -static-libstdc++ "$here/tpc_main.cpp" -o "$out/tpc"
  echo "adb push $out/tpc $out/libtpc.so /data/local/tmp/wwhd-tpc/ && adb shell 'cd /data/local/tmp/wwhd-tpc && ./tpc ./libtpc.so $rounds'"
  exit 0
fi
c++ -std=c++20 -O2 -fPIC -shared -w -I"$inc" "$here/tpc_lib.cpp" -o "$out/libtpc.so"
c++ -std=c++20 -O2 "$here/tpc_main.cpp" -o "$out/tpc"
"$out/tpc" "$out/libtpc.so" "$rounds"
