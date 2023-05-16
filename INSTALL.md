# formuls build process

### Requirements
You must have the following installed before attempting to build formuls:
- python 3.x (python.org)
- sounddevice python library (https://pypi.org/project/sounddevice/)
- pyinstaller (pyinstaller.org)
- faust (faust.grame.fr)

### libraries
formuls uses libpd and abl_link. These are included as submodules when checking out the formuls git repo using the following command:
`git clone --recurse-submodules https://github.com/jrdooley/formuls.git`

### Building for macOS x64 and ARM64
From the "formuls" directory: run the "build-arm64.sh" script for M1/M2 architectures; run the "build-x64.sh" script for 64 bit Intel architectures. This will produce an app bundle in the same directory as the "build-*64.sh" script

### Process overview (to be completed...)
