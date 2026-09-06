#!/bin/zsh
# macOS build script for the formuls JUCE app.
# Builds for the native architecture (Apple Silicon or Intel).
#
# Requirements: Xcode (command line tools), JUCE (with Projucer.app) at
# ~/JUCE (or set the PROJUCER env var), faust, wget, python3.
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

# Make staging directories and copy assets across.
#
# build/ is removed first. A run that failed part-way leaves a populated
# build/gui behind, and "cp -r src/gui build/gui" would then copy INTO it
# rather than replacing it.
#
# Only the two files the app actually reads from src/gui are copied. Copying
# all of src/gui also picks up open-stage-control/ and node/ left behind by
# earlier runs -- both gitignored, so invisible in "git status". The fresh
# download below would then nest inside the stale tree, brand-osc.sh would
# find it already branded and exit 0, and the build would silently ship the
# old GUI plus tens of MB of dead weight.
rm -rf build
mkdir -p build/gui
cp src/gui/_main.json src/gui/_formuls-default.state build/gui/
cp -r src/pd build/pd
mkdir -p build/pd/externals

# build faust pd externals
cd "$ROOT/src/faust"
faust2puredata -vec -lv 0 -vs 4 -clang f_repeater.dsp f_reverb.dsp formuls.dsp
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

# Download open stage control and nodejs (native arch) into build/, not into
# src/gui/: src/ is a source tree and downloads left there become the stale
# copies the staging step above has to work around.
cd "$ROOT/build"
wget https://openstagecontrol.ammd.net/packages/open-stage-control_1.31.0_node.zip
unzip open-stage-control_1.31.0_node.zip
mv open-stage-control_1.31.0_node "$ROOT/build/gui/open-stage-control"
rm -f open-stage-control_1.31.0_node.zip

# Rebrand the client's greeting header as formuls rather than Open Stage
# Control. Matched on the markup rather than a line number, so it stops the
# build if the header ever changes instead of silently shipping unbranded.
sh "$ROOT/src/tools/brand-osc.sh" "$ROOT/build/gui/open-stage-control" "$VERSION"

# Serialise each broadcast once instead of once per connected tablet.
# Add --batch-ms 20 to also coalesce value updates into one WebSocket
# frame; that is off by default because it costs the tablet more work
# than it saves. Both measured in docs/gui/README.md.
python3 "$ROOT/src/tools/patch-osc-perf.py" "$ROOT/build/gui/open-stage-control"

cd "$ROOT/build"
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
# Projucer resolves MODULEPATH relative to the .jucer file, and the committed
# value only resolves when the repo sits exactly two levels below home. Rewrite
# it to an absolute path derived from the Projucer binary, resave, then put the
# file back: the generated Builds/ and JuceLibraryCode/ keep the absolute path,
# which is all the build needs. The restore matters -- without it every build
# leaves the tracked .jucer dirty with a machine-specific path, one
# "git commit -a" away from being baked into the repository. The trap restores
# it even if the resave or the build below fails under "set -e".
# (--set-global-search-path does not override per-project MODULEPATH entries.)
JUCE_DIR="$PROJUCER"; for _ in 1 2 3 4; do JUCE_DIR="$(dirname "$JUCE_DIR")"; done
JUCE_MODULES="$JUCE_DIR/modules"
cp src/app/formuls.jucer "$ROOT/build/formuls.jucer.orig"
trap 'cp -f "$ROOT/build/formuls.jucer.orig" "$ROOT/src/app/formuls.jucer" 2>/dev/null || true' EXIT
sed -i '' "s|path=\"[^\"]*JUCE/modules\"|path=\"$JUCE_MODULES\"|g" src/app/formuls.jucer
"$PROJUCER" --resave src/app/formuls.jucer
cp -f "$ROOT/build/formuls.jucer.orig" src/app/formuls.jucer
rm -f "$ROOT/build/formuls.jucer.orig"
trap - EXIT

# xcodebuild reads src/app/Builds/MacOSX, not the .jucer, so restoring the
# .jucer above does not affect it.
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
