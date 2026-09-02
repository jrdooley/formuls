# abl_link~ tempo limit: minimal reproduction

`abl_link~` stops applying tempo changes after **exactly 16 of them**. Every
subsequent `commitAudioSessionState()` is silently ignored: the call returns,
but reading Link straight back still reports the 16th tempo, and the beat
keeps running at that rate for the rest of the session.

This is not the formuls patch. `test.pd` contains **only** `abl_link~`, a
receive to set its tempo, and two `print` objects. No GUI, no OSC, no
sequencer.

## What was measured

Sweeping the tempo 120 -> 100 in 1 bpm steps (21 values):

| step interval | tempo changes Link accepted | final tempo |
| --- | --- | --- |
| 30 ms  | 16 | 105 |
| 100 ms | 16 | 105 |
| 300 ms | 16 | 105 |

Sixteen every time, so it is a **count, not a rate or a timing problem**.
Also independent of:

* `link.enable(true)` vs `enable(false)` — identical,
* peers (`numPeers()` is 0 throughout),
* the values themselves (105 is simply the 16th),
* audio dropouts (reproduces with a perfectly steady render loop).

The commits are definitely reaching Link, and are definitely being dropped:

```
DIAG commit: wrote 105 -> Link now 105    <- 16th, accepted
DIAG commit: wrote 104 -> Link now 105    <- ignored
DIAG commit: wrote 100 -> Link now 105    <- ignored
```

The timestamps passed to `setTempo` are strictly increasing and ~31 ms
apart, so they are not stale or out of order.

This looks like a fixed-capacity realtime buffer on Link's audio-thread
commit path that is never drained in this embedding, so it fills after 16
entries and discards everything afterwards.

## Things tried that did NOT fix it

* balancing acquire/release so the commit is not gated on a `shared_ptr`
  use count (kept: a genuine latent bug, but not this one),
* taking the timestamp from `link.clock()` instead of a free-running sample
  counter through a `HostTimeFilter` (kept: measurably more robust across an
  audio dropout, but not this one),
* committing only a state that was actually modified,
* routing tempo through `captureAppSessionState()` / `commitAppSessionState()`
  — this made it worse (stuck at the very first value),
* periodically calling `captureAppSessionState()` to try to pump Link's
  controller.

## Building and running

```bash
LP=../../libs/libpd
clang++ -std=c++17 -O2 -w -I$LP/cpp -I$LP/libpd_wrapper \
    -I$LP/libpd_wrapper/util -I$LP/pure-data/src \
    repro.cpp $LP/libs/libpd.dylib -Wl,-rpath,$LP/libs -o repro
./repro /path/to/formuls.app/Contents/Resources/pd/externals
```

`STEPMS=300` changes the sweep interval, `ENABLE_LINK=1` enables Link.
It renders in real time deliberately: `abl_link~` derives its beat from the
host clock, so running flat out gives meaningless results.
