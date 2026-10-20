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

## Verified

- `auval -v aumu Fmls Frml`: AU VALIDATION SUCCEEDED.
- A JUCE test host loading each format, rendering 4 s at 48 kHz/512 and
  sending `/testtone` through the plugin's GUI WebSocket, as a tablet would:
  all externals load (`formuls~`, `f_repeater~`, `f_reverb~`, `abl_link~`),
  output peaks at 0.0998 (the patch's 0.1 test tone), editor opens, clean
  unload. VST3 and AU alike.

Not yet tried in a DAW.

## Limits of this first version

- **One formuls per host process.** libpd keeps its state in globals. A
  second instance stays silent and says so. Lifting this needs libpd built
  with `PDINSTANCE`, and the Faust and abl_link~ externals rebuilt for it
  (the prebuilt abl_link~ cannot be).
- 2 output channels only (the app also offers 14).
- The GUI state is not saved with the host project
  (`getStateInformation` is empty).
- No MIDI, no host automation, no tempo sync from the host (the patch has
  its own clock and Ableton Link).
- No CLAP: JUCE 9.0.1's Projucer has no CLAP target; it needs a CMake build
  with clap-juce-extensions.
- macOS only so far.
