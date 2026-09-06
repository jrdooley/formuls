# Filter pitch track

With **Filter Pitch Track** selected, the Low Pass node stops being an absolute
cutoff and becomes an offset above the carrier. `f.util.oscinparse`'s
`RESONANT_FILTER____` feeds the carrier into `f.util.oscformatxy`'s `x-offset`
inlet, and the abstraction adds it to the node before scaling and sending the
value to faust.

## The dead zone

The sum is `clip(node + carrier, 0, 1)`, and faust maps that 0-1 onto MIDI
0-136 (`lop` in `src/faust/ffx.lib`). Node and carrier therefore consume the
same range, so **the node's usable travel is only `1 - carrier`** -- past that
the sum sits on the ceiling and moving the node does nothing at all.

Measured with `sweep.pd`-style traffic through a real `f.formuls`: node held at
0.3, carrier sweeping its full range, **42% of the frames pinned at the
ceiling**, and a node move from 0.3 to 0.6 in that region changed the cutoff not
at all. That is the "moving the node doesn't adjust the filter cutoff" report.

This is left in place deliberately -- changing it changes what the node's travel
means, which is a decision about the instrument. The two candidates are written
up below as patches so they can be tried without re-deriving them.

## Trying an option

Both patches replace the single `[+]` that adds the offset, and append a comment
explaining the choice. They apply on top of the current tree:

    git apply docs/filter/option-A-scale-into-headroom.patch
    ./build-macOS.sh          # the app runs a *copy* of src/pd -- see src/tools/README.md

Revert with `git apply -R` and the same file. Only one at a time; they touch the
same object.

Neither changes the object numbering: the `[+]` is replaced in place and the
comment is appended after the last object, so every existing connection stays
valid. That matters more than it sounds -- inserting an object mid-list silently
renumbers every connection after it.

### Option A -- scale into the headroom

    expr $f1 * (1 - $f2) + $f2

Node at minimum is the carrier exactly, node at maximum is fully open, and no
part of the travel is ever dead. The cost is that the span the node covers
shrinks as the carrier rises: at carrier 0.5 the node sweeps 68 semitones, at
carrier 0.8 only 27. The same gesture means different things at different
pitches.

### Option B -- a fixed span above the carrier

    expr $f1 * 0.35 + $f2

The node always means the same number of semitones above the carrier -- 0.35 of
the MIDI 0-136 span is about four octaves. Edit the number to taste. This is the
conventional reading of "carrier plus an offset", and the gesture stays
consistent wherever the carrier sits. It still saturates eventually, just much
later: with 0.35 the ceiling is only reachable with the carrier above 0.65.

### What each measures

Same sweep as above, node held at 0.3:

| | frames pinned at the ceiling |
|---|---|
| current | 77 of 183 (42%) |
| option A | 0 |
| option B | 0 |

Both remove the dead zone for that setting. They differ in what the node's
travel means, not in whether it works.

## The stray messages, already fixed

Separately: both `f.util.oscformatxy` instances that make up this pad were
broadcasting their own half-length pair on the pad's shared address, because
`f.formuls`'s `MESSAGE_FILTER` tries to block `resonantfilterfreqq$2` with a
`route` and cannot -- by that point the message is an `oscformat`ed byte list
whose selector is `list`, so it fell out of the reject outlet to the interface.
At the end of a drag the stray was the last frame the widget received.

Open Stage Control's `MultiXy.setValue` ignores any array whose length is not
`2 * npoints`, so this cost bandwidth and nothing else -- but it was a trap for
whoever next changed the pad's point count. `f.util.oscformatxy` now takes an
optional twelfth argument: pass `1` to keep that instance's own GUI value off
`to-o-s-c-interface`. Both filter nodes pass it. The `$2-modvalue` tap is
upstream of the gate and still fires, so `f.gui.modpack` is unaffected, and an
instance with no twelfth argument behaves exactly as before.
