# Load time and tab switching: what the widget count costs

`_main.json` is 1.7 MB and takes seconds to load, and switching synth tabs has a
visible delay. This is where that time goes, measured rather than argued, and what
each way of reducing it is worth: build-time flattening, client patches, bespoke
canvas widgets, and `multixy`.

Measured on an Apple M5, in the Claude desktop app's built-in Chromium pane at an
emulated 1366×1024 viewport, against Open Stage Control 1.31.0 exactly as
`formuls-0.3.1.app` ships it (`--read-only --client-options framerate=25 hdpi=0`,
`_formuls-default.state` loaded) with `performance.mark()`s added. **Absolute
numbers are a fast desktop; a tablet will be several times slower. The ratios are
the point.** Tooling is in `tools/session/`.

## The short version

| | widgets built | `widget-created` listeners | `value-changed` listeners | load (build + state) | synth-tab switch | per incoming message |
|---|---|---|---|---|---|---|
| **today** | 13,442 | 13,144 | 2,197 | **10.9 s** (9.6 + 1.4) | **63 ms** | **92 µs** |
| **A** flatten clones at build time | 10,694 | 1,961 | 1,768 | **3.6 s** (2.7 + 0.9) | 59 ms | 66 µs |
| A + tab-show patch | 10,694 | 1,961 | 1,768 | 3.6 s | **49 ms** | 66 µs |
| **B** A + slider compounds as one canvas | 7,523 | 904 | 1,466 | 1.8 s † | 42 ms | 53 µs |
| **C** B + sequencer step panel as one canvas | 5,219 | 904 | 698 | 1.1 s † | 36 ms | **24 µs** |

† build only; B and C drop widget ids, so the state file no longer applies to them.

- **A changes nothing the performer can see**. It was verified against today's
  session widget by widget (below), and it is a **3× faster load** and **28% cheaper
  per message**. It does not help tab switching.
- **Tab switching is set by how many `<canvas>` elements a tab holds** (400 on a
  synth tab today), not by drawing or widget count. Only fewer canvases (B, C) or the
  one-line tab-show patch move it.
- **Mode switching is not a cost.** Pressing a mode button produced no long task and
  re-created no widgets, in today's session and in the canvas variant: `interaction` and
  `alphaFillOn` are dynamic props.

## Where load time goes

`SessionManager.load` does three things, and the first two are quadratic:

1. **Build** (`parse`) — 9.6 s. Every widget whose props contain `@{…}`, `OSC{…}`
   or `VAR{…}` registers a **global** `widget-created` and `prop-changed` listener,
   and every widget's creation calls all of them. In today's session that is
   13,144 listeners × 13,442 creations ≈ 88 million callbacks.
2. **Initial value broadcast** — 1.4 s. One global `value-changed` per widget,
   delivered to all 2,197 value listeners.
3. **State** — 1.4 s. `StateManager.set` calls `setValue(…, {sync:true})` for every
   saved id, each another global broadcast.

The curve, from sessions cut down to 0, 1, 3 and 6 synth tabs (build only):

| widgets | build | per widget |
|---|---|---|
| 2,702 | 0.6 s | 0.22 ms |
| 4,492 | 1.6 s | 0.36 ms |
| 8,072 | 3.9 s | 0.49 ms |
| 13,442 | 9.0 s | 0.67 ms |

A fit of `a·n + b·n²` puts about 80% of the full build in the `n²` term. **Halving
the widget count cuts load roughly 3–3.5×, not 2×**, and every option below is
worth more than its widget count suggests.

Two things make the listener count what it is:

- **Nearly every listener is there only for `@{parent.variables…}`.** 12,694
  widgets have a `widget-created` dependency on `parent`, which is how a clone gives
  its contents their ids (`attack@{parent.variables.n}`). Those references never
  change after load.
- **2,385 widgets exist only to be cloned.** The hidden `formuls0` tab (1,790, a
  whole synth panel) and the `Widgets` tab (595) are built, listened to and
  state-loaded like everything else, and never shown. A further 425 widgets are
  the `clone` wrappers themselves.

## Where tab-switch time goes

All seven synth tabs are built at load. A hidden tab is only detached from the DOM,
so a switch is not a rebuild. What `Tab.show()` does is re-attach the tab and call
`check(widget, true)`. That walks every resize-listening element in the session,
reads `getComputedStyle` for each one inside the tab, and then **resizes every
canvas on the tab, forced, even if its size is unchanged**. A resize reallocates
the backing store and redraws.

Draw cost is not the problem. A compound's five faders draw in 6.3 µs between them,
and the canvas that replaces them in 4.7 µs. What tracks the switch time is the
number of canvases:

| canvases on the synth tab | switch, median (p90) |
|---|---|
| 400 (today) | 63 ms (73) |
| 400 (A) | 59 ms (70) |
| 400 (A, unforced resize) | 49 ms (74) |
| 300 (B) | 42 ms (46) |
| 173 (C) | 36 ms (43) |

(15 switches each, any switch during which the page was hidden discarded. The
measurement's own floor, two animation frames with nothing to do, is 3.8 ms.)

## Option A — flatten the session at build time (implemented)

**Shipped as a build step:** `src/tools/flatten-session.py`, run by both build
scripts after `patch-osc-perf.py`, writes the bundle's `_main.json`. Usage,
failure modes and the defaults table are in `src/tools/README.md`.

Resolve every **static** reference (`@{parent.variables…}`,
`@{this.variables…}`, `@{this.id}`) once, replace each clone with the widget it
renders, and replace the template-only tabs with empty placeholders. Anything
genuinely dynamic is left alone: `OSC{}`, `JS{}`, `@{modeselector1.value}`,
`@{this.value}`. You keep editing `_main.json` with clones in the o-s-c editor,
and the build ships the flattened file.

**It must keep tab indices.** Pd learns the selected synth from the root tab
*index* (the `/GET root` poll, see `README.md`). `formuls0` sits at index 1, so it
is replaced by an empty hidden tab rather than deleted.

The first version was `tools/session/flatten.js`, which runs inside a loaded
client and resolves each reference with o-s-c's own `resolveProp`. The build
step is a Python port of `resolveProp`, `balancedReplace` and `balanced-match`
themselves, so it needs no browser. Three subtleties both handle, each found by
the equivalence check failing first:

- A static prop that resolves to an object is written back as a **JSON string**.
  o-s-c coerces strings inside object literals (`"true"` → `true`) but not inside a
  resolved string, so an object literal would change types.
- Nested object props (`gradient: {"0.3": "@{…}"}`) are resolved per value, not as
  the whole prop.
- A clone's own `css`, `interaction:false` and `visible:false` apply to its
  container. They are moved onto the inlined widget, and `css` is joined with a `;`.

### Verified

All of the following were run on the Python build step's output.

- **Every widget, every prop:** `fingerprint.js` + `compare_fp.py` over all
  10,692 widgets of both sessions compared the resolved value of every prop
  (defaults included), the current value, the on-screen rectangle on every visible
  tab, and computed `font-size`, `opacity`, `pointer-events`, `display`,
  `visibility` and `border-radius`. **No differences** other than those
  deliberately moved: a clone's own geometry replacing the inner widget's ignored
  geometry, and the carried `css`/`interaction`/`visible`.
- **Popups:** all 13 popups on synth 1 opened in turn. 988 of 991 widgets match
  exactly, and three are 1 px different (sub-pixel rounding where a clone boundary
  was, in the sequencer popup).
- **Scripts:** the four scripts that depend on ids or DOM structure were audited. The
  FX-chain canvas was driven in both sessions and `fxorder1` received the same order.
  The `modfxa` overlay walks the DOM, but it is switched off (`SHOW_MARKER = false`),
  so it is inert either way. If it is ever switched on, it needs re-checking, since
  flattening removes one wrapper level.

- **Listeners and load:** the same 10,694 widgets, 1,961 `widget-created` and
  1,768 `value-changed` listeners as the browser version.
- **The build:** `./build-macOS.sh` runs it and completes. The bundle's
  `_main.json` is byte-identical to a standalone run of the same command.

### What porting it turned up

Each of these was found by a comparison failing, not by reading:

- **o-s-c's parser deletes every prop the widget type does not define.**
  `for (k in data) if (defaults[k] === undefined) delete data[k]`. So a prop the
  type doesn't define is invisible to `@{}`, and the flattener drops it as well.
- **Inside `OSC{}`, arguments are split on `,` before values are substituted.**
  Splicing a value in as text changes the split if the value contains a comma,
  so such references are left for o-s-c to resolve. The browser `flatten.js` got
  this wrong: it spliced with `String()`, which the fingerprint could not see,
  because no message ever arrives on either address. The Python port matches the
  live client's receiver address exactly.
- **A clone `props` override that does not parse never applies.** It is spread as
  a string, and the parser deletes the character keys. The flattener reproduces
  this and prints a note.

Three overrides in the session never take effect today for these reasons, and
flattening keeps them exactly as they behave now:

- `bpmglobal` (mixer): its `"n": @{parent.variables.n}` is `undefined` there, so
  the override is not JSON.
- `modepanelparent` and `modepanelseq`: they override `n`, which panels don't
  define, so it is deleted.

And one bug that matters more:

- **The keyboard panel's velocity slider gets the whole variables object as its
  `n`.** Its ids come out as `velocity{"n":1,"colour":"#e53db8"}` instead of
  `velocity1`, its address with them, and its mode layers listen on
  `/chaos{"n":1,…}`, which nothing sends. This is how it behaves in today's build.
  The clone override presumably wants `@{parent.variables.n}`.

### Not verified

- Anything with Pd running.
- Multi-client sync.
- Saving state from the GUI. Ids are unchanged, so the state format is too, but this
  wasn't exercised.
- The Linux build script: it gets the same step, but only the macOS build was run.

The flattened file is 2.5 MB against 1.7 MB, because the template text is now
repeated per instance.

## Option B/C — bespoke canvas compounds

`gen_canvas.py` replaces each slider compound with **one** `canvas` (B). A compound
is 22 widgets today (panel, 7 status LEDs, 5 stacked faders, a 6-button mod matrix,
a label switch and a label button). The canvas draws the same information: the
main value with the synth-colour gradient, the four mode values colour-coded and
dashed, the status LEDs, the mod-source cells and the label.

On touch, it reads the active mode with `get('lfofreq1')` etc. and drags that value,
so it holds **no linked props at all**. C does the same for the 128-step sequencer
panel, which is 385 widgets and 128 `@{quantiseseq1.value}` listeners per synth.

These are **benchmark prototypes**: they render and respond to touch, but they are
not wired to Pd, and they don't reproduce snap, double-tap reset or sensitivity.

What they cost to finish, beyond drawing:

- **I/O.** One canvas holds what Pd sends to five or more addresses (`/attack1`,
  `/attackchaos1`, `/attacklfofreq1` …). Something has to map between them:
  - Pd packing and unpacking (the `f.gui.modpack` approach);
  - an o-s-c server `--custom-module` that keeps the packed value and translates
    both ways, leaving Pd untouched;
  - hidden `variable` widgets that forward into the canvas, which costs two
    broadcasts per message.
- **State.** Values are keyed by widget id, so `_formuls-default.state` needs
  migrating.
- **Look.** The prototype draws the same information, but it is a redraw of the
  instrument rather than the same widgets.

Packing does pay once mapped: a 13-value canvas update costs **23.6 µs** against
23.4 µs for a single fader in the same session. The cost is the broadcast, not the
payload, so one message per compound costs the same as one per parameter.

**The sequencer is the best single candidate.** It is one widget instead of 385 per
synth, it has no performance-mode interplay, and its 768 value listeners are 43% of
all value listeners left after A.

## multixy, and the blocker hit last time

**Per-point colours already exist in 1.31.** `pointsAttr` takes one object per point
with `color`, `colorFill`, `colorStroke`, `alphaFillOn`, `pointSize`, `label` and
`visible`. It is a **dynamic prop**, so an `OSC{}` inside it updates in place with no
rebuild. Tested: three points, three colours, rendering as specified.

**What it lacks is "visible but locked".** `visible:false` hides a point and takes it
out of touch selection together, so a mode value cannot stay on screen while locked,
which is the whole idea of the performance view.

`patch-experiments.py --multixy-point-interaction` adds that: a per-point
`interaction` key that keeps the point drawn but skips it when choosing what a touch
grabs. It is two anchors in the same style as `patch-osc-perf.py`. Tested end to end
with `"interaction": "OSC{/lfofreq1, 0}"` and a real `/lfofreq1` over UDP:

- with the mode off, a drag starting on the LFO point left it in place and moved
  the main point;
- after `/lfofreq1 1`, the same drag moved the LFO point, and it filled, because
  `alphaFillOn` followed the same address.

The coercion matters: inside `pointsAttr`, `OSC{/x, 0}` resolves to the **string**
`"0"`, which is truthy, and the first version of the patch got this wrong.

The main point needs the same treatment (locked while any mode is on), because a
touch picks the *nearest* interactive point rather than the topmost layer. And
multixy draws **each point on its own canvas**: it cuts widgets and listeners, so
build time and per-message cost improve, but **not** canvases, so tab switching does
not. It has the same one-widget, many-addresses I/O question as the canvas options.

## Smaller findings

- **Tab-show patch** (`patch-experiments.py --tab-show-unforced`): 58.6 → 48.7 ms on
  the flattened session. Canvases updated while their tab was hidden were checked
  after the switch by comparing the drawn bitmap with a fresh `draw()`, and all were
  up to date. A negative control (clearing a canvas by hand) was detected, so the
  check can fail.
- **A colour reference that never resolves.** 13 template props
  (`@{formuls@{parent.variables}.variables.colour}`, 192 widgets) embed an object,
  which becomes `formuls[object Object]`. Gradient faders such as `panning1` and
  `volumemaster` throw `addColorStop … 'undefined'` on every resize. The intent is
  presumably `@{formuls@{parent.variables.n}.variables.colour}`. It is unrelated to
  performance, and flattening deliberately preserves it.

## What does not help

- **Hiding panels or popups.** Their widgets are built and listening regardless
  (`README.md`, "Why gating in Pd is the only lever").
- **`fragment` widgets instead of clones.** They would move the templates out of the
  session, but each instance still builds the same widgets with the same
  `@{parent…}` listeners. A subsumes it.
- **Packing alone.** It needs fewer widgets to pay, as in B/C.

## Not prototyped: one synth panel for all six

From the 1-tab cut above, one synth tab instead of six is a 4,492-widget session (a
1.6 s build before flattening). It would need a server `--custom-module` that
re-addresses `/attack` ↔ `/attackN` for the selected synth, holds all six synths'
values, and replays the selected one's on switch. At ~1,000 values × 24–66 µs, that
is 25–65 ms per switch before drawing, so it trades tab-switch time for load time.
It is the largest saving available and the largest change.

## Suggested order

1. **A** (done): no visual change, verified, 3× faster load, −28% per message, which also
   helps the disconnects in `README.md`.
2. **The tab-show patch** (done, on `gui-compounds`): one line, −17% tab switch. Now
   applied always by `src/tools/patch-osc-perf.py`.
3. **The sequencer as one canvas** (C without B): the largest further cut per unit
   of work, and it halves per-message cost on its own.
4. **Slider/xy/menu compounds**, as canvases or multixy with the point patch. This is
   the decision about how the instrument looks, the same decision as Step 3 in
   `README.md`, and it brings the I/O mapping with it.
