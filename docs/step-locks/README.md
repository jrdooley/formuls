# Step locks: a parameter value per sequencer step

Merged to `main` on 17 November 2026 in PR #57 (merge `7026431`).

A step lock gives any of a synth's parameters its own value on a particular step
of that synth's note sequencer. When the sequence reaches the step, the parameter
jumps (or glides) to the stored value. On steps without a lock, nothing is sent,
so the parameter stays wherever it was left. The same idea is called a "parameter
lock" on Elektron machines.

## Using it

The three buttons are in each synth's STEP SEQUENCER modal, below the mode
buttons:

| button | kind | what it does |
|---|---|---|
| **STEP WRITE** | toggle | While on, tapping a step **selects** it instead of switching its note on or off. The modal heading reads "WRITING STEP n". Any parameter of that synth moved now is written to that step. Tap the step again, or turn STEP WRITE off, to release it. **While a step is selected, edits are silent**: the control moves and the value is stored, but the sound changes only when the sequence plays that step. When the step is released, the control returns to the value you can hear. |
| **STEP MUTE** | hold | While held, tapping a step that has values **mutes or unmutes** them, keeping the values. Works whether or not STEP WRITE is on. A tap on a step with no values does nothing. |
| **STEP CLEAR** | press | Forgets every step value on this synth. **Nothing else is touched**: notes, the parameters' own record/loop automation, and their current values all stay as they are. |

A step with values shows a corner flag: **filled** when they play, an
**outline** when muted. A synth reset clears that synth's locks too.

Locks play only while the note sequencer is running, because the step number
they follow is only sent then.

### Editing parameters inside the modal

The bottom of the modal's right-hand column has an **EDIT** menu listing the
synth's 25 parameters: everything on the synth page except the sequencer controls
already in the modal. Choosing one shows that parameter's own widget under the
menu, so a step can be written without going back to the synth page. Sequencer
Add,Drop, Flam Hits,Spread and Swing/Step Delay/Warp were reduced from a third
each to 22% of the column to make room. Osc Frequency,Wave also shows its pitch
readout and waveshape labels, as on the synth page. The menu is a drag menu:
press it, drag onto a parameter and let go. Its pop-up is 300 × 500 px (o-s-c's
default is 200 × 200), two columns, opening up and to the left from the corner.

The widget shown is a second copy of the one on the synth page (`seqparameditor`
in `src/gui/_main.json`, one holder panel per parameter, shown when the menu
names it). Both copies are made from the same template with the same variables,
so they control the same OSC addresses and show the same value. Holder panels
are needed because `flatten-session.py` refuses a dynamic `visible` on a clone.
The menu has `bypass: true`, so it sends nothing to Pd.

The copies cost load size: the flattened session grows from 8782 to 11500
widgets and from 2.50 MB to 3.36 MB.

## How it works

Every parameter of a synth is driven by one `f.seq.automater`. Each automater
contains an `f.seq.record`, which used to hold an unfinished, disconnected
"sequencer record/playback" pair. That pair was rewritten to hold the locks.

```
GUI button / step tap
  -> /steplock*N  (OSC)
  -> f.util.oscinparse  STEPLOCK subpatch
  -> per-synth symbols  <synth $0>-seq-record / -seq-step / -seq-step-on /
                        -seq-play / -seq-trans-time / -seq-lock-clear
  -> f.seq.automatermessage (one per parameter) turns each into a message
  -> f.seq.automater: anything its own route does not claim goes on to
  -> f.seq.record: SEQUENCER_RECORD_MODULE / SEQUENCER_PLAYBACK_MODULE
```

- **Storage**: two 128-entry tables per parameter, `$0-this-2` (the value, `-1`
  meaning no lock) and `$0-this-2-on` (the mute mask). Muting writes only the
  mask, so the value survives. Writing a value switches the step's mask back on.
- **Writing**: `f.seq.automater` offers every value the parameter receives
  (`$0-input-1`) to `f.seq.record` as `seqvalue`. It is stored only while lock
  mode is on **and** a step is selected.
- **Playback**: the note sequencer's 0-based `seqposition` becomes a 1-based
  `beat`. On each beat the playback module reads the mask and then the value. If
  both are set, it sends the value through `[line]`, so `/steplockglideN <ms>`
  gives a glide; 0 is a jump.
- **Cost**: a `has-locks` gate keeps the table reads away from parameters that
  hold no lock, which is nearly all of the ~421 automaters.
- **Back to the GUI**: each write is announced on `seq-lock-written`. The
  `WRITTEN` and `MARK` subpatches in `STEPLOCK` send `/steplockhasN-i`, where `i`
  is the 0-based step index and the value is `1` (locked), `0.5` (locked and
  muted) or `0` (none). These go straight to `to-o-s-c-interface`, not through
  `message-collect`, so the flags arrive whichever synth page is showing.
- **The step panel**: `src/gui/compounds/sequencer-lib.js` draws the flags and
  the selection ring, and handles taps in the two modes.
  `src/tools/compile-compounds.py` adds the lock widgets' addresses to the
  compiled panel and routes `/steplockhasN-i` into its per-step variable `lk`.

### OSC addresses (`N` = synth number)

| address | arguments | |
|---|---|---|
| `/steplockN` | `1`/`0` | lock mode (STEP WRITE). Leaving it releases the step. |
| `/steplockstepN` | step `1..128`, or `0` | select the step to write, or none |
| `/steplockmuteN` | step, `0`/`1` | mute (`0`) or unmute (`1`) a step's values |
| `/steplockplayN` | `0`/`1` | stop or allow lock playback (on by default) |
| `/steplockglideN` | ms | glide time to a locked value |
| `/steplockclearN` | anything | forget this synth's locks |
| `/steplockhasN-i` | `0`, `0.5`, `1` | Pd to GUI: what step `i` (0-based) holds |

### Silent writes

`f.seq.record` publishes `$1-seq-writing`, which is 1 while lock mode is on and a
step is selected. In `f.seq.automater`, a spigot after each audio-side
`[r $0-input-1]` (in `VALUE_READ`, `VALUE_JUMP` and `VALUE_EVOLVE`) closes while
writing. Lock playback enters on `$0-textseq`, so it is not gated. The GUI echo
is switched the same way: while writing it carries the written value, because
the audible one would pull the fader back. When writing ends, a bang to
`[change -1]` sends the audible value again.

The spigots go **after** the existing receivers rather than on a new relay
receiver. Pd calls the newest `[r]` first, and `RECORD_GUI_UPDATE_PAUSE` must open
its spigot before a value reaches it. A relay receiver created later lost the
first value given to every parameter.

## Two traps found while building it

**`seq-reset` was already taken.** The first STEP CLEAR sent on
`<synth>-seq-reset`. `f.seq.record` also uses that symbol to reset each
parameter's record/loop automation, so clearing locks reset the whole synth's
automation as well. It now has its own symbol, `seq-lock-clear`, and only the two
lock tables listen on it. Before reusing a Pd send name, search `src/pd` for every
`[r name]`: every receiver hears every message.

**A synth reset used to switch lock playback off.** The original record module
closed the playback spigot on reset, and nothing reopened it. In the app, locks
then never played. `bc38fef` removed that connection.

## Tests

`src/tools/steplock-probe.py` runs one automater headless in Pd (no audio, GUI or
built app). It sends the same per-synth messages the GUI would and walks an
8-step sequence six times, checking the value each step leaves the parameter at:

1. locks on steps 3 and 7
2. step 3 muted
3. step 3 unmuted, with its value intact
4. playback off
5. an automation reset (`seq-reset`), after which the locks must survive
6. STEP CLEAR, after which they must be gone

Two more checks cover silent writes: nothing new is heard while a step is written, and the GUI shows the written values and then the audible one. All eight pass. Against the commit before step locks, the probe fails.

Checked in the app (macOS, 17 Nov 2026): a lock written by dragging the
Sequencer Swing fader, marking, playback on exactly the locked steps, mute with
STEP WRITE off (including a real finger hold on a tablet), and clear.

## Not checked

- CPU cost with many locks set.
- Synths and parameters beyond the two tried (synths 1 and 4; delay time and
  sequencer swing).
- Whether locks survive saving and reloading a state. Nothing in this work saves
  the tables, so assume they are lost on restart.
- Linux, Windows and the plugin builds.
