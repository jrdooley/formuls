# Automater modulation: what it cost while running, and the fix

The earlier efficiency pass (`README.md` in this folder) got the automaters cheap
while **idle**. This pass looks at them while they **modulate**: automation
playback, chaos and LFO on many parameters at once.

## Where the time went

Measured with `src/tools/automater-load-bench.py`: N automaters, each followed by
the per-value tail `f.util.oscformat` runs, with the app's throttle rates, in
`pd -batch`. That is deterministic and matches how libpd is driven by the audio
callback. Profiles came from `sample` on a real-time run.

1. **Chaos made every automater run clocks of its own.** Each one ran a
   `[metro 30]` and a `[line 0 10]` that ramped every 10 ms whenever slew was
   above 0. Pd keeps every pending clock in one sorted linked list, and each
   reschedule walks it, so the cost grows with the *square* of the number of
   parameters using chaos. `clock_delay` alone was **65%** of busy time in chaos
   mode, and going from 100 to 400 instances made chaos 16× dearer.
2. **Each source sent its own copy of the value.** Chaos, LFO and playback each
   pushed the whole chain (`CHAOS -> LFO -> EXT_MOD -> quantise -> Faust`) on their
   own timing. With all three on, one parameter sent **~300 values/s** to Faust
   where 100 is the useful maximum.
3. **Repeated values were sent again.** A held playback value, a clipped
   modulation and a quantised parameter all re-sent the same number every tick.

## The change (one abstraction: `f.seq.automater.pd`)

`CHAOS___`, `LFO_____` and `EXT_MOD_` are replaced by one `MOD_MIX` subpatch:

- **One throttle receive per automater**, shut unless chaos or the LFO is running
  (so idle cost is unchanged). Each tick it steps chaos, then the LFO, then sends
  the mix once.
- **Chaos uses no clocks.** A new random target every 3rd tick (was
  `metro 30`). The slew is `[line]`'s own linear ramp at a 10 ms grain, worked
  out per tick in one `[expr]`, and it still slews back to 0 when chaos is
  turned off.
- **Values are summed and clipped exactly as the old serial chain did**
  (`clip(clip(clip(base+chaos)+lfo)+ext)`), using plain `[+]`/`[clip]`.
  `[expr]` and message boxes were tried on this path and measured slower:
  `expr` runs an interpreter, and a message box re-parses itself on every bang.
- While nothing is modulating, a new value goes straight out, with no added
  latency. While something is, it is sent on the next tick.
- **`[change -1]` on the output** drops repeated values. The `-1` start means
  a first value of 0 still goes out.

## Results (`pd -batch`, share of one core, median of 3)

| modes running | 100 automaters old → new | 400 automaters old → new |
|---|---|---|
| idle | 0.0 → 0.0 | 0.1 → 0.1 |
| playback | 1.0 → 0.9 | 5.1 → 4.5 |
| LFO | 0.5 → 0.5 | 3.0 → 3.0 |
| chaos | 0.9 → 0.8 | **14.5 → 4.0** |
| LFO + chaos | 1.4 → 0.9 | **17.1 → 4.5** |
| playback + LFO + chaos | 2.2 → 1.3 | **20.7 → 5.9 (−71%)** |

Values sent to Faust per parameter with all three modes on: **299/s → 91/s**.
The bench does not include the `formuls~` external's own handling of each
message, so the real saving in the app is larger than the table shows.

For scale, the running patch has 427 automaters.

## How behaviour was checked

A scenario harness drove the old (HEAD) and new abstractions through the same
timeline and compared every value on outlet 0:

- **Automation playback** (`automation-probe.py`, ramp/triangle/hold): identical,
  except that repeated values are no longer sent again.
- **LFO** (start, depth to 0, restart, base change while running): every value
  identical. The one difference is a base change mid-LFO, where the old code
  sent twice in the same tick and the new code sends once.
- **Chaos, continuous, slew 200 and slew 0**: the same random sequence (Pd seeds
  `[random]` deterministically), the same range and step sizes, and the same slew
  back to the base value when chaos is turned off.
  The ramp runs **one tick (10 ms) earlier**, because the first step now happens
  in the tick where the target is drawn rather than 10 ms after it.
- **Chaos, triggered by sequencer step**: same ramp values, also one tick earlier.
- **Reset, quantise, a first value of 0**: same end values.
- **Full `_main.pd` load**: console output identical to HEAD.

## Reproduce

    python3 src/tools/automater-load-bench.py -n 400 --modes playback lfo chaos
    python3 src/tools/automater-load-bench.py -n 400 --modes playback lfo chaos --rev HEAD~1
