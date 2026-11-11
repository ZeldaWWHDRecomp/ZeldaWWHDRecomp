#!/bin/sh
# run.sh DECOMP_DIR PORT_DIR: gabi.h GABI_STACKARG test (stackarg_test.cpp) with the fast game-build defines
set -e
here=$(cd "$(dirname "$0")" && pwd); out=${TMPDIR:-/tmp}/wwhd-stackarg; mkdir -p "$out"
defs="-DGABI_GAME_BUILD=1 -DGABI_FAST=1 -DGABI_FAST_NE=1 -DGABI_SHARED_TLS=1 -DGABI_LEAN_ENTRY=1 -DGABI_SLIM_SCOPE=1 -DGABI_TP_CACHE=1 -DGABI_TP_ENTRY=1 -DGABI_NATIVE_ENTRIES=1 -DGABI_COLD_FALLBACK=1 -DGABI_PASS_TS=1 -DGABI_NOCHECK=1 -DGABI_STACKARG=1"
c++ -std=c++20 -O2 -w $defs -I"$1/tools/verify/include" -I"$2/runtime/include" "$here/stackarg_test.cpp" -o "$out/stackarg_test" \
  $( [ "$(uname)" = Darwin ] && echo -Wl,-undefined,dynamic_lookup || echo -Wl,--unresolved-symbols=ignore-all )
# (the guest-call paths gabi.h has but this test never takes need the port's runtime; their symbols stay unresolved)
"$out/stackarg_test"
