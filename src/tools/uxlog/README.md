# UX logging build

A study variant of formuls that records how people use the touchscreen GUI:
where they touch, what they navigate to, and where they struggle with widgets.

## Build

```bash
FORMULS_UXLOG=1 ./build-macOS.sh
```

This produces `formuls-<version>-uxlog.app`. The GUI header reads `<version>-uxlog`,
so participants and you can always tell which build is running. A normal build
contains none of this: the app turns logging on only if
`gui/formuls-uxlog-module.js` is in the bundle.

## What is recorded

Logs go to `~/Music/formuls/uxlog/uxlog-YYYY-MM-DD-HHMMSS.jsonl`: one file per
app start, one JSON event per line. Each event has two timestamps: `t` (the
tablet's clock) and `st` (the computer's clock). Use `st` when comparing tablets.

| event | meaning |
|---|---|
| `down` / `move` / `up` / `cancel` | finger or pen contact; `move` is thinned to 30 Hz per finger |
| `down.miss` | the touch landed on no interactive widget (dead space) |
| `leave` | a drag slid off the widget it started on |
| `down.param` / `layer` / `cell` / `modes` | on a slider canvas: the parameter, which layer the touch adjusts (`main`, `chaos`, `lfofreq`, `lfodepth`, `moddepth`, `mod` cell 0-5, `label`) and which mode buttons were on. The layer is worked out with slider-lib.js's own rule, because the mode buttons choose it, not the touch position |
| `up` summary | duration, path length, value before and after, `changes`, `reversals` (overshoot-and-correct) |
| `tab` | a tab switch (navigation) |
| `param` | every value the GUI sent to the sound engine |
| `in` | values the sound engine sent to the GUI (LFOs, chaos, automation, sequencer): up to 5 frames a second, changed values only (`FORMULS_UXLOG_IN_HZ` to change) |
| `snap` | every widget's value: when a tablet connects, every 5 minutes, after a preset load |
| `preset` | a preset load from the app window |
| `client`, `hidden`/`visible`, `resize`, `orientation` | session context |

### Session audio

The engine's stereo output is recorded from Start to Stop (or quit) to
`uxaudio-YYYY-MM-DD-HHMMSS.ogg` in the same folder. It is Ogg Vorbis at quality
0.7 (~220 kbps): about 100 MB an hour, against about 1 GB for WAV. No save
dialog is shown, and the record button reads "Recording session" and is locked
so the take can't be stopped by mistake. Ogg is written as independent pages, so a
crash still leaves a playable file up to that point.

The log gets `rec-start` and `rec-stop` events (sent by the app). Sample 0 of the
file is at `first_block_st`, accurate to about one audio buffer (~11 ms at 512
frames/48 kHz). `rec-stop.samples` lets you check for drift against the sample
rate. Each tablet's clock is mapped to the computer's using `ft`, the time each
batch was sent, so gestures from any tablet land in the right place in the audio.

No personal data are recorded, apart from the session audio itself. The browser's user-agent string is
recorded, though. Tell participants the session is being logged.

## Analyse

```bash
python3 src/tools/uxlog/analyse-uxlog.py ~/Music/formuls/uxlog/*.jsonl \
    --layout src/gui/_main.json --out uxlog-report
```

Add `--audio` (needs `pip install numpy soundfile`) for `sound.csv`: for each
gesture, the loudness and spectral centroid before, during and after it, and how
much it changed them.

`sliders.csv` (each slider parameter split by layer) and `modes.csv` (each layer
across all parameters) show how people use the mode layers. Also produced: `widgets.csv` (hot spots and difficulty per widget), `misses.csv`, `tabs.csv`,
`gestures.csv`, `untouched.txt` (cold spots), `heatmap.html`. See the script's
docstring for what each column means.

## Replay

A uxlog build has a **Replay Session...** button in the app window, available
while formuls is stopped. Choose a `uxlog-*.jsonl`, and the app:

- plays its `uxaudio-*.ogg` through the selected output device, with Play/Pause
  and a seek bar in the app window. No sound engine runs.
- starts the GUI as usual. Open it in a browser (the addresses are listed) and
  the widgets move as they did in the session, in time with the audio:
  - **Your values:** everything the participants sent, plus the sound engine's
    modulation at 5 Hz.
  - **Tabs:** the tabs follow one tablet's view.
  - **Touches:** drawn over the widgets: an orange dot and trail per finger, a
    blue ring for a touch on dead space, a red flash where a drag slid off its
    widget.
- shows a panel in the browser, bottom right: which tablet to follow, the
  touch overlay on or off, and the session time.

Seeking restores the latest state snapshot before the new position and
applies everything logged since, so you can jump anywhere. Nothing you do in
the GUI during a replay is sent or recorded. Replay draws widget state, not
pixels, so momentary visual feedback that isn't widget state (a press
highlight, a menu that opened and closed) isn't shown.

## How it works

- `formuls-uxlog-client.js` is loaded into the GUI page. It uses passive,
  capture-phase pointer listeners, so it never interferes with widgets. It
  finds the widget under a touch through `container._widget_instance` and
  sends batches every 500 ms over the GUI's existing WebSocket.
- `formuls-uxlog-module.js` is the Open Stage Control `--custom-module`. It
  writes the log and drops the logger's messages before they reach Pd.
  It wraps `formuls-module.js` if a build ships one.
- `formuls-replay-module.js` replaces the logging module in Replay mode. The
  app sends it the audio position 20 times a second, and it sends the GUI the
  logged values up to that moment through the same path Pd's messages take.
  `formuls-replay-client.js` draws the touch overlay; in a live session it does
  nothing.
- `../patch-osc-uxlog.py` performs the build-time edits and fails the build
  if the Open Stage Control text it matches has changed.
