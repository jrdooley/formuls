# Spectral descriptors in the session log

A UX-logging build writes a **`spec`** stream: every engine channel's level and
four spectral descriptors, a few times a second, into the same log as the
touches. This is why it exists, what the numbers are, and what they cannot say.

## The problem it replaces

`sound.csv` measured loudness and **spectral centroid** over the **stereo
master mix**, and centroid on that signal says very little about this
instrument. Six polyphonic synths and the bus effects are summed before
anything is measured, so the figure follows whichever source is momentarily
loudest, and it cannot distinguish a brighter timbre from a higher pitch from a
change in which synth dominates.

The case that makes it concrete: synth 1 plays a scalar passage climbing one
step at a time over two octaves while synth 2 plays chords whose fundamental
moves randomly between 200 Hz and 2 kHz. The mix centroid moves a great deal
and none of that movement is attributable.

## Why not record the channels instead

The app can already record them: the channel menu offers 2 or 14, the 14 being
the individual synth and bus outputs, and the recorder writes one channel per
output channel. The obstacle is size. At Ogg q0.7 a stereo session is about
100 MB an hour; fourteen largely uncorrelated channels are roughly seven times
that — **about 700 MB an hour, so a four-hour session near 2.8 GB**, before
multiplying by participants and against the retention promises in the consent
sheet.

Logging the numbers instead costs about what the `in` stream costs — of the
order of **15 MB an hour** — and leaves the stereo recording untouched for
listening and replay. The trade is that the descriptor set is fixed when the
session is recorded, where audio can be re-analysed any way at all later; the
stereo take is the hedge against that, and is why it stays.

## What is written

One frame per interval, at `FORMULS_UXLOG_SPEC_HZ` (default **5**, as the `in`
stream):

```json
{"k":"spec","n":1024,"sr":48000,"ch":[
  {"i":0,"db":-18.4,"cen":2310,"spread":1840,"flat":0.0131,"irr":0.4102,"skew":1.42},
  {"i":1,"db":-91.0}
]}
```

`spec-start` is written once when logging begins, with the frame rate, window
size, channel count and sample rate.

| field | meaning |
|---|---|
| `i` | engine output channel index, 0-based |
| `db` | RMS of the window, dBFS |
| `cen` | spectral centroid, Hz |
| `spread` | spectral spread, Hz (see below) |
| `flat` | spectral flatness, 0–1 |
| `irr` | spectral irregularity (Jensen) |
| `skew` | spectral skewness, dimensionless |

A channel below **−90 dBFS** gets its `db` and nothing else: the moments of an
empty spectrum are noise, and a null says "nothing was sounding" where a number
would have claimed "this is what it sounded like".

## The definitions

Taken from **LibXtract** (github.com/jamiebullock/LibXtract) so a number here
is comparable with one from that library. The named function is given against
each in `SpectralAnalyser.cpp`.

| descriptor | definition | what it distinguishes |
|---|---|---|
| **centroid** | `xtract_spectral_centroid`: Σ(f·a)/Σa | the energy-weighted mean frequency. Conflates pitch and timbre, which is the whole problem — kept because the others are read against it |
| **spread** | `xtract_spectral_variance`: Σ((f−c)²·a)/Σa, logged as its **square root** (`xtract_spectral_standard_deviation`) so the number is in Hz | how widely energy sits around the centroid: a dense chord against a single tone of the same mean frequency |
| **flatness** | `xtract_flatness`: geometric over arithmetic mean of the magnitudes, over the bins above zero only, as LibXtract does | noise-like (near 1) against tonal (near 0) — FM index rising, noise added, a resonant filter opening |
| **irregularity** | `xtract_irregularity_j` (Jensen): Σ(a[n]−a[n+1])² / Σa[n]² | how much neighbouring bins differ, relative to total energy. Krimphoff's `_k` variant is not normalised, so it would rise with level alone |
| **skewness** | `xtract_spectral_skewness`: Σ((f−c)³·a)/(Σa·sd³) | which side of the centroid the energy leans, which the centroid alone hides |

**LibXtract is not linked.** It is a C library that would need a submodule and
build changes on three platforms plus the plugin, and these are four
closed-form summations over a magnitude spectrum. The transform is a radix-2
FFT of one fixed size, written out in the .cpp — `juce_dsp` is not among this
project's modules, and adding a module for one function would mean module paths
in three exporters. If exact parity with LibXtract is ever wanted, the function
boundary is already the right shape to swap.

## How the numbers behave

Checked against synthetic signals (1024-point window, 48 kHz):

| signal | centroid | spread | flatness | irregularity | skew |
|---|---|---|---|---|---|
| sine 440 Hz | 440 | 58 | 0.00001 | 0.64 | 24.7 |
| sine 5 kHz | 5001 | 61 | 0.00002 | 0.64 | 2.77 |
| two tones, 440 Hz + 8 kHz | 4214 | **3781** | 0.00005 | 0.64 | 0.00 |
| white noise | 12010 | 6894 | **0.845** | **0.27** | 0.02 |
| 20 harmonics of 440 Hz | 2347 | 2194 | 0.00017 | 0.64 | 1.16 |

Two things to read off that table.

**Spread earns its place in the third row.** Two tones an octave and a half
apart give a centroid of 4214 Hz — a frequency where there is no energy at
all — and only the spread of 3781 Hz reveals that the signal is nowhere near
its own mean.

**Irregularity runs the opposite way to intuition.** It is *high* for sparse
spectra, where neighbouring bins differ enormously (0.64 for a single sine),
and *low* for dense ones (0.27 for white noise). It is a measure of spectral
sparsity against density, not of "roughness"; reading it as roughness would
invert every conclusion drawn from it.

## The patch renders more than the player hears

In a UX-logging build the patch is booted with **14 channels whatever the
player chose**, while the audio device opens with their choice. So the
individual synth and bus outs always exist for the analyser, and the player
still hears plain stereo out of channels 1 and 2; channels 3–14 go nowhere but
the analyser.

What this does and does not change:

| | with the player on stereo |
|---|---|
| what the device opens | 2 channels, as chosen |
| what reaches the speakers | channels 1–2 only |
| what the session recording contains | 2 channels — a take stays ~100 MB an hour |
| what the analyser measures | all 14 |

Three details that make it work. `start()` takes `enginePatchChannels` and
boots libpd with the larger of that and the device's count, leaving the device
setup alone. `render()` de-interleaves **every** patch channel into scratch
allocated at boot and hands it to the analyser one libpd tick at a time, so
the audio thread never allocates and the device copy is untouched. And the
recorder is now driven by the **device's** channel count rather than the
patch's — without that, a UX-logging build would silently have started writing
14-channel session audio, at seven times the size, which is the very cost this
design exists to avoid.

The `spec-start` frame records both numbers: `ch` is how many channels are
analysed, `audible` how many the player hears.

**Expected cost: a copy, not DSP.** Pd computes the whole signal graph
whichever channels the device took — only the final write to an unopened
channel is dropped — so the synths and buses feeding `dac~ 3`–`14` were
already being computed in stereo mode. The extra work is de-interleaving
twelve more channels per tick. That expectation has not been measured yet;
compare CPU with the patch on 2 and on 14 before a long session.

Only a UX-logging build does this: `MainComponent` passes
`kPatchChannelsForAnalysis` when `isUxLogBuild()`, and 0 otherwise, so a
release build boots exactly as before.

## The channel map

Traced from `src/pd/_main.pd`: the two `dac~` objects, the order of the
outlets feeding them (Pd orders outlets by x position, not by object index),
and what each of those outlets is connected to. The patch's own comments —
"clean out", "Reverb Out", "Repeater Out", "Rolling Freezer Out" — sit under
the matching outlet pairs and agree.

| channel | `i` in a `spec` frame | what it carries |
|---|---|---|
| 1–2 | 0–1 | **master stereo mix**, after the limiter (`catch~ $0-limiter-l/r`, labelled "clean out"). This is what stereo mode plays |
| 3–4 | 2–3 | **reverb** return, L then R |
| 5–6 | 4–5 | **repeater** return, L then R |
| 7–8 | 6–7 | **rolling freezer** return, L then R (also thrown to the limiter, so it is in the master mix too) |
| 9 | 8 | **synth 1**, mono |
| 10 | 9 | **synth 2**, mono |
| 11 | 10 | **synth 3**, mono |
| 12 | 11 | **synth 4**, mono |
| 13 | 12 | **synth 5**, mono |
| 14 | 13 | **synth 6**, mono |

Four things to know before reading a number off one of these.

**The synth channels are mono, and the effect channels are stereo.** Each
synth channel is `(r~ N-l + r~ N-r) * 0.5` — the average of that synth's own
stereo pair, not one side of it. So a synth's panning is invisible on its own
channel, and a stereo-width or panning gesture will show on channels 1–2 while
channels 9–14 barely move.

**Channels 1–8 are after the master amp; 9–14 are before it.** The
`AUDIO_SAFETY` subpatch multiplies each of the eight by the smoothed
`$0-master-amp` (plus the 6 dB of fixed headroom described in
`docs/headroom/`), while the synth channels are taken straight from the
synths' sends. A gesture on the master volume therefore changes every
descriptor's `db` on 1–8 and none on 9–14 — which is a useful cross-check, and
a trap if the two groups are compared on level.

**A synth's sound reaches the master through the effects.** Channels 9–14 are
the dry synths; 3–8 are the master-bus returns. The same note appears on
several channels at once, so the channels are not independent sources and
their descriptors should not be summed or averaged into a "whole instrument"
figure.

**The test tone goes to all of 1–8.** `TEST_TONE~~~`'s single outlet is
connected to every inlet of `dac~ 1 2 3 4 5 6 7 8`. If it is ever left on, it
is on all eight at once and on none of 9–14.

## Threading

`pushBlock()` runs on the audio thread and does no transform: it copies samples
into a per-channel staging buffer and, once a whole window is there, publishes
it by flipping an atomic index. `takeFrame()` runs on the message thread at the
frame rate, reads the most recently published window per channel, and does the
FFT and the arithmetic — fourteen 1024-point transforms five times a second,
a few hundred microseconds in all. Nothing blocks the audio thread, and no
frame is half of one window and half of another.

A frame is a **snapshot**: it describes one 1024-sample window (21 ms at
48 kHz) taken at that moment, not an average over the interval.

## What is not settled

- ~~Per-synth descriptors need the channel count set to 14.~~ **Done.** See
  *The patch renders more than the player hears* above.
- ~~Which channel is which is not written down.~~ **Done.** See *The channel
  map* above.
- **Nothing reads the stream yet.** `analyse-uxlog.py` does not use `spec`
  frames; per-gesture, per-channel spectral change is the next step, and it
  should be regime-aware in the same way `sound.csv` now is.
