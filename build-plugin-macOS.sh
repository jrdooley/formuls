#!/bin/bash
# Builds the formuls instrument plugin (VST3 + AU) for macOS.
#
# The plugin runs the same Pd patch and serves the same control GUI as the
# app, so it takes those resources (pd/ with its compiled externals, and the
# flattened gui/) from an app bundle built by ./build-macOS.sh, rather than
# compiling Faust and fetching Open Stage Control a second time. node is not
# copied: the plugin serves the GUI itself.
#
#   ./build-plugin-macOS.sh                 # uses formuls-$VERSION.app
#   FORMULS_APP=/path/to/formuls.app ./build-plugin-macOS.sh
#   INSTALL=1 ./build-plugin-macOS.sh       # also copies both into ~/Library/Audio/Plug-Ins
#
# Produces formuls-$VERSION.vst3 and formuls-$VERSION.component in the repo root.
# See docs/plugin/README.md.

set -euo pipefail

VERSION=0.3.2
ROOT="$(cd "$(dirname "$0")" && pwd)"
APP="${FORMULS_APP:-$ROOT/formuls-$VERSION.app}"
PROJUCER="${PROJUCER:-$HOME/JUCE/Projucer.app/Contents/MacOS/Projucer}"
LIBPD="$ROOT/src/libs/libpd/libs/libpd.dylib"

RES="$APP/Contents/Resources"
if [[ ! -f "$RES/pd/_main.pd" || ! -f "$RES/gui/_main.json" || ! -d "$RES/gui/open-stage-control/client" ]]; then
    echo "No built app resources at $APP." >&2
    echo "Run ./build-macOS.sh first, or point FORMULS_APP at a built formuls app." >&2
    exit 1
fi

# libpd: reuse the app's copy if the submodule's build has been cleaned away.
if [[ ! -f "$LIBPD" ]]; then
    LIBPD="$APP/Contents/Frameworks/libpd.dylib"
fi

# Projucer, with the JUCE module path pointed at wherever JUCE really is
# (same approach as build-macOS.sh; the .jucer is restored afterwards).
JUCE_DIR="$PROJUCER"; for _ in 1 2 3 4; do JUCE_DIR="$(dirname "$JUCE_DIR")"; done
JUCER="$ROOT/src/plugin/formuls-plugin.jucer"
cp "$JUCER" "$JUCER.orig"
trap 'mv -f "$JUCER.orig" "$JUCER" 2>/dev/null || true' EXIT
sed -i '' "s|path=\"[^\"]*JUCE/modules\"|path=\"$JUCE_DIR/modules\"|g" "$JUCER"
"$PROJUCER" --resave "$JUCER"
mv -f "$JUCER.orig" "$JUCER"
trap - EXIT

# The plugin links libpd from the submodule's build folder.
if [[ ! -f "$ROOT/src/libs/libpd/libs/libpd.dylib" ]]; then
    (cd "$ROOT/src/libs/libpd" && make UTIL=true EXTRA=true \
        && install_name_tool -id @rpath/libpd.dylib libs/libpd.dylib)
    LIBPD="$ROOT/src/libs/libpd/libs/libpd.dylib"
fi

# VST3 and AU only. The .jucer also lists LV2, for build-plugin-linux.sh; on
# macOS its manifest step cannot find libpd (the rpaths are set for VST3/AU
# bundle depth), and macOS hosts take VST3 or AU anyway.
xcodebuild -project "$ROOT/src/plugin/Builds/MacOSX/formuls.xcodeproj" \
    -target "formuls - VST3" -target "formuls - AU" \
    -configuration Release ARCHS="$(uname -m)" ONLY_ACTIVE_ARCH=YES

OUT="$ROOT/src/plugin/Builds/MacOSX/build/Release"

for ext in vst3 component; do
    BUNDLE="$ROOT/formuls-$VERSION.$ext"
    rm -rf "$BUNDLE"
    cp -R "$OUT/formuls.$ext" "$BUNDLE"
    mkdir -p "$BUNDLE/Contents/Resources" "$BUNDLE/Contents/Frameworks"
    cp -R "$RES/pd" "$BUNDLE/Contents/Resources/pd"
    rsync -a --exclude node "$RES/gui/" "$BUNDLE/Contents/Resources/gui/"
    cp "$LIBPD" "$BUNDLE/Contents/Frameworks/"
    # Bind the externals' Pd symbols to this libpd. Linked flat-namespace,
    # they would take whichever image exports class_new, gensym, post... first:
    # in a Max-based host (SoundDesk) that is Max, and every external fails.
    python3 "$ROOT/src/tools/bind-externals-to-libpd.py" --arch "$(uname -m)" \
        "$BUNDLE/Contents/Frameworks/libpd.dylib" @loader_path/../../../Frameworks/libpd.dylib \
        "$BUNDLE"/Contents/Resources/pd/externals/*.pd_darwin
    # --deep below does not reach code under Resources/, and rebinding removed
    # the externals' signatures: unsigned, arm64 refuses to load them.
    codesign --force -s - "$BUNDLE"/Contents/Resources/pd/externals/*.pd_darwin
    codesign --force --deep -s - "$BUNDLE"
    echo "Built $BUNDLE"
done

if [[ "${INSTALL:-0}" == "1" ]]; then
    mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"
    rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/formuls.vst3" "$HOME/Library/Audio/Plug-Ins/Components/formuls.component"
    cp -R "$ROOT/formuls-$VERSION.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/formuls.vst3"
    cp -R "$ROOT/formuls-$VERSION.component" "$HOME/Library/Audio/Plug-Ins/Components/formuls.component"
    echo "Installed into ~/Library/Audio/Plug-Ins (VST3, Components)"
fi
