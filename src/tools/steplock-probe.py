#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2014-2026 James Dooley (aka formuls)

"""Lock parameter values to sequencer steps and check what plays back.

Drives one f.seq.automater headless, the way a synth would: step-lock messages
go out on the per-synth `<id>-seq-*` symbols that f.seq.automatermessage
listens on, and values arrive as bare floats, as they do from the GUI. Then it
walks the step counter round an 8-step sequence several times and compares the
value each step leaves the parameter at with what the locks say it should be.

Needs no audio device, no GUI, no externals and no built app.

    python3 src/tools/steplock-probe.py
    python3 src/tools/steplock-probe.py --trace
"""
import argparse
import importlib.util
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "automation_probe", os.path.join(HERE, "automation-probe.py"))
ap_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ap_mod)

ID = 999          # stands in for a synth's $0
STEPS = 8
STEP_MS = 100
BASE = 0.1        # where the performer leaves the parameter before playback


def timeline():
    """-> (events, cycles). events: (ms, kind, text). cycles: (label, start_ms,
    {step: expected value at the end of that step})."""
    ev, cycles = [], []
    bus = lambda t, name, *a: ev.append((t, "bus", f"{ID}-seq-{name} " + " ".join(map(str, a))))
    val = lambda t, v: ev.append((t, "val", str(v)))

    val(100, 0.2)                       # lock mode off: must not be stored
    bus(200, "record", 1)
    val(220, 0.3)                       # lock mode on, no step selected: not stored
    bus(240, "step", 3, 1)
    val(260, 0.6)
    val(270, 0.8)                       # the last value given wins
    bus(310, "step", 3, 0)              # after a GUI tick (50 ms), so 0.8 is shown
    bus(320, "step", 7, 1)
    val(340, 0.4)
    bus(360, "step", 7, 0)
    bus(380, "record", 0)
    val(430, BASE)                      # lock mode off again: not stored
    bus(450, "play", 1)

    t = 600

    def cycle(label, expect, start=BASE):
        nonlocal t
        cycles.append((label, t, expect, start))
        for s in range(1, STEPS + 1):
            ev.append((t + (s - 1) * STEP_MS, "beat", str(s)))
        t += STEPS * STEP_MS + 100

    cycle("locks on steps 3 and 7", {3: 0.8, 7: 0.4})
    bus(t - 50, "step-on", 3, 0)
    cycle("step 3 muted", {7: 0.4}, start=0.4)
    bus(t - 50, "step-on", 3, 1)
    cycle("step 3 unmuted (its value must have survived)", {3: 0.8, 7: 0.4}, start=0.4)
    bus(t - 50, "play", 0)
    cycle("playback off", {}, start=0.4)
    bus(t - 50, "play", 1)
    bus(t - 40, "reset", "bang")
    cycle("after an automation reset (locks must survive)", {3: 0.8, 7: 0.4}, start=0.4)
    bus(t - 50, "lock-clear", "bang")
    cycle("after lock clear", {}, start=0.4)
    return ev, cycles, t


def build_patch(path, events, until):
    p = ap_mod.Patch()
    load = p.obj(20, 20, "loadbang")
    for x, name, ms in ((400, "master-param-update-throttle", 10),
                        (600, "master-gui-update-throttle", 50)):
        m = p.obj(x, 50, f"metro {ms}")
        p.connect(load, 0, m, 0)
        p.connect(m, 0, p.obj(x, 80, f"s {name}"), 0)

    auto = p.obj(20, 500, f"f.seq.automater {ID} {ID}-probe 0 0")
    split = p.obj(20, 530, "t f b")
    timer = p.obj(760, 50, "timer")
    pack = p.obj(20, 560, "pack f 0")
    p.connect(load, 0, timer, 0)
    p.connect(auto, 0, split, 0)
    p.connect(split, 1, timer, 1)
    p.connect(timer, 0, pack, 1)
    p.connect(split, 0, pack, 0)
    p.connect(pack, 0, p.obj(20, 590, "print VAL"), 0)

    # outlet 1 is what the automater sends back to the GUI
    gsplit = p.obj(160, 530, "t f b")
    gpack = p.obj(160, 560, "pack f 0")
    p.connect(auto, 1, gsplit, 0)
    p.connect(gsplit, 1, timer, 1)
    p.connect(timer, 0, gpack, 1)
    p.connect(gsplit, 0, gpack, 0)
    p.connect(gpack, 0, p.obj(160, 590, "print GUI"), 0)

    # the step counter is logged with the same clock, just before it is sent
    blog = p.obj(300, 500, f"r {ID}-probe-beat")
    bsplit = p.obj(300, 530, "t f b")
    bpack = p.obj(300, 560, "pack f 0")
    p.connect(blog, 0, bsplit, 0)
    p.connect(bsplit, 1, timer, 1)
    p.connect(timer, 0, bpack, 1)
    p.connect(bsplit, 0, bpack, 0)
    p.connect(bpack, 0, p.obj(300, 590, "print BEAT"), 0)

    for i, (ms, kind, text) in enumerate(events + [(until, "quit", "")]):
        d = p.obj(20 + (i % 12) * 70, 120 + (i // 12) * 50, f"del {ms}")
        p.connect(load, 0, d, 0)
        y = 145 + (i // 12) * 50
        x = 20 + (i % 12) * 70
        if kind == "val":
            p.connect(d, 0, p.msg(x, y, text), 0)
            p.connect(len(p.objs) - 1, 0, auto, 0)
        elif kind == "bus":
            p.connect(d, 0, p.msg(x, y, f"\\; {text}"), 0)
        elif kind == "beat":
            # the note sequencer's step reaches an automater as a 0-based
            # `seqposition`, from the f.util.oscformat that hosts it
            p.connect(d, 0, p.msg(x, y, f"\\; {ID}-probe-beat {text}"), 0)
            p.connect(d, 0, p.msg(x, y + 20, f"seqposition {int(text) - 1}"), 0)
            p.connect(len(p.objs) - 1, 0, auto, 0)
        else:
            p.connect(d, 0, p.msg(x, y, r"\; pd quit"), 0)
    p.write(path)


def parse(log):
    out = []
    for line in log.splitlines():
        m = re.match(r"(VAL|BEAT|GUI): (-?[\d.e+-]+) ([\d.]+)$", line.strip())
        if m:
            out.append((float(m.group(3)), m.group(1), float(m.group(2))))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--path", default=ap_mod.ABSTRACTIONS)
    ap.add_argument("--trace", action="store_true", help="print every logged line")
    ap.add_argument("--pd", default=None, help="path to the Pd binary")
    args = ap.parse_args()

    pd = ap_mod.find_pd(args.pd)
    events, cycles, until = timeline()
    tmp = tempfile.mkdtemp(prefix="steplock-probe-")
    try:
        patch = os.path.join(tmp, "probe.pd")
        build_patch(patch, events, until)
        log = ap_mod.run(pd, patch, args.path, timeout=until / 1000.0 + 10)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print(f"step-lock probe: {args.path}")
    bad = 0
    for line in log.splitlines():
        if "error" in line.lower() or "couldn't create" in line:
            print(f"  pd: {line.strip()}")
            bad += 1
    rows = parse(log)
    vals = [(t, v) for t, k, v in rows if k == "VAL"]
    beats = [(t, int(v)) for t, k, v in rows if k == "BEAT"]
    if args.trace:
        for t, k, v in rows:
            print(f"    {t:7.0f} ms  {k:4s} {v:g}")
    if not vals or len(beats) != len(cycles) * STEPS:
        print(f"  {len(vals)} values and {len(beats)} beats logged; expected "
              f"{len(cycles) * STEPS} beats -- the patch did not run as written")
        return 1

    def value_at(t):
        cur = None
        for vt, v in vals:
            if vt > t:
                break
            cur = v
        return cur

    # Silent writes: with lock mode on and a step selected (240-300 and
    # 320-360 ms), the values given are stored but must not reach the output.
    # Values given with no step selected (0.3 at 220 ms) still sound.
    heard = [v for t, v in vals if 240 <= t < 380]
    loud = [v for v in heard if v > 0.3 + 1e-4]
    # 0.2 is the very first value: it once got lost when the gate's receiver
    # fired before RECORD_GUI_UPDATE_PAUSE had opened its spigot
    sounded = (any(abs(v - 0.3) < 1e-4 for t, v in vals if 220 <= t < 240)
               and any(abs(v - 0.2) < 1e-4 for t, v in vals if 100 <= t < 120))
    ok = not loud and sounded
    bad += not ok
    print(f"  {'ok  ' if ok else 'FAIL'} edits while writing a step are silent")
    print(f"         heard while writing: {' '.join(f'{v:g}' for v in heard) or 'nothing new'}"
          f"; 0.2 (lock mode off) and 0.3 (no step selected) {'sounded' if sounded else 'did NOT both sound'}")

    # ...but the GUI is told the written values, so the fader stays where it was
    # put, and once the step is let go (360 ms) it is told the audible value again
    gui = [(t, v) for t, k, v in rows if k == "GUI"]
    shown = [v for t, v in gui if 240 <= t < 380]
    back = [v for t, v in gui if 360 <= t < 430]
    ok = (any(abs(v - 0.8) < 1e-4 for v in shown) and any(abs(v - 0.4) < 1e-4 for v in shown)
          and back and abs(back[-1] - 0.3) < 1e-4)
    bad += not ok
    print(f"  {'ok  ' if ok else 'FAIL'} the GUI shows the written values, then the audible one")
    print(f"         GUI while writing: {' '.join(f'{v:g}' for v in shown) or '-'}"
          f"; after: {' '.join(f'{v:g}' for v in back) or '-'}")

    for n, (label, start, expect, held) in enumerate(cycles):
        got, want, line = [], [], []
        cur = held
        for s in range(1, STEPS + 1):
            bt = beats[n * STEPS + s - 1][0]
            cur = expect.get(s, cur)
            seen = value_at(bt + STEP_MS - 20)     # where the step has left the parameter
            want.append(cur)
            got.append(seen)
            line.append(f"{seen:g}" if seen is not None else "-")
        ok = all(g is not None and abs(g - w) < 1e-4 for g, w in zip(got, want))
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {label}")
        print(f"         steps 1-{STEPS}: {' '.join(line)}")
        if not ok:
            print(f"         expected:  {' '.join(f'{w:g}' for w in want)}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
