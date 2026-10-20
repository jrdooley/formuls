# formuls as a plugin (VST3 / AU)

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
  `Contents/Resources`.

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

## Diagnostics

Every instance logs to `~/Library/Logs/formuls/plugin.log` (1 MB, trimmed):
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
- No MIDI, no host automation, no tempo sync from the host (the patch has
  its own clock and Ableton Link).
- No CLAP: JUCE 9.0.1's Projucer has no CLAP target; it needs a CMake build
  with clap-juce-extensions.
- macOS only so far.

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
