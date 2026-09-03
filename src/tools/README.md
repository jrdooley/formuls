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

Note: neither build script currently calls this, despite the comment in the
script saying they do. Branding is applied by hand, or not at all.


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
