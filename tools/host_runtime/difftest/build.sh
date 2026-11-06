#!/bin/sh
# Builds the host-runtime difference tests into OUT (default build/difftest):
#   tools/host_runtime/difftest/build.sh GAME_RPX [GEN_DIR] [OUT]
# GEN_DIR is recomp.py output (default build/gen); OUT gets the RPX image (game data: keep it in build/).
#   OUT/mathsurvey OUT/image.bin FUNC|all [threads] [count]   bit-exactness survey of the math library
#   OUT/abisurvey  OUT/image.bin FUNC|all [threads] [count]   compiler helpers (64-bit, conversions)
#   OUT/regtest    OUT/image.bin ROOT|all [count]             register-only replacements vs originals (0: every input)
#   OUT/calltest   OUT/image.bin ROOT|all [count]             replacements that write memory / call game code
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
RPX=$1
GEN=${2:-$ROOT/build/gen}
OUT=${3:-$ROOT/build/difftest}
D=$ROOT/tools/host_runtime/difftest
mkdir -p "$OUT"
python3 "$ROOT/tools/verify/mkimage.py" "$RPX" "$OUT/image.bin" > /dev/null
GCC="-O3 -ffp-contract=off -fno-strict-aliasing -fwrapv -w -I$ROOT/runtime/include"
CXX="-std=c++20 -O2 -ffp-contract=off -I$ROOT/runtime/include -I$ROOT/runtime/src/hostrt -I$D"
clang -c $GCC "$ROOT/runtime/src/espresso_fp.c" -o "$OUT/espresso_fp.o"
clang -c $GCC "$ROOT/runtime/src/guest_addr_identity.c" -o "$OUT/guest_addr_identity.o"
MATH="028F4384 028F37F0 028F43F8 028F4BE0 028F423C 028F4114 028F41B0 028F3F90 028F4DC4 028F4F20 028F4FAC 028F4034 028F4560 028F4D28 028F38F8 028F3C78 028F3CA0 028F3EC0 028F3B98 028F3AB8"
ABI="028F5680 028F5678 028F543C 028F597C 028F5A1C 028F5B8C"
REG="0286CD54 028F35A8 0286CBC4 0286D5F0 028F4114 028F41B0 028F3B98 028F543C 028F597C 028F5680 028F5678"
CALL="028EA09C 028F0294 0286CD7C 0286C5F8 0286D620 0286D938 0286D9E0 0286CA50 0286CAA4 0286CAE8 0286D280 0286D2F4 0286D338 0286CB7C 0286C590 0286C9CC 0286D37C 028EA038 028F2EE8 028F3090 0286CA30 0286CA40 0286CB2C 0286CB3C 0286CB4C 0286CB5C 0286CB6C 0286CBB4 0286D270 0286D2D4 0286D2E4 0286D3C0 0286D3D0 0286D3E0 0286D3F0 0286D400 028F1748 028F17A8 028F186C 028F171C 028F18EC 028F5C88 0286DA70 0286DA9C 0286DACC 0286DB18 028EFFD0 028EFF8C 028F0020 028F0164 028F026C 028F521C 028F5914 028F040C"
for set in MATH ABI REG CALL; do
    eval addrs=\$$set
    python3 "$D/mkclosure.py" "$GEN" "$OUT/closure_$set.c" $addrs > /dev/null
    clang -c $GCC "$OUT/closure_$set.c" -o "$OUT/closure_$set.o"
done
HOSTRT="$ROOT/runtime/src/hostrt/hostrt_math.cpp $ROOT/runtime/src/hostrt/hostrt_abi.cpp $ROOT/runtime/src/hostrt/hostrt_cxx.cpp $ROOT/runtime/src/hostrt/hostrt_libc.cpp $ROOT/runtime/src/hostrt/hostrt_nn.cpp $ROOT/runtime/src/hostrt/hostrt_printf.cpp"
clang++ $CXX "$D/mathsurvey.cpp" "$D/rt.cpp" "$OUT/closure_MATH.o" "$OUT/espresso_fp.o" -o "$OUT/mathsurvey"
clang++ $CXX "$D/abisurvey.cpp" "$D/rt.cpp" "$ROOT/runtime/src/hostrt/hostrt_abi.cpp" "$OUT/closure_ABI.o" "$OUT/espresso_fp.o" -o "$OUT/abisurvey"
clang++ $CXX "$D/regtest.cpp" "$D/rt.cpp" $HOSTRT "$OUT/closure_REG.o" "$OUT/espresso_fp.o" "$OUT/guest_addr_identity.o" -o "$OUT/regtest"
clang++ $CXX "$D/calltest.cpp" "$D/rt.cpp" $HOSTRT "$OUT/closure_CALL.o" "$OUT/espresso_fp.o" "$OUT/guest_addr_identity.o" -o "$OUT/calltest"
echo "difftest tools in $OUT"
