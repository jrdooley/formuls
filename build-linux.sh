#!/bin/zsh
# macOS ARM64 build script

# make build directories and copy assets across
mkdir build
cp -r src/gui build/gui
cp -r src/pd build/pd
mkdir build/pd/externals
cp src/linux/formuls.sh build/formuls.sh
chmod +x build/formuls.sh

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
wget https://openstagecontrol.ammd.net/packages/open-stage-control_1.30.1_node.zip
unzip open-stage-control_1.30.1_node.zip
cp -r open-stage-control_1.30.1_node ../../build/gui/open-stage-control
rm -rf open-stage-control_1.30.1_nod*

wget https://nodejs.org/dist/v22.17.0/node-v22.17.0-linux-x64.tar.xz
tar -xf node-v22.17.0-linux-x64.tar.xz
cp node-v22.17.0-linux-x64/bin/node ../../build/gui/node
chmod +x ../../build/gui/node
rm -rf node-v22.17.0-linux-x64*

# build libpd and formulsengine
cd ../
make
chmod +x ../build/formulsengine

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