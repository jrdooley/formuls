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
| `up` summary | duration, path length, value before and after, `changes`, `fturns_x`/`fturns_y` (direction changes of the finger, ignoring under 4 px), `addr` (the address the control sends), `reversals` (of the displayed value; unreliable on sliders, see below) |
| `tab` | a tab switch (navigation) |
| `param` | every value the GUI sent to the sound engine |
| `in` | values the sound engine sent to the GUI that **modulate the sound** (LFOs, chaos, automation, `extmodulation`): up to 5 frames a second, changed values only (`FORMULS_UXLOG_IN_HZ` to change) |
| `disp` | **display feedback**: values the engine sent to the GUI that the player never sets and that change no sound — the sequencer playhead (`/seqpos*`), the step it is on (`/sequencerstep*`) and the pitch readout (`/oscpitchgui*`). Framed exactly like `in`, in its own stream |
| `grab` | the GUI telling the patch to stop echoing an address back while a finger is on that widget (`/grab/<address> 1` on touch, `0` on release; see `f.util.guiinhibit` in the patch). Not a parameter change, so it is kept out of `param` — and it is the GUI's own account of when each touch began and ended, so `down`/`up` can be checked against it |
| `snap` | every widget's value: when a tablet connects, every 5 minutes, after a preset load |
| `spec` | **spectral descriptors**: every engine channel's level (`db`) and its spectral centroid, spread, flatness, irregularity and skewness, at `FORMULS_UXLOG_SPEC_HZ` frames a second (default 5). Written by the app, not the tablet. `spec-start` records the frame rate, window size, channel count and sample rate. See `docs/uxlog-spectral/README.md` |
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

Difficulty is measured on **adjustments**. **Sweeps** (deliberate back-and-forth
playing: long, with finger travel well beyond net movement, and several direction
changes) are counted separately; `--sweep-ms`, `--sweep-ratio` and
`--sweep-turns` tune the rule. Reversals are counted in the values the
participant's tablet **sent** (`sent_rev_*`) and in the finger's movement
(`finger_turns_*`), each as mean and median. They are not taken from the
widget's displayed value, which the sound engine's echoes overwrite mid-drag. A
straight drag on a slider showed up to 15 false reversals that way.

**The spectral stream is read too.** `spectral.csv` gives one row per gesture
per engine channel — the channel's level, centroid, spread, flatness,
irregularity and skewness averaged over the frames before the gesture and
after it, and the change — and `channels.csv` summarises each channel over the
whole session. No audio is needed: the numbers come from the log's `spec`
frames. Channels are named from the patch's map (1–2 master, 3–4 reverb, 5–6
repeater, 7–8 rolling freezer, 9–14 the six synths), so a synth's own timbre
can be followed while its parameters are adjusted. Two things to hold on to
when reading it, both from `docs/uxlog-spectral/README.md`: the synth channels
are taken **before** the master amp while 1–8 are after it, so a master-volume
gesture moves the level on one group and not the other; and a note reaches the
master through the effect returns, so the channels are not independent sources
and must not be averaged into one figure. The frames are sparse — at 5 Hz a
500 ms window holds two or three — so each figure is a mean over a handful of
frames, not a reading.

`sliders.csv` (each slider parameter split by layer) and `modes.csv` (each layer
across all parameters) show how people use the mode layers. Also produced: `widgets.csv` (hot spots and difficulty per widget), `misses.csv`, `tabs.csv`,
`gestures.csv`, `untouched.txt` (cold spots), `heatmap.html`. See the script's
docstring for what each column means.

**Display feedback is a separate stream.** The sequencer playhead, the step
number and the pitch readout are the interface reporting on itself: the player
does not set them and they change no sound. They are also among the densest
addresses in a log — over the three pilot sessions they were **28.5% of all
engine-to-GUI updates, from just 12 addresses** — so counting them as
modulation makes every gesture look as though it were made into a running
modulation network. They therefore go to `disp` and never into `in`. The list
of what counts as display feedback is `DISPLAY_FEEDBACK` at the top of
`formuls-uxlog-module.js`; add to it there rather than filtering downstream.
Replay reads both streams, since both are widget values it has to redraw.

**Every gesture is tagged with a regime**, and `widgets-by-regime.csv` splits
the per-widget measures by it. `static` means nothing in the `in` stream
changed within `--regime-ms` (1 s) of the gesture, so the player's hand was the
only thing moving the sound and `sound.csv`'s before-and-after really is that
gesture's. `active` means modulation was running (`mod_n` counts the
addresses), so the same difference mixes the gesture with the network, and
where the network feeds back on itself the two cannot be separated at all.
Pooling the two into one median per widget describes neither: over the pilot
sessions, the median gesture changed loudness by 0.7 dB in the static regime
and 2.2 dB in the active one, and the pooled figure was the active one in
disguise, because 196 of 226 gestures were made with the engine modulating.
A log recorded before the `disp` split still carries display feedback in `in`;
the analyser moves those addresses out and says so, so logs from either side of
that change are tagged the same way.

Two things the analyser does that are worth knowing about, because both decide
headline numbers:

- **Tab taps are not misses.** Builds before 2026-10-23 logged a tap on a tab
  bar as dead space, so a log from one of them would report the interface's
  most successful interaction as a failure. The analyser drops a miss that
  matches the tab switch it caused, within `--tab-miss-ms` (250 ms by default),
  and reports how many it dropped. Logs from later builds have none to drop:
  the client names the widget `tab:<label>` itself.
- **`untouched.txt` is built from the logs, not from `_main.json`.** Most ids
  in the session file are templates (`@{parent.variables.name}...`) that o-s-c
  fills in per clone at run time, so reading the file gives mostly names no
  widget ever has. The cold-spot list is therefore the ids the logs show were
  really in the page — the `snap` state snapshots, plus every widget touched —
  and `--layout` only adds the file's *literal* ids, counting the templated
  ones in the list's header. Because a snapshot only sees widgets the page has
  built, **open every tab during a session** or the widgets in an unopened tab
  will be missing from the universe rather than listed as cold.

## Driving a build without touching it

Useful for checking the log end to end. A build with
`GCC_PREPROCESSOR_DEFINITIONS` including `FORMULS_AUTOSTART_TEST=1` starts the
engine a second after launch, and these pick what it starts with:
`FORMULS_TEST_DEVICE` (first output device whose name contains this text --
point it at a virtual cable to keep a test off the speakers),
`FORMULS_TEST_CHANNELS`, `FORMULS_TEST_SAMPLERATE`,
`FORMULS_TEST_RECORD_SECONDS`.

The engine then takes OSC on udp 9000 exactly as the GUI's would:
`/testtone 1` plays a 100 ms 440 Hz blip through channels 1-8,
`/volumemaster <0-1>` is the master volume, and `/pd 1` quits -- which matters,
because the session Ogg is only written on a clean stop.

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

  **If you change this file, the `?v=` hash must change with it.**
  `patch-osc-uxlog.py` appends a content hash to the injected `<script src>`
  for exactly this reason. It is served as a static file with no `no-store`
  header, so without the hash a tablet that has loaded any earlier build keeps
  running the *old* logger. That failure is silent: the fields the new version
  added are simply missing from the log, and the analyser prints them as empty
  columns, which reads like "the participant never did this". It cost four
  sessions of `finger_turns` before anyone noticed. To check which logger a
  tablet is on, read the key set of an `up` event rather than trusting the
  build.
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

## The echo that used to repaint a widget mid-drag

Open Stage Control's native widgets queue an incoming value while they are
touched (`setValueTouchedQueue`) and apply it on release, so a fader was never
fought by the engine. formuls' own slider canvases draw themselves and had no
such protection: the engine's echo on the same address repainted them under the
finger. Measured over the sessions of 27 October, `canvas` touches showed a mean
of **2.65** displayed reversals against a finger mean of **0.08**, while native
`xy` touches showed 2.31 against 3.00 — inflated by a factor of about 33 on the
canvases and not at all on the native widgets.

`f.util.guiinhibit`, at the patch's single GUI-bound choke point, now drops a
packet whose address is under a finger. Checked on a real build on 28 October:
canvas display and finger counts came back at **parity, 0.62 against 0.61**;
grab and release paired 58 out of 58 with none left open and the safety timeout
never needed; all ten grabbed addresses that had been modulating echoed again
after release, so no slot leaked. Two gestures out of 58 saw one last echo on
the grabbed address, at +33 ms and +76 ms into nine-second holds -- packets that
had already left before the grab reached Pd, and the rest of both holds was
silent. Two consequences for reading a log:

- `display_reversals` should fall towards `finger_turns` on canvas widgets in
  logs recorded after this change. It is still not the measure to use — it is
  kept for comparison — but a log from before and a log from after are not
  comparable on it.
- `in` and `disp` go quiet for an address while it is grabbed. A gesture's
  before-and-after window therefore has a hole in it for the touched address
  only; every other address keeps streaming, so regime tagging is unaffected.
