#!/usr/bin/env python3
"""Measure what N busy f.seq.automater instances cost while they modulate.

Builds a throwaway patch holding N automaters, each followed by a copy of the
tail that f.util.oscformat runs on every value it receives (scale, prepend the
parameter name, send towards Faust). The master throttles run at the app's
rates. After a take is recorded into every automater, the chosen modulation
modes are switched on, and the steady-state CPU of the Pd process is measured.

Pd runs in -batch mode: logical time advances as fast as the machine allows
and nothing sleeps, so the CPU time a run takes is exactly the work the patch
did, free of the scheduler-slack effects that make real-time runs noisy (see
docs/efficiency/README.md). The cost is the CPU-time difference between two
runs of different simulated length, divided by the simulated seconds between
them, so startup and patch loading cancel out. It is reported as the share of
one core the patch would need in real time.

    python3 src/tools/automater-load-bench.py                       # working tree
    python3 src/tools/automater-load-bench.py --rev HEAD            # a revision
    python3 src/tools/automater-load-bench.py -n 200 --modes lfo chaos
"""
import argparse
import os
import re
import resource
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import importlib.util

_spec = importlib.util.spec_from_file_location(
    "probe", os.path.join(HERE, "automation-probe.py"))
probe = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(probe)

REPO = probe.REPO
ABSTRACTIONS = probe.ABSTRACTIONS

SCENARIOS = {
    "idle": [],
    "playback": ["active 1"],
    "lfo": ["lfofreq 0.5", "lfodepth 0.5"],
    "chaos": ["slew 200", "chaos 0.5"],
}


def build_patch(path, n, modes, until_ms):
    p = probe.Patch()
    load = p.obj(20, 20, "loadbang")

    pmetro = p.obj(400, 20, "metro 10")
    p.connect(load, 0, pmetro, 0)
    p.connect(pmetro, 0, p.obj(400, 45, "s master-param-update-throttle"), 0)
    gmetro = p.obj(600, 20, "metro 40")
    p.connect(load, 0, gmetro, 0)
    p.connect(gmetro, 0, p.obj(600, 45, "s master-gui-update-throttle"), 0)

    send = p.obj(20, 300, "s bench-in")

    def at(ms, text, x):
        d = p.obj(x, 60, f"del {ms}")
        m = p.msg(x, 90, text)
        p.connect(load, 0, d, 0)
        p.connect(d, 0, m, 0)
        p.connect(m, 0, send, 0)
        return d

    # A beat every 500 ms: playback starts on a beat.
    beat = p.obj(20, 120, "metro 500")
    p.connect(load, 0, beat, 0)
    bmsg = p.msg(20, 150, "clockin bang")
    p.connect(beat, 0, bmsg, 0)
    p.connect(bmsg, 0, send, 0)

    # A starting value, as the app sends at load: until one arrives the
    # automater's output spigot stays shut.
    at(200, "0.5", 20)

    # Playback: record a 1 s triangle into every automater first.
    take = "playback" in modes
    press = at(300, "record 1" if take else "bang", 120)
    at(1300, "record 0" if take else "bang", 220)
    gest = p.obj(120, 180, "metro 10")
    cnt = p.obj(120, 210, "f")
    inc = p.obj(170, 210, "+ 1")
    shape = p.obj(120, 240, r"expr 1 - abs(1 - 2 * min(1 \, $f1 / 100))")
    if take:
        p.connect(press, 0, gest, 0)
    p.connect(gest, 0, cnt, 0)
    p.connect(cnt, 0, inc, 0)
    p.connect(inc, 0, cnt, 1)
    p.connect(cnt, 0, shape, 0)
    p.connect(shape, 0, send, 0)
    stop = p.msg(220, 180, "stop")
    stopdel = p.obj(260, 150, "del 1000")
    p.connect(press, 0, stopdel, 0)
    p.connect(stopdel, 0, stop, 0)
    p.connect(stop, 0, gest, 0)

    x = 320
    for mode in modes:
        for text in SCENARIOS[mode]:
            at(1500, text, x)
            x += 90

    # Emissions reaching the Faust-bound tail, counted across all instances.
    total = p.obj(700, 120, "r bench-count")
    acc = p.obj(700, 150, "f")
    accinc = p.obj(750, 150, "+ 1")
    p.connect(total, 0, acc, 0)
    p.connect(acc, 0, accinc, 0)
    p.connect(accinc, 0, acc, 1)
    # Count only from 2 s on, when every mode is running.
    zero = p.msg(700, 90, "0")
    zd = p.obj(700, 60, "del 2000")
    p.connect(load, 0, zd, 0)
    p.connect(zd, 0, zero, 0)
    p.connect(zero, 0, acc, 1)

    # Just before quitting, read the count out through a spigot that opens
    # only then, so the per-count outputs of [f] are not printed.
    report = p.obj(820, 60, f"del {until_ms - 1}")
    p.connect(load, 0, report, 0)
    trig = p.obj(820, 90, "t b b")
    one = p.msg(880, 120, "1")
    gate = p.obj(820, 180, "spigot")
    p.connect(report, 0, trig, 0)
    p.connect(trig, 1, one, 0)
    p.connect(one, 0, gate, 1)
    p.connect(trig, 0, acc, 0)
    p.connect(acc, 0, gate, 0)
    p.connect(gate, 0, p.obj(820, 210, "print EMITTED"), 0)

    quit_at = p.obj(1000, 60, f"del {until_ms}")
    p.connect(load, 0, quit_at, 0)
    p.connect(quit_at, 0, p.msg(1000, 90, r"\; pd quit"), 0)

    # The instances.
    for i in range(n):
        y = 400 + i * 30
        r = p.obj(20, y, "r bench-in")
        a = p.obj(100, y, f"f.seq.automater 999 999-b{i} 0.5 0")
        p.connect(r, 0, a, 0)
        # f.util.oscformat's per-value tail, minus the external's own work.
        t = p.obj(400, y, "t f b")
        p.connect(a, 0, t, 0)
        p.connect(t, 1, p.obj(460, y, "s bench-count"), 0)
        sw = p.obj(520, y, "f.util.switch")
        p.connect(t, 0, sw, 0)
        pre = p.obj(620, y, f"list prepend formuls1 param{i}")
        p.connect(sw, 0, pre, 0)
        trim = p.obj(800, y, "list trim")
        p.connect(pre, 0, trim, 0)
        p.connect(trim, 0, p.obj(880, y, "s to-faust-bench"), 0)
        # The GUI outlet goes to oscformat's GUI tail.
        g = p.obj(1000, y, f"list prepend param{i}")
        p.connect(a, 1, g, 0)
        p.connect(g, 0, p.obj(1150, y, "s bench-gui"), 0)

    p.write(path)


def run_once(pd, search, n, modes, until_ms):
    tmp = tempfile.mkdtemp(prefix="automater-bench-")
    try:
        patch = os.path.join(tmp, "bench.pd")
        build_patch(patch, n, modes, until_ms)
        cmd = [pd, "-batch", "-nomidi", "-stderr",
               "-path", search, "-open", patch]
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        out = subprocess.run(cmd, capture_output=True, text=True,
                             timeout=until_ms / 1000 + 60)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        cpu = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
        log = out.stdout + out.stderr
        errors = [l for l in log.splitlines()
                  if "couldn't create" in l or "error" in l.lower()]
        m = re.findall(r"EMITTED: ([\d.e+]+)", log)
        emitted = float(m[-1]) if m else float("nan")
        return cpu, emitted, errors
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-n", type=int, default=100, help="automater instances")
    ap.add_argument("--modes", nargs="*", default=["playback", "lfo", "chaos"],
                    choices=sorted(SCENARIOS))
    ap.add_argument("--short", type=float, default=4.0, help="short run, s")
    ap.add_argument("--long", type=float, default=24.0, help="long run, s")
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--rev", default=None)
    ap.add_argument("--path", default=None)
    ap.add_argument("--pd", default=None)
    args = ap.parse_args()

    pd = probe.find_pd(args.pd)
    revtmp = None
    try:
        if args.rev:
            revtmp, search = probe.checkout(args.rev)
            label = args.rev
        else:
            search = args.path or ABSTRACTIONS
            label = "working tree" if not args.path else args.path
        steady = args.long - args.short
        shares, rates = [], []
        for _ in range(args.repeat):
            c1, _, err = run_once(pd, search, args.n, args.modes, int(args.short * 1000))
            c2, e2, err2 = run_once(pd, search, args.n, args.modes, int(args.long * 1000))
            for e in sorted(set(err + err2))[:5]:
                print("  pd:", e)
            shares.append(100 * (c2 - c1) / steady)
            rates.append(e2 / (args.long - 2) / max(args.n, 1))
        shares.sort()
        med = shares[len(shares) // 2]
        print(f"{label}: n={args.n} modes={'+'.join(args.modes) or 'none'}  "
              f"steady CPU {med:5.1f}% of a core  (runs: "
              f"{', '.join(f'{s:.1f}' for s in shares)})  "
              f"emissions/instance/s {sum(rates) / len(rates):6.1f}")
    finally:
        if revtmp:
            shutil.rmtree(revtmp, ignore_errors=True)


if __name__ == "__main__":
    main()
