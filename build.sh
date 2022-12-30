#!/bin/zsh

# make build directories and copy assets across
mkdir build
cp -r src/gui build/gui
cp -r src/icons build/icons
cp -r src/pd build/pd
cp src/formuls-0.1-0.py build/formuls-0.1-0.py

# build faust pd externals
cd src/faust
faust2puredata f_digitaliser.dsp
faust2puredata f_limiter.dsp
faust2puredata f_repeater.dsp
faust2puredata f_reverb.dsp
faust2puredata formuls.dsp

mv *.pd_darwin ../../build/pd/externals

# download open stage control and nodejs
cd ../gui
wget https://github.com/jean-emmanuel/open-stage-control/releases/download/v1.21.0/open-stage-control-1.21.0-node.zip
unzip open-stage-control-1.21.0-node.zip
cp -r open-stage-control-1.21.0-node ../../build/gui/open-stage-control

wget https://nodejs.org/dist/v18.12.1/node-v18.12.1-darwin-arm64.tar.gz
tar -xf node-v18.12.1-darwin-arm64.tar.gz
cp node-v18.12.1-darwin-arm64/bin/node ../../build/gui/node

# build libpd and formulsengine
cd ../
make

# run pyinstaller and build app bundle
cd ../build
pyinstaller --icon icons/formuls.icns formuls-0.1-0.py

cp -r icons dist/formuls-0.1-0/icons
cp -r pd dist/formuls-0.1-0/pd
cp -r gui dist/formuls-0.1-0/gui
cp -r libs dist/formuls-0.1-0/libs

cp -r dist/formuls-0.1-0 ../formuls-0.1-0.app