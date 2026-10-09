#!/usr/bin/env bash
# Builds the native libraries of the Android app (arm64-v8a) with the NDK: the game as libmain.so,
# with SDL3, glslang, zlib and LZ4 built in the same CMake run from the pinned sources (URL + SHA-256)
# of cmake/WindowsDependencies.cmake (WWHD_BUNDLED_DEPS; SDL 3.4.18, the version of the app's SDL
# Java files), and copies libmain.so, libSDL3.so and libc++_shared.so to android/app/libs/arm64-v8a
# for Gradle (cd android && ./gradlew assembleRelease).
#
# usage: android/build_native.sh
# env:   ANDROID_SDK (default $ANDROID_HOME or $ANDROID_SDK_ROOT, else the SDK's usual place:
#        ~/Library/Android/sdk, ~/Android/Sdk, C:/Android/Sdk), NDK_VERSION (default 30.0.16248370),
#        JOBS (default 8), GEN_DIR (default build/gen), OUT (default android/build),
#        ABI (default arm64-v8a; x86_64 for emulator CI),
#        CPU (default generic: any arm64 phone. CPU=oryon-1 tunes for the Snapdragon 8 Elite, e.g.
#        Galaxy S25, where the port was measured; the APK then runs only on that CPU family)
# The game code in build/gen is generated from your own game (tools/recomp/recomp.py) and stays on
# your machine; the APK built from it contains it and must not be distributed. CI builds this with
# placeholder code (tools/recomp/stubgen.py) only to check that it compiles.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
case "$(uname -s)" in
Darwin) host=darwin-x86_64; exe=; default_sdk="$HOME/Library/Android/sdk" ;;
Linux) host=linux-x86_64; exe=; default_sdk="$HOME/Android/Sdk" ;;
MINGW* | MSYS* | CYGWIN*) host=windows-x86_64; exe=.exe; default_sdk="C:/Android/Sdk" ;;
*) echo "unsupported host $(uname -s)"; exit 2 ;;
esac
sdk="${ANDROID_SDK:-${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$default_sdk}}}"
ndk="$sdk/ndk/${NDK_VERSION:-30.0.16248370}"
[ -f "$ndk/build/cmake/android.toolchain.cmake" ] || { echo "no NDK at $ndk (sdkmanager --install \"ndk;${NDK_VERSION:-30.0.16248370}\")"; exit 1; }
jobs="${JOBS:-8}"
abi="${ABI:-arm64-v8a}"
case "$abi" in
arm64-v8a) target_triple=aarch64-linux-android ;;
x86_64) target_triple=x86_64-linux-android ;;
*) echo "unsupported Android ABI: $abi"; exit 2 ;;
esac
api=33  # Android 13: bionic backtrace() (crash logs); Vulkan 1.3 phones ship 13 or newer
# CMake and Ninja: from PATH, else the SDK's newest
cmake="$(command -v cmake || true)"
[ -n "$cmake" ] || cmake="$(ls -d "$sdk"/cmake/*/bin 2>/dev/null | tail -1)/cmake$exe"
ninja="$(command -v ninja || true)"
[ -n "$ninja" ] || ninja="$(dirname "$cmake")/ninja$exe"
out="${OUT:-$here/build}/game"
cpu="${CPU:-generic}"
flags=()
if [ "$cpu" != generic ]; then flags=("-DCMAKE_C_FLAGS=-mcpu=$cpu" "-DCMAKE_CXX_FLAGS=-mcpu=$cpu"); fi

echo "== game (libmain.so), NDK $(basename "$ndk"), CPU $cpu"
mkdir -p "$out"
"$cmake" -S "$root" -B "$out" -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" \
    "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake" "-DANDROID_ABI=$abi" \
    "-DANDROID_PLATFORM=android-$api" -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release \
    -DWWHD_BUNDLED_DEPS=ON "-DGEN_DIR=${GEN_DIR:-$root/build/gen}" ${flags[@]+"${flags[@]}"} > "$out.log"
"$cmake" --build "$out" -j "$jobs" --target wwhd >> "$out.log" 2>&1 || { tail -40 "$out.log"; exit 1; }
libs="$here/app/libs/$abi"
mkdir -p "$libs"
bin="$ndk/toolchains/llvm/prebuilt/$host"
"$bin/bin/llvm-strip$exe" --strip-unneeded "$out/libmain.so" -o "$libs/libmain.so"  # 400 MB -> 52 MB
cp "$(find "$out/_deps" -name libSDL3.so -print -quit)" "$libs/"
cp "$bin/sysroot/usr/lib/$target_triple/libc++_shared.so" "$libs/"
# libadrenotools loads these hooks by soname from ApplicationInfo.nativeLibraryDir.
for hook in main_hook hook_impl file_redirect_hook gsl_alloc_hook; do
    hook_file="$(find "$out/_deps" -name "lib${hook}.so" -print -quit)"
    [ -n "$hook_file" ] || { echo "missing AdrenoTools hook: $hook"; exit 1; }
    cp "$hook_file" "$libs/"
done
ls -la "$libs"
