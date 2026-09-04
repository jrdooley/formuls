# formuls — Agent Guide (juce-port branch)

Electronic musical instrument: 6-operator FM synth with touchscreen GUI.
Stack: JUCE (native app), libpd (embedded audio), Faust (DSP), Pure Data (patches), Open Stage Control (web GUI).

**This branch replaces the Python/Tkinter + RtAudio architecture on `main`.**
Python is no longer a build or runtime dependency. See `docs/README.md` for the full branch history.

## Build

**Prerequisites**: Xcode + CLI tools, JUCE at `~/JUCE` (with Projucer), faust, wget. Clone with submodules:
```
git clone --recurse-submodules https://github.com/jrdooley/formuls.git
```

**Full build (macOS)**:
```
./build-macOS.sh
```
Produces `formuls-0.3.0-beta.app` in the repo root. Steps:
1. Compiles 3 Faust externals: `f_repeater.dsp`, `f_reverb.dsp`, `formuls.dsp`
2. Copies **prebuilt** `abl_link~.pd_darwin` (never builds from source on macOS)
3. Downloads Open Stage Control 1.31.0 + Node 22.17.0
4. Runs `brand-osc.sh` and `patch-osc-perf.py` on the downloaded O-S-C package
5. Builds libpd with `UTIL=true EXTRA=true`, sets `@rpath` install name
6. Runs Projucer (`--resave src/app/formuls.jucer`) then `xcodebuild`
7. Assembles self-contained `.app` bundle, codesigns, cleans up

**Linux** (untested): `./build-linux.sh` — same flow, needs Linux Projucer build, produces a directory not `.app`.

**JUCE not at `~/JUCE`?** Set `PROJUCER=/path/to/Projucer` or update the `MODULEPATH` entries in `src/app/formuls.jucer`.

**No incremental build.** Each run does `rm -rf build/` at the end. Comment out cleanup lines to iterate faster.

### Developing just the JUCE app
```bash
# once: build libpd
cd src/libs/libpd && make UTIL=true EXTRA=true
install_name_tool -id @rpath/libpd.dylib libs/libpd.dylib
codesign --force -s - libs/libpd.dylib

# regenerate Xcode project after editing formuls.jucer
~/JUCE/Projucer.app/Contents/MacOS/Projucer --resave src/app/formuls.jucer

# build + run
cd src/app/Builds/MacOSX
xcodebuild -project formuls.xcodeproj -configuration Release ARCHS=$(uname -m) ONLY_ACTIVE_ARCH=YES
./build/Release/formuls.app/Contents/MacOS/formuls
```
A dev build finds `src/pd` and `src/gui` by walking up the directory tree (see `ResourceLocator.h`). It finds `libpd.dylib` via a second rpath pointing to `src/libs/libpd/libs`.

### Test autostart
Build with `FORMULS_AUTOSTART_TEST=1` to auto-pick the first device and press Start after 1s:
```bash
xcodebuild ... GCC_PREPROCESSOR_DEFINITIONS='$(inherited) FORMULS_AUTOSTART_TEST=1'
```
Supports env vars: `FORMULS_TEST_SAMPLERATE`, `FORMULS_TEST_CHANNELS`, `FORMULS_TEST_RECORD_SECONDS`, `FORMULS_SNAPSHOT_PATH`.

## Architecture

### Runtime (JUCE app spawns one child process)
```
formuls (JUCE app, embeds libpd)  →  spawns:
  node + Open Stage Control (web GUI on port 9001)
```
The JUCE app handles audio device selection, libpd, recording, and GUI addresses.
GUI sends OSC to `127.0.0.1:9000` (the Pd patch listens there).
Ports: `OpenStageControlProcess::guiPort` (9001), `patchOscPort` (9000).

### Source layout
| Directory | Language | Purpose |
|---|---|---|
| `src/app/` | C++ (JUCE) | The native app: `Main.cpp`, `MainComponent`, `FormulsEngine`, `AudioRecorder`, `OpenStageControlProcess`, `FormulsLookAndFeel`, `ResourceLocator` |
| `src/app/formuls.jucer` | XML | Projucer project — generates Xcode and Linux Makefile builds |
| `src/faust/` | Faust DSP | `formuls.dsp` (main synth), `f_reverb.dsp`, `f_repeater.dsp` + libs |
| `src/faust/*.lib` | Faust lib | `ffx.lib` (effects), `fsynth.lib` (synth), `fosc.lib` (oscillators) — **tracked, do not gitignore** |
| `src/pd/` | Pure Data | `_main.pd` + abstractions (`audioabstractions/`, `controlabstractions/`) |
| `src/gui/` | JSON | Open Stage Control session (`_main.json`, 43k lines) and default state |
| `src/libs/libpd/` | submodule | libpd |
| `src/libs/abl_link/` | submodule | Ableton Link Pd external |
| `src/prebuilt/` | binary | Known-good `abl_link~.pd_darwin` (arm64) |
| `src/tools/` | mixed | `brand-osc.sh`, `patch-osc-perf.py`, `check-reset-coverage.py`, `automation-probe.py`, `bpm-probe`, `abl-link-repro` |
| `docs/` | markdown/html | Efficiency report, GUI performance write-up, session logs |

### Signal flow
```
Faust DSP (formuls.dsp)  →  fsynth.lib → ffx.lib  →  Pd patch (_main.pd)
OSC from GUI ─────────────────────────────────────→  libpd (inside JUCE app)
libpd ──→ juce::AudioIODeviceCallback ──→ audio output
```

### Recording
Record button in the app window, enabled only while engine runs. Writes 24-bit PCM WAV to `~/Music/formuls/` during the take; save dialog only moves the file. Channel count matches the selected output (2 or 14). RF64 automatic past 4 GB. No disk I/O on the audio thread (`AudioFormatWriter::ThreadedWriter`).

## Critical gotchas

### abl_link~ on macOS: NEVER build from source
Building `abl_link~` from source with current Xcode (libc++ 2100) produces a binary where Ableton Link's worker threads silently die after 16 tempo changes. The prebuilt binary at `src/prebuilt/abl_link~.pd_darwin` is from Deken (libc++ 1300) and works. `build-macOS.sh` handles this automatically. See `src/prebuilt/README.md` for the full diagnosis.

To refresh: install via Pure Data's Deken package manager, copy here, **verify** with `src/tools/abl-link-repro`. Linux is not affected.

### Faust compilation flags
`faust2puredata -vec -lv 0 -vs 4 -clang` — set in the build scripts. Don't change without understanding vectorisation implications.

### Only 3 Faust files are compiled
This branch removed OTT, digitaliser, widener, and limiter from the master bus. The build compiles only: `f_repeater.dsp`, `f_reverb.dsp`, `formuls.dsp`. `f_looper.dsp` was also deleted (unreferenced).

### The app runs a COPY of the Pd patch
`build-macOS.sh` copies `src/pd/` into the app bundle. Editing `src/pd/_main.pd` has **no effect on an already-built app** until you rebuild or copy manually:
```bash
diff src/pd/_main.pd /path/to/formuls.app/Contents/Resources/pd/_main.pd
```

### Faust `.lib` files are NOT Windows libraries
`ffx.lib`, `fsynth.lib`, `fosc.lib` in `src/faust/` are Faust source files. The `.gitignore` deliberately does not exclude `*.lib`.

### GUI JSON is 43k lines
`src/gui/_main.json` is the Open Stage Control session definition. Edits change the touchscreen interface. Be careful with search-and-replace.

### `src/app/Builds/` and `JuceLibraryCode/` are gitignored
Projucer regenerates both from `formuls.jucer`. Never commit them.

### `f.gui.modpack` is generated, not hand-wired
`src/pd/controlabstractions/f.gui.modpack.pd` is generated by `gui/tools/gen-modpack.py`. Don't edit it by hand.

## Pd patch: quitting from inside the patch
`FormulsEngine` subscribes to Pd receive symbol **`formuls-quit`**. The old `pd quit` was a no-op under libpd. Now:
```
[; formuls-quit bang(
```
The message arrives on the JUCE message thread via libpd's queued interface.

## Diagnostic tools

- **`src/tools/patch-osc-perf.py`**: Performance patches to vendored Open Stage Control. Serialises OSC once per broadcast (not per client). Opt-in `--batch-ms N` for WebSocket frame coalescing (off by default — adds client load). Both build scripts apply it automatically.
- **`src/tools/check-reset-coverage.py`**: Verifies every GUI parameter with chaos/LFO/mod sub-widgets is reset by `f.util.reset.pd`. Run after adding a parameter or renaming a widget. Exits non-zero on mismatch.
- **`src/tools/automation-probe.py`**: Records a gesture into one automater, reports value discontinuities. Use `--rev` to compare against a specific commit. No audio device or externals needed.
- **`src/tools/bpm-probe`**: Loads `_main.pd` under libpd, drives BPM over OSC, measures actual beat rate. Needs a built app's externals. Build instructions in `src/tools/README.md`.
- **`src/tools/brand-osc.sh`**: Rebrands Open Stage Control's header to formuls. Called by build scripts.
- **`src/tools/abl-link-repro`**: Reproduction harness for the abl_link~ Xcode bug.

## Key runtime details

- Audio: default 48kHz (44.1/88.2/96 also offered), 512 buffer frames, 6 polyphonic synths
- Sample rate adapts to what the device actually opens (status line reports if different)
- GUI: Open Stage Control at `http://127.0.0.1:9001`; app lists all reachable network addresses automatically
- Output channels: 2 (stereo) or 14 (individual synth + bus)
- Pd patch sends `formuls-quit` to close the app (not `pd quit`)
- Style all JUCE widgets in `src/app/Source/FormulsLookAndFeel.h` — it is the single place for colours, fonts, layout

## Branch context

- `docs/README.md` is the index to all juce-port work: efficiency refactor, GUI performance, recording, master bus cuts, branch map
- `docs/efficiency/` — Faust + Pd efficiency refactor with measured results
- `docs/gui/` — why the control GUI disconnects, what each fix is worth, the load rig
- `git log juce-port..main` shows four duplicate commits, not missing work
- `alacarte-prototype` branch was never merged and can be deleted
- The `FORMULS_TRACE` env var belongs to the discarded à la carte branch, not this one
