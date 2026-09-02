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

## Root cause, traced into Ableton Link

`include/ableton/link/Controller.hpp`:

```cpp
// Assuming a wake up time of one ms for the threads owned by the CallbackDispatcher
// and the ioService, buffering 16 client states allows to set eight client states
// per ms.
static const std::size_t kBufferSize = 16;
CircularFifo<IncomingClientState, kBufferSize> mClientStateFifo;
```

`setClientStateRtSafe()` pushes into that fifo and **silently discards the
value if it is full** ("we expect the setter to be called again soon").
Sixteen is exactly the number of tempo changes that get through, so the fifo
fills once and is never emptied again.

Draining it needs this chain, and instrumenting each link shows where it
breaks on macOS:

| step | what should happen | what actually happens |
| --- | --- | --- |
| `tryPush` | pushes, calls `mCallbackDispatcher.invoke()` | ok, until the fifo is full |
| `LockFreeCallbackDispatcher` thread | wakes on notify or every 500 ms and runs its callback | **thread starts, but `mCondition.wait_for()` never returns — the callback never runs** |
| callback | `mIo->async(processPendingClientStates)` | never reached |
| io context thread | `service.run()` serves posted handlers | **`run()` returns immediately despite a live work guard and `stopped()==0`, so the thread exits** |
| `processPendingClientStates` | drains the fifo | **never executes, not even once** |

So *both* threads that Link relies on are broken in this embedding, and the
fifo can only ever accept its initial 16 entries.

Asio itself is fine here: a standalone program using the bundled asio with
the same flags creates `io_service` + `work` + thread, and `run()` blocks and
executes posted handlers correctly. The same abl_link~ and Link versions are
reported working on Linux, and both platforms select the same
`platforms::asio::Context`, so this is specific to macOS.

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
  controller,
* making the io context thread restart `service.run()` instead of exiting
  when it returns early (the thread then stays alive, but the drain still
  never runs, because the dispatcher above it never posts anything),
* replacing the dispatcher's `condition_variable` wait with a 1 ms sleep
  (the thread then polls, but the chain still does not complete).

The last two are real improvements to the threading and may be needed as
part of a full fix, but neither is sufficient on its own, so neither is
carried in `src/patches/`.

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
