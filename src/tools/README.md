# Build and diagnostic tools

## brand-osc.sh

Replaces the Open Stage Control client's greeting header

```
Open Stage Control <span class="version">v1.31.0</span>
```

with formuls' own name and version

```
formuls <span class="version">0.3.0-beta</span>
```

Both build scripts call it straight after unpacking the downloaded Open
Stage Control package, so the shipped GUI never carries the toolkit's
branding:

```bash
./brand-osc.sh <open-stage-control-dir> <version>
```

It matches on the header text rather than on a line number (the header is
at line 40 in 1.31.0, but that moves whenever Open Stage Control is
updated), and **fails the build** if the markup no longer matches, rather
than silently shipping unbranded. Re-running it on an already-branded tree
is a no-op, so rebuilds are safe. Written in POSIX sh without `sed -i`, so
it behaves identically on macOS and Linux.

To rebrand an already-built app in place, point it at the bundle and
re-sign:

```bash
./brand-osc.sh /path/to/formuls.app/Contents/Resources/gui/open-stage-control 0.3.0-beta
codesign --force --deep -s - /path/to/formuls.app
```

Only needed for an app that was built before branding was wired into the
build scripts; a fresh build is branded already.


## patch-osc-perf.py

Performance surgery on the same downloaded Open Stage Control package. Both
build scripts run it straight after unpacking:

```bash
python3 src/tools/patch-osc-perf.py build/gui/open-stage-control
```

**Applied always.** `IpcServer.send` ships serialising its payload once per
connected client:

```js
for (var s in i) ... i[s].send(e, t)      // JSON.stringify per client
```

so one parameter move costs N `JSON.stringify` calls for N tablets. The patch
serialises once and hands every client the same string. Worth −4.5% of server
CPU with four clients connected, nothing with one, and no behavioural change.

**Opt-in: `--batch-ms N`.** Also coalesces broadcast value updates into one
WebSocket frame per N ms, using the `bundle` event the client has always
handled but that no server release sends. At four clients that is −64% server
CPU and 83× fewer frames — but it makes the *client* do 32% more work, in
12 ms slabs instead of 0.1 ms slivers. Since a saturated tablet main thread is
what drops the connection in the first place, it is off by default. Turn it on
when the server or the access point is the bottleneck rather than the tablet.

Measurements, and the rig that produced them, are in `docs/gui/`.

Same contract as `brand-osc.sh`: matched on the code rather than line numbers
or byte offsets (it is a webpack bundle, so both move on every upgrade), a
hard failure if an anchor is missing, and a no-op on an already-patched tree.
Python rather than sh because the anchors are long minified strings full of
characters `sed` would need escaping for, and BSD and GNU `sed` disagree about
several of them.


## flatten-session.py

Writes the session the app actually ships. `src/gui/_main.json` is authored
with clones, whose contents take their ids and values from
`@{parent.variables...}`. Every widget carrying a reference like that
registers a global listener, and every other widget's creation calls it, so
Open Stage Control's load time grows with the square of the widget count. This
resolves those references once, at build time:

```bash
python3 flatten-session.py src/gui/_main.json build/gui/_main.json \
    --defaults osc-defaults-1.31.0.json --osc-package build/gui/open-stage-control \
    --template-tab formuls0 --template-tab Widgets
```

- every clone is replaced by the widget it renders, and the clone's geometry,
  `css`, `interaction:false` and `visible:false` are carried onto it;
- static references (`@{parent.variables...}`, `@{this.variables...}`,
  `@{parent.id}`, `@{this.id}`) are resolved. Everything dynamic is left as
  written: `OSC{}`, `JS{}`, `#{}`, `@{otherWidget.value}`, `@{this.value}` and
  scripts;
- the template tabs become empty hidden tabs **in the same position**, because
  Pd reads the selected synth from the root tab index;
- props equal to the type's default are dropped, as are props the type does not
  define. o-s-c's parser refills the first and deletes the second.

Both build scripts run it after `patch-osc-perf.py`. Measured on an M5, load
drops from 10.9 s to 3.6 s, and each incoming message costs 28% less. The
flattened file is 2.5 MB against 1.7 MB.

**Edit `src/gui/_main.json`, never the flattened output.** A dev build that
finds `src/gui` by walking up the tree (`ResourceLocator.h`) loads the
unflattened source: identical behaviour, just the old load time.

It is a line-by-line port of o-s-c 1.31's `Widget.resolveProp`,
`balancedReplace` and `balanced-match`. The quirks are reproduced, not fixed:

- nested `@{}` values are spliced in with `String()`, so objects become
  `[object Object]`;
- inside `OSC{}`, arguments are split on commas *before* values are
  substituted, so a value that would change that split is left as a reference;
- a clone override that does not parse never applies.

It prints a note when it meets one of these, and **fails the build** on anything
it does not model, in the style of `patch-osc-perf.py`:
- scoped clones or `fragment` widgets;
- an ambiguous clone target;
- a clone prop that would be lost;
- `css` set on both a clone and its content in a way that compounds;
- a reference to a widget that only exists in a template tab;
- an o-s-c package whose version differs from the defaults table.

**`osc-defaults-1.31.0.json`** is each widget type's defaults, read from the
1.31.0 client's widget classes. Only the browser bundle holds them, so they
are extracted rather than computed. When the vendored o-s-c version changes,
the build stops until it is regenerated: load any session in the new client
and run `docs/gui/tools/session/extract-defaults.js` in its console.

**Verifying a change to it.** The reference is o-s-c itself:
`docs/gui/tools/session/` fingerprints every widget of the original and
flattened sessions in a browser (resolved props, values, geometry, computed
style, popups) and compares them. The only differences should be the clone
props moved deliberately. See `docs/gui/session-size.md`.

## compile-compounds.py

Runs after `flatten-session.py`. Turns each `slider` compound (15 stacked widgets)
into one canvas, drawn and touched by `src/gui/compounds/slider-lib.js`, and each
128-step `seqsteppanel` (385 widgets) into one, by `sequencer-lib.js`. Pd keeps its
per-parameter addresses: `src/gui/formuls-module.js`, an o-s-c server module, does
the translation from the `compounds.json` this writes.

It also:
- migrates the state file to the packed values;
- rewrites the outside references to a sub-widget (`@{saturation1.value}` ->
  `@{saturation1_c.value.0}`);
- hooks the mode inputs to redraw the canvases they gate;
- wires a widget left on the same address as a canvas slot (the master-panel reverb
  faders) to that slot, both ways, so the pair stays in step on one tablet.

A compound whose wiring is not static is left as authored.

The app passes `--custom-module` only when both the module and `compounds.json` sit
next to the session, so a dev run from `src/gui` is unaffected.

Verifying a change: `docs/gui/tools/session/` has the harness. It records the OSC a
one-compound session sends for a fixed set of gestures, sends both versions the same
Pd-side messages, and compares the state-recall bursts. See
`docs/gui/session-size.md`, "B, implemented".

## check-reset-coverage.py

Checks that every parameter carrying chaos/LFO/mod sub-widgets in
`gui/_main.json` is reset by `f.util.reset.pd`, with the right abstraction
for its widget type and the right mod-matrix flag:

```bash
python3 src/tools/check-reset-coverage.py
```

The same parameter is declared in two places -- the interface builds the
widgets, and Pd zeroes them on reset -- and Pd has no way to derive the
second from the first. When the two drift, nothing complains: Pd sends to an
address no widget owns and the interface never hears about the widget it was
never told to move. That is how the Sequencer Add/Drop, Chorus/Phaser,
Saturation/Bitcrush, Pitchshift, Gate Threshold/Release and Flam widgets came
to sit through a reset untouched, along with Filter Type, Sequencer
Swing/Delay/Warp and Pitch Repeat.

Run it after adding a parameter or renaming a widget. It exits non-zero and
names the fix for each mismatch.


## bpm-probe

Loads the real `pd/_main.pd` under libpd, drives BPM over genuine OSC to
udp 9000 exactly as the Open Stage Control GUI does, and measures the beat
rate `abl_link~` actually produces after each change. It needs no audio
device and no tablet, so BPM behaviour can be tested in isolation from the
GUI.

Build (libpd must already be built -- see `src/app/README.md`):

```bash
LP=../libs/libpd
clang++ -std=c++17 -O2 -w -I$LP/cpp -I$LP/libpd_wrapper \
    -I$LP/libpd_wrapper/util -I$LP/pure-data/src \
    bpm-probe.cpp $LP/libs/libpd.dylib -Wl,-rpath,$LP/libs -o bpm-probe
```

Run it against an assembled app's patch folder (it needs the faust and
`abl_link~` externals, which only exist in a built app):

```bash
./bpm-probe /path/to/formuls.app/Contents/Resources/pd
```

It renders in real time deliberately: `abl_link~` derives its beat from the
host clock, so running flat out makes the beat appear frozen and produces
completely misleading results.

### IMPORTANT: the app runs a *copy* of the patch

`build-macOS.sh` copies `src/pd` into `formuls.app/Contents/Resources/pd`.
Editing `src/pd/_main.pd` therefore has **no effect on an already-built
app** until you re-run the build script (or copy the patch in by hand).
Check with:

```bash
diff src/pd/_main.pd /path/to/formuls.app/Contents/Resources/pd/_main.pd
```

Run bpm-probe against the source patch (plus a built app's externals, which
are not in the repo) to test patch edits without rebuilding:

```bash
mkdir -p /tmp/pd-test && cp -R src/pd/* /tmp/pd-test/
cp -R /path/to/formuls.app/Contents/Resources/pd/externals /tmp/pd-test/externals
./bpm-probe /tmp/pd-test
```

### What it has established so far

- `abl_link~` on its own accepts repeated tempo changes correctly, and
  ignores a bogus `tempo 0` without needing a restart.
- The full `_main.pd`, driven by OSC on `/bpm`, also follows repeated
  changes correctly (90, 160, 100, 160 all measured exact).
- So neither the external's tempo path nor the patch's core BPM chain is
  inherently broken, and compiling the external into libpd would not change
  any of this -- static linking only affects how an object is *found*, not
  how it behaves once created.
- `bpmhold` does not block changes: it stores the current BPM, and
  *releasing* it restores that stored value, silently discarding anything
  set while it was engaged.

The first measurement after a change often reads negative -- the patch
sends `reset` to `abl_link~`, so the beat counter jumps backwards once.
That is an artefact of the probe, not a fault.


## automation-probe.py

Records a gesture into one `f.seq.automater` and reports every discontinuity
in the value it sends towards Faust:

```bash
python3 src/tools/automation-probe.py
python3 src/tools/automation-probe.py --gesture triangle --press 520
```

It builds a throwaway patch that drives a single automater the way the running
app does -- both master throttles, a beat on `clockin`, `record 1`, a gesture,
`record 0` -- runs it under `pd -nogui`, and diffs consecutive output frames.
It needs no audio device, no GUI, no externals and no built app: the automater
and everything under it are plain Pd, so a run costs about as long as the
timeline it simulates (four seconds by default).

A step in that value is what a click sounds like, so this is the cheapest way
to tell a real automation fault from a Faust smoothing problem. It answers a
question reading the patch cannot: *when* a value moves, relative to the beat
and to the record button.

`--gesture` picks what the take records. `ramp` sweeps 0 to 1. `triangle`
returns to where it started, so a correct implementation loops it with no step
at all -- the useful one for judging a fix. `hold` never moves, so any step it
reports is the harness's own fault and not the patch's; run it first when a
result looks surprising.

`--press` and `--release` move the take relative to the beat grid, which is the
axis most automation faults vary along. `--trace` prints every frame.

### Comparing revisions

`--rev` extracts one revision's `controlabstractions` and probes that instead of
the working tree, which is how to tell a regression from something that was
always broken:

```bash
python3 src/tools/automation-probe.py --gesture triangle              # working tree
python3 src/tools/automation-probe.py --gesture triangle --rev HEAD   # last commit
python3 src/tools/automation-probe.py --gesture triangle --rev 2194d48
```

Errors Pd reports while loading a historical tree are printed with the object
that caused them, so an old revision's broken objects are visible rather than
silently changing the result.

### What it has established so far

- **Open:** releasing the record button steps the parameter by an arbitrary
  amount. Automation playback is armed as soon as recording starts, so the
  first beat *during* the take sets the read head running underneath it;
  releasing record swaps the output onto that head mid-gesture. Measured steps
  of +0.72, -0.19, -0.48 and -0.25 for the same gesture, varying only with when
  record was pressed relative to the beat. Reproduce with:

  ```bash
  python3 src/tools/automation-probe.py --gesture triangle --press 520
  ```

- The fault is **not** a regression. `--rev 2194d48`, the commit that
  introduced the JUCE front end and predates all the efficiency work,
  reproduces it exactly.
- A gate on `clockin`, held closed while `$0-record` is 1, was tried in
  `f.seq.automater`'s `TIMING_+_SCHEDULING______` and does fix it -- the
  parameter holds its last live value until the next beat, then playback starts
  from index 0, and a gesture that returns to where it started loops with no
  step at all. It is not in the tree: it changes where the loop's phase comes
  from, which is a musical decision rather than a bug fix.
- Nothing in the Pd value path smooths. The `[line 0 5]` in
  `VALUE_READ_EVOLUTION_SEND` only ever receives bare floats, so it passes them
  straight through; whether a step is audible depends entirely on whether the
  Faust parameter carries `si.smoo`.

## automater-load-bench.py

Measures what N `f.seq.automater` instances cost while they modulate (automation
playback, LFO, chaos, in any combination), plus the number of values each one
sends towards Faust per second. It runs Pd in `-batch` mode, so the figures are
deterministic, and needs no audio device or built app:

```bash
python3 src/tools/automater-load-bench.py -n 400 --modes playback lfo chaos
python3 src/tools/automater-load-bench.py -n 400 --modes chaos --rev HEAD~1
```

See `docs/efficiency/automater-modulation.md` for the results it produced.
