#!/bin/zsh
# Linux build script for the formuls JUCE app.
# NOTE: untested -- adapted from the macOS build. You need a Linux build of
# Projucer (see ~/JUCE/extras/Projucer) plus the JUCE Linux dependencies
# (see JUCE/docs/Linux Dependencies.md), faust, and wget.
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

# make staging directories and copy assets across
mkdir -p build
cp -r src/gui build/gui
cp -r src/pd build/pd
mkdir -p build/pd/externals

# build faust pd externals
cd "$ROOT/src/faust"
faust2puredata -vec -lv 0 -vs 4 -clang f_ott.dsp f_digitaliser.dsp f_widener.dsp f_limiter.dsp f_repeater.dsp f_reverb.dsp formuls.dsp
mv *.pd_linux ../../build/pd/externals

# build ableton link (abl_link~) pd external
cd "$ROOT/src/libs/abl_link/external"
make
mv abl_link~.pd_linux "$ROOT/build/pd/externals"

# download open stage control and nodejs
cd "$ROOT/src/gui"
wget https://openstagecontrol.ammd.net/packages/open-stage-control_1.31.0_node.zip
unzip open-stage-control_1.31.0_node.zip
cp -r open-stage-control_1.31.0_node "$ROOT/build/gui/open-stage-control"
rm -rf open-stage-control_1.31.0_node*

# Put formuls' own name and version in the GUI's greeting header,
# in place of Open Stage Control's. Fails the build if the markup has
# changed, rather than silently shipping the toolkit's branding.
"$ROOT/src/tools/brand-osc.sh" "$ROOT/build/gui/open-stage-control" "$VERSION"

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
"$PROJUCER" --resave src/app/formuls.jucer
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
