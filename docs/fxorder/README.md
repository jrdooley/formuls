# Reorderable, bypassable effects chain

Branch **`fxorder-crossbar`** (five commits on top of `main` at `43fd1d3`).
The nine effects after the oscillator — AM, pitchshift, saturation, bitcrush2,
chorus, phaser, filter, gate, delay — used to be wired in one fixed order inside
`fx` in `src/faust/ffx.lib`. They can now be **reordered** and **bypassed** per
synth from the touchscreen, live, without rebuilding anything.
The default order now puts **delay before gate** (originally gate came first), so the
GUI column and the signal path read the same way.

| commit | what |
|---|---|
| `e030b40` | Faust crossbar, Pd routing, canvas chain strip (prototype) |
| `cb26dcd` | duck and debounce for live order changes |
| `aeef792` | strip embedded in the synth panels as a modal, synth tab re-laid out |
| `f610c8e` | Feedback slider, shorter FX ORDER button, white modal text |
| `9096ca6` | synth and global reset restore the default order |

## How it works

### Faust (`src/faust/ffx.lib`)

`fxchain` is a **feedback crossbar**: nine effect slots, nine `fxslot0..8`
sliders (`hslider("fxslotJ", J, 0, N, 1)`), where slot *j* names the effect that
runs at position *j* and the value `9` means "empty". All routing is one
recursive state machine, `body ~ si.bus(FXN-1)`, so each hop costs one sample.

- **Latency is constant.** Any chain, any number of active effects, is exactly
  `FXLAT = FXN - 1 = 8` samples. The dry path in `fx` is delayed by `@(FXLAT)`
  so wet and dry stay aligned at the `fxsend` crossfade.
- **Dedupe rule.** A slot whose effect an earlier slot already holds counts as
  empty, so a half-updated or malformed slot list can never put one effect in two
  places (that would route it through itself).
- **Defaults reproduce the old order.** `fxfixed` (the original chain) is kept
  in the file as the reference the null tests compare against.
- `formuls.dsp` is unchanged. Compile flags are unchanged
  (`-vec -lv 0 -vs 4`).

Faust rejects mutually recursive named definitions ("endless evaluation
cycle"), which is why the control logic and the signal routing are single
`~` expressions rather than a set of named feedback loops.

### Live changes: debounce and duck

Changing the order makes every effect see a different signal at once, which
pops. A change is therefore not applied the instant the sliders move:

1. Wait until the sliders have been still for `FXQUIET` (3 ms). This also makes
   the update **atomic** however the nine values arrive.
2. Duck the *wet* path to zero over `FXDOWN` (5 ms), hold `FXHOLD` (2 ms).
3. Latch the new order while the wet path is silent.
4. Bring the wet path back over `FXUP` (10 ms).
5. Apply at most one change per `FXCOOL` (200 ms); whatever the sliders hold
   when the hold-off ends is what gets applied.

The dry path is not ducked.

### Pd

- `f.formuls~.pd` (FX_MESSAGE_PARSE): `route -bitcrush phaser fxorder`; an
  `fxorder` message of nine floats is unpacked into nine `fxslotJ` messages.
- `f.util.oscinparse.pd` (FX canvas): `/fxorder<n>` becomes
  `formuls<n> fxorder ...` to `to-faust-<n>`. `/fxstate<n>` (the strip's own
  state, see below) is deliberately swallowed.
- `f.util.reset.pd` (FX canvas): on `<n>-reset` — raised by both a synth's reset
  button and the global reset — sends the default `fxstate` to the GUI and
  `fxorder 0 1 2 3 4 5 6 7 8` straight to Faust. Sending to Faust directly means
  the audio resets even if no GUI is connected.

### GUI (`src/gui/_main.json`)

Each synth tab has an **FX ORDER** modal button at the top of the
effects column. It opens the chain strip:

- Nine blocks, `in > out`, left to right.
- **Tap** an effect to bypass it. It is dimmed in place and keeps its slot, so
  reinstating it returns it to the position it had.
- **Drag** a block to reorder. A dashed outline shows where it will land.
- **RESET** restores the default order with everything active.

The strip is a canvas (`fxstate<n>`, 18 values: order[9] then on[9]). Its
`onValue` computes the slot list — active effects in order, padded with `9` —
and `set()`s a hidden sender canvas (`fxorder<n>`), which is what reaches Pd as
`/fxorder<n>`. Bypass state lives in the GUI only; Faust just sees fewer
occupied slots.

Synth-tab layout: column 1 (top to bottom) is Velocity, Osc Slide
Range/Time, **Feedback**, FM Frequency/Depth, Noise Frequency/Depth. Column 2
starts with the **FX ORDER** modal, then the amplitude-modulation panel, the
pitchshift pad, and four sliders: **Saturation, Bitcrush, Chorus, Phaser**.
Freq Snap, Osc Frequency/Wave and Velocity did not move. All modal text is
white. The ADSR panel's three buttons are exact thirds of its height.

### Saturation/bitcrush and chorus/phaser are sliders

These were two xy pads (`saturationbitcrush`, `chorusphaser`) driven by
`f.util.oscformatxy`. They are now four `slider` clones driven by four
`f.util.oscformat` instances in the FX canvas of `f.util.oscinparse.pd`
(`saturation 0 0 1`, `bitcrush 0 0 1`, `chorus 0 0 2.7`, `phaser 0 0 1` —
same names, defaults and curves as the pads' two axes, so Faust sees
the same messages). Knock-on changes: `f.util.reset.pd` (GUI reset values and
`f.util.lfoguireset` entries), `f.gui.modpack.pd` regenerated by
`docs/gui/tools/gen-modpack.py` (still eight slots), the default state file, and
`docs/gui/tools/pick-addresses.py`. Saved states with the old pad ids are
simply ignored for those widgets.

## The Feedback slider

A clone of the `velocity` slider, named `extmodulation`, driving the Faust
parameter of the same name in `fsynth.lib`
(`extmod = hslider("extmodulation",0,0,1,0.01) : *(16) : si.smoo`): how much of
an incoming signal modulates the carrier frequency. That path had been
bypassed for many versions; the GUI control and its Pd route were gone.

- Pd: `/extmodulation<n>` goes through `f.util.oscformat` with the same
  2.7 power curve as velocity (so `0.5` arrives at Faust as `0.1539`), in the
  OSC_FM__NOISE canvas of `f.util.oscinparse.pd`.
- The modulation **source** matrix (`extmodulationsource`) no longer has a GUI,
  so the slider would have done nothing. Pd now selects the synth's own output as
  the source with gain 1, 2 s after load and 100 ms after every reset.
- Registered in `f.util.reset.pd` (`f.util.lfoguireset`) so
  `src/tools/check-reset-coverage.py` passes (37 parameters).

## Measured

| check | result |
|---|---|
| Null test, crossbar vs `fxfixed`, default order | matches at an 8-sample lag; the residual equals that of the old chain fed a signal delayed 8 samples, i.e. it comes from the existing effects, not the crossbar |
| `fxorder` message vs nine direct `fxslot` messages (headless Pd render) | bit-identical |
| Whole-instance CPU | 1.38–1.44 % before and after (inside bench noise) |
| Click size on live reorder, normal settings | before duck/debounce 2.8–8.5×; after 1.00× |
| Non-atomic update, extreme settings | before 22×; after 2.0× |
| Rapid random switching at extreme settings | before: pumped to about 3785; after: peaks 29–350 at ≤ 5 switches/s |
| Feedback: `/extmodulation1 0.5` | `formuls1 extmodulation 0.153893`, then `extmodulationsource 1 1` |
| Reset (Pd harness) | GUI receives `0..8, 1×9`; Faust receives `fxorder 0..8` |
| GUI in a real Open Stage Control server | tap-bypass dims in place; bypassed then reinstated returns to its slot after other moves; drag reorders; RESET restores; tab 3 has its own strip and `/fxorder3`; incoming `/fxstate` default restores strip and hidden sender |

## Known limits

- **Remaining transient.** Reverse → default still has a transient: the old
  delay buffer's content echoes out through the new order. At extreme
  settings (delay feedback 0.85 with filter resonance 0.9) the chain still
  pumps when reordered ≤ 5 times per second.
  The candidate fix is **flushing the delay buffer on reorder** ("Option 1").
  It was not applied; it was left for a by-ear decision.
- **Reset coverage.** The strip is wired into the per-synth reset
  (`f.util.reset.pd`) but is **not** in `_formuls-default.state`.
- **Not verified:** touchscreen / multitouch drags, saved-state restore of the
  strip, tabs 2 and 4–6, and anything by ear (including how Feedback sounds).
- **Unrelated, pre-existing:** the Step Sequencer popup opens by itself when a
  synth tab loads.

## Reproducing

```
# Pd side (no audio device): an fxorder / reset / extmodulation message in,
# the to-faust-N and formulsN messages out, with pd -nogui -noaudio -batch
# and a patch that [r to-faust-1] / [r formuls1] / [r to-o-s-c-interface].

python3 src/tools/check-reset-coverage.py          # exits non-zero on mismatch
./build-macOS.sh                                   # full app
```

The Faust bench and the null-test harness were scratch tools and are not in
the repository; the null test compares `fxfixed` against `fx` after an 8-sample
alignment, with the sliders at the defaults.
