#!/bin/zsh
# macOS build script for the formuls JUCE app.
# Builds for the native architecture (Apple Silicon or Intel).
#
# Requirements: Xcode (command line tools), JUCE (with Projucer.app) at
# ~/JUCE (or set the PROJUCER env var), faust, wget.
# Run from the repository root:  ./build-macOS.sh

set -e

VERSION="0.3.0-beta"
ROOT="$(pwd)"
PROJUCER="${PROJUCER:-$HOME/JUCE/Projucer.app/Contents/MacOS/Projucer}"

if [[ ! -x "$PROJUCER" ]]; then
    echo "Projucer not found at: $PROJUCER"
    echo "Install JUCE at ~/JUCE, or set PROJUCER to your Projucer binary."
    exit 1
fi

# make staging directories and copy assets across
mkdir -p build
cp -r src/gui build/gui
cp -r src/pd build/pd
mkdir -p build/pd/externals

# build faust pd externals
cd "$ROOT/src/faust"
faust2puredata -vec -lv 0 -vs 4 -clang f_ott.dsp f_digitaliser.dsp f_widener.dsp f_limiter.dsp f_repeater.dsp f_reverb.dsp formuls.dsp
mv *.pd_darwin ../../build/pd/externals

# ableton link (abl_link~) pd external
# Do NOT build this from source on macOS with a current Xcode: the resulting
# binary applies exactly 16 tempo changes and then silently ignores every
# later one, because Link's worker threads never run. See
# src/prebuilt/README.md for the full diagnosis. Ship the known-good build.
if [ -f "$ROOT/src/prebuilt/abl_link~.pd_darwin" ]; then
    cp "$ROOT/src/prebuilt/abl_link~.pd_darwin" "$ROOT/build/pd/externals/"
    echo "using pre-built abl_link~ (see src/prebuilt/README.md)"
else
    echo "WARNING: src/prebuilt/abl_link~.pd_darwin missing; building from"
    echo "         source, which on current Xcode freezes the tempo after 16"
    echo "         changes. Verify with src/tools/abl-link-repro."
    cd "$ROOT/src/libs/abl_link/external"
    make
    mv abl_link~.pd_darwin "$ROOT/build/pd/externals"
fi

# download open stage control and nodejs (native arch)
cd "$ROOT/src/gui"
wget https://openstagecontrol.ammd.net/packages/open-stage-control_1.31.0_node.zip
unzip open-stage-control_1.31.0_node.zip
cp -r open-stage-control_1.31.0_node "$ROOT/build/gui/open-stage-control"
rm -rf open-stage-control_1.31.0_node*

# Put formuls' own name and version in the GUI's greeting header,
# in place of Open Stage Control's. Fails the build if the markup has
# changed, rather than silently shipping the toolkit's branding.
"$ROOT/src/tools/brand-osc.sh" "$ROOT/build/gui/open-stage-control" "$VERSION"

NODE_ARCH=$(uname -m | sed 's/x86_64/x64/')
wget "https://nodejs.org/dist/v22.17.0/node-v22.17.0-darwin-$NODE_ARCH.tar.gz"
tar -xf "node-v22.17.0-darwin-$NODE_ARCH.tar.gz"
cp "node-v22.17.0-darwin-$NODE_ARCH/bin/node" "$ROOT/build/gui/node"
rm -rf node-v22.17.0-darwin-$NODE_ARCH*

# build libpd (the dylib the JUCE app links against) and give it an
# @rpath install name so the app finds it inside Contents/Frameworks
cd "$ROOT/src/libs/libpd"
make UTIL=true EXTRA=true
install_name_tool -id @rpath/libpd.dylib libs/libpd.dylib
codesign --force -s - libs/libpd.dylib

# generate the Xcode project from formuls.jucer and build the JUCE app
cd "$ROOT"
"$PROJUCER" --resave src/app/formuls.jucer
xcodebuild -project src/app/Builds/MacOSX/formuls.xcodeproj -configuration Release ARCHS=$(uname -m) ONLY_ACTIVE_ARCH=YES

# assemble the final self-contained app bundle
rm -rf "formuls-$VERSION.app"
cp -R src/app/Builds/MacOSX/build/Release/formuls.app "formuls-$VERSION.app"
cp -r build/pd "formuls-$VERSION.app/Contents/Resources/pd"
cp -r build/gui "formuls-$VERSION.app/Contents/Resources/gui"
mkdir -p "formuls-$VERSION.app/Contents/Frameworks"
cp src/libs/libpd/libs/libpd.dylib "formuls-$VERSION.app/Contents/Frameworks/"
codesign --force --deep -s - "formuls-$VERSION.app"

# clean up build files
cd "$ROOT"
rm -rf build/

cd "$ROOT/src/libs/libpd"
make clean
rm -f libs/libpd.dylib

cd "$ROOT/src/libs/abl_link/external"
make clean

echo "Done: $ROOT/formuls-$VERSION.app"
exit 0
