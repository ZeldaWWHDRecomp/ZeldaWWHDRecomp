#!/bin/sh
# run.sh DECOMP_DIR PORT_DIR: gabi.h GABI_LEAF_BARE test (leaf_bare_test.cpp) with the fast game-build defines, plus the
# count of gabi_ts_pass calls in bare_body() (0 expected) and leaf_body() (1: the glue as before)
set -e
here=$(cd "$(dirname "$0")" && pwd); out=${TMPDIR:-/tmp}/wwhd-leaf-bare; mkdir -p "$out"
defs="-DGABI_GAME_BUILD=1 -DGABI_FAST=1 -DGABI_FAST_NE=1 -DGABI_SHARED_TLS=1 -DGABI_LEAN_ENTRY=1 -DGABI_SLIM_SCOPE=1 -DGABI_TP_CACHE=1 -DGABI_TP_ENTRY=1 -DGABI_NATIVE_ENTRIES=1 -DGABI_COLD_FALLBACK=1 -DGABI_PASS_TS=1 -DGABI_NOCHECK=1 -DGABI_LEAF=1 -DGABI_LEAF_BARE=1"
c++ -std=c++20 -O2 -w $defs -I"$1/tools/verify/include" -I"$2/runtime/include" "$here/leaf_bare_test.cpp" -o "$out/leaf_bare_test" \
  $( [ "$(uname)" = Darwin ] && echo -Wl,-undefined,dynamic_lookup || echo -Wl,--unresolved-symbols=ignore-all )
# (leaf_body's guest-call path needs the port's runtime; the test never runs it, so its symbols stay unresolved)
"$out/leaf_bare_test"
count() { objdump -d --no-show-raw-insn "$out/leaf_bare_test" | awk -v f="$1" '$0 ~ "^[0-9a-f]+ <_?"f">:"{p=1;next} /^[0-9a-f]+ <.*>:/{p=0} p' | grep -c "gabi_ts_pass" || true; }
b=$(count bare_body); l=$(count leaf_body)
echo "leaf_bare_test: gabi_ts_pass calls: bare_body $b (0 expected), leaf_body $l (1 expected)"
[ "$b" -eq 0 ] && [ "$l" -ge 1 ]
