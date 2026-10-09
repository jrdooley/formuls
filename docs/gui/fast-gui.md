# The shipped GUI: flattening and canvas compounds

What the build now does to the Open Stage Control session before it ships, how each
part works, and what to know before changing it. The investigation, every
measurement and the options not taken are in [`session-size.md`](session-size.md).
This document is the summary of what was built and the explanation of its mechanics.

**Branch:** `gui-compounds`, all four stages; not yet merged. **Nothing the performer or Pd
sees has changed:** Pd keeps every address it has today, and the GUI sends the same
messages for the same gestures.

## The result

Measured on an Apple M5 in a Chromium pane, Open Stage Control 1.31.0, the full
session with `_formuls-default.state`. A tablet is several times slower in absolute
terms; the ratios are what carry over.

| | original | A flatten + A+ tab patch | + B slider canvases | + C sequencer canvases |
|---|---|---|---|---|
| widgets built | 13,442 | 10,694 | 7,650 | **5,346** |
| load (build + state) | 10.9 s | 3.6 s | 2.2 s | **1.8 s** |
| synth-tab switch, median | 63 ms | 51.6 ms | 38.9 ms | not measured † |
| client cost per incoming message | 92 µs | ~66 µs | ~50 µs | **~23.5 µs** |

† The test pane was hidden during the run, which invalidates animation-frame timing.
C removes another 131 canvases from each synth tab (304 → 173), and canvas count is
what sets switch time, so it should be lower than B's.

The per-message figure matters beyond speed: it is the client work that, when it
piles up, makes the tablet miss its heartbeat and disconnect ([`README.md`](README.md)).

## Why the original was slow

Three costs, all in the o-s-c client, all set by the *layout* rather than by any one
widget:

1. **Load is quadratic.** Every widget with `@{…}`, `OSC{…}` or `VAR{…}` in its props
   registers a *global* listener, and every widget creation calls all of them. The
   clone-based layout makes almost every widget such a listener, because each clone
   names its contents with `@{parent.variables.n}`. That is 13,144 listeners ×
   13,442 creations at load. All seven synth tabs, plus two template tabs nobody
   sees, are built up front.
2. **Each incoming message is broadcast.** o-s-c fires a global `value-changed` to
   every widget with a linked prop, so a message's cost grows with the number of
   listeners in the session, wherever the addressed widget is.
3. **Showing a tab resizes every canvas on it**, forced, even when nothing changed
   size. A synth tab had 400 `<canvas>` elements.

So the levers are: fewer listeners, fewer widgets, fewer canvases.

## The build pipeline

`src/gui/_main.json` is still the file you edit, with clones, in the o-s-c editor.
The build ships a transformed copy. In `build-macOS.sh` (and `build-linux.sh`), in
order:

```
src/gui/_main.json ─┐
                    ▼
  1. patch-osc-perf.py    patches the vendored o-s-c client (tab-show: no forced resize)
  2. flatten-session.py   clones inlined, static refs resolved          → build/gui/_main.json
  3. compile-compounds.py compounds → canvases, state migrated, map    → build/gui/_main.json
                                                                       → build/gui/_formuls-default.state
                                                                       → build/gui/compounds.json
  4. copy formuls-module.js                                            → build/gui/formuls-module.js
```

At run time the app passes o-s-c `--custom-module formuls-module.js` **only if both
`formuls-module.js` and `compounds.json` are in the GUI folder**
(`src/app/Source/OpenStageControlProcess.cpp`). Running o-s-c directly on
`src/gui/_main.json` therefore still gives you the original, unflattened GUI.

Every stage **fails the build** on anything it does not model (an unknown o-s-c
version, unbalanced braces, a compound of an unexpected shape) rather than ship a
session that differs silently.

## Stage A: flattening

`src/tools/flatten-session.py` evaluates, once, at build time, everything o-s-c would
evaluate at load and that can never change afterwards:

- **Clones are replaced by the widget they render**, with the clone's own geometry,
  `css`, `interaction: false` and `visible: false` carried onto it.
- **Static references are resolved**: `@{parent.variables…}`, `@{this.variables…}`,
  `@{this.id}`. A widget left with no `@{}` registers no global listener.
- **Dynamic references are left alone**: `@{modeselector1.value}`, `@{this.value}`,
  `JS{}`, and `OSC{}`/`VAR{}` wherever substituting would change how o-s-c parses them.
- **The template tabs `formuls0` and `Widgets` are emptied but kept**, because Pd
  learns the selected synth from the root tab *index*.
- Props equal to the type's default are dropped, using a defaults table taken from
  the live client (`src/tools/osc-defaults-1.31.0.json`).

To be identical rather than approximately right, it is a Python port of o-s-c's own
`resolveProp`, `balancedReplace` and `balanced-match`, quirks included:

- a nested `@{}` is spliced in with `String()`, a top-level one as a raw string or JSON;
- inside object members, numbers and booleans stay strings;
- the parser deletes every prop the widget type does not define, so a clone override
  that is not valid JSON never applies;
- `OSC{}` arguments are split on commas *before* substitution.

It also checks that no reference points at an id that only existed in a template.

## Stage A+: the tab-show patch

`patch-osc-perf.py` changes one argument in the client's `Tab.show()`:
`check(this.widget, true)` becomes `check(this.widget, false)`. Canvases are still
resized when their size changes; they are no longer resized when it hasn't. Canvases
updated while their tab was hidden were checked after the switch against a fresh
draw, and were up to date.

## Stages B and C: compounds become one canvas

A **slider compound** was 15 stacked widgets: 5 faders (the main value and the chaos,
LFO frequency, LFO depth and mod-depth layers), 7 status LEDs, a 6-cell mod-source
matrix, a label switch and a parameter-select button. Only one layer takes touches at
a time, chosen by the performance-mode buttons.

A **sequencer step panel** was 385 widgets: 128 cells, each a toggle button under a
10-step fader, with the fader layer active while quantise is held.

`src/tools/compile-compounds.py` replaces each with a single o-s-c `canvas` widget
whose value is an array holding every sub-widget's value:

| compound | canvas id / address | value | count |
|---|---|---|---|
| slider | `<main>_c` at `/fc/<k>` | 20 slots: 0 main, 1 chaos, 2 LFO freq, 3 LFO depth, 4 mod depth, 5–10 mod cells, 11 mod label, 12–18 LEDs (quantise, reverse, act, trigstep, trigseq, chaostrig, chaostrigseq), 19 label | 145 of 151 |
| sequencer | `seqsteps<n>_c` at `/fs/<n>` | 256 slots: 0–127 step toggles, 128–255 step faders | 6 |

The six keyboard velocity sliders are left as authored, because their wiring is not
static (a template bug gives them an object as their `n`).

**Drawing and touch** live in two libraries, `src/gui/compounds/slider-lib.js`
(`globals.FC`) and `sequencer-lib.js` (`globals.FS`). The compiler inlines both into
the root widget's `onCreate`, once. Each canvas's `onDraw`/`onTouch` is a one-line
call into them, passing that compound's constants (its addresses, the ids of its mode
inputs). A slider touch goes to the first active layer in this order: parameter
select, mod depth, mod cells, LFO depth, LFO frequency, chaos, main. Because the
canvas reads the modes with `get()` at touch time, it holds no linked props and
registers no listeners. The mode buttons instead get an `onValue` that redraws only
the canvases they gate (`updateCanvas`, no broadcast).

**Outside references** to a sub-widget are rewritten to the slot:
`@{saturation1.value}` becomes `@{saturation1_c.value.0}`.

## At run time: keeping Pd's addresses

Pd still sends and expects `/attack1`, `/attackchaos1`, `/attackmod1`,
`/seqwrite1-5` and so on. The canvases have one address each. The server module
`src/gui/formuls-module.js` translates between the two using `compounds.json`, which
the compiler writes.

```
                       ┌──────────── o-s-c server ────────────┐
Pd ── /attackchaos1 0.4 ─▶ oscInFilter ─ receive('/fc_patch', 'attack1_c', 1, 0.4) ─▶ mailbox ─▶ canvas slot 1
Pd ◀─ /attack1 0.52 ────────────── (passes through untouched) ◀─────────────────────── canvas script send()
Pd ◀─ /attack1 … /attacklabel1 ◀─ oscOutFilter unpacks ◀─ /fc/3 [20 values] ◀──────── state recall
```

**Pd → GUI: patch one slot, never the whole array.** `oscInFilter` looks the address
up and, instead of passing it on, hands the client a patch for the mailbox: a hidden
`variable` widget with id `fc_patch` at `/fc_patch`. Its `onValue` calls
`globals.FC.patch`, which copies the canvas's current array, writes only the named
slots, and sets it locally without sending. There is deliberately no server-side copy
of the packed values: if the server rebuilt and sent the whole array, an LED flashing
on from Pd while you drag the fader would overwrite the fader with a stale value.

Two refinements:

- Sequencer step-fader values are **quantised to the 10 steps on the way in**, as
  o-s-c's fader did on receipt (an exact tie goes to the lower step).
- The sequencer's **per-step dimming** (`/aseqwriteN-i`, the steps beyond the
  sequence length) was never widget state, only an `OSC{}` opacity. It becomes a
  widget-variable patch (`'@var', canvas, 'op', step, value`) and the library reads it
  when drawing.

An address that a widget still in the session also listens on (a shared one, such as
an effect's toggle that doubles as a slider LED) is patched *and* passed on
(`passthrough` in the map). Every other mapped address is consumed by the module.

**GUI → Pd, gestures: the canvas sends the sub-widget's own message.** A touch sets
the slot locally and calls `send('/attack1', 0.52)` (or `/attackmod1`,
`/seqwriteoff1-7` …) directly from the canvas script. These go through the server
untouched. o-s-c only allows a script `send()` from a user-initiated event, which is
another reason inbound updates go through `set(…, {send: false})`.

**GUI → Pd, state recall: unpacked.** Loading a state makes each canvas send its
packed address once (`/fc/3` with 20 values). `oscOutFilter` drops that message and
sends the per-address messages the stacked widgets would have sent, as floats, in the
same order (`recall` in the map).

## The state file

The compiler migrates `_formuls-default.state` to the packed format: each canvas id
gets its array, built from the old per-widget values. Two details keep recall
identical:

- Keys whose ids still exist (for example `reverb1`, shared between an effect's
  toggle and a slider LED) are kept, and a key shared by several compounds is written
  into all of them.
- Recall order follows the state file's key order.

Recall sends the same 7,140 messages to Pd as before, with the same values. 714 arrive
in a different position within the burst, because a compound whose keys were
scattered through the file now recalls as one block.

States saved by the original GUI use the old ids and won't load into this one. The
compiler only migrates the default state.

## What had to be reproduced exactly

The canvases replace o-s-c's own widgets, so their behaviour had to be copied, not
approximated. Each of these was found by a recording comparison failing:

- **Faders** snap to the touch, then drag relatively. The drag accumulates unclipped,
  so moving past the end and back has to travel the overshoot. Ctrl divides the drag
  by 10.
- **Unchanged values are not sent**, compared at 2 decimals, *before* quantising to
  steps. Exact ties between steps go to the lower step.
- **Double tap** (within 375 ms and 20 px) resets a fader to its default.
- **Traversing drags.** A button drag toggles only cells whose state equals the first
  cell's. With `smart`, it affects only widgets of the type it started on. A fader
  entered mid-drag takes its position relative to the *first* fader, so it usually
  clips.
- **Hit-testing.** The browser picks the touched element at the *rounded* pointer
  position, while a canvas's `offsetX` is floored. Cells are found from the canvas's
  real edge, cached at draw time, and in CSS pixels, because the grid's cells are
  fractional.
- **Colours.** Under `fxa`/`fxb`, `colorBg` is `@{this}`, which a canvas receives as
  `"-1"`. An invalid `fillStyle` is silently ignored and the previous colour reused,
  which gave the first build a white overlay and a full-width quantise LED on those
  eight sliders. The libraries now validate every colour (`validColor`) and set one
  per layer. The sequencer's labels take the text colour CSS actually inherits.

## How it was verified

- **Flattening:** every prop of every widget (10,692), current values, on-screen
  rectangles and key computed styles were fingerprinted in both sessions and compared.
  No differences beyond the intended moves. All 13 popups on synth 1 matched (3
  widgets differ by 1 px of sub-pixel rounding).
- **Gestures:** a fixed set of gestures (taps, drags in every mode, mod cells,
  traversing drags, double taps, label tap; for the sequencer, step taps, row and
  column drags, quantised fader drags) was recorded from the original widgets with a
  UDP recorder standing in for Pd, and replayed on the canvas. The slider's 25 messages
  are byte-identical. The sequencer's 20 match, except where a double tap's reset lands
  in the order (an artefact of synthetic input, not reachable by a finger).
- **Pd → GUI:** every sub-address sent to both versions gave the same values and
  indistinguishable screenshots.
- **State recall:** the same 7,140-message multiset to Pd.
- **With real Pd:** the shipped `_main.pd` running headless against the compiled GUI,
  which is how the `fxa`/`fxb` colour bug was reproduced and its fix confirmed.

The rig is in [`tools/session/`](tools/session/). The reference recordings are
`ref-gestures-attack1.log` and `ref-gestures-seq1.log`.

## Working with it

- **Edit `src/gui/_main.json` as before.** The build regenerates everything.
- **A new slider** made by cloning the existing template is compiled automatically,
  provided its wiring is static. A compound whose shape the compiler does not
  recognise fails the build; one whose wiring is dynamic is left as authored.
- **Changing the look or touch of a compound** means editing `slider-lib.js` or
  `sequencer-lib.js`. Re-run the gesture recordings and look at the result in a real
  synth tab, not only in the one-compound test session: the colour bug only appeared
  under the real panels' colours.
- **Upgrading Open Stage Control** fails the flattener until the defaults table is
  regenerated (`tools/session/extract-defaults.js`, run on the clone-based session),
  and `patch-osc-perf.py` fails if its anchor text has moved. Both are intentional.
- **Running from `src/gui`** gives the original session, without the module. That is
  the easiest way to compare old and new side by side.

## Not verified

- Several tablets connected at once.
- A real touch screen. Gestures were driven with a mouse; multi-touch is handled per
  pointer but untested.
- The Linux build script (it has the same steps; only the macOS build was run).
- Tab-switch time with C.

## Files

| file | role |
|---|---|
| `src/tools/flatten-session.py` | Stage A. Usage and failure modes in [`src/tools/README.md`](../../src/tools/README.md). |
| `src/tools/osc-defaults-1.31.0.json` | Per-type defaults the flattener uses; regenerate with `tools/session/extract-defaults.js`. |
| `src/tools/patch-osc-perf.py` | Stage A+ (and the earlier server patches). |
| `src/tools/compile-compounds.py` | Stages B and C, the state migration, and `compounds.json`. |
| `src/gui/compounds/slider-lib.js` | `globals.FC`: slider drawing, touch and the mailbox `patch`. |
| `src/gui/compounds/sequencer-lib.js` | `globals.FS`: sequencer drawing and touch. |
| `src/gui/formuls-module.js` | o-s-c server module: inbound patches, outbound recall unpacking. |
| `src/app/Source/OpenStageControlProcess.cpp` | Adds `--custom-module` when the module and map are present. |
| `build-macOS.sh`, `build-linux.sh` | Run the stages in order. |
