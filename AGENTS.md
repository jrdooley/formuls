# formuls — Agent Guide

Electronic musical instrument: 6-operator FM synth with touchscreen GUI.
Stack: JUCE (native app), libpd (embedded audio), Faust (DSP), Pure Data (patches), Open Stage Control (web GUI).

**This is `main`.** The JUCE rewrite replaced the old Python/Tkinter + RtAudio
architecture and was merged long ago; the `juce-port` branch no longer exists, so
anything describing this as a side branch is out of date. `docs/README.md` has
the history.

The app's own runtime is C++ only — no Python, no Tkinter. **Python 3 is still a
build dependency on every platform**, though, so "Python is no longer a
dependency" (as older notes put it) is only true of the runtime:

| script | runs |
|---|---|
| `build-macOS.sh`, `build-linux.sh` | `patch-osc-perf.py`, `flatten-session.py`, `compile-compounds.py` |
| `build-plugin-macOS.sh` | `bind-externals-to-libpd.py` |
| `build-windows.bat`, `build-plugin-windows.bat` | `jucer-module-path.py` (and need `python` or `python3` on PATH) |

## Licensing

GPL-3.0-or-later (`LICENSE`). Third-party components and why each copyleft one is
compatible: `THIRD-PARTY-NOTICES.md`. JUCE is used under a **commercial** JUCE
licence, not its AGPL option — that is why GPLv3 rather than AGPLv3 is possible.

- New source files get a two-line SPDX notice after any shebang: see
  `CONTRIBUTING.md`. `#` for shell and Python, `REM` for `.bat`, `//` for C++,
  JavaScript and Faust.
- `src/libs/` (submodules) and `src/prebuilt/` are upstream work: never stamp or
  reformat them. `docs/**/tools/` are one-off analysis scripts and are not stamped.
- Commits need `git commit -s` (DCO sign-off). Sole copyright is what keeps the
  relicensing options open, so this is not decoration.
- Ableton Link (GPLv2-or-later, inside `abl_link~`) is the binding copyleft
  dependency and is **not** optional: `src/pd/_main.pd` instantiates it.

## Build

**Prerequisites**: Xcode + CLI tools, JUCE at `~/JUCE` (with Projucer), faust, wget. Clone with submodules:
```
git clone --recurse-submodules https://github.com/jrdooley/formuls.git
```

**Full build (macOS)**:
```
./build-macOS.sh
```
Produces `formuls-<VERSION>.app` in the repo root, where `VERSION` is set at
the top of the build script (currently `0.3.3`). Steps:
1. Compiles 3 Faust externals: `f_repeater.dsp`, `f_reverb.dsp`, `formuls.dsp`
2. Copies **prebuilt** `abl_link~.pd_darwin` (never builds from source on macOS)
3. Downloads Open Stage Control 1.31.0 + Node 22.17.0
4. Runs `brand-osc.sh` and `patch-osc-perf.py` on the downloaded O-S-C package, then `flatten-session.py` to write the bundle's `_main.json`
5. Builds libpd with `UTIL=true EXTRA=true`, sets `@rpath` install name
6. Runs Projucer (`--resave src/app/formuls.jucer`) then `xcodebuild`
7. Assembles self-contained `.app` bundle, codesigns, cleans up

**Linux**: `./build-linux.sh` — same flow, needs Linux Projucer build, produces a
directory not `.app`. This is how the appliance images are built:
formuls-embedded's `scripts/build-formuls.sh` runs it inside an x86-64 machine
for every image, so it is exercised on Linux each time one is cut.

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
| `src/tools/` | mixed | `brand-osc.sh`, `patch-osc-perf.py`, `flatten-session.py` (+ `osc-defaults-1.31.0.json`), `check-reset-coverage.py`, `automation-probe.py`, `bpm-probe`, `abl-link-repro` |
| `docs/` | markdown/html | Efficiency report, GUI performance write-up, session logs |

### Signal flow
```
Faust DSP (formuls.dsp)  →  fsynth.lib → ffx.lib  →  Pd patch (_main.pd)
OSC from GUI ─────────────────────────────────────→  libpd (inside JUCE app)
libpd ──→ juce::AudioIODeviceCallback ──→ audio output
```

### Recording
Record button in the app window, and an **AUDIO RECORD** toggle on the GUI's RESET tab (`/audiorecord`, intercepted in `OscBridge` like `/poweroff`) — the appliance is headless, so the tablet is the only way to record there. Both drive the same `startRecording()`/`finishRecording()`; a take started from the tablet skips the save dialog.

Output passes through `SafetyLimiter` (ceiling −1 dBFS, 128 samples of latency, `FORMULS_LIMITER=off` to bypass) in `FormulsEngine::render()` before the meters and the device; the recorder taps *before* the limiter by default, so a take is the engine's own output and may exceed 0 dBFS (float stores it exactly; `FORMULS_RECORD_PRE_LIMITER=0` records the limited signal); `src/tools/limiter-probe.cpp` checks it with no JUCE or libpd. Writes 32-bit float WAV (float so any overshoot upstream of the limiter is not clipped into the file — `docs/headroom/README.md`) to `~/formulsAudioRecordings/` during the take, or to a mounted USB stick on the appliance — `RecordingLocation.h` decides, and explains why. Save dialog only moves the file. Channel count matches the selected output (2 or 14). RF64 automatic past 4 GB. No disk I/O on the audio thread (`AudioFormatWriter::ThreadedWriter`).

Recording refuses to start, and stops cleanly if already running, when the destination has less than 256 MB free (`FORMULS_RECORDING_MIN_FREE_MB`, 0 disables). That guard is about the machine, not the take: on the appliance the fallback destination is the root filesystem.

The plugin compiles the same GUI server but has no transport to record, so the server hides the button unless the host calls `setRecordingAvailable(true)`.

## Critical gotchas

### abl_link~ on macOS: NEVER build from source
Building `abl_link~` from source with current Xcode (libc++ 2100) produces a binary where Ableton Link's worker threads silently die after 16 tempo changes. The prebuilt binary at `src/prebuilt/abl_link~.pd_darwin` is from Deken (libc++ 1300) and works. `build-macOS.sh` handles this automatically. See `src/prebuilt/README.md` for the full diagnosis.

To refresh: install via Pure Data's Deken package manager, copy here, **verify** with `src/tools/abl-link-repro`. Linux is not affected.

### Faust compilation flags
`faust2puredata -vec -lv 0 -vs 4 -clang` — set in the build scripts. Don't change without understanding vectorisation implications.

### Only 3 Faust files are compiled
The JUCE rewrite removed OTT, digitaliser, widener, and limiter from the master bus. The build compiles only: `f_repeater.dsp`, `f_reverb.dsp`, `formuls.dsp`. `f_looper.dsp` was also deleted (unreferenced).

### The app runs a COPY of the Pd patch
`build-macOS.sh` copies `src/pd/` into the app bundle. Editing `src/pd/_main.pd` has **no effect on an already-built app** until you rebuild or copy manually:
```bash
diff src/pd/_main.pd /path/to/formuls.app/Contents/Resources/pd/_main.pd
```

### Faust `.lib` files are NOT Windows libraries
`ffx.lib`, `fsynth.lib`, `fosc.lib` in `src/faust/` are Faust source files. The `.gitignore` deliberately does not exclude `*.lib`.

### GUI JSON is 43k lines
`src/gui/_main.json` is the Open Stage Control session definition. Edits change the touchscreen interface. Be careful with search-and-replace.

### The app ships a *flattened* `_main.json`
The build does not copy `src/gui/_main.json` into the bundle: `src/tools/flatten-session.py` writes an equivalent session with the clones inlined and `@{parent.variables...}` resolved (load 10.9 s -> 3.6 s, `docs/gui/session-size.md`). Edit the source, never the bundle's copy. Template tabs `formuls0` and `Widgets` are kept as empty placeholders because Pd reads the synth from the root tab **index**. A dev build loads the unflattened source (same behaviour, slower). Upgrading Open Stage Control stops the build until `src/tools/osc-defaults-<version>.json` is regenerated (see `src/tools/README.md`).

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

- **`src/tools/compile-compounds.py`** + **`src/gui/formuls-module.js`**: Compile slider compounds to single canvases; the server module keeps Pd's addresses unchanged. The app loads the module only when `compounds.json` is present (a build).
- **`src/tools/flatten-session.py`**: Writes the shipped, flattened session (clones inlined, static `@{}` resolved). Both build scripts run it. Fails the build on anything it does not model.
- **`src/tools/patch-osc-perf.py`**: Performance patches to vendored Open Stage Control. Serialises OSC once per broadcast (not per client), and makes tab show resize only canvases whose size changed. Opt-in `--batch-ms N` for WebSocket frame coalescing (off by default — adds client load). Both build scripts apply it automatically.
- **`src/tools/check-reset-coverage.py`**: Verifies every GUI parameter with chaos/LFO/mod sub-widgets is reset by `f.util.reset.pd`. Run after adding a parameter or renaming a widget. Exits non-zero on mismatch.
- **`src/tools/automater-load-bench.py`**: CPU cost of N automaters with playback/LFO/chaos running, in deterministic `pd -batch`. Use `--rev` to compare against a commit.
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

## History and other branches

- `docs/README.md` is the index to the JUCE rewrite's work: efficiency refactor, GUI performance, recording, master bus cuts, branch map
- `docs/efficiency/` — Faust + Pd efficiency refactor with measured results
- `docs/gui/` — why the control GUI disconnects, what each fix is worth, the load rig
- `docs/claude/memory/` — working notes a Claude session keeps for itself, copied
  into the repo; a snapshot of `~/.claude/projects/<project>/memory/`, so it can drift
- The `juce-port` branch is gone (merged into `main`), so any `juce-port..main`
  comparison in older notes cannot be run. The four duplicate commits it used to
  show were duplicates, not missing work.
- **Do not delete `alacarte-prototype`.** An older note here said it could go.
  It holds **21 commits that exist on no other branch** (checked 11 Nov 2026:
  `git rev-list origin/alacarte-prototype --not <every other origin ref>`), so
  deleting it loses them. Every other remote branch also still has unmerged
  work; `main`, `uxlog` and `uxlog-fx-strip-backup` are the live ones, and
  `uxlog` is load-bearing for `formuls-embedded`, which pins `origin/uxlog`.
- Before deleting any branch, test containment with
  `git rev-list <branch> --not --remotes=origin`, not just
  `git merge-base --is-ancestor`: the ancestor test only asks about the tip and
  has under-reported unmerged commits here before.
- The `FORMULS_TRACE` env var belongs to the discarded à la carte branch; it is
  referenced nowhere in this tree's code and does nothing here.
