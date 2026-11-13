#!/bin/bash
# Builds a macOS release folder that can also build the game for an Apple TV, iPhone or iPad, from this checkout:
# the Mac runtime and the tvOS / iOS runtimes against placeholder guest code, packaged with --apple-build.
# Then run setup from the folder it prints (docs/apple-devices.md).
#
#   tvos/build_test_package.sh [out dir, default build/apple-test]
#
# Needs Xcode 26 with the tvOS and iOS platforms, CMake, Ninja and Python 3.
set -eu
cd "$(dirname "$0")/.."
OUT="${1:-build/apple-test}"
# the latest release's number, so the mod catalogue's version checks pass
LATEST="$(git tag --list 'v[0-9]*' --sort=-v:refname | head -1)"
VERSION="${WWHD_VERSION:-${LATEST#v}-apple-test}"
if [ -e "$OUT/dist" ]; then
  echo "$OUT/dist already exists: move it away or pass another folder" >&2
  exit 1
fi
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
python3 tools/recomp/stubgen.py "$OUT/gen-stub"
COMMON=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DWWHD_VERSION="$VERSION" -DWWHD_RENDERER=METAL
        -DGEN_DIR="$OUT/gen-stub" -DWWHD_BUNDLED_DEPS=ON -DWWHD_BUNDLED_ZSTD=ON)
if command -v ccache > /dev/null; then
  COMMON+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi
cmake -S . -B "$OUT/mac" "${COMMON[@]}" -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DWWHD_SETUP_GUI=ON
cmake --build "$OUT/mac"
cmake -S . -B "$OUT/tvos" "${COMMON[@]}" -DCMAKE_SYSTEM_NAME=tvOS -DCMAKE_OSX_SYSROOT=appletvos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0
cmake --build "$OUT/tvos" --target wwhd
cmake -S . -B "$OUT/ios" "${COMMON[@]}" -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0
cmake --build "$OUT/ios" --target wwhd
python3 tools/release/package.py --build "$OUT/mac" --platform macos-arm64 --version "$VERSION" --out "$OUT/dist" \
  --toolchain xcode-clt --setup-gui "$OUT/mac/wwhd-setup" --apple-build "$OUT/tvos" --apple-build "$OUT/ios" \
  --no-rando --no-zip
echo
echo "Release folder: $OUT/dist/WindWakerHD-$VERSION-macos-arm64"
