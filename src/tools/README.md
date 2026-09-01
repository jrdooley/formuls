# Diagnostic tools

## bpm-probe

Loads the real `pd/_main.pd` under libpd, drives BPM over genuine OSC to
udp 9000 exactly as the Open Stage Control GUI does, and measures the beat
rate `abl_link~` actually produces after each change. It needs no audio
device and no tablet, so BPM behaviour can be tested in isolation from the
GUI.

Build (libpd must already be built -- see `src/app-alacarte/README.md`):

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
