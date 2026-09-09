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

## If someone picks this up

Cheapest experiment: chord size into that `[* 1]`, small and reversible. Before
that, worth establishing **whether the clipping comes from one channel playing
chords or from several channels stacking** — those point at different fixes, and
the answer is a listening test, not a code change.
