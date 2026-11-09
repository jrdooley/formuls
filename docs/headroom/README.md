# Headroom: the master gain, clipping, and the polyphony count

Written 9 September 2026, alongside the `AUDIO_SAFETY______` hotfix (`409b73a`).
Nothing here has been implemented. It records what the patch currently does, why
the obvious next step is not as easy as it looks, and what the options cost.

## What shipped

`AUDIO_SAFETY______` in `_main.pd` now inserts `[*~ 0.5]` between `[sig~ 0]` and
`[lop~ 100]`, halving the smoothed gain that feeds the right inlet of all eight
channel `[*~]` objects. A polyphonic instance with many voices sounding sums past
0 dBFS, and this buys 6 dB of headroom for that sum.

Two properties worth keeping in mind:

- **It is unconditional.** A single voice is now 6 dB quieter too. Fixed headroom
  is the price of not touching the waveform.
- **It is headroom, not a guarantee.** Enough correlated voices will still exceed
  it.

Placing it on the control signal rather than the audio is deliberate: one `[*~]`
scales all eight channels instead of eight, and sitting *before* the `[lop~ 100]`
smoother means the halved value inherits the same slew, so master-amp moves still
ramp and the new stage adds no zipper noise.

## You are already clipping

Pd clamps to plus or minus one on the way to the audio device. Anything past
0 dBFS is therefore already hard clipped, at the last possible stage, with no
control over how. The question was never whether to alter the signal but **where,
and how gracefully**.

## What `clip~` does perceptually

Hard clipping flattens the peaks, which is equivalent to adding odd harmonics that
appear abruptly the instant the threshold is crossed.

- **The onset is discontinuous.** No distortion, then a lot. Brief transient
  overshoot is close to inaudible; sustained overshoot reads as a change of timbre
  rather than as a fault.
- **Aliasing is the real problem.** The new harmonics run far above Nyquist and
  fold back as inharmonic tones that move the wrong way with pitch. This is why
  digital hard clipping sounds worse than an overdriven analogue circuit, and it
  is most audible on high, bright, sustained voices.
- **It is program dependent.** Dense polyphony masks it, which is unhelpful,
  because dense polyphony is when it happens.

So `clip~` is a poor tone control but an acceptable last-ditch guard, if it sits
well above where the music normally lives.

## The options

| approach | signal change | cost |
|---|---|---|
| Voice-count normalisation | gain only, no distortion | loudness varies with voice count |
| Fixed headroom (**shipped**) | none until clip | quiet when few voices sound |
| Soft clip, e.g. `tanh` | gradual harmonics | always slightly coloured |
| Look-ahead limiter | dynamic gain movement | latency, pumping |
| `clip~` alone | abrupt harmonics plus aliasing | harsh when it engages |

A sensible layering would be voice-count normalisation as the primary control, the
fixed headroom underneath it, and a soft clip set several dB above nominal purely
to catch the correlated worst case. Then nothing shapes the sound in normal use,
and the only thing that ever engages is a curve rather than a corner.

## How the master gain is actually wired

The only writer is the GUI fader. In `MASTER_BUS_FX_+_AUDIO_OUT`, a
`[route fmasterbusfx]` feeds `[s $0-master-amp]` (`_main.pd:1520`). The matching
receive is in the safety subpatch (`_main.pd:2058`):

```
[r $0-master-amp]   [loadbang]->[del 5000]->[msg 1]
        |                            |
     [* 1]                           |
        +------------+---------------+
                     v
                 [sig~ 0]
                     v
                 [*~ 0.5]        <- the hotfix
                     v
                [lop~ 100]
                     v
        right inlet of all eight channel [*~]
```

**`[* 1]` is the insertion point** for anything adaptive. It is already a scaler in
the control path, currently multiplying by one, and every writer passes through
`[sig~ 0]` below it, so nothing can bypass it.

## Why the existing polyphony count will not do

`POLYPHONY_PARSE_______` in `controlabstractions/f.util.oscinparse.pd:2394`:

```
[r $0-monopoly]      [r $0-chord-size]
      v                     |
   [== 0]                   |
      v                     v
  [sel 0 1]----------->[f 1] right inlet
   |     \
   |      \-->[msg 20]
   v            |
   +------------+
        v
  [pack f $1] -> [msg formuls$2 polyphony $1] -> [s to-faust]
```

**This is an allocation ceiling, not a live voice count.** In monophonic mode it
forwards the chord size; in polyphonic mode it sends a flat 20 regardless of how
many notes sound. Normalising by it would attenuate one held note in polyphonic
mode exactly as much as twenty, which is backwards.

The deeper reason: **voice allocation lives in Faust.** Pd sends one
`formuls<n> triggerx bang` per note event and Faust decides when that voice dies,
with a release of up to 3 s (`gaterelease`). Any count derived in Pd is a *model*
of Faust's allocator, not an observation of it, and models drift.

(Related, and already recorded in the root `docs/README.md` open-decisions table:
`option-B-polyphony-12.patch` proposes dropping `POLYPHONY` from 20 to 12 for a
44% synth throughput win. That is the same constant, viewed as a cost rather than
as a level problem.)

## Three candidate signals, most honest last

- **Chord size.** Genuinely available, genuinely meaningful, per instance, clamped
  to a minimum of 1. Scaling by its inverse square root tracks the commonest cause
  of summing: one instrument playing thicker chords. Ignores the other channels.
- **A Pd-side active-voice count.** Count triggers, decay the count with a timer
  matched to the envelope release. Closer to the truth; still wrong whenever the
  release setting changes.
- **Ask Faust.** The voice allocator knows the active count exactly. Exposing it as
  an output and returning it is the only version that cannot drift, and the
  largest change.

There is also the option of **measuring the outcome instead of predicting it**. An
`[env~]` on the summed bus reports the actual level whatever its cause, and a slow
gain riding on that is a compressor in all but name.

## Measured, 9 November 2026: one instance is enough

The open question below — one channel playing chords, or several channels
stacking — has been answered by measuring a performance recording made with a
**single** synth instance, on a build that already had the `[*~ 0.5]` hotfix.

`solo2-Dphyrigian#4-57.wav`, 2 ch 24-bit 48 kHz, 466.8 s:

| | |
|---|---|
| peak | 0.00 dBFS (exactly ±1.0, both channels) |
| RMS | −21.5 dBFS (crest factor ~21 dB) |
| frames pinned at full scale | 8,784 (0.039%) |
| clipped runs | ~1,000 runs of ≥3 samples, longest **4 samples** (83 µs) |
| seconds containing clipping | **27 of 466** |

So: **one instance playing chords clips on its own.** That favours per-instance
control (chord size) over anything that only watches the master sum, and it
confirms 6 dB of fixed headroom covers only about two voices at full velocity.

The overshoot is small. Just below full scale the material occupies roughly
4,000–6,000 frames per dB; 9,000 frames are heaped at exactly 0 dBFS, which puts
the true peaks **on the order of 2–3 dB over** — an estimate from that density,
not a measurement, because the file is already clamped. Working back through the
−6 dB safety gain, the summed bus inside Pd reached about 2.8 linear (+9 dB).

Runs of 1–4 samples are the benign kind of clipping: the waveform pokes through
and comes straight back, so the aliasing is brief and largely masked. But it
happens a thousand times, and the cause is the commonest thing the instrument
does.

## Shipped since (1): the recording no longer clips

`AudioRecorder::bitsPerSample` is now **32**, so takes are 32-bit IEEE float and
JUCE's `convertFloatsToInts` clamp (`samp >= 1.0` → `INT_MAX`) is never reached;
overshoot survives into the file and the level can be brought down afterwards
with the waveform whole.

This was never a fix for the problem this document is about — the audio device
clamps too, so the room still heard it. It stopped the archive being damaged
while the real decision was outstanding.

## Shipped since (2): a safety limiter in the engine

`SafetyLimiter` (`src/app/Source/SafetyLimiter.h`), called from
`FormulsEngine::render()` after the de-interleave and before both the VU meters
and the recorder tap. This is the option that was not in the table above, and
it is now the answer to "where, and how gracefully".

**Why the engine and not the patch.** It is the last stage, which is where a
ceiling belongs; one implementation covers every channel, including the
`dac~ 9..14` per-instance sends and the `TEST_TONE~~~` subpatch, both of which
are wired straight to the `dac~` and bypass `AUDIO_SAFETY______`; the device and
the recorder get the same samples, so the file and the room agree; and it
survives patch edits, which a guard inside `_main.pd` does not.

**What it guarantees, and how.** Audio is read in frames of 64 samples (one
libpd tick) and each frame's peak is recorded. While frame *m* is read, frame
*m−2* is emitted, so the peaks of frames *m−2* and *m−1* are both known. Frame
*j* is emitted under a gain ramping linearly from `a[j]` to

```
b[j] = min (1, ceiling/P[j], ceiling/P[j+1], a[j] * riseLimit)
```

with `a[j] = b[j−1]`. Because `b[j−1]` already included the `ceiling/P[j]` term,
**both ends of the ramp are ≤ ceiling/P[j]**, and a linear ramp never leaves the
interval between its ends — so every gain applied to frame *j* is at or below
`ceiling/P[j]`, and `P[j]` is the largest sample in that frame. The ceiling
therefore holds for every sample, however violent the input, with **no final
clamp behind it** — a clamp would be the hard clipping the thing exists to
avoid. Needing `P[j+1]` is what costs two frames rather than one.

| | |
|---|---|
| ceiling | −1 dBFS (`FORMULS_LIMITER_CEILING_DB`) |
| latency | 128 samples, 2.67 ms at 48 kHz, constant |
| release | 40 dB/s (`FORMULS_LIMITER_RELEASE_DB_PER_SEC`) |
| linking | channels 1–8 in stereo pairs, 9–14 independent |
| bypass | `FORMULS_LIMITER=off`, which also removes the latency |

The channel map that linking follows was read out of `_main.pd` rather than
assumed: channels 1/2 are the main mix (`catch~ $0-limiter-l`/`-r`), 3/4 the
reverb, 5/6 the repeater, 7/8 the dry pair through `hip~ 5`, and 9–14 six mono
per-instance sends. The first eight are four stereo stems, so they are linked in
pairs and no limiter action can shift an image; 9–14 are separate recorded
stems, so ducking one because another peaked would be wrong.

The plugin reports the latency (`FormulsPluginProcessor::prepareToPlay` calls
`setLatencySamples`), without which a host would line formuls up 2.67 ms early
against everything else in the session.

**It is checked, not asserted.** `src/tools/limiter-probe.cpp` compiles the
header on its own — no JUCE, no libpd, no audio device — and runs 33 checks: the
ceiling against six hostile signals up to +21.6 dBFS, bit-exact transparency
below the ceiling, the exact latency, pair linking and stem independence, the
release rate, identical output across nine device block sizes, NaN/infinity
handling, and the bypass. All passing as of 9 November 2026.

**What it costs on real material.** The 29 October take, with 3 dB of pre-gain
to put the peaks back roughly where they were before the device clamped them:

| pre-gain | limiter engaged | mean reduction | worst | RMS cost | output peak |
|---|---|---|---|---|---|
| 0 dB | 1.3% of the take | 0.8 dB | 1.0 dB | −0.21 dB | −1.00 dBFS |
| **+3 dB** | **4.0%** | **2.1 dB** | **4.0 dB** | **−0.91 dB** | **−1.00 dBFS** |
| +6 dB | 8.3% | 3.2 dB | 7.0 dB | −1.70 dB | −1.00 dBFS |

So at the level that was actually clipping, it works on about one twenty-fifth
of the performance, by around 2 dB, and costs under 1 dB of loudness — in place
of roughly a thousand bursts of aliasing.

## Built and checked on Linux, 9 November 2026

Both commits build and the limiter behaves, in the OrbStack `formuls-x64` VM:

- `build-linux.sh` from scratch (libpd included) in 2m50s, exit 0, no warning
  on any touched file. The binary contains the limiter's strings, and Projucer's
  re-save of `formuls.jucer` left the tree clean -- the new `<FILE>` entry
  matches Projucer's own formatting.
- `build-plugin-linux.sh` in 2m27s: VST3 and LV2, `PluginProcessor.cpp`
  compiled, no warnings. libpd prints its banner during LV2 manifest
  generation, so the plugin instantiates.
- `limiter-probe`: 33/33 under g++ 13/x86_64, with the same numbers as
  clang 17/arm64 (0.891251, 0.297084, 0.1778 s).
- `plugin-probe` against the built VST3, rendering the real patch and the real
  Faust synth with no audio device: the test tone gives the documented healthy
  `peak 0.099832`; a 22-note stack peaks at 0.348509 with the limiter off and
  **0.348509 with it on** (transparent); with the ceiling forced to -20 dBFS the
  same material peaks at **exactly 0.100000**.

The default preset cannot be driven past -1 dBFS from MIDI notes alone, so the
engine-level ceiling check uses a lowered ceiling; the clipping in the
29 October take came from performance settings, not the defaults.

## And on the appliance, against a real audio device

An image was cut from `ca12461` (`formuls-20261109-x86_64.img`) and booted in
QEMU on a qcow2 overlay, with an emulated HDA codec so the guest has a real
ALSA device. The guest found it --

```
card 0: Intel [HDA Intel], device 0: Generic Analog [Generic Analog]
```

-- and the app's own journal shows the limiter configured and the engine
opening that device:

```
safety limiter: ceiling -1.00 dBFS, release 40 dB/s, latency 128 samples
formuls engine started: "Default ALSA Output", 2 channels @ 48000 Hz
```

So the limiter is live on the appliance, at the intended ceiling, release and
latency. The dist image's sha256 was identical before and after the boot, so
the overlay held.

**Not verified: a real take written on the appliance.** `/audiorecord 1` sent
to udp 9000 did nothing, and the reason is not a fault in any of this:
`FormulsEngine::start` calls `oscBridge.startDirect (pd)` when the GUI is
served in-process, as it is on the appliance, so **udp 9000 is never bound**
there and the message was silently lost -- no take, and
`~/formulsAudioRecordings/` was never created. Triggering the record button on
the appliance needs Open Stage Control's WebSocket protocol on 9001. The
recording format itself is verified at the JUCE level (`src/tools/README.md`
and `AudioRecorder.h`), which is the same writer the appliance runs.

Also still open: whether any of this is **audibly** better, which is a
listening test and not a measurement.

## Where the recorder taps, 9 November 2026

The limiter shipped with the recorder taking its copy *after* it, which made a
take identical to what the speakers emitted and incapable of exceeding the
ceiling. Measured on the user's own 205.8 s take of that build: peak exactly
`0.891251` (the ceiling constant to the last digit), 0 samples above it, 0 at
full scale, `fmt` tag 3.

That was changed to tap **before** the limiter, which is what 32-bit float
recording was for in the first place. The reasoning:

- A float WAV has no full-scale ceiling. A sample of 1.8 is written as 1.8 and
  read back as 1.8; the 24-bit integer writer's `samp >= 1.0 -> INT_MAX` clamp,
  the thing that was destroying the overshoot, does not exist on this path. So
  scaling a take down in post recovers the original waveform exactly — the
  overshoot was never clipped, only stored above 1.0. Distortion would need
  something in the chain to have *clamped* it, and nothing does.
- That makes the limiter purely a loudspeaker and converter protection, which
  is the only place it is unavoidable, and leaves the artistic decision — how
  much to pull down, and with what — in post, where it can be heard and undone.
- The limiter's gain reduction is otherwise baked into the file permanently.

The cost is two things, both stated in `src/app/README.md`: the file runs 128
samples ahead of the room because it skips the look-ahead delay, and any player
that assumes 0 dBFS is maximum will display (and may itself clip) the
overshoot. Neither touches the stored samples.

`FORMULS_RECORD_PRE_LIMITER=0` restores the post-limiter tap. `prepareRender()`
logs which one is in use, so a take's provenance is in the journal rather than
inferred.

**Checked.** The macOS app builds with no new warning, and the shipped binary
logs the right tap in all three environment states -- unset and
`FORMULS_RECORD_PRE_LIMITER=0` / `=off` -- beside an unchanged limiter line. A
standalone probe over the real `SafetyLimiter`, mirroring `render()`'s tap
block on a 1.8-peak block, confirms the copy is bit-identical to the engine
output, still holds samples above full scale, keeps its per-channel stride, and
that the device buffers come out at exactly 0.891251 (the ceiling).
`limiter-probe` still passes 33/33.

**Confirmed end to end on a played take**, 9 November 2026,
`formuls-2026-11-09-184307.wav`, 134.9 s of chords on one instance through the
pre-limiter build:

| | |
|---|---|
| format | `fmt` tag 3 (IEEE float), 2 ch, 48000 Hz, 32-bit |
| peak | **1.508472 = +3.57 dBFS** |
| RMS | −11.53 dBFS |
| samples above 1.0 | 2920, 0.023% of 12,948,480 |
| samples above the −1 dBFS ceiling | 11,495 |
| samples at exactly 1.0 | **0** |
| samples at exactly the ceiling | **0** |

Every part of that is the point. The peak is 3.57 dB *past* full scale, so the
tap really is upstream of the limiter. Nothing sits at exactly 1.0, so the
integer writer's clamp never ran — on the old 24-bit path those 2920 samples
would all have become the same number, in runs of up to 38 samples (0.4 ms),
which is the audible part. Nothing sits at exactly the ceiling either, which is
what the post-limiter take of the previous build read to the last digit.

And it is recoverable, not merely stored: ×0.5908 (−4.57 dB) brings the peak to
0.891251 with 0 samples above 1.0, and scaling down and back up returns the
file with a worst-case error of 1.1e-16, i.e. −319 dBFS — float rounding, some
200 dB below the noise floor of anything that could be played. A single gain
move in post is exact for practical purposes.

## What is still open

The limiter is a backstop, not a gain structure. It makes clipping impossible;
it does not make the engine's level sensible. The options table above still
stands for that, and the measurement still points at **chord size** as the
honest per-instance control — on the per-instance `volume` in `formuls.dsp`
rather than the global `[* 1]`, since one instance playing chords is the cause.

With a guaranteed ceiling in place, that is now a tuning question rather than a
safety one, and the fixed `[*~ 0.5]` in `AUDIO_SAFETY______` is worth
revisiting: it costs 6 dB on every single-voice passage to buy headroom the
limiter no longer needs it to buy.
