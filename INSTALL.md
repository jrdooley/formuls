# formuls build process

### Requirements
You must have the following installed before attempting to build formuls:
- Xcode and its command line tools (macOS)
- JUCE (https://juce.com / https://github.com/juce-framework/JUCE), expected at
  `~/JUCE` — including `Projucer.app` (a pre-built copy ships with JUCE releases).
  If yours lives elsewhere, set the `PROJUCER` environment variable to the
  Projucer binary and update the JUCE module paths in `src/app/formuls.jucer`.
- faust (faust.grame.fr)
- wget (this can be installed via homebrew: https://brew.sh/)

Python is no longer required: the old Python/Tkinter launcher and the
standalone RtAudio engine have been replaced by a JUCE application in
`src/app` that embeds libpd directly. See `src/app/README.md` for its
documentation (including how to restyle the app window).

### libraries
formuls uses libpd and abl_link. These are included as submodules when checking out the formuls git repo using the following command:
`git clone --recurse-submodules https://github.com/jrdooley/formuls.git`

### Building for macOS
From the repository root run `./build-macOS.sh`. It builds the faust and
abl_link~ Pd externals, downloads Open Stage Control and node.js, builds
libpd, generates and builds the Xcode project for the JUCE app, and produces
a self-contained `formuls-<version>.app` for the native architecture
(Apple Silicon or Intel) in the repository root.

### Building for Linux
Run `./build-linux.sh` (untested port of the macOS script). It requires a
Linux Projucer (`~/JUCE/Projucer` from the JUCE download, or one built from
`~/JUCE/extras/Projucer`) and JUCE's Linux dependencies, and produces a
`formuls-<version>-linux-<arch>` folder (`x64` or `arm64`) containing the
binary plus its `pd/`, `gui/` and `libs/` resources.

formuls needs an X display. Without one (an SSH or OrbStack shell, say), run
it under a virtual display and pass `--start` to press Start automatically,
then open the control GUI at the address it prints:

```bash
xvfb-run -a ./formuls --start
```
