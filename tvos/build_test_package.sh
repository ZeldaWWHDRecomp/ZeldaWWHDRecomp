#!/bin/bash
# Builds a macOS release folder that can also build the game for an Apple TV, iPhone, iPad or Apple Vision Pro, from
# this checkout: the Mac runtime and the tvOS / iOS / visionOS runtimes against placeholder guest code, packaged with --apple-build.
# Then run setup from the folder it prints (docs/apple-devices.md).
#
#   tvos/build_test_package.sh [--again] [--only tvos|ios|visionos] [out dir, default build/apple-test]
#
#   --again       build in an out dir used before: only what changed is rebuilt, and the release folder
#                 in it is replaced (for test rounds; a release to share starts from an empty out dir)
#   --only P      the Apple runtime for P only (the release then builds only that platform's app)
#
# Needs Xcode 26 with the tvOS, iOS and visionOS platforms, CMake, Ninja and Python 3.
set -eu
cd "$(dirname "$0")/.."
AGAIN=0
PLATFORMS=(tvos ios visionos)
while [ $# -gt 0 ]; do
  case "$1" in
    --again) AGAIN=1; shift ;;
    --only)
      case "${2:-}" in tvos|ios|visionos) PLATFORMS=("$2") ;; *) echo "--only takes tvos, ios or visionos" >&2; exit 2 ;; esac
      shift 2 ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) break ;;
  esac
done
OUT="${1:-build/apple-test}"
# the latest release's number, so the mod catalogue's version checks pass
LATEST="$(git tag --list 'v[0-9]*' --sort=-v:refname | head -1)"
VERSION="${WWHD_VERSION:-${LATEST#v}-apple-test}"
if [ "$AGAIN" = 0 ] && [ -e "$OUT/dist" ]; then
  echo "$OUT/dist already exists: move it away, pass another folder, or add --again" >&2
  exit 1
fi
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
# the placeholder guest code, generated beside it and copied over only where it changed: a rewrite of
# unchanged files would rebuild everything that includes them
python3 tools/recomp/stubgen.py "$OUT/gen-stub.new"
mkdir -p "$OUT/gen-stub"
for f in "$OUT/gen-stub.new"/*; do
  cmp -s "$f" "$OUT/gen-stub/$(basename "$f")" || cp "$f" "$OUT/gen-stub/"
done
COMMON=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DWWHD_VERSION="$VERSION" -DWWHD_RENDERER=METAL
        -DGEN_DIR="$OUT/gen-stub" -DWWHD_BUNDLED_DEPS=ON -DWWHD_BUNDLED_ZSTD=ON)
# a test build whose renderer changes leave the shaders as they were can keep an earlier build's pipeline identity
# (its recorded shaders and models step stay usable): WWHD_PIPELINE_RUNTIME_ID_OVERRIDE=<that identity>
COMMON+=(-DWWHD_PIPELINE_RUNTIME_ID_OVERRIDE="${WWHD_PIPELINE_RUNTIME_ID_OVERRIDE:-}")
if command -v ccache > /dev/null; then
  COMMON+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi
cmake -S . -B "$OUT/mac" "${COMMON[@]}" -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DWWHD_SETUP_GUI=ON
cmake --build "$OUT/mac"
APPLE=()
for p in "${PLATFORMS[@]}"; do
  case "$p" in
    tvos) sdk=(-DCMAKE_SYSTEM_NAME=tvOS -DCMAKE_OSX_SYSROOT=appletvos -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0) ;;
    ios) sdk=(-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0) ;;
    visionos) sdk=(-DCMAKE_SYSTEM_NAME=visionOS -DCMAKE_OSX_SYSROOT=xros -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0) ;;
  esac
  cmake -S . -B "$OUT/$p" "${COMMON[@]}" "${sdk[@]}" -DCMAKE_OSX_ARCHITECTURES=arm64
  cmake --build "$OUT/$p" --target wwhd
  APPLE+=(--apple-build "$OUT/$p")
done
# (package.py replaces a release folder of the same name in dist)
python3 tools/release/package.py --build "$OUT/mac" --platform macos-arm64 --version "$VERSION" --out "$OUT/dist" \
  --toolchain xcode-clt --setup-gui "$OUT/mac/wwhd-setup" "${APPLE[@]}" --no-rando --no-zip
echo
echo "Release folder: $OUT/dist/WindWakerHD-$VERSION-macos-arm64"
