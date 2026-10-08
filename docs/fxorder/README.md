# Reorderable, bypassable effects chain

Branch **`fxorder-crossbar`** (commits on top of `main` at `43fd1d3`, listed below).
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
| `c642bcb` | this document |
| `c9d8d34`, `1415d12`, `b567bbe`, `b817d8c` | a four-column synth-tab layout, tried and then **reverted** (`cfcf3c5`); kept in history only |
| `cfcf3c5` | revert of the four-column layout; interface back to `c642bcb` |
| `90dff37` | FX ORDER at the top of the effects column; saturation/bitcrush and chorus/phaser xy pads become four sliders (Pd `oscformatxy` -> `oscformat`, reset, modpack, default state) |
| `21dd203` | equal slider rows in the effects column; xy pads match column 1 |
| `530b3c1` | Feedback, Saturation, Bitcrush, Chorus and Phaser grey out at zero (the Reverb overlay pattern) |
| `2c81307` | column 2 sliders 8.5%; pitchshift pad shrinks, AM xy unchanged |
| `10d935f` | default order puts delay before gate (Faust slot swap); gate pad follows delay in the GUI |

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

The lower half of every synth tab (48% of its height, the left 75%) is the
**effects dock**. It replaces the nine effect panels that used to share the
upper part of the tab, and the FX ORDER modal.

```
chain strip   AM > PITCH > SAT > CRUSH > CHORUS > PHASER > FILTER > DELAY > GATE   RESET
editor        the selected effect, plus any pinned ones, side by side
```

**Chain strip.** Nine tiles in signal order that tessellate: no gaps, one 2px seam
between neighbours, rounded only at the strip's outer corners. Each tile is split
into three equal bands, each its own touch zone:

| zone | gesture | does |
|---|---|---|
| top third, dots (grip) | drag | moves the effect, inserting it between two others; a bar shows where it lands |
| middle third (name, with a mini view of the effect's main control) | tap | selects the effect for the editor below |
| bottom third, left button (`ON` / `BYP`) | tap | bypasses the effect; it is dimmed in place and keeps its slot |
| bottom third, right button (`PIN`) | tap | keeps the effect in the editor while another is selected (at most two pinned columns) |

The mini view is a dot at the pad's x/y for an xy effect, or a bar for a
single-slider effect, so an effect's state is visible without selecting it.
The tiles use the synth tab's own colour (the widget colour every other control
uses): a dark face, an outline in that colour, a grip band tinted with it whose
dots, like the mini view, are drawn in the full outline colour, and
`ON` / `PIN` buttons drawn like the GUI's other buttons (dark face, coloured
outline, tinted when lit). Nothing is padded: the grip band and the two buttons
run edge to edge, and `ON` and `PIN` are exact, equal halves of the bottom third. The selected tile has a white outline. **Chain Reset**
(restores the default order with everything active) is **hidden for now**: the
widget is still in `_main.json` with `visible: false`; to bring it back, set
`visible` to `true` and narrow `fxstate` and `fxview` to 94% wide, because the
strip uses the full width.

**Editor.** The effects are the same widgets as before, moved into one panel
(`fxeditor<n>`) under the strip. The dock panel behind it (`fxa<n>`) is the synth tab's own colour at 20%
opacity, which shows wherever the editor has room to spare (the editor itself is
transparent). The 0.2 is `DIM` in the generator and the `rgba(...)` in `fxa`'s
`colorBg`; it reads the tab's `colour` variable, which only `fxa` can see. Each one is positioned and shown by a
property expression that reads the chain state: shown if it is the selected
effect or pinned; its column is its rank among the shown effects, in chain
order, and each gets an equal share of the width, end to end with no gaps, at
most three columns.
**The four slider effects (Saturation, Bitcrush, Chorus, Phaser) share one column,
stacked vertically in chain order, never side by side.** Selecting or pinning any
of them shows all four; pinning one pins all four (the pin buttons on the four
tiles move together), and the stack counts as one column against the limit.
**Filter Pitch Track** now lives in the filter column, as a full-width button row
(16% of the column) above the Filter Frequency, Q pad; the pad and its Filter Type
slider sit beneath it, so nothing overlaps.

**State.** One canvas, `fxstate<n>`, now holds 28 values: order[9], on[9],
selected effect, pinned[9]. The first 18 are exactly what they were. Its
`onValue` computes the slot list — active effects in order, padded with `9` —
and `set()`s a hidden sender canvas (`fxorder<n>`), which is what reaches Pd as
`/fxorder<n>`; it only sends when that list changes, so selecting or pinning an
effect is silent on the audio side. Selection and pins live in the GUI only. A
reset from Pd still sends 18 values to `fxstate<n>`; the missing ones read as
"first effect selected, nothing pinned". `/fxstate<n>` is still swallowed by Pd.
`fxview<n>` is a second canvas, non-interactive, that draws the strip from a
computed value (the state plus each effect's main control) and redraws every
frame, because a computed value does not redraw a canvas by itself;
`fxstate<n>` sits on top, transparent, and takes the touches and draws the drag
ghost.

Synth-tab layout. Upper 52% of the left 75%: Osc Frequency/Wave keeps its original
size (50% wide, 23% tall). Under it, two columns: **ADSR**, then Velocity over Osc
Slide Range/Time. The column to the right (25%) is Feedback (16.2% of the column),
FM Frequency/Depth and Noise Frequency/Depth, with Noise running down to the dock
so nothing is left between it and the strip.

Sequencer column (right 25%, full height), top to bottom: the panel with
**Sequencer On** and **Asynchronous** (sharing it equally); the STEP SEQUENCER
modal and button; the Sequencer Add, Drop pad immediately below it; the rhythm
controls (left) and frequency
generator (right), with the **Chaos Slew / Evolve Time** buttons under the rhythm
controls; and, at the foot, the **Master Volume column** (Envelope/Sidechain,
Panning, Reverb, Master Volume with Mute, in its own panel `mixcol<n>`).

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
