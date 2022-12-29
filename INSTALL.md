# formuls build process

### Requirements
You must have the following installed before attempting to build formuls:
- python 3.x (python.org)
- pyinstaller (pyinstaller.org)
- sounddevice python library (https://pypi.org/project/sounddevice/)
- faust (faust.grame.fr)

### Building for macOS arm64 systems
from the "formuls" directory, run the "build.sh" script. This will produce an app bundle in the same directory as the "build.sh" script

#### building for non-arm64
The build script uses nodejs. The current link is to the arm64 build. If you are building for Intel processors you should replace the link in the build script with: https://nodejs.org/dist/v18.12.1/node-v18.12.1-darwin-x64.tar.gz

The abl_link~ Pure Data external that is included in the github repo is an arm64 build. You can either build your own version of the abl_link~ external from source (https://github.com/libpd/abl_link), or download a pre-compiled version with Deken in Pure Data (http://msp.ucsd.edu/software.html).

### Process overview (to be completed...)
#### compiling formulsengine and libpd
1) cd src/
2) make (this triggers Makefile at src/Makefile)

#### building faust pd externals, run pyinstaller and create app bundle
1) run 