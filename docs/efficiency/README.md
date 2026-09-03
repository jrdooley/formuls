# efficiency-dev: what changed, what it bought, and what is left on the table

Branch: `efficiency-dev`, cut from `juce-port`. Five commits, each independently
verified. Nothing here changes what the instrument does — the two changes that
alter any number at all are quantified below.

## Where the time actually went

Sampled the running patch (`sample`, 20 s, DSP on, three paired runs), and
normalised each run by its own non-idle work so the scheduler's slack cancels:

| | share of all non-idle CPU |
|---|---|
| `formuls~` synth (compute + its libm calls) | **50%** |
| everything else in Pd (message dispatch, `lop~`, `throw~`/`catch~`, …) | ~35% |
| master-bus effects (reverb, OTT, repeater, limiter, widener, digitaliser) | ~7% |

The synth was half the machine. Separately, with DSP **off**, the patch still
burned 17.4% of a core doing nothing but housekeeping — that turned out to be
the two master throttles fanning out to 427 automater instances.

## The five commits

**1. `faust:` per-voice `powf`/`sinf`/`tanhf` removed.**
Each of the 20 voices ran three `powf()`, one `sinf()` and one `tanhf()` on every
sample. Now: `x^2.7` for the ADSR curve is a 1024-point interpolated table;
`pow(2, oscsliderange)` moved to before its sample-and-hold so Faust hoists it to
control rate (bit-exact); `sin(pi*x)` and `tanh(100*sin(pi*x))` are tables in
`fosc.lib`. Per-sample calls: `pow` 66→28, `sin` 24→5, `tanh` 22→2.

**2. `pd:` automaters read the master throttles directly.**
Each 100 Hz tick previously ran ~10 message dispatches per automater —
`r master-param-update-throttle` → message box → `t a` → `spigot` → outlet →
23-selector `route` → `s $0-…` → `r $0-…`. The three consumers now bind the
global symbols directly.

**3. `pd:` the automater LFO runs at control rate.**
It generated its waveform with `osc~` and read it with `snapshot~` on a throttle
tick — 48000 samples a second computed so that 100 could be looked at, ×427.
Replaced with a phase accumulator clocked by the same throttle.

**4. `pd:` broken objects and the dead branches they fed.**
433 "couldn't create" errors on every start, from two objects someone disabled by
typing a hyphen in front of the class name. Startup console: 876 lines → 10.

**5. `pd:` the automation playback clock binds the throttle directly.**
The last big relay: every automater built a `paramthrottle bang`, `f.seq.record`
matched it *last* of 15 route selectors, and it ended at a spigot that is shut
unless automation is playing back.

## Results

**Synth throughput** (`faust2bench -vec -lv 0 -vs 4`, the flags `build-macOS.sh`
ships, median of 5):

| | MB/s | vs before |
|---|---|---|
| before | 57.6 | — |
| after | 72.7 | **+26%** |

**Synth cost inside the running app** (3 paired 20 s profiles, rebuilt external):
share of non-idle work 50.4% → 45.7%, i.e. the synth itself got **17% cheaper**.
libm calls attributable to it: `sinf` 200→**0**, `tanhf` 218→19, `exp2f` 191→20,
`powf` 558→296; **−71% overall**.

**Idle control-rate CPU** (25 s headless, DSP off, startup subtracted):

| | share of a core |
|---|---|
| before | 17.4% |
| after commit 2 | 7.9% |
| after commit 5 | **3.8%** |
| floor (throttles not running at all) | 1.0% |

**−78%**, and what remains is within 2.8 points of not running the throttles.

**Runtime signal objects**: 2225 → 1371 (−38%). `osc~` 432→5, `snapshot~` 427→0.

**Startup console**: 876 lines → 10.

## How the two behavioural changes were checked

*Faust.* Seven parameter scenarios (ADSR poly, waveshape sweep, sustained mono,
FM + pitch slide, noise, full FX chain, AM sweep) rendered from both versions and
compared: worst case **−74 dB** error relative to signal, RMS envelope agreement
−71 dB or better. Below any plausible audibility threshold.

*LFO.* Old and new LFO canvases extracted into standalone patches and driven
identically inside the real `_main.pd` for 5 s: output range identical
(`[0.2273, 0.7726]`), rate identical (4.0 cycles in 5.0 s), and at one tick of lag
the maximum difference is **0.00093** — 0.17% of modulation depth. The one-tick
offset is inherent, because `snapshot~` returns the previous block's last sample.

*Playback clock.* Both deliveries run side by side into identical `[i 0]`→`[+ 1]`
counters for 399 ticks: captured sequences **byte-identical**.

## Options not applied

Ready to `git apply` from the repo root.

### A — `option-A-tabulate-slide-pow.patch`
Tabulates the *second* `pow(2.7)`, the one shaping the pitch-slide envelope.
Worth roughly another **+7%** synth throughput (72.7 → ~77 MB/s).

Not applied because it is the one change that is not transparent. It is exact
while the pitch is static, but during a slide a ~1e-7 error in the slide
multiplier accumulates as oscillator phase. Amplitude is unaffected (RMS envelope
agrees to −59 dB) but the average spectrum deviates by up to −15 dB in that mode,
against −66 dB for everything shipped. Probably inaudible; not demonstrably so,
which is why it is your call rather than mine.

### B — `option-B-polyphony-12.patch`
`POLYPHONY` is 20. Every voice is computed on every sample and then multiplied by
`poly(i,c)`, which is 0 for all but the current voice unless the *polyphonic*
checkbox and the ADSR are both on — so in three of the four modes 19 of 20 voices
are computed and thrown away. Faust cannot skip them; only the constant can change.

| POLYPHONY | MB/s | CPU per instance | ×6 instances |
|---|---|---|---|
| 20 (current) | 72.7 | 1.50% | 9.0% |
| 16 | 86.0 | 1.26% | 7.6% |
| 12 | 104.6 | 1.04% | 6.3% |
| 8 | 135.3 | 0.81% | 4.9% |

The patch sets 12. The audible cost is how many releasing voices can overlap, so
it depends on your longest release against your fastest retrigger — a judgement
about the instrument, not about the code.

### C — `option-C-wire-up-automater-trig.patch`
A live bug, not an optimisation. `f.seq.automater` creates
`f.seq.automatermessage $1` with **one** argument, but the abstraction uses `$2`:
`[r $2-trig]` expands to `[r 0-trig]`, a symbol nothing sends. It gates a
`[spigot]` that therefore never opens, so `trig bang` never reaches the automater
from `$1-automater-trig`. Passing `$2` through is what the code looks like it
wants. Left unapplied because it switches on dormant behaviour rather than
speeding anything up, and that is a decision about the instrument.

## Also found, deliberately left alone

- **`f.util.oscinparse`, `ACT_TRIG_MOD_CHAOS_LFO`**: five `[sel 1]`→`[pipe 30]`
  chains, of which three have no output connection. Two of five being wired looks
  like an edit that stopped halfway rather than dead code, so it is reported
  rather than removed. Runtime cost is negligible.
- **`f.seq.record` carries a `TEST_UNIT___` panel** — a manual test harness with
  GUI widgets — in all 427 instances, about 7,700 objects. Costs allocation and a
  little load time, nothing per tick. Removing it would lose a development
  affordance for no measurable gain.
- **Master-bus effects are cheap.** Measured individually: reverb 0.46%, repeater
  0.27%, OTT 0.14%, digitaliser 0.06%, widener 0.06%, limiter 0.02% of a core.
  Wrapping them in `switch~` to bypass them would buy ~1% and risk graph-rebuild
  clicks. Not worth it.
- **`fsynth.lib` line 18** wraps the carrier frequency in
  `si.interpolate(adsron)` over a value that `ox()` has *already* selected on
  `adsron` — a genuine double-selection. Removing it measures at 0% and perturbs
  how Faust factors the surrounding expression, so it is left as written.

## A measurement caveat, so nobody repeats the mistake

`pd -nogui -noaudio` **with DSP on is not a valid way to compare DSP cost.** Pd's
polling scheduler spends whatever slack it has spinning, so making the DSP cheaper
shows up as *more* measured CPU. Verified directly: swapping in the faster
`formuls~` raised the number while `faust2bench` showed the same binary 26%
faster. The `dsp=0` runs behave correctly and monotonically and are what the
control-rate numbers above come from; DSP claims come from `faust2bench` and from
`sample` profiles normalised by total non-idle work.

Two smaller traps in the same harness: `[print]` output is swallowed under
`-nogui`, so capture values with `[textfile]` and a `write` message instead; and
piping Pd's console into `head` kills it with SIGPIPE partway through loading, so
redirect to a file.

Tooling to reproduce all of it is in `tools/`.
