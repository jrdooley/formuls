#!/usr/bin/env bash
# Android build script for the formuls JUCE app (experimental).
#
# Produces formuls-<VERSION>-android-<config>.apk in the repository root.
# Runs on a macOS or Linux host; see docs/android/README.md for the full
# picture of how the Android build differs from the desktop ones.
#
# Usage, from the repository root:
#   ./build-android.sh            # debug APK
#   ./build-android.sh release    # release APK (signed with the debug key)
#
# Requirements:
#   - JUCE with Projucer at ~/JUCE (or set PROJUCER)
#   - Android SDK (set ANDROID_HOME) with platform 35, build-tools and
#     NDK 28.1.13356709 -- the NDK version JUCE's Android exporter pins
#   - a JDK 17 (JAVA_HOME, or java on the PATH)
#   - faust, cmake >= 3.25, curl, unzip, python3
#   - the git submodules (git clone --recurse-submodules ...)
#
# Optional environment:
#   FORMULS_ANDROID_ABIS   ABIs to build (default "arm64-v8a x86_64";
#                          arm64-v8a alone is enough for real tablets,
#                          x86_64 is for the emulator)
#   ANDROID_NDK_HOME       NDK location (default $ANDROID_HOME/ndk/<version>)

set -euo pipefail

VERSION="0.3.0"
CONFIG="${1:-debug}"
ROOT="$(cd "$(dirname "$0")" && pwd)"

OSC_VERSION="1.31.0"
NODEJS_MOBILE_VERSION="18.20.4"
NDK_VERSION="28.1.13356709"     # must match JUCE's generated app/build.gradle
ANDROID_API="28"                # must match androidMinimumSDK in formuls.jucer (Android 9;
                                # libpd needs glob(), which bionic has from API 28)
ABIS="${FORMULS_ANDROID_ABIS:-arm64-v8a x86_64}"

case "$CONFIG" in
    debug)   GRADLE_TASK="assembleDebug" ;;
    release) GRADLE_TASK="assembleRelease" ;;
    *)       echo "usage: $0 [debug|release]"; exit 1 ;;
esac

if [[ "$(uname)" == "Darwin" ]]; then
    PROJUCER="${PROJUCER:-$HOME/JUCE/Projucer.app/Contents/MacOS/Projucer}"
else
    PROJUCER="${PROJUCER:-$HOME/JUCE/Projucer}"
fi

fail() { echo "error: $*" >&2; exit 1; }

#------------------------------------------------------------------ checks
[[ -x "$PROJUCER" ]] || fail "Projucer not found at $PROJUCER (set PROJUCER)"
[[ -n "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}" ]] || fail "set ANDROID_HOME to your Android SDK"
ANDROID_HOME="${ANDROID_HOME:-$ANDROID_SDK_ROOT}"
NDK="${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk/$NDK_VERSION}"
[[ -f "$NDK/build/cmake/android.toolchain.cmake" ]] \
    || fail "NDK $NDK_VERSION not found at $NDK (sdkmanager \"ndk;$NDK_VERSION\")"

for tool in faust cmake curl unzip python3; do
    command -v "$tool" >/dev/null || fail "$tool is required"
done

CMAKE_VERSION="$(cmake --version | head -1 | sed 's/[^0-9.]//g')"
[[ "$(printf '%s\n' "3.25" "$CMAKE_VERSION" | sort -V | head -1)" == "3.25" ]] \
    || fail "cmake >= 3.25 is required (libpd's CMake project needs it); found $CMAKE_VERSION"

[[ -f "$ROOT/src/libs/libpd/pure-data/src/m_pd.h" && -f "$ROOT/src/libs/abl_link/external/link/include/ableton/Link.hpp" ]] \
    || fail "submodules missing: git submodule update --init --recursive"

if command -v nproc >/dev/null; then JOBS="$(nproc)"; else JOBS="$(sysctl -n hw.ncpu)"; fi

BUILD="$ROOT/build/android"
DOWNLOADS="$ROOT/build/downloads"   # kept between runs; everything else is rebuilt

rm -rf "$BUILD"
mkdir -p "$BUILD" "$DOWNLOADS"

download() {   # download <url> <file>: fetch once, reuse afterwards
    if [[ ! -f "$DOWNLOADS/$2" ]]; then
        curl -fL --retry 3 -o "$DOWNLOADS/$2.part" "$1" || fail "could not download $1"
        mv -f "$DOWNLOADS/$2.part" "$DOWNLOADS/$2"
    fi
}

#--------------------------------------------------------- stage the assets
# Everything under build/android/assets ends up in the APK's assets/ folder
# (Projucer's "Extra Android Assets"), and is unpacked on the device by
# android::extractResources(). The layout of assets/formuls matches the
# desktop resource root, so the rest of the app finds things where it
# always does.
ASSETS="$BUILD/assets/formuls"
mkdir -p "$ASSETS/gui"
cp -R "$ROOT/src/pd" "$ASSETS/pd"
cp "$ROOT/src/gui/_main.json" "$ROOT/src/gui/_formuls-default.state" "$ASSETS/gui/"
cp "$ROOT/src/android/node/formuls-android-preload.js" "$ASSETS/gui/"

# Open Stage Control, branded and patched exactly as for the desktop build.
download "https://openstagecontrol.ammd.net/packages/open-stage-control_${OSC_VERSION}_node.zip" \
         "open-stage-control_${OSC_VERSION}_node.zip"
unzip -q "$DOWNLOADS/open-stage-control_${OSC_VERSION}_node.zip" -d "$BUILD"
mv "$BUILD/open-stage-control_${OSC_VERSION}_node" "$ASSETS/gui/open-stage-control"
sh "$ROOT/src/tools/brand-osc.sh" "$ASSETS/gui/open-stage-control" "$VERSION"
python3 "$ROOT/src/tools/patch-osc-perf.py" "$ASSETS/gui/open-stage-control"

# Leave out what the node server never loads -- the offline docs, source
# maps, and the Electron launcher -- which halves what has to be unpacked
# on the tablet at first launch.
rm -rf "$ASSETS/gui/open-stage-control/docs" \
       "$ASSETS/gui/open-stage-control/launcher" \
       "$ASSETS/gui/open-stage-control/server/electron"
find "$ASSETS/gui/open-stage-control" -name "*.map" -delete

#-------------------------------------------- faust externals -> C++ sources
# The same three objects and the same options as build-macOS.sh, but only
# translated to C++ here: they are compiled into the app by the CMake
# project in src/android/native instead of into loadable .pd_* files.
FAUST_GEN="$BUILD/faust"
mkdir -p "$FAUST_GEN"
FAUST_ARCH="$(faust --archdir)/puredata.cpp"

(
    cd "$ROOT/src/faust"
    for dsp in formuls f_reverb f_repeater; do
        faust -i -a "$FAUST_ARCH" -vec -lv 0 -vs 4 "$dsp.dsp" -o "$FAUST_GEN/$dsp.cpp"
    done
)

#----------------------------------------------------------- nodejs-mobile
download "https://github.com/nodejs-mobile/nodejs-mobile/releases/download/v${NODEJS_MOBILE_VERSION}/nodejs-mobile-v${NODEJS_MOBILE_VERSION}-android.zip" \
         "nodejs-mobile-v${NODEJS_MOBILE_VERSION}-android.zip"
NODEJS_MOBILE="$BUILD/nodejs-mobile"
unzip -q "$DOWNLOADS/nodejs-mobile-v${NODEJS_MOBILE_VERSION}-android.zip" -d "$NODEJS_MOBILE"

#------------------------------------------------- native libraries per ABI
for abi in $ABIS; do
    echo "=== native libraries: $abi"

    [[ -f "$NODEJS_MOBILE/bin/$abi/libnode.so" ]] || fail "nodejs-mobile has no build for $abi"

    cmake -S "$ROOT/src/android/native" -B "$BUILD/cmake-$abi" \
          -G "Unix Makefiles" \
          -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
          -DANDROID_ABI="$abi" \
          -DANDROID_PLATFORM="android-$ANDROID_API" \
          -DANDROID_STL=c++_static \
          -DCMAKE_BUILD_TYPE=Release \
          -DFORMULS_FAUST_GEN_DIR="$FAUST_GEN" \
          -DNODEJS_MOBILE_DIR="$NODEJS_MOBILE"

    cmake --build "$BUILD/cmake-$abi" --parallel "$JOBS" --target formulspd formuls_node

    # Static libraries the JUCE project links (libraryPath in formuls.jucer).
    mkdir -p "$BUILD/native/$abi"
    cp "$BUILD/cmake-$abi/libformulspd.a" "$BUILD/native/$abi/"
    cp "$(find "$BUILD/cmake-$abi/libpd" -name "libpd.a" | head -1)" "$BUILD/native/$abi/"

    # Shared libraries packed into the APK as-is (jniLibs, see formuls.jucer).
    # libnode.so is built against the shared C++ runtime, which therefore has
    # to ship too. (The JUCE app itself links the runtime statically; the
    # two never meet, since node only ever runs in the ":gui" process.)
    case "$abi" in
        arm64-v8a)   triple="aarch64-linux-android" ;;
        armeabi-v7a) triple="arm-linux-androideabi" ;;
        x86_64)      triple="x86_64-linux-android" ;;
        x86)         triple="i686-linux-android" ;;
        *)           fail "unknown ABI $abi" ;;
    esac
    CXX_SHARED="$(find "$NDK/toolchains/llvm/prebuilt" -path "*/sysroot/usr/lib/$triple/libc++_shared.so" | head -1)"
    [[ -f "$CXX_SHARED" ]] || fail "libc++_shared.so for $abi not found in the NDK"

    mkdir -p "$BUILD/jniLibs/$abi"
    cp "$NODEJS_MOBILE/bin/$abi/libnode.so" "$BUILD/jniLibs/$abi/"
    cp "$BUILD/cmake-$abi/libformuls_node.so" "$BUILD/jniLibs/$abi/"
    cp "$CXX_SHARED" "$BUILD/jniLibs/$abi/"
done

#---------------------------------------------- generate the Android project
# Same MODULEPATH rewrite as build-macOS.sh (see the comment there): point
# the .jucer at this JUCE install, resave, and always put the tracked file
# back, even if the resave fails.
if [[ "$(uname)" == "Darwin" ]]; then
    JUCE_DIR="$PROJUCER"; for _ in 1 2 3 4; do JUCE_DIR="$(dirname "$JUCE_DIR")"; done
else
    JUCE_DIR="$(dirname "$PROJUCER")"
fi
JUCE_MODULES="$JUCE_DIR/modules"
[[ -d "$JUCE_MODULES/juce_core" ]] || fail "JUCE modules not found at $JUCE_MODULES"

JUCER="$ROOT/src/app/formuls.jucer"
cp "$JUCER" "$BUILD/formuls.jucer.orig"
trap 'cp -f "$BUILD/formuls.jucer.orig" "$JUCER" 2>/dev/null || true' EXIT
sed -i.bak "s|path=\"[^\"]*JUCE/modules\"|path=\"$JUCE_MODULES\"|g" "$JUCER"
rm -f "$JUCER.bak"
"$PROJUCER" --resave "$JUCER"
cp -f "$BUILD/formuls.jucer.orig" "$JUCER"
trap - EXIT

ANDROID_PROJECT="$ROOT/src/app/Builds/Android"
echo "sdk.dir=$ANDROID_HOME" > "$ANDROID_PROJECT/local.properties"

# JUCE signs every build with this key; create it the way Android Studio
# would if this machine has never built an Android app before.
DEBUG_KEYSTORE="$HOME/.android/debug.keystore"
if [[ ! -f "$DEBUG_KEYSTORE" ]]; then
    KEYTOOL="keytool"
    [[ -n "${JAVA_HOME:-}" ]] && KEYTOOL="$JAVA_HOME/bin/keytool"
    mkdir -p "$HOME/.android"
    "$KEYTOOL" -genkeypair -v -keystore "$DEBUG_KEYSTORE" -storepass android \
               -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 \
               -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
fi

#------------------------------------------------------------------- gradle
(
    cd "$ANDROID_PROJECT"
    ./gradlew --no-daemon "$GRADLE_TASK"
)

APK="$(find "$ANDROID_PROJECT/app/build/outputs/apk" -name "*.apk" -path "*$CONFIG*" | head -1)"
[[ -n "$APK" ]] || fail "gradle finished but no $CONFIG APK was found"

OUT="$ROOT/formuls-$VERSION-android-$CONFIG.apk"
cp "$APK" "$OUT"

echo "Done: $OUT"
echo "Install on a connected tablet with:  adb install -r \"$OUT\""
