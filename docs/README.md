# formuls — what changed, why, and where the detail lives

A single index to the recent work on **`juce-port`**, the branch that will become
`main`. Each entry says what happened and why, gives the measured result where one
exists, and points at the document that carries the full argument.

Everything below landed between **1 and 4 September 2026**, in 31 commits on
`juce-port` (`git log main..juce-port`). Earlier history is ordinary instrument
development and is not covered here.

## Branch map

| branch | what it is |
|---|---|
| **`juce-port`** | The live line of development. Everything in this document. Will become `main`. |
| `main` / `development` | The Python-era instrument plus four bug fixes. Superseded — `juce-port` carries content-equivalent copies of all four. |
| `alacarte-prototype` | A discarded experiment. **Nothing was ever merged from it.** Three fixes were *authored* there and re-made independently on the other branches (different SHAs, same change): the chorus fix, the Link tempo readout, and the prebuilt `abl_link~`. |

Because those four `main` commits (`1be83bc`, `2c95a92`, `7de1b8a`, `40e5a28`) are
duplicates of work `juce-port` already has rather than ancestors of it, **`git log
juce-port..main` is not a list of things `juce-port` is missing.** Confirmed by
content: the only remaining difference in `src/faust/ffx.lib` between the two
branches is the `multiwave4` cleanup from the efficiency work, not the chorus fix.

## The documents

| document | covers |
|---|---|
| [`efficiency/README.md`](efficiency/README.md) | The Faust + Pd efficiency refactor: findings, results, options left unapplied, harness caveats. |
| [`efficiency/report.html`](efficiency/report.html) | The same efficiency findings as a presentation-layer report. A **snapshot**, not a live document: measured 3 September 2026 at `e360ce7`, with an inline note on the one table `480cc97` superseded. |
| [`efficiency/tools/README.md`](efficiency/tools/README.md) | How to re-derive every number above. |
| [`gui/README.md`](gui/README.md) | Why the control GUI disconnects, what each proposed fix is worth, what shipped, and the JUCE/WebView alternative costed. |
| [`gui/tools/README.md`](gui/tools/README.md) | The OSC/WebSocket load rig, step by step. |
| [`../src/app/README.md`](../src/app/README.md) | The JUCE application: source layout, recording, GUI addresses, styling, building, test harness. |
| [`../src/prebuilt/README.md`](../src/prebuilt/README.md) | Why `abl_link~` ships prebuilt and must not be built locally on macOS. |
| [`../src/tools/README.md`](../src/tools/README.md) | `brand-osc.sh`, `patch-osc-perf.py`, `bpm-probe`. |
| [`../src/tools/abl-link-repro/README.md`](../src/tools/abl-link-repro/README.md) | Minimal reproduction and root-cause trace of the Ableton Link tempo limit. |
| [`../INSTALL.md`](../INSTALL.md) / [`../README.md`](../README.md) | Build requirements and the user-facing feature list, both updated for the port. |

---

## 1. The front end became a JUCE application

**`2194d48`** — see [`../src/app/README.md`](../src/app/README.md)

The Python/Tkinter launcher **and** the standalone `formulsengine` binary (RtAudio
+ libpd) were replaced by one JUCE app in `src/app` that embeds libpd directly. It
picks the audio device, output channel count (2 = stereo, 14 = isolated synth and
bus outs) and sample rate; on **Start** it boots libpd on `src/pd/_main.pd` and
launches the bundled node build of Open Stage Control as a child process.

Two things it fixes rather than merely reimplements:

- It **lists the web addresses the control GUI is reachable at**, enumerated from
  the machine's own interfaces via `juce::IPAddress::getAllAddresses()` — so nobody
  has to look up their IP by hand. Loopback first, link-local skipped, IPv6 omitted.
- `pd quit` was a no-op under libpd, so the patch could never close the app. The
  patch now sends to the `formuls-quit` receive symbol, delivered on the message
  thread through libpd's queued interface.

Net: **−12,341 lines** (RtAudio, PyInstaller, the Tkinter script) for **+1,645**.
Build scripts drive Projucer + `xcodebuild` instead of PyInstaller. Python is no
longer a build or run dependency.

## 2. Repository hygiene

**`12b9d1f`**

A complete built `.app` bundle had been committed — 222 files, including
`libpd.dylib`, eight compiled Pd externals and a vendored Open Stage Control
distribution, all of it reproducible by `build-macOS.sh`. The old rules only
ignored `/build/` at the repo root. Tracked files: **312 → 88**, with `src/faust`
and `src/pd` untouched. Deliberately no `*.lib` rule — the Faust sources
`ffx.lib`, `fsynth.lib` and `fosc.lib` use that extension.

## 3. Ableton Link: tempo changes stopped after exactly 16

**`c737098`, `3c67d3d`, `a8d3d76`** — see
[`../src/prebuilt/README.md`](../src/prebuilt/README.md) and
[`../src/tools/abl-link-repro/README.md`](../src/tools/abl-link-repro/README.md)

Dragging the tempo slider stopped having any effect after 16 changes — the same
count at 30 ms, 100 ms and 300 ms steps, so a count rather than a rate. Reproduced
in a patch containing nothing but `abl_link~`, then traced into Link itself:
`Controller.hpp` holds a 16-entry `mClientStateFifo` that `setClientStateRtSafe()`
silently discards into when full, and **both** threads that should drain it are
dead in this embedding — the dispatcher's `condition_variable::wait_for()` never
returns, and the io context's `run()` returns immediately despite a live work guard.

The cause turned out to be **the build, not the source**: byte-identical source
built with a current Xcode (libc++ 2100) fails, and the Deken package build
(libc++ 1300) works. Deployment target, C++ standard, optimisation level and
`-ffast-math` were each ruled out by rebuilding. So the known-good arm64 binary
ships in `src/prebuilt/`, and `build-macOS.sh` prefers it, warning and falling back
to a source build if it is missing. Linux is unaffected and still builds from
source. `src/tools/bpm-probe` and `src/tools/abl-link-repro` are the harnesses for
re-verifying a refreshed binary — a broken one is silent until a performer drags
the slider.

`a8d3d76` restores this branch's `$ROOT`-based form of that conditional, which the
`main` merge had replaced with a relative-path version that worked only by
coincidence.

## 4. Two audio bug fixes carried over from the discarded prototype

**`fc0c9bd`** — *the chorus was notching 50 Hz all the time.* The chorus stage
summed its delayed copy at a hardcoded level of `1`, while the slider only drove
`depth` (the delay-time modulation). At `chorus = 0` the modulation collapsed and
the delay froze at a static 10 ms summed at unity gain — a fixed feedforward comb
`(1 + z^-10ms)` permanently in the path, with a total null at 50 Hz and −18 dB at
48/52 Hz. Because it sits after `fs.synth` it hit mono and polyphonic modes alike.
Level is now `log2(depth+1)`: zero at 0, unchanged at full depth.

**`c9cc7cf`** — the GUI tempo readout is driven from Link on its own address, so
the displayed BPM follows the session rather than the last value sent.

## 5. Efficiency: the synth and the idle control load

**`ace39e5`, `68365d9`, `f1f38cf`, `be0df71`, `4952fc1`, `8087a66`** (plus docs
`afde34d`, `544877f`, `5680863`, `e360ce7`, `3f304c6`) — full write-up in
[`efficiency/README.md`](efficiency/README.md), and the same findings as a
formatted report in [`efficiency/report.html`](efficiency/report.html)

Profiling put **50%** of all non-idle CPU in the `formuls~` synth, and found that
with DSP **off** the patch still burned **17.4% of a core** doing nothing —
the two master throttles fanning out to 427 automater instances. Nothing here
changes what the instrument does; the two changes that alter any number at all are
quantified and bounded in the document.

- **Faust hot path** — three `powf`, one `sinf` and one `tanhf` per voice per
  sample removed by tabulation (`x^2.7`, `sin(pi*x)`, `tanh(100*sin(pi*x))`) and by
  hoisting `pow(2, oscsliderange)` to control rate. Per-sample calls: `pow` 66→8,
  `sin` 24→5, `tanh` 22→2.
- **Pd control rate** — automaters bind the global throttle symbols directly
  instead of relaying them through `f.seq.automatermessage`; the automater LFO
  became a phase accumulator instead of `osc~` + `snapshot~` computing 48,000
  samples a second so 100 could be read; the automation playback clock binds the
  throttle directly; and 433 "couldn't create" errors from two objects disabled
  by a leading hyphen were removed along with the dead branches they fed.

| | before | after |
|---|---|---|
| synth throughput (`faust2bench`, shipping flags) | 58.9 MB/s | **74.9 MB/s (+27%)** |
| synth share of non-idle work in the running app | 50.4% | 45.7% (**17% cheaper**) |
| idle control-rate CPU, DSP off | 17.4% of a core | **3.8% (−78%)**, floor is 1.0% |
| runtime signal objects | 2,225 | 1,371 (−38%) |
| startup console | 876 lines | 10 |

Every behavioural change was checked rather than assumed: seven Faust A/B
scenarios agree to −74 dB or better; the tabulated pitch-slide curve is bit-exact
at both endpoints and deviates by at most **0.02 cents** mid-slide; old and new
LFOs match to 0.17% of modulation depth; and the two playback-clock deliveries
produce byte-identical tick sequences.

The document also records **three options left unapplied** — two as ready-to-apply
patches — including `option-C`, a live latent bug (`f.seq.automatermessage` is
created with one argument but reads `$2`, so `[r $2-trig]` is `[r 0-trig]` and a
`[spigot]` never opens). It is left unapplied because switching on dormant
behaviour is a decision about the instrument, not an optimisation.

And one methodological warning worth reading before repeating any of it: **`pd
-nogui -noaudio` with DSP on is not a valid way to compare DSP cost** — Pd's
polling scheduler spends slack spinning, so cheaper DSP measures as *more* CPU.

## 6. The GUI link: why it drops, and what actually fixes it

**`de704a6`, `dcc0393`, `868cbc8`, `661688b`, `5c80136`, `996fa10`** — full
write-up in [`gui/README.md`](gui/README.md)

The control interface becomes unreliable and disconnects when a lot of parameters
are automated. The mechanism, measured rather than argued: one OSC message becomes
**one WebSocket frame per client** (Open Stage Control's `Client.send` has no
coalescing and never reads `bufferedAmount`), and the client heartbeat **closes the
connection if no pong is handled within 3 s** — a main-thread timer queueing behind
every pending widget update. The server's own watchdog allows 25 s, so the client
always gives up first, which is exactly why it reads as "unreliable" rather than
"dead".

What shipped, in `src/tools/patch-osc-perf.py`, applied to the vendored package by
both build scripts:

- **Always** — `IpcServer.send` serialises its payload once instead of once per
  connected client. −4.5% server CPU at four tablets, no behavioural change.
- **Behind `--batch-ms N`** — coalesce broadcasts into one frame per N ms, using
  the `bundle` event the client has always handled but no server release sends.
  −64% server CPU and 83× fewer frames — but **+32% client work**, delivered in
  12 ms slabs instead of 0.1 ms slivers. Since a saturated tablet is what drops the
  connection, it is **off by default**.
- **Behind `--skip-prearg-search`** — short-circuits a lookup that is a guaranteed
  miss in this layout. Off by default: on its own it is a regression, and it only
  pays alongside packing.

Both build scripts also run `src/tools/brand-osc.sh` on the same unpacked package,
so the shipped client's greeting header reads `formuls 0.3.0-beta` rather than
`Open Stage Control v1.31.0`. Same contract as the perf patch: matched on the
markup rather than a line number, a no-op on an already-branded tree, and a hard
build failure if the header ever changes. Builds up to and including
`formuls-0.3.0-beta.app` shipped unbranded — that script existed but nothing
called it.

**The one change that measurably fixed idle behaviour** (`868cbc8`) was a single
object. `SEQPOS_LED___` sent straight to `to-o-s-c-interface`, bypassing
`f.formuls.pd`'s `MESSAGE_THROTTLE`, so **all six instances streamed their
sequencer position at all times** — a message that lives inside a modal, and that
costs **710 µs of client JS** against 76 µs for a slider. Routing it through
`$1-message-collect` puts it behind the gate everything else uses:

| idle, one synth tab open | before | after |
|---|---|---|
| messages/s to the client | 81 | **46 (−43%)** |
| client main thread | 10.6% | **6.3% (−41%)** |
| handler p50 | 1.2 ms | **0.2 ms** |

Three findings in this work overturned the plan it started from, and are the
reason the document is worth reading before touching the GUI again:

1. **The `/GET` poll is load-bearing, not waste.** Deleting it was step 1 of the
   original plan. It is the *only* thing telling Pd which tab is selected —
   verified by putting a sink on the server's `--send` target and watching tab
   switches produce no OSC at all — and therefore the only thing driving the gate
   that keeps traffic down.
2. **The cost is not the widget.** `setValue` is 2.9 µs and `draw()` 0.5 µs; the
   70 µs is a **global `value-changed` broadcast** to every widget holding a
   linked-props binding, and formuls' clone-based layout makes nearly all ~1,500
   widgets listeners. The same message in a bare 75-widget session costs 7.8 µs.
   **Per-message cost is a property of the layout, not of the widget addressed.**
3. **Hiding a panel saves nothing.** An address the session defines costs the full
   69 µs even with its panel torn down and absent from the DOM, against 2.3 µs for
   an address it does not define. A 30× gap — the only lever is not sending.

**Packing** (step 3) was measured at **7.1× cheaper per value**, 20× with the
lookup patch, and implemented for one panel: the `fxa` xy pads on all six
instances now emit a single `/modfxa<n>` of 8 floats through the existing gate,
into a `canvas` overlay. `f.gui.modpack` is generated by `gui/tools/gen-modpack.py`
rather than hand-wired. **The saving is not yet realised** — it is additive until
the four per-pad streams are cut, and cutting them stops resets and state recalls
from moving the pads, so it needs the base-vs-modulation question answered first.
`996fa10` turns the overlay marker off in the meantime so the pads do not draw two
nodes for one value; the pipeline stays intact and measurable behind
`if (SHOW_MARKER)`.

The document also costs the **JUCE + WebView + React rewrite** honestly, correcting
the assumption behind it: JUCE has no HTTP or WebSocket server, and its Web*Relay
bridges only wire up the in-window WKWebView — it serves nothing to a tablet. Both
servers would be yours to write. **3–6 months**, and worth doing only after steps
1–3, at which point it is a port of a UI whose cost model is understood.

## 7. Recording

**`03a1285`** — see [`../src/app/README.md`](../src/app/README.md)

A **Record** button beside Start/Stop, enabled only while the engine runs. The file
matches what the engine actually produces, not what was requested: the rate the
device really opened at, and one WAV channel per *selected* output channel — so a
14-channel take keeps its channel numbering even on a stereo interface, with the
missing channels written as silence. 24-bit PCM, automatically RF64 past 4 GB.

Audio goes straight to a timestamped file in `~/Music/formuls/` while recording and
the save dialog only *moves* it, so cancelling the dialog, pressing Stop mid-take
or closing the app never loses a recording. Nothing allocates or touches disk on
the audio thread — blocks go to a `juce::AudioFormatWriter::ThreadedWriter`.

## 8. The master bus was cut back

**`480cc97`, `f5ca88e`, `b0f3b05`, `a6942e0`, `d33e554`**

The OTT multiband compressor, digitaliser, stereo widener and bus-smashing limiter
were removed from the master bus, along with the crossover and stereo-width
controls that fed them, their Faust sources, and the 27 Open Stage Control widgets
that drove them. `f_looper.dsp` went too — nothing referenced it. The bus keeps the
**reverb, rolling spectral freezer and repeater**.

Three follow-ups closed the gaps that opened:

- `f5ca88e` — both build scripts stopped asking `faust2puredata` for the four
  deleted `.dsp` files, which had broken a clean build.
- `b0f3b05` — the feature list in `README.md` no longer advertises the removed
  effects.
- **`d33e554`** — a real regression: the reverb's wet signal left `MASTER_BUS_FX`
  through `throw~ $0-ott-l/r`, whose `catch~` objects went with the OTT. Between
  `480cc97` and this commit **the reverb never reached the stereo mix**, surviving
  only on its isolated 14-channel outlets. It now throws to
  `$0-limiter-l/r` alongside the repeater and freezer.

`a6942e0` removed the last documentation references to the à la carte branch — the
`FORMULS_TRACE` section (that env var belongs to the à la carte app, not this one)
and a stale `.gitignore` comment.

## 9. One investigation with no document of its own

A right-channel amplitude imbalance that tracked reverb send level was traced to a
single miswired connection in `MASTER_BUS_FX`: `#X connect 50 0 66 1` sent
`catch~ $0-ott-r` into the **gain** inlet of the dry-right `*~` instead of its
signal inlet, so the reverb return both missed the dry-right path and
amplitude-modulated its gain. Object 66 was the only occurrence of that shape in
`_main.pd` and all 60 abstractions.

**It was never committed as a fix.** `480cc97` deleted that whole dry/wet crossfade
along with the OTT, so the defect no longer exists — but it is recorded here
because the diagnosis is not written down anywhere else, and because the second
finding from the same session still stands: `widener` in `ffx.lib` used a
genuinely L/R-unequal Hass delay, `(l@(ma.SR/100) - r)`, gated by `max(0, w-1)` —
inaudible at the default width of 0.5, a left-biased comb above 50%. That effect
has since been removed.

---

## Open decisions and known-live findings

Nothing below is a bug introduced by this work; each is a judgement about the
instrument that the code deliberately leaves to its author.

| | where | the decision |
|---|---|---|
| **Base vs. modulated value** | [`gui/README.md`](gui/README.md), "Step 3" | Packing only pays once the per-pad streams are cut, which stops resets and state recalls moving the widgets. Option **A** (canvas draws everything, widgets move only on touch) or **B** (widgets show what the player set, overlay shows modulation). Both are redesigns of the instrument's visual feedback. |
| **Polyphony** | `efficiency/option-B-polyphony-12.patch` | `POLYPHONY` is 20 and every voice is computed then multiplied by zero in three of four modes. 12 buys +44% synth throughput; the cost is how many releasing voices can overlap. |
| **`$2-trig` never fires** | `efficiency/option-C-wire-up-automater-trig.patch` | A live latent bug. Fixing it switches on dormant behaviour. |
| **`oscinparse` half-wired** | [`efficiency/README.md`](efficiency/README.md) | Three of five `[sel 1]`→`[pipe 30]` chains have no output connection — an edit that stopped halfway. Reported, not removed. |

## Before `juce-port` becomes `main`

- `git log juce-port..main` lists four commits that are **duplicates, not missing
  work** (see Branch map). Verify by content, not by ancestry.
- `alacarte-prototype` has never been merged into anything and can be deleted.
- `src/pd` is copied into the app bundle at build time, so editing `src/pd/_main.pd`
  has no effect on an already-built app — see the note in
  [`../src/tools/README.md`](../src/tools/README.md).
