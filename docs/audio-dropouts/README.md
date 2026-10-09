# Clicks and dropouts: what caused them, and what fixed them

Three kinds of click were heard while playing the app, and each had a different
cause. They were traced with a temporary timing log in the app, macOS's own Core
Audio log, and headless Pd. The log is on branch `diag-audio-timing`, not on
`main`.

| what was heard | cause | fix |
|---|---|---|
| A click when stopping an automation recording | The synth jumped to the take's first value, and some synth parameters weren't smoothed | `si.smoo` on those parameters in `src/faust/fsynth.lib` |
| A click when pressing the global reset | The reset held Pd's lock for 65–69 ms, six times an audio callback, so the output dropped out | The app sends GUI messages itself (`PdOscOut.h`); the reset is staggered per synth (`_main.pd`) |
| Occasional clicks with nothing happening | The virtual audio cable between formuls and SoundDesk | None in formuls: they stopped with a direct audio interface |

## 1. The record-stop click: a jump the synth didn't smooth

Since `cd94ae0`/`e0c6797`, stopping a take holds the parameter's last live value
until the next beat, then plays the take from its start (see
`src/tools/README.md`, `automation-probe.py`). If the gesture ended somewhere
other than where it began, that restart is an instant jump. So is every loop
wrap.

Pd sends these values as bare floats, so whether a jump is audible depends on
Faust. Most effect parameters already pass through `si.smoo`. Several synth
parameters did not, and those were the ones that clicked:

- **The oscillator frequency in mono mode (ADSR off).** `synth()` feeds the
  oscillator `(oxO, ox) : si.interpolate(adsron)`, so with the ADSR off it gets
  `oxO`, the raw slider, directly. A frequency jump was an instant pitch jump.
- **`fmfreq` and `fmdepth`**: an instant change of FM ratio or depth.
- **`noise`**: an instant change in how much noise is added to the frequency.

Each now passes through `si.smoo`: a one-pole smoother with a 5 ms time constant,
settling in about 20 ms. No glide was noticeable in playing. What is unchanged:

- **The ADSR path (`ox1`)** still samples the exact frequency at each trigger.
  The smoothing is on `oxO` only where mono mode uses it.
- **Audio-rate frequency modulation (`extmodulation`)** is applied later, in
  `fmosc`'s `freqset`, so it never passes through the smoother.

The player found this by noticing the click only happened on the OSC
Frequency/Wave pad in mono mode. Two tests confirmed it:

- with an earlier fix removed (a 5 ms glide on automation playback in
  `f.seq.automater`), other parameters didn't click on record stop;
- smoothing the oscillator frequency removed the click on that pad.

The FM and noise cases appeared next and were fixed the same way. The glide is
not in the tree: the cause was in the synth, so the fix went there.

## 2. The reset click: a dropout

### What the log showed

Core Audio logged "IO overload, client timeout" with `net.formuls.formuls` as the
late client. Callbacks were taking about 10.1 ms of a 10.7 ms budget (512 samples
at 48 kHz), although average CPU was about 2%. The app's timing log matched each
heard click to a callback that missed its deadline while a GUI message held Pd:

| message | held Pd for | |
|---|---|---|
| `/resetglobal` | 65–69 ms | missed every time |
| `/recordglobal` (record stop) | 6–16 ms | missed now and then |

### Why a message can stop the audio

`OscBridge` passes each GUI message to libpd on the message thread.
`libpd_message()` takes Pd's global lock (`sys_lock`), the same lock the audio
callback needs to render each 64-sample tick. Pd handles control and audio one at
a time, so while a message's work runs, the audio waits. Moving that work to the
audio thread wouldn't help either: 65 ms of work doesn't fit in a 10.7 ms
callback wherever it runs. The work itself had to shrink or be spread out.

Normal playing already uses 50–65% of each callback for synthesis, which leaves
4–5 ms for any burst.

### Where the reset's time went

Measured in headless Pd, with a timer around each incoming message and macOS's
`sample` profiler:

- **One reset made Pd send 7,354 OSC messages to the GUI**: 128 steps × 3 kinds of
  value × 6 sequencers, plus every slider's mod cells on every synth.
- **About half** the time was `sendto()`, one system call per message, made by
  `[netsend]` while holding the lock. Almost all the rest was Pd's own reset
  logic; synthesis was 4%.
- **Almost all of the patch work** was each synth's `RESET` in
  `f.util.oscinparse`, about 3.7 ms per synth.

### Fix 1: the app sends the GUI messages (`src/app/Source/PdOscOut.h`)

This is the counterpart of `OscBridge`, for the sending direction:

- **App side:** the app binds a receiver to `formuls-osc-out` inside Pd. Its list
  method only copies each packet's bytes into a 4 MB pre-allocated lock-free FIFO,
  about a microsecond. A background thread does the UDP sends, outside the lock.
- **Patch side:** in `_main.pd`'s `O-S-C_&_FORMULS_SEND`, a switch before
  `[netsend]` sends everything to `[s formuls-osc-out]` once the app sends
  `formuls-app-osc 1`. In plain Pd nothing sends that message, so `[netsend]`
  works as before, which keeps the patch usable for editing.

libpd's own queued message path wasn't used: its ring buffer is 16 KB, and a
reset is several megabytes of OSC, so most of the burst would be dropped.

### Fix 2: the reset is staggered (`_main.pd`, `RESET_STAGGER`)

Both senders, the GUI button and `INIT_PARAMETER_SET` at start-up, now send
`resetglobal-in`. The dispatcher handles it in two halves:

- **press (`1`):** to the master section (`resetglobal`) at once, and to synths
  1–6 (`resetglobal-1` … `-6`, received in `f.util.oscinparse`) 30 ms apart;
- **release (`0`):** held for 190 ms, past the last synth's press, then sent to
  the master section and all six synths together.

Holding the release matters. The first version passed the release straight on.
The start-up reset releases 10 ms after its press, and a button tap releases
after 58–80 ms, so synths got their press after the master section's release.
Those synths then stayed silent until reset individually. Every part has to see
the press before any part sees the release, as before the stagger.

### Result

| | before | after |
|---|---|---|
| reset, held all at once | 35–55 ms (headless), 65–69 ms (app) | — |
| reset, immediate part | — | about 1 ms |
| each synth's share, 30 ms apart | — | about 3.7 ms |

In playing, no reset or record stop missed a deadline afterwards, and the app
sent every OSC packet with none dropped.

### Still to do

Neither of these is audible now:

- **Record stop** still holds Pd for 4–12 ms, because all 421 automaters handle
  `record` on every press. Only the automater that recorded needs the full stop
  handling.
- **Per-synth reset buttons** (`/reset1` … `/reset6`) hold Pd for 6–12 ms. They do
  the same work as one share of the global reset, all at once.

## 3. Random clicks: the virtual audio cable

Some clicks had no missed deadline anywhere in the log. Core Audio listed SoundDesk
on the SoundDesk Virtual Cable at 47,990.9 Hz while formuls ran at 48,000 Hz. A
cable bridging two slightly different clocks has to drop or repeat a sample now
and then. With formuls sent straight to a Studio 1810c, the random clicks
stopped. This is outside formuls.

## Reproducing the measurements

- **App timing:** branch `diag-audio-timing`. Build, play, then read
  `~/Library/Logs/formuls/timing.log`:
  - `SLOW BLOCK`: a callback over half its budget, marked `MISSED` if late;
  - `OSC HOLD`: a GUI message that held Pd for more than 1 ms;
  - `OSC BUTTON`: every record or reset press, with its value;
  - `SUMMARY`: every 10 seconds.
- **macOS's view:**
  `/usr/bin/log show --last 1h --predicate 'process == "coreaudiod" AND eventMessage CONTAINS "ClientTimeoutStart"'`.
  The message names the late app and how long its callback took.
- **Headless:** a copy of `src/pd` with the GUI ports moved off 9000/9001, and a
  shim patch that times each incoming message with `[realtime]`. Run with
  `pd -nogui -nosound`, and profile with `sample <pid>`.
