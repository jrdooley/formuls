#!/bin/zsh
# macOS Intel 64bit build script

# make build directories and copy assets across
mkdir build
cp -r src/gui build/gui
cp -r src/icons build/icons
cp -r src/pd build/pd
mkdir build/pd/externals
cp src/python/formuls-0.2-beta.py build/formuls-0.2-beta.py
cp src/python/pyinstaller.py build/pyinstaller.py

# build faust pd externals
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
wget https://github.com/jean-emmanuel/open-stage-control/releases/download/v1.27.0/open-stage-control_1.27.0_node.zip
unzip open-stage-control_1.27.0_node.zip
cp -r open-stage-control_1.27.0_node ../../build/gui/open-stage-control

wget https://nodejs.org/dist/v18.12.1/node-v18.12.1-darwin-x64.tar.gz
tar -xf node-v18.12.1-darwin-x64.tar.gz
cp node-v18.12.1-darwin-x64/bin/node ../../build/gui/node

# build libpd and formulsengine
cd ../
make

# run pyinstaller and build app bundle
cd ../build
python3.12 pyinstaller.py

cp -r icons dist/formuls-0.2-beta/_internal/icons
cp -r pd dist/formuls-0.2-beta/_internal/pd
cp -r gui dist/formuls-0.2-beta/_internal/gui
cp -r libs dist/formuls-0.2-beta/_internal/libs

cp -r dist/formuls-0.2-beta ../formuls-0.2-beta.app