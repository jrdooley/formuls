# formuls as a plugin (VST3 / AU / LV2)

Branch `ws-prototype`. An instrument plugin that runs the same Pd patch as the
app and serves the same control GUI. The GUI is still played in a browser or
on a tablet; the plugin's window shows where to open it.

## Build

```bash
./build-macOS.sh            # once: the app, whose pd/ and gui/ the plugin reuses
./build-plugin-macOS.sh     # formuls-0.3.1.vst3 and formuls-0.3.1.component
INSTALL=1 ./build-plugin-macOS.sh   # also copies them into ~/Library/Audio/Plug-Ins
```

The script takes `pd/` (with the compiled Faust and abl_link~ externals) and
the flattened `gui/` from the app bundle, leaves node out, and puts libpd in
each bundle's `Contents/Frameworks`. `FORMULS_APP=/path/to/formuls.app`
points it at another app build.

On Linux, VST3 and LV2:

```bash
./build-linux.sh            # once: the app folder, whose pd/ and gui/ the plugin reuses
./build-plugin-linux.sh     # formuls-0.3.1-linux-<arch>-plugins/{formuls.vst3, formuls.lv2}
INSTALL=1 ./build-plugin-linux.sh   # also copies them into ~/.vst3 and ~/.lv2
```

It takes `pd/` (with the `.pd_linux` externals) and `gui/` from
`formuls-0.3.1-linux-<arch>/` (or `FORMULS_APP=`), and puts `libpd.so` next
to the plugin binary in each bundle, found through the `$ORIGIN` rpath:

- `formuls.vst3/Contents/<arch>-linux/{formuls.so, libpd.so}`, resources in
  `formuls.vst3/Contents/Resources/{pd, gui}`;
- `formuls.lv2/{formuls.so, libpd.so, *.ttl, pd, gui}`.

The bundles keep the plain name `formuls`: a Linux VST3 host looks for
`Contents/<arch>-linux/<bundle name>.so`, so a renamed bundle does not load.
The LV2 URI is `urn:net.formuls:formuls`; hosts save projects against it, so
it must not change.

On Windows, VST3, from a Developer Command Prompt for VS 2022:

```bat
build-windows.bat           :: once: the app folder, whose pd\ and gui\ the plugin reuses
build-plugin-windows.bat    :: formuls-0.3.1-windows-x64-plugins\formuls.vst3
```

`set INSTALL=1` first also copies it into
`%LOCALAPPDATA%\Programs\Common\VST3` (the per-user VST3 folder; the
all-users `C:\Program Files\Common Files\VST3` needs an administrator).
The bundle holds `Contents\x86_64-win\{formuls.vst3, libpd.dll,
pthreadVC3.dll}` and `Contents\Resources\{pd, gui}`.

Windows resolves a DLL's imports from the *host's* folder, so the plugin
delay-loads `libpd.dll` (`/DELAYLOAD` in the .jucer) and
`SharedEngine::acquire()` loads the bundle's copy by full path before the
engine exists. The externals import `libpd.dll` by name and get that copy:
Windows reuses an already loaded module of the same name. The .bat files
are checked out with CRLF (`.gitattributes`).

The .bat builds the three Visual Studio projects one at a time: Shared Code
(x64), the VST3 manifest helper (Win32, the only platform JUCE builds it for)
and the VST3 (x64), with `SolutionDir` set. Building the solution's
`formuls_-_VST3` target does not work: msbuild passes that target name on to
every dependency, which fails with MSB4057.

Sources: `src/plugin/` (processor and editor) plus the app's own
`FormulsEngine`, `OscBridge`, `PdOscOut`, `EmbeddedGuiServer` and
`WebSocketServer`, compiled into the plugin unchanged
(`src/plugin/formuls-plugin.jucer`).

## How it fits together

- `FormulsEngine::startHosted()` boots libpd with no audio device, and
  `processHosted()` renders the patch into the host's buffer in any block
  size (libpd's 64-frame ticks carried across calls). The app's device path
  uses the same `bootPd()` / `render()`.
- The GUI server takes the first free port from 9001 to 9020, so the plugin
  can run next to the app or in two hosts at once.
- A host loads plugins `RTLD_LOCAL`, which would hide libpd's symbols from
  the externals (built with `-undefined dynamic_lookup`). The processor
  re-opens libpd `RTLD_GLOBAL` before the patch loads.
- `ResourceLocator` finds `pd/` and `gui/` in the plugin's own
  `Contents/Resources` (macOS, Linux VST3), or next to the binary (LV2).

## Saved with the host project

> **Switched off for now** (`SharedEngine::saveProjectState = false`): the
> restore is not reliable yet. formuls stores nothing in the host project,
> ignores state stored by an earlier build, and every load starts from the
> defaults set by the patch's start-up reset (`resetglobal-in`). The rest of
> this section describes the feature as it works when switched back on.

`getStateInformation` stores two things (`EmbeddedGuiServer`, capture on):

- **the GUI state**: what the tablets show, as o-s-c state JSON. o-s-c keeps
  widget values in the browser, so after a tablet's change the server asks
  one tablet for its state (o-s-c's own `/STATE/SAVE`, answered to the server
  instead of written to a file), at most once a second;
- **the patch state**: the last message the patch was sent on each address,
  recorded as it goes by (tablet gestures, state loads, canvas recalls after
  the compounds module; not the `/GET` poll or `/STATE/*`).

On load the patch messages are replayed to Pd, so the project sounds right
with no tablet open, and tablets that connect later are shown the GUI state
(`stateLoad` with `send: false`; the patch already has it).

The replay waits for the patch to finish starting: `_main.pd`'s
INIT_PARAMETER_SET does a global reset (`resetglobal-in` 1 then 0) 5 s after
loading, which would undo anything sent before it. The plugin restores on
the first `resetglobal-in 0` (FormulsEngine::onGlobalReset), with a 10 s
fallback. Before this, restored values were reset by the patch and the patch
then reported its defaults to the tablets, so e.g. Repeater Level came back
at 0 in both the sound and the GUI.

Replaying what the patch actually received, rather than working it out again
from the session, is deliberate: matrix children, rounding to each widget's
`decimals` and string values only exist in the browser. A first version that
derived the messages from the session sent 2,343 values with 187 Pd errors
and missed 947 matrix-child addresses; the replay is exact by construction.

## Verified

- `auval -v aumu Fmls Frml`: AU VALIDATION SUCCEEDED.
- A JUCE test host loading each format, rendering 4 s at 48 kHz/512 and
  sending `/testtone` through the plugin's GUI WebSocket, as a tablet would:
  all externals load (`formuls~`, `f_repeater~`, `f_reverb~`, `abl_link~`),
  output peaks at 0.0998 (the patch's 0.1 test tone), editor opens, clean
  unload. VST3 and AU alike.

- State, with the test host saving and restoring as a DAW does, and the GUI
  in a browser: a project saved after changing Mono and Repeater Level
  restores 5,105 patch messages with no Pd errors and no tablet connected;
  a tablet that connects afterwards gets the saved GUI state; saving the
  restored project again gives the same 3,445 GUI values and 5,105 messages.

- Hosts that keep an idle instance: the test host creating and preparing a
  first instance it never processes, then playing a second at 128-sample
  blocks, gets the test tone at 0.0999 (silent before `SharedEngine`); auval's
  log shows the instance it drives rendering every block.

- Linux (Ubuntu 24.04 x86_64, OrbStack VM), the same JUCE test host built
  with VST3 and LV2 hosting: both bundles load, all four externals load
  (Pd's symbols reach them through the `RTLD_GLOBAL` re-open), and the
  `/testtone` output peaks at 0.0998. Not yet run: the editor window (the VM
  has no display), a real Linux DAW, MIDI input.

- Windows (CI, `windows-latest`, MSVC 17.14), the same test host with VST3
  hosting: the bundle loads (`libpd.dll` delay-loaded from
  `Contents\x86_64-win`), the patch becomes ready and the `/testtone`
  output peaks at 0.0998 over 375 blocks, with a clean unload ("engine
  released"). JUCE's manifest helper, which loads the plugin to write
  `moduleinfo.json`, passes too. The only compiler warnings are C4458 in
  libpd's `PdBase.hpp`. The editor opens in a real desktop window (520x300)
  with the plugin's own child window inside the host's, and CI fails if it
  does not (`PROBE_SHOW_EDITOR`, `PROBE_REQUIRE_EDITOR`); a capture is
  uploaded as `windows-plugin-editor-snapshot`. Not yet run: a real Windows
  DAW, MIDI input.

- CI (`.github/workflows/release.yml`): the Linux and Windows jobs build the
  plugins after the app and run `src/tools/plugin-probe` on every bundle,
  failing below a 0.05 peak. The plugins are uploaded as their own artifacts
  (and land in the draft release on a tag).

## Diagnostics

Every instance logs to `~/Library/Logs/formuls/plugin.log` on macOS and
`~/.config/formuls/plugin.log` on Linux and `%APPDATA%\formuls\plugin.log`
on Windows (1 MB, trimmed):
creation and host, `prepareToPlay` (rate, block size, channels), engine and
GUI start-up, Pd's own messages (externals, Link, errors), and every 5 s how
many blocks the host called, how many this instance rendered, and the peak.
Hosts run plugins in their own process, so this is the place to look when a
host misbehaves.

### Known gaps in the GUI half

These only affect what a tablet *shows* after a restore; the patch state is
replayed exactly either way.

(Fixed: 8 controls, e.g. the Rolling Freezer transpose switch, shared their
id with the panel around them, so in o-s-c's id-keyed state the panel's value
overwrote theirs. Those panels, and the sequencer / swing panels that shared
ids with their modals, now end in `panel`.)

- The GUI copy can be up to ~2 s behind the last gesture when the host saves.

## Limits of this first version

- **One formuls per host process.** libpd keeps its state in globals, so
  the engine and its GUI server belong to the process (`SharedEngine`), not
  to an instance. Whichever instance the host actually plays renders it: if
  the one that rendered last has not been called for 250 ms, the next one
  takes over. Hosts that keep an extra instance alive (auval does; SoundDesk
  evidently did) therefore work. Two instances that are both playing cannot
  each have a formuls: the second stays silent and says so. Lifting this
  needs libpd built with `PDINSTANCE`, and the Faust and abl_link~ externals
  rebuilt for it (the prebuilt abl_link~ cannot be).
- 2 output channels only (the app also offers 14).
- MIDI note-ons only (see Keyboard input); no host automation or MIDI CC,
  by design, and no tempo sync from the host (the patch has its own clock
  and Ableton Link).
- No CLAP: JUCE 9.0.1's Projucer has no CLAP target; it needs a CMake build
  with clap-juce-extensions.
- Windows: VST3 only, built and checked by CI (see Verified); not yet tried
  in a real Windows DAW.

## Max-based hosts (SoundDesk)

Pd externals are linked flat-namespace: they look up `class_new`, `gensym`,
`outlet_new`, `post` and the rest of Pd's API in whichever loaded image
exports them first. Max-based hosts such as SoundDesk export Max's C API,
which uses the same names, from their own executable, so the externals
registered their classes with Max. Pd never saw `formuls~`, the Faust
effects or `abl_link~`, logged `maximum object loading depth 1000 reached`
for each one, and the patch ran silent with no Link clock.

`build-plugin-macOS.sh` runs `src/tools/bind-externals-to-libpd.py` on the
bundle's externals. The tool binds every symbol that libpd exports to the
bundle's libpd, and leaves all other symbols as they were. It patches the
binaries in place, so `abl_link~` stays the prebuilt binary. Each external
keeps only the build architecture and is signed again. A test host whose
executable exports Max-like stand-ins reproduced the failure. With the
rebound externals, everything loads and the test tone plays.

## Keyboard input (on-screen and MIDI)

Each synth has a small **keyboard** (icon) button next to **CHAOS SLEW** and
**EVOLVE TIME**, at the top of its sequencer column. The keyboard is a
secondary way of playing the oscillator, so its button sits with the other
secondary popups, and the main panel is unchanged. The button opens the
keyboard popup (the on-screen keyboard, RECORD, RECORD CLEAR and TRANSPOSE),
which also holds two buttons per synth:

- **MIDI IN**: off by default, and switched off again on every reset. While
  it is on, every MIDI note-on, on any channel, plays the synth like a tap
  on the on-screen keyboard. Several synths with MIDI IN on play the same
  notes, layered. The on-screen keyboard always plays.
- **QUANTISE** decides the tuning for both keyboards:
  - *off*: every note plays at its equal-tempered pitch;
  - *on*: a note whose pitch class (counted from the root) is active in the
    PITCH GAMUT takes that slot's tuning, in its own octave; notes on
    inactive pitch classes keep their equal-tempered pitch.

  It is off by default and on every reset. Root F with C, E and F active is
  F, A and B♭, each at its slot's tuning.

**How keyboard notes reach the oscillator:**
- They enter its pitch chain after Freq Snap, so Freq Snap never re-snaps
  them.
- They update the tablet's pitch display and Filter Pitch Track, and
  trigger the synth, as the on-screen keyboard always did.
- The keyboard performance recording plays back through the same QUANTISE
  setting.

**Reset:** a synth's reset, and the global reset, clear that synth's
keyboard recording, as RECORD CLEAR does. The recorder used to listen for
`resetGlobal`, which nothing sends, so it was never cleared.

**Where the notes come from:**
- the plugin plays the host's MIDI;
- the app listens to every connected MIDI input, including ones plugged in
  while it runs.

**Note handling:**
- Note-offs and velocity are ignored: a triggered note plays the synth's own
  envelope at formuls's own velocity.
- MIDI notes are not recorded into the keyboard performance recording.

A synth trigger is a toggle that Faust reads once per 64-sample tick, so two
triggers in one tick cancel out. The engine therefore hands Pd at most one
note-on per tick (`FormulsEngine::queueNoteOn`). A chord is spread over a few
ticks (1.3 ms each at 48 kHz), and every note sounds. In the plugin, notes
are taken at the start of the host block they arrive in, not at their exact
sample position.

**Implementation:**
- Pd, all in `f.util.oscinparse.pd`, one per synth:
  - `MIDI_KEYBOARD___`, in the ADSR_VEL_TRIG message chain: the
    free/quantised step for both keyboards;
  - `$0-key-pitch` in `FREQ_SNAP_NOTE_GEN__`: where keyboard pitches enter
    after the snap;
  - `$0-key-trig` in `OP1FREQ_PITCH_QUANTISE`: the trigger.
- Plugin: `SharedEngine::render`.
- App: `MainComponent::handleIncomingMidiMessage`.

Parameter control from the host (automation, MIDI CC) is deliberately not
offered: formuls's parameters are played from the tablet.
