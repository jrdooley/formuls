#!/bin/zsh
# macOS ARM64 build script

# make build directories and copy assets across
mkdir build
cp -r src/gui build/gui
cp -r src/icons build/icons
cp -r src/pd build/pd
cp src/formuls-0.1-2.py build/formuls-0.1-2.py

# build faust pd externals
cd src/faust
faust2puredata f_digitaliser.dsp
faust2puredata f_limiter.dsp
faust2puredata f_repeater.dsp
faust2puredata f_reverb.dsp
faust2puredata formuls.dsp

mv *.pd_darwin ../../build/pd/externals

# build ableton link (abl_link~_) pd external
cd ../libs/abl_link/external
make
mv abl_link~.pd_darwin ../../../../build/pd/externals

# download open stage control and nodejs
cd ../../../gui
wget https://github.com/jean-emmanuel/open-stage-control/releases/download/v1.25.0/open-stage-control_1.25.0_node.zip
unzip open-stage-control_1.25.0_node.zip
cp -r open-stage-control_1.25.0_node ../../build/gui/open-stage-control

wget https://nodejs.org/dist/v18.12.1/node-v18.12.1-darwin-arm64.tar.gz
tar -xf node-v18.12.1-darwin-arm64.tar.gz
cp node-v18.12.1-darwin-arm64/bin/node ../../build/gui/node

# build libpd and formulsengine
cd ../
make

# run pyinstaller and build app bundle
cd ../build
pyinstaller formuls-0.1-2.py

cp -r icons dist/formuls-0.1-2/icons
cp -r pd dist/formuls-0.1-2/pd
cp -r gui dist/formuls-0.1-2/gui
cp -r libs dist/formuls-0.1-2/libs

cp -r dist/formuls-0.1-2 ../formuls-0.1-2.app