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


## FORMULS_TRACE (built into the a la carte app)

Set `FORMULS_TRACE` to a comma-separated list of Pd send symbols and the app
subscribes to them, timestamping everything they carry:

```bash
FORMULS_TRACE=bpmglobal,link ./formuls-alacarte-0.3.0-beta.app/Contents/MacOS/formuls-alacarte
```

```
trace     5547 ms  bpmglobal                110.000000
```

A value being fought over between the patch and the GUI shows up as an
obvious alternation between two numbers; a value that is simply not
arriving shows up as silence.

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
