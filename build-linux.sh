#!/bin/zsh
# macOS ARM64 build script

# make build directories and copy assets across
mkdir build
cp -r src/gui build/gui
cp -r src/pd build/pd
cp src/linux/formuls.sh build/formuls.sh
mkdir build/pd/externals

# build faust pd externals
cd src/faust
faust2puredata -vec -lv 0 -vs 4 -clang f_digitaliser.dsp f_limiter.dsp f_repeater.dsp f_reverb.dsp formuls.dsp

mv *.pd_darwin ../../build/pd/externals

# build ableton link (abl_link~_) pd external
cd ../libs/abl_link/external
make
mv abl_link~.pd_darwin ../../../../build/pd/externals

# download open stage control and nodejs
cd ../../../gui
wget https://github.com/jean-emmanuel/open-stage-control/releases/download/v1.29.5/open-stage-control_1.29.5_node.zip
unzip open-stage-control_1.29.5_node.zip
cp -r open-stage-control_1.29.5_node ../../build/gui/open-stage-control
rm -rf open-stage-control_1.29.5_nod*

wget https://nodejs.org/dist/v22.17.0/node-v22.17.0-linux-x64.tar.xz
tar -xf node-v22.17.0-linux-x64.tar.xz
cp node-v22.17.0-linux-x64/bin/node ../../build/gui/node
rm -rf node-v22.17.0-linux-x64*

# build libpd and formulsengine
cd ../
make

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