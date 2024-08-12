# formuls build process

### Requirements
You must have the following installed before attempting to build formuls:
- faust (faust.grame.fr)
- python 3.12 (python.org)
- Tkinter
- pyinstaller (pyinstaller.org)
- sounddevice python library (https://pypi.org/project/sounddevice/)
- wget (this can be installed via homebrew: https://brew.sh/)

### libraries
formuls uses libpd and abl_link. These are included as submodules when checking out the formuls git repo using the following command:
`git clone --recurse-submodules https://github.com/jrdooley/formuls.git`

### Building for macOS x64 and ARM64
From the "formuls" directory: run the "build-arm64.sh" script for M1/M2 architectures; run the "build-x64.sh" script for 64 bit Intel architectures. This will produce an app bundle in the same directory as the "build-*64.sh" script

### Process overview (to be completed...)
