# Pre-built externals

## abl_link~.pd_darwin (arm64)

**Use this instead of building `abl_link~` locally on macOS.**

Building `abl_link~` from source with a current Xcode (26.x, libc++ 2100)
produces a binary in which Ableton Link's worker threads never do any work,
and tempo changes stop being applied after exactly 16 of them. The same
source built with an older toolchain works. This file is the Deken package
build (libc++ 1300), and it is correct.

Verified with `src/tools/abl-link-repro`, same harness and patch, back to
back:

```
DEKEN pre-compiled : fast drag 120->100  ->  beat 100.0   ok
local build        : fast drag 120->100  ->  beat 105.0   STUCK
```

The failure is entirely in the build. Ruled out, each by rebuilding and
re-running the reproduction:

* deployment target — 10.6, 10.9, 11.0, 13.0 all fail identically,
* C++ standard — c++11, c++14, c++17 all fail identically,
* optimisation — `-O0`, `-O1`, `-O2`, `-O3` all fail identically,
* `-ffast-math` — failing with and without it,
* the source itself — byte-identical between the two builds.

What actually goes wrong in the locally built binary (traced by
instrumenting Link's own headers):

* `LockFreeCallbackDispatcher`'s thread starts, but its
  `condition_variable::wait_for()` never returns, so its callback never runs;
* the io context thread's `asio::io_service::run()` returns immediately
  despite a live work guard and `stopped() == 0`, so that thread exits;
* consequently `processPendingClientStates()` never executes, Link's
  16-entry `mClientStateFifo` fills, and `setClientStateRtSafe()` silently
  discards every later tempo change.

Asio on its own is fine with the same flags on the same machine, so this is
specific to compiling Link.

### Provenance

Installed via Pure Data's Deken package manager
(`~/Documents/Pd/externals/abl_link~/`), arm64, `minos 11.0`.

### If you need to refresh it

Install `abl_link~` through Deken in Pd and copy the `.pd_darwin` here, or
build the external on a machine with an older Xcode. Afterwards, confirm it
with the reproduction in `src/tools/abl-link-repro` before shipping —
a broken build is silent until a performer drags the tempo slider.

### Linux

Not affected: building `abl_link~` from source on Linux works, so
`build-linux.sh` still builds it normally.
