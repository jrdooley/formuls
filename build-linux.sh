#!/bin/zsh
# Linux build script for the formuls JUCE app.
# NOTE: untested -- adapted from the macOS build. You need a Linux build of
# Projucer (see ~/JUCE/extras/Projucer) plus the JUCE Linux dependencies
# (see JUCE/docs/Linux Dependencies.md), faust, wget, and python3.
# Run from the repository root:  ./build-linux.sh

set -e

VERSION="0.3.0-beta"
ROOT="$(pwd)"
PROJUCER="${PROJUCER:-$HOME/JUCE/extras/Projucer/Builds/LinuxMakefile/build/Projucer}"

if [[ ! -x "$PROJUCER" ]]; then
    echo "Projucer not found at: $PROJUCER"
    echo "Build it from ~/JUCE/extras/Projucer, or set PROJUCER to your Projucer binary."
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
mv *.pd_linux ../../build/pd/externals

# build ableton link (abl_link~) pd external
cd "$ROOT/src/libs/abl_link/external"
make
mv abl_link~.pd_linux "$ROOT/build/pd/externals"

# Download open stage control and nodejs into build/, not into src/gui/:
# src/ is a source tree and downloads left there become the stale copies the
# staging step above has to work around.
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
wget https://nodejs.org/dist/v22.17.0/node-v22.17.0-linux-x64.tar.xz
tar -xf node-v22.17.0-linux-x64.tar.xz
cp node-v22.17.0-linux-x64/bin/node "$ROOT/build/gui/node"
chmod +x "$ROOT/build/gui/node"
rm -rf node-v22.17.0-linux-x64*

# build libpd (the shared library the JUCE app links against)
cd "$ROOT/src/libs/libpd"
make UTIL=true EXTRA=true

# generate the Makefile from formuls.jucer and build the JUCE app
cd "$ROOT"
# UNTESTED on Linux -- ported from build-macOS.sh, which is verified.
#
# Projucer resolves MODULEPATH relative to the .jucer file, and the committed
# value (../../../../JUCE/modules) only resolves when the repo sits exactly two
# levels below home. Rewrite it to an absolute path, resave, then put the file
# back: the generated Builds/ and JuceLibraryCode/ keep the absolute path,
# which is all the build needs, and the tracked .jucer is left alone. The trap
# restores it even if the resave or the build below fails under "set -e".
# (--set-global-search-path does not override per-project MODULEPATH entries.)
#
# Two deliberate differences from build-macOS.sh:
#   - the rewrite goes through a temp file rather than "sed -i". BSD and GNU
#     sed disagree about that flag, and src/tools/brand-osc.sh already avoids
#     it for the same reason. This form needs no in-place support at all, so
#     it is one less thing that can differ between the two platforms.
#   - macOS walks up a fixed 4 directories because Projucer.app/Contents/MacOS
#     is a fixed layout. On Linux the Projucer lives at a depth that depends on
#     how it was built (6 levels up from the default
#     extras/Projucer/Builds/LinuxMakefile/build/Projucer), so this searches
#     upward for the JUCE root instead of counting.
JUCE_DIR="$(dirname "$PROJUCER")"
while [[ "$JUCE_DIR" != "/" && ! -d "$JUCE_DIR/modules/juce_core" ]]; do
    JUCE_DIR="$(dirname "$JUCE_DIR")"
done

if [[ ! -d "$JUCE_DIR/modules/juce_core" ]]; then
    echo "Could not find the JUCE modules folder above: $PROJUCER"
    echo "Set PROJUCER to a Projucer binary inside your JUCE checkout."
    exit 1
fi

JUCE_MODULES="$JUCE_DIR/modules"
cp src/app/formuls.jucer "$ROOT/build/formuls.jucer.orig"
trap 'cp -f "$ROOT/build/formuls.jucer.orig" "$ROOT/src/app/formuls.jucer" 2>/dev/null || true' EXIT
sed "s|path=\"[^\"]*JUCE/modules\"|path=\"$JUCE_MODULES\"|g" \
    src/app/formuls.jucer > "$ROOT/build/formuls.jucer.tmp"
mv -f "$ROOT/build/formuls.jucer.tmp" src/app/formuls.jucer
"$PROJUCER" --resave src/app/formuls.jucer
cp -f "$ROOT/build/formuls.jucer.orig" src/app/formuls.jucer
rm -f "$ROOT/build/formuls.jucer.orig"
trap - EXIT

# make reads src/app/Builds/LinuxMakefile, not the .jucer, so restoring the
# .jucer above does not affect it.
cd "$ROOT/src/app/Builds/LinuxMakefile"
make CONFIG=Release

# assemble the distributable folder; libpd.so sits in libs/ next to the
# binary, matching the $ORIGIN/libs rpath set in formuls.jucer
cd "$ROOT"
rm -rf "formuls-$VERSION-linux"
mkdir -p "formuls-$VERSION-linux/libs"
cp src/app/Builds/LinuxMakefile/build/formuls "formuls-$VERSION-linux/"
cp -r build/pd "formuls-$VERSION-linux/pd"
cp -r build/gui "formuls-$VERSION-linux/gui"
cp src/libs/libpd/libs/libpd.so "formuls-$VERSION-linux/libs/"

# clean up build files
rm -rf build/

cd "$ROOT/src/libs/libpd"
make clean
rm -f libs/libpd.so

cd "$ROOT/src/libs/abl_link/external"
make clean

echo "Done: $ROOT/formuls-$VERSION-linux"
exit 0
