#!/bin/sh
# run.sh DECOMP_DIR PORT_DIR [ROUNDS]: gabi.h GABI_PASS_TS test (pass_ts_test.cpp) with the fast game-build defines, plus
# the count of gabi_ts_pass calls in body() (one expected: the compiler keeps the first answer for the other uses)
set -e
here=$(cd "$(dirname "$0")" && pwd); out=${TMPDIR:-/tmp}/wwhd-pass-ts; mkdir -p "$out"
defs="-DGABI_GAME_BUILD=1 -DGABI_FAST=1 -DGABI_FAST_NE=1 -DGABI_SHARED_TLS=1 -DGABI_LEAN_ENTRY=1 -DGABI_SLIM_SCOPE=1 -DGABI_TP_CACHE=1 -DGABI_TP_ENTRY=1 -DGABI_NATIVE_ENTRIES=1 -DGABI_COLD_FALLBACK=1 -DGABI_PASS_TS=1"
c++ -std=c++20 -O2 -w $defs -I"$1/tools/verify/include" -I"$2/runtime/include" "$here/pass_ts_test.cpp" -o "$out/pass_ts_test"
"$out/pass_ts_test" ${3:-300}
n=$(objdump -d --no-show-raw-insn "$out/pass_ts_test" | awk '/^[0-9a-f]+ <_?body>:/{f=1;next} /^[0-9a-f]+ <.*>:/{f=0} f' | grep -c "gabi_ts_pass" || true)
echo "pass_ts_test: body() calls gabi_ts_pass $n time(s) for 8 uses of the block"
[ "$n" -le 1 ]
