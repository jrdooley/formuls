# Claude Activity History

This file acts as a summary file of Claude's activity during a session. At the end of each session, tell Claude to "append a short session summary to docs/claude/CLAUDE.md and copy over any artifacts generated that are currently outside the working branch".
---

## 4 September 2026 — audio crackling, then the filter

Started from two reports: a click when the automation record button is
deselected, and artefacts when the `pitchshiftpitchmix` pair is automated. Both
were suspected to be regressions from the preceding "quick fixes" session. **They
were not** — verified, not assumed.

Ten commits, `0642f24..4cff632`, pushed to `juce-port`.

### Fixed

**`faust: smooth pitchshiftmix and fxsend` (236b19c).** Both are gains into
`si.interpolate` and both were being rewritten as hard steps every 10 ms by the
parameter throttle. `pitchshiftmix` crossfades dry against a ±24 semitone
`ef.transpose` copy, so every step was a discontinuity between two very different
signals — the reported artefact. Every comparable wet/dry in `ffx.lib` already had
`si.smoo`; these two were the outliers, and `git log -S` shows `pitchshiftmix`
never had it. Compile-checked with the build's own flags.

**`pd: stop the filter pad's two nodes broadcasting on its shared address`
(46036eb).** The Filter Frequency/Q pad is one multixy assembled from two
`f.util.oscformatxy` instances, and each was also broadcasting its own
half-length pair on the pad's address. `f.formuls`'s `MESSAGE_FILTER` is meant to
block that and cannot — it routes on the selector, but `oscformat` has already
turned the message into a byte list whose selector is `list`. Harmless in
practice (o-s-c ignores wrong-length arrays) but a trap for whoever next changes
the pad's point count. `f.util.oscformatxy` now takes an optional twelfth
argument to keep an instance's GUI value off the wire; unflagged instances are
unaffected because an unsupplied `$n` expands to 0.

**`pd: delete submodroute's channel 7 signal path` (4cff632).** Removed
`[r~ 7-m] → [*~] → [throw~ $0-out]` and its gain leg. Worth recording *why*
deletion rather than repair: `[r~ n-m]` pairs with `[s~ $1-m]` inside
`f.formuls~`, and `_main.pd` creates instances 1–6 — so `7-m` has never had a
writer. The branch was silent long before it was parked as `-r~ 7-m`. The channel
7 control side is deliberately left wired.

Also committed, authored in the preceding session rather than this one:
`0642f24` (build scripts finally call `brand-osc.sh`), `53bcc6e` (reset coverage
across the widget set, plus three argument bugs of the same shape), `53df17b`
(the automation clock's dead receive), `d709d60` (the docs index).

### Diagnosed, deliberately not fixed

**The click on record release.** Automation playback is armed as soon as
recording starts, so the first beat *during* a take sets the read head running
underneath it; releasing record swaps the output onto that head mid-gesture and
the parameter steps by an arbitrary amount. Measured +0.72, −0.19, −0.48 and
−0.25 for the same gesture, varying only with when record was pressed relative to
the beat. Not a regression: `--rev 2194d48`, which predates the JUCE front end
and all the efficiency work, reproduces it exactly.

A gate on `clockin` fixes it — the parameter holds its last live value until the
next beat, then playback starts from index 0, and a gesture returning to its
start loops with no step at all. Built, verified, then **reverted on request**:
it changes where the loop's phase comes from, which is a decision about the
instrument rather than a bug fix. The exact place it goes is named in
`src/tools/README.md`.

**The filter pitch-track dead zone.** With pitch track on the cutoff is
`clip(node + carrier, 0, 1)`, and faust maps that onto MIDI 0–136, so node and
carrier share one range and the node's usable travel is only `1 − carrier`.
Measured **42% of frames pinned at the ceiling** with the node at 0.3 and the
carrier sweeping, and a node move from 0.3 to 0.6 in that region changing nothing
— the "moving the node doesn't adjust the cutoff" half of the report. Left in
place on request. Both candidate mappings are ready in `docs/filter/` as patches,
verified to apply and to remove the dead zone entirely.

**The node jumping** turned out not to be a patch fault. A `ws-watch.js` capture
of the live app showed Pd echoing back recent-but-superseded positions 65–470 ms
late; consistent with record having been armed on that widget by accident, and it
stopped reproducing afterwards. Same mechanism as the click above — playback
replaying a take while it is still being made.

### Tools added

**`src/tools/automation-probe.py`** drives one `f.seq.automater` headless the way
the app does and reports every step in the value reaching faust. No audio device,
no GUI, no externals, no built app. `--gesture hold` is the control — it must
report nothing. `--rev` probes a git revision instead of the working tree, which
is how the "is this a regression?" question got answered in one command rather
than by argument.

**`docs/gui/tools/ws-watch.js`** connects to a running app's o-s-c server as an
extra headless client and prints the frames it receives. No browser console, no
change to the client, and nowhere near the OSC ports `osc-sink.py` must avoid
while the app is live. It settled the node-jump question in one capture after two
headless rigs had failed to reproduce it.

### Worth carrying forward

- **Nothing in the Pd value path smooths.** The `[line 0 5]` in
  `VALUE_READ_EVOLUTION_SEND` only ever receives bare floats, so it passes them
  straight through. Whether a jump is audible depends entirely on whether the
  faust parameter carries `si.smoo`. Still unsmoothed: `gatethresh`,
  `phaserdepth`, `phaserfeedback`, `sustain`, `fmdepth`, `fmfreq`, `noise`,
  `oscsliderange`.
- **Client and Pd frames are distinguishable in a capture.** The client sends the
  widget's configured decimals, Pd echoes full float precision. That is how to
  tell which side wrote a value when both target the same address.
- **Three variants of one Pd-file-editing trap cost time today.** An `#X obj`
  inserted mid-list silently renumbers every connection after it. The ROOT
  canvas's connect block is the *trailing* run of `#X connect` lines — the first
  one in the file belongs to a nested subpatch. And a comment record without its
  `#X ` prefix makes Pd try to create an object called `text`. Every one was
  caught by loading the patch afterwards and reading the console, which is now
  the routine after any hand edit.

---

## 5 September 2026 — VU meters, sequencer modal fix, screenshot tool, AGENTS.md

Full session on `juce-port`. Four distinct tasks plus repo setup.

### Commits

`a0213f5` — `docs: add AGENTS.md for the juce-port branch`
`55d5f95` — `gui: add stereo VU meters and right-justify Record button`
`eaf7ec6` — `gui: add screenshot button, fix sequencer modal, load O-S-C state file`
`c2ad0ee` — `gui: fix screenshot button — directory chooser and ChildProcess`

### 1. AGENTS.md

Created `AGENTS.md` for the `juce-port` branch — a compact instruction file for
future AI sessions covering build prerequisites, JUCE architecture, directory
layout, signal flow, critical gotchas (abl_link~ Xcode bug, Faust `.lib` files,
Pd patch copy trap, `formuls-quit` mechanism), diagnostic tools, and branch
context pointing to `docs/README.md`.

### 2. Stereo VU meters

Added horizontal stereo VU meters to the JUCE app window monitoring the first
two audio output channels from libpd.

- **`src/app/Source/StereoMeter.h`** — header-only component: green→yellow→red
  gradient bars with white peak hold markers, 30Hz timer with smart repaint.
- **`src/app/Source/FormulsEngine.h/cpp`** — added `std::array<std::atomic<float>,
  2> peakLevels` computed on the audio thread, zeroed on device stop.
- **`src/app/Source/MainComponent.cpp`** — meters placed between button row and
  address panel, same width as the address panel. Record button right-justified
  via `removeFromRight()`.
- **`src/app/Source/FormulsLookAndFeel.h`** — `meterHeight` constant, window
  height bumped 446→553.
- **`src/app/formuls.jucer`** — registered `StereoMeter.h`.

### 3. Sequencer modal fix

The `sequencer@{parent.variables.n}` modal in `src/gui/_main.json` was always
open on synth tabs. Two root causes found and fixed:

1. **Missing `--state` flag.** The JUCE launcher (`OpenStageControlProcess.cpp`)
   did not pass `--state _formuls-default.state` to O-S-C, unlike the old Python
   launcher. The state file sets `sequencer1:0` through `sequencer6:0`. Fixed by
   adding the `--state` argument when the file exists.

2. **`click: false` + empty `default`.** The modal had `click: false` (renders
   inline, always visible) and `default: ""` (no initial value). Changed to
   `click: true` (renders as a tappable button with closeable popup) and
   `default: "0"` (popup starts closed).

### 4. Screenshot tool

**`src/tools/screenshot-gui.py`** — Playwright/Chromium script that connects to
the running O-S-C server, finds all tabs in `_main.json`, clicks each visible
tab, and saves a PNG. Skips hidden tabs (`display: none`) and the RESET page.

- Prerequisites: `pip3 install playwright && python3 -m playwright install chromium`
- Default viewport: 2048x1536 (tablet-sized), customisable via `--width`/`--height`
- Output: `~/Desktop/formuls-gui/` (customisable via `-o`)

**Take Screenshot button** added to the JUCE app window, right-justified above
the Record button. Enabled only while the engine is running. Opens a directory
chooser dialog (default: `~/Desktop/formuls-gui/`), then launches
`screenshot-gui.py` via `juce::ChildProcess` with the chosen `-o` path. The
script is found by walking up from the resource root to locate `src/tools/` in
the repo, since `tools/` is not copied into the app bundle.

### 5. MODULEPATH bug fix

`src/app/formuls.jucer` had `../../../../../JUCE/modules` (5 levels up →
`/Users/JUCE/modules`). Fixed to `../../../../JUCE/modules` (4 levels up →
correct `~/JUCE/modules`). Pre-existing bug, surfaced during the VU meters build.

### Session cost and energy

| Metric | Value |
|---|---|
| **Cost** | $0.33 (as reported by MiMoCode session meter) |
| **Energy** | ~0.22 kWh (~790 kJ) estimated for inference compute; negligible local CPU for builds |

---

## 27 January 2026 — repeater and flooper refactor in ffx.lib

Review and refactor of two audio effects in `src/faust/ffx.lib`. One commit,
`07a1248`, pushed to `juce-port`.

### Commit

`07a1248` — `faust: refactor repeater and flooper in ffx.lib`

### Repeater (lines 257–305)

**Bug fixes:**
- Right channel beat divisions were beat *multiples* (1, 2, 4, 8, 16), not
  subdivisions. Both channels now use divisors (4, 5, 6, 8, 10, 12, 16).
- Slider range was 1–5, leaving voices 6–7 unreachable (dead code). Now 1–7.
- Asymmetric voice count (7 left, 5 right) — now balanced at 7 per channel.
- `de.sdelay` max size was hardcoded 96000 samples (2s at 48kHz). Now derived
  from sample rate: `int(ba.sec2samp(60.0) * 16)`.
- Integer division `tempo/(d)` truncated at edge BPMs. Now `float(tempo) / d`.

**Refactor:**
- Unified `delayvoice(cap, cnt, sel, i, d, x)` replaces separate `delayvoicel`
  and `delayvoicer`. Single point of change for voice logic.

**Note:** `par(i, N, fn)` was tried but breaks when `fn` uses `~` (feedback
operator) with signal parameters — the feedback loop doesn't correctly route
parameterised signals. Explicit comma-separated parallel composition was used
instead.

### Flooper (lines 393–439)

**Bug fixes:**
- Right channel `rwtable` initial value was 0.1, not 0.0 — DC offset before any
  recording.
- `floopertablesize` was hardcoded 3840002 (80s at 48kHz). Now
  `int(ma.SR * 80) + 2`, safe at any sample rate.
- `speed2` was an exact duplicate of `speed` (both `hslider(...) : si.smoo`).
  Removed; `speed` used throughout.
- Sample dropout was muting by zeroing `readindex`, which read the empty guard
  sample. Now applied as amplitude multiplication after the table read.
- Cosine grain envelope replaced with parabolic window `env = 1 - (2p-1)²`
  where `p = 2*phase - 1`. Both close to zero at grain boundaries (first version
  `1 - phase²` did not close at the end, causing clicks — fixed during testing).

### Shared helper

`exp27 = pow(2.7)` added at file top. Used by djfilter (2 sites) and repeater
level. `ba.tabulate` was tried for a lookup table but doesn't compose in signal
chains — `rdtable`'s internal `_` passthrough creates extra inputs when the
helper is used inside `with` blocks or `*(...)`. `pow(2.7)` is ~15 cycles per
call, negligible at audio rates.

### Session cost and energy

| Metric | Value |
|---|---|
| **Cost** | TBD |
| **Energy** | TBD |

---

## 6 September 2026 — build path fix, OSC routing investigation

Session on `juce-port`. Build fix committed; OSC routing diagnosed but not yet fixed.

### Commit

`319c110` — `build: set JUCE module path dynamically before Projucer resave`

### 1. Build: JUCE module path fix

`build-macOS.sh` failed on fresh checkouts at different directory depths because
`formuls.jucer` contains hardcoded relative `MODULEPATH` entries
(`../../../../JUCE/modules`) that only resolve when the repo sits exactly two
levels below home.

Fix: before `--resave`, the build script now derives the absolute JUCE modules
path from the Projucer binary location (4 `dirname` calls from
`Projucer.app/Contents/MacOS/Projucer` to the JUCE root), then uses `sed` to
rewrite every `path="...JUCE/modules"` attribute in `formuls.jucer` to the
absolute path. Projucer's `--set-global-search-path` was tried first but does
not override per-project MODULEPATH entries in the `.jucer` file.

Verified: fresh `git clone` to `/tmp`, `./build-macOS.sh` succeeds.

### 2. OSC routing investigation — **RETRACTED, see 6 September (audit)**

> **This diagnosis was wrong.** Both "defects" were counting errors, not faults
> in the patch. `OPEN_STAGE_CONTROL_RECEIVE_` routes correctly and always has;
> nothing here needs fixing. The original text is struck through rather than
> deleted so the mistake stays legible.

~~The `OPEN_STAGE_CONTROL_RECEIVE_` subpatch in `_main.pd` at commit `664cf31`
has two pre-existing defects that prevent GUI parameter changes from reaching
the Pd backend:~~

- ~~**Broken data path.** Connection `#X connect 1 0 22 0` references object 22,
  which does not exist in the subpatch (only objects 0-21).~~ Object 22 does
  exist: it is `route pd GET oscmonitor testtone`, and the subpatch has 25
  objects (0-24). The connection is valid.
- ~~**`listen 9000` never triggered.**~~ It is triggered: `#X connect 2 0 14 0`
  wires `loadbang` to `msg listen 9000`. Port 9000 opens normally.

Multiple approaches to add a startup gate (spigot, del 3000) were attempted but
reverted due to Pd's object numbering with `#X text` comment lines - text
objects ARE counted for connection indices, which shifted all references after
the comment at line 46. The `juce-port` branch was reset to `664cf31`.

The irony is exact: the sentence above states the very rule the diagnosis broke.
`#X text` records *are* counted, which is precisely why object 22 exists. The
lesson to carry forward is not the rule (it was already written down) but the
method - **count objects with a parser, never by eye**, and check the claim
against the whole file before acting on it.

### Session cost and energy

| Metric | Value |
|---|---|
| **Cost** | TBD |
| **Energy** | TBD |

---

## 6 September 2026 (later) - codebase audit, repeater sizing regression

Read-only audit of the whole tree, then one fix. On `juce-port`.

### Fixed

**`faust: size repeater delay lines per voice, not 64x over`.** `f_repeater~`
was allocating **28.0 GiB**. `ffx.lib`'s `maxsamp` read

    maxsamp = int(ba.sec2samp(60.0) * 16);  // largest delay: 1 BPM, division 16

but the delay a voice asks for is `tempo / d`, so the longest is
`sec2samp(60) / 4` - the factor should have been `/ 4`, not `* 16`. 64x over,
which faust then rounds up to 2^28 samples per line across 28 lines. The
comment describing the slider was right; the arithmetic did the opposite.

Introduced by `07a1248` (the 27 January refactor), where it replaced a
hardcoded `96000`. Pre-refactor the struct was ~12.6 MB, so this was a live
regression: `new mydsp` either fails outright or faults in 28 GiB on
`instanceClear`. The repeater has been unusable in any build from that commit
onward.

Now `maxsamp(d)` sizes each voice from its own divisor, and `minbpm = 1` is a
named constant feeding **both** the `hslider` floor and the sizing expression,
so the two cannot drift apart again - which is what made the bug possible.

| | `sizeof(mydsp)` | delay lines |
|---|---|---|
| before | 30,064,772,396 B (28.00 GiB) | 28 x 2^28 |
| after | 268,436,804 B (256.00 MiB) | 8 x 2^22, 12 x 2^21, 8 x 2^20 |

112x smaller. Verified by compiling with the build's own flags
(`faust -vec -lv 0 -vs 4`) and summing the generated `mydsp` struct, before and
after. The three line sizes account for all 28 lines exactly: divisors 4-5 land
on 2^22, 6-10 on 2^21, 12-16 on 2^20, x4 for two channels x two ensembles.
Delay *times* are untouched, so there is no audible change.

### Worth carrying forward

- **Faust sizes every `ba.sec2samp` table for 192 kHz.** `platform.lib:54`:
  `SR = min(192000.0, max(1.0, ...))`. That is why the repeater is 256 MiB and
  not the 64 MiB the arithmetic suggests at 48 kHz. Any table sized in seconds
  carries 4x headroom over the app's 48 kHz default, whatever rate it runs at.
- **Two levers remain on that 256 MiB, both behaviour-changing, both deliberate
  choices rather than bugs.** Raising `minbpm` to 20 gives 13.0 MiB (about the
  pre-refactor footprint), 30 gives 8.0 MiB; below the floor the longest repeats
  truncate. Clamping the SR assumption to the app's 96 kHz ceiling halves it
  again, at the cost of truncating on a device that opens at 192 kHz. Left
  alone: narrowing the BPM range is a decision about the instrument.

### Also fixed: the build scripts (`68fd729`, `9a3330a`)

Three defects, all invisible in `git status` because the files involved are
gitignored.

- **A stale GUI was being shipped.** `cp -r src/gui build/gui` ran without
  clearing `build/` first, while `src/gui/open-stage-control/` sat there as a
  15 MB leftover from an earlier run. The fresh download then landed *inside*
  the stale tree, so `brand-osc.sh` read the old already-branded `index.html`,
  took its "already branded; leaving it alone" path and exited 0 - the build
  looked clean and shipped the old GUI. Now `build/` is removed first and only
  the two files the app actually reads are copied (`OpenStageControlProcess.cpp`
  reads exactly four things from `gui/`: those two plus `node` and
  `open-stage-control`, both of which the download steps provide).
- **Downloads landed in the source tree.** Both scripts `cd`'d to `src/gui` to
  wget and unpack, which is how the stale copies got there. They now unpack in
  `build/`.
- **The macOS build left `formuls.jucer` dirty** with a machine-specific
  absolute `MODULEPATH`, one `git commit -a` from being baked in. Now backed
  up, rewritten, resaved and restored, with a `trap` so the restore happens
  even when a later step fails under `set -e`.
- `src/gui/node` is now gitignored - the bare name was not covered by
  `src/gui/node-v*`, so a 105 MB binary sat untracked.
- **`build-linux.sh` never received `319c110` at all** and still had the
  original MODULEPATH bug. Ported, and **marked UNTESTED in the script**: no
  Linux toolchain here. Two deliberate differences from macOS - a temp-file
  rewrite instead of `sed -i` (BSD and GNU disagree about that flag;
  `brand-osc.sh` already avoids it and says why), and an upward search for
  `modules/juce_core` instead of counting dirnames, because the Linux
  Projucer's depth depends on how it was built.

Verified in a sandbox mirroring the layout: the old staging reproduces the
nesting and the `brand-osc.sh` no-op; the new staging leaves the package
unnested and `brand-osc.sh` brands it for real. The `.jucer` save/restore was
exercised on the success path and on a genuine `set -e` abort, on both scripts.

### Also fixed: the ungated debug `[print]` (`b095838`)

`[r o-s-c-messages] -> [route multixy_1] -> [f.util.multitouchdebounce] ->
[print]` in `OPEN_STAGE_CONTROL_RECEIVE_` had no gate, so under libpd every
frame of every multi-touch drag reached `juce::Logger`. Removed, and the
canvas's connect block renumbered: object 21 goes, and the three objects after
it shift from 22/23/24 to 21/22/23.

**The other bare `[print]`, in `O-S-C_&_FORMULS_SEND_______`, was deliberately
left.** It sits behind a `[spigot]` whose toggle inits to 0 and is re-closed by
a `[del 1000]` - an off-by-default diagnostic, not a leftover probe. Worth
knowing before anyone greps for `#X obj .* print;` and removes both.

Method, given the renumbering trap: a before/after dump resolving every
connection to its source and destination **objects** rather than indices, which
came out identical apart from the removed link; then loading the patch in Pd
0.56-2 before and after, with byte-identical console output and no creation or
connection errors. The `netreceive: Address already in use` lines present in
both runs are a running formuls app holding 9000/9009 - which incidentally
confirms that `listen 9000` does fire, as the retraction above says.

### Deliberately kept: `flooper` and `ott`

Both are unreachable from `fx`, `formuls.dsp`, `f_reverb.dsp` and
`f_repeater.dsp` - confirmed, not assumed: zero occurrences in the generated
C++. So the 27 January flooper rework does not affect the shipped instrument.

**They stay. Do not remove them** - decided on request, 6 September 2026. Not a
pending cleanup and not to be re-proposed; if a future session lists them as
dead code, that is this note's cue, not a new finding.

What is worth knowing *if* either is ever wired into the chain: two divisions in
`flooper` produce NaN, which would poison the output for the rest of the
session.

- `readindex : %(D)` takes its modulo from an `si.smoo`'d value whose one-pole
  starts at 0, and `fmod(x, 0)` is NaN.
- `phase = countmaster / (samplelength * speed)` divides by zero whenever
  `samplelength` truncates to 0 - reachable at `duration` 1024 with `speed` 20
  on a short take.

Neither can fire while the functions are unreachable. They are recorded here so
that wiring `flooper` up starts from a known list rather than from a debugging
session.

### Found, not fixed

Reported in full in-session; recorded here so they are not re-derived.

All small, none urgent.

- `StereoMeter::paint` draws one `drawVerticalLine` per pixel - about 1000 draw
  calls per frame at 30 Hz. A `ColourGradient` fill is one call and looks the
  same.
- `MainComponent::screenshotClicked` captures raw `this` alongside its
  `SafePointer` and uses the raw one. Safe today only because the null check
  runs first, which defeats the point of the `SafePointer`.
- `populateDeviceList` enumerates every `AudioIODeviceType`, but
  `deviceManager.initialise()` resolves the name against the current type only,
  with `selectDefaultDeviceOnFailure = true`. On Linux with ALSA and JACK both
  present, picking a JACK device by name silently opens the ALSA default and the
  status line still reports success. macOS is unaffected (CoreAudio only).
- Six abstractions are never instantiated: `f.util.floatint`,
  `f.util.matrixunpack`, `f.util.message`, `f.util.seqwriteparse`,
  `f.util.xylfoparse`, `f.util.xywidgetparse`.

### Checked clean

All 41 patches in `src/pd/` parsed for dangling connections - **none**. All
`s~`/`r~`, `throw~`/`catch~` pairs resolve. All three `.dsp` files compile.
`_main.json` parses. `check-reset-coverage.py`: all 36 parameters covered.
`sequencer0`-`sequencer6` present in `_formuls-default.state`. Every Python
tool byte-compiles.
