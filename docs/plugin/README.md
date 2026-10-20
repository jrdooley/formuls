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

`getStateInformation` stores two things (`EmbeddedGuiServer`, capture on):

- **the GUI state**: what the tablets show, as o-s-c state JSON. o-s-c keeps
  widget values in the browser, so after a tablet's change the server asks
  one tablet for its state (o-s-c's own `/STATE/SAVE`, answered to the server
  instead of written to a file), at most once a second;
- **the patch state**: the last message the patch was sent on each address,
  recorded as it goes by (tablet gestures, state loads, canvas recalls after
  the compounds module; not the `/GET` poll or `/STATE/*`).

On load the patch messages are replayed to Pd straight away, so the project
sounds right with no tablet open, and tablets that connect later are shown
the GUI state (`stateLoad` with `send: false`; the patch already has it).

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

Not yet tried in a DAW.

### Known gaps in the GUI half

These only affect what a tablet *shows* after a restore; the patch state is
replayed exactly either way.

- 8 controls share their id with the panel around them (7 faders and the
  Rolling Freezer transpose switch, e.g. `rollingfreezertranspose`). o-s-c
  keys state by id, so the panel's value overwrites the control's. The app's
  own Save State has the same problem; renaming those panels in the session
  fixes it.
- A widget with an explicit `value` in the session (5 faders, 18 switches,
  135 menus, ...) can show that value instead of the restored one, as
  `repeaterlevel` (`value: 0`) did while `repeatermono` (no `value`) showed
  the restored state. Likely o-s-c re-applying the property after the load;
  not yet confirmed.
- The GUI copy can be up to ~2 s behind the last gesture when the host saves.

## Limits of this first version

- **One formuls per host process.** libpd keeps its state in globals. A
  second instance stays silent and says so. Lifting this needs libpd built
  with `PDINSTANCE`, and the Faust and abl_link~ externals rebuilt for it
  (the prebuilt abl_link~ cannot be).
- 2 output channels only (the app also offers 14).
- No MIDI, no host automation, no tempo sync from the host (the patch has
  its own clock and Ableton Link).
- No CLAP: JUCE 9.0.1's Projucer has no CLAP target; it needs a CMake build
  with clap-juce-extensions.
- macOS only so far.
