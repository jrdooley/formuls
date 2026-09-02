#!/bin/zsh
# macOS ARM64 build script

# make build directories and copy assets across
mkdir build
cp -r src/gui build/gui
cp -r src/pd build/pd
mkdir build/pd/externals
cp src/python/formuls-0.2.5-beta.py build/formuls-0.2.5-beta.py
cp src/python/pyinstaller.py build/pyinstaller.py

# build faust pd externals
cd src/faust
faust2puredata -vec -lv 0 -vs 4 -clang f_digitaliser.dsp f_limiter.dsp f_repeater.dsp f_reverb.dsp formuls.dsp

mv *.pd_darwin ../../build/pd/externals

# ableton link (abl_link~) pd external
# Do NOT build this from source on macOS with a current Xcode: the resulting
# binary applies exactly 16 tempo changes and then silently ignores every
# later one, because Link's worker threads never run. See
# src/prebuilt/README.md for the full diagnosis. Ship the known-good build.
# NB: the cd stays outside the conditional; the rest of this script
# navigates by paths relative to src/libs/abl_link/external.
cd ../libs/abl_link/external
if [ -f "../../../prebuilt/abl_link~.pd_darwin" ]; then
    cp "../../../prebuilt/abl_link~.pd_darwin" ../../../../build/pd/externals/
    echo "using pre-built abl_link~ (see src/prebuilt/README.md)"
else
    echo "WARNING: src/prebuilt/abl_link~.pd_darwin missing; building from"
    echo "         source, which on current Xcode freezes the tempo after 16"
    echo "         changes. Verify with src/tools/abl-link-repro."
    make
    mv abl_link~.pd_darwin ../../../../build/pd/externals
fi

# download open stage control and nodejs
cd ../../../gui
wget https://github.com/jean-emmanuel/open-stage-control/releases/download/v1.29.5/open-stage-control_1.29.5_node.zip
unzip open-stage-control_1.29.5_node.zip
cp -r open-stage-control_1.29.5_node ../../build/gui/open-stage-control
rm -rf open-stage-control_1.29.5_nod*

wget https://nodejs.org/dist/v22.17.0/node-v22.17.0-darwin-x64.tar.gz
tar -xf node-v22.17.0-darwin-x64.tar.gz
cp node-v22.17.0-darwin-x64/bin/node ../../build/gui/node
rm -rf node-v22.17.0-darwin-x64*

# build libpd and formulsengine
cd ../
make

# run pyinstaller and build app bundle
cd ../build
python3 pyinstaller.py

cp -r icons dist/formuls-0.2.5-beta/_internal/icons
cp -r pd dist/formuls-0.2.5-beta/_internal/pd
cp -r gui dist/formuls-0.2.5-beta/_internal/gui
cp -r libs dist/formuls-0.2.5-beta/_internal/libs

cp -r dist/formuls-0.2.5-beta ../formuls-0.2.5-beta.app

# clean up build files
cd ../
rm -rf build/

cd src/formulsengine
rm -rf *.o

cd ../libs/libpd
make clean
rm libs/libpd.dylib

cd ../abl_link/external
make clean

exit 0