#!/usr/bin/env python3
"""Drive the pitch quantiser (f.util.keyselector) and check what it snaps to.

Builds a throwaway Pd patch that does what NOTE_SNAP in f.util.oscinparse
does with the quantiser on: every MIDI note n from 0 to 131 is sent as the
0..1 GUI value n/132, scaled to a gamut index by `* (gamut length * 11)`,
and fed to the real f.util.keyselector, with master-pitch-gamut,
pitch-gamut-length and scale-root-note set as pitch-gamut-parse and
f.util.masterkey would set them. With the quantiser off, the same value
plays the pitch n (x * 132), so n is the pitch the quantiser is given.

The gamut is relative to the root: root F with C E F active is F A Bb. So
the quantiser must give root + the gamut pitch nearest to (n - root), and a
pitch that is in the gamut under both the old and the new root must not
move when the root changes. Each result is checked against the nearest
pitch of the gamut (transposed by the root, in every octave):
  - a note that is in the gamut must come out unchanged (failure otherwise:
    "exact C does not give C");
  - any other note should come out as its nearest gamut pitch (reported;
    an exact tie between two gamut pitches accepts either).
Exits 1 if any note in the gamut moved.

--nudge subtracts a tiny amount from the GUI value. That is float32
rounding on its way from the GUI over OSC, so a quantiser that truncates
instead of rounding shows up here.

Needs no audio device, no GUI, no externals and no built app: only plain
Pd. A run takes about a second per case.

    python3 src/tools/quantise-probe.py                     # the built-in cases
    python3 src/tools/quantise-probe.py --gamut "0 2 4 7 9" --root 2
    python3 src/tools/quantise-probe.py --gamut "0 1.5 4 5.5 7 9.5" --all-notes
    python3 src/tools/quantise-probe.py --rev 884dedb       # compare history
"""
import argparse
import importlib.util
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# The Pd helpers (finding Pd, writing and running a patch, --rev checkouts)
# are automation-probe.py's; its hyphenated name needs importlib.
_spec = importlib.util.spec_from_file_location("automation_probe", os.path.join(HERE, "automation-probe.py"))
probe = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(probe)

NOTES = 132                     # NOTE_SNAP's range: 11 octaves of 12 notes
NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

# name -> (gamut as semitones above the root, root note 0..12)
CASES = {
    "major":            ([0, 2, 4, 5, 7, 9, 11], 0),
    "major-root-D":     ([0, 2, 4, 5, 7, 9, 11], 2),
    # The gamut is relative to the root: C E F with root C, then root F
    # (F A Bb). F is in both, so a pitch held on F must stay F when the root
    # changes, not be transposed with it.
    "CEF-root-C":       ([0, 4, 5], 0),
    "CEF-root-F":       ([0, 4, 5], 5),
    "pentatonic":       ([0, 2, 4, 7, 9], 0),
    "whole-tone":       ([0, 2, 4, 6, 8, 10], 0),
    "C-only":           ([0], 0),
    "microtonal":       ([0, 1.5, 4, 5.5, 7, 9.5], 0),
    "chromatic":        (list(range(12)), 0),
}
DEFAULT_NUDGE = 1e-7            # about the float32 rounding of a GUI value near 0.45


def build_patch(path, gamut, root, nudge):
    p = probe.Patch()
    length = len(gamut)
    values = " ".join("%g" % g for g in gamut)

    load = p.obj(20, 20, "loadbang")
    fan = p.obj(20, 50, "t b b b")      # right to left: set up, run, quit
    p.connect(load, 0, fan, 0)

    # What pitch-gamut-parse and f.util.masterkey leave behind.
    p.obj(400, 20, "array define master-pitch-gamut 12")
    setup = p.msg(200, 80, f"\\; master-pitch-gamut 0 {values} \\; pitch-gamut-length {length} "
                           f"\\; scale-root-note {root}")
    p.connect(fan, 2, setup, 0)

    # for n in 0 .. NOTES-1
    loop = p.obj(20, 110, f"t b b")
    p.connect(fan, 1, loop, 0)
    zero = p.msg(120, 140, "0")
    p.connect(loop, 1, zero, 0)
    until = p.obj(20, 140, "until")
    p.connect(loop, 0, until, 0)
    count = p.obj(20, 170, "f")
    incr = p.obj(60, 170, "+ 1")
    p.connect(until, 0, count, 0)
    p.connect(count, 0, incr, 0)
    p.connect(incr, 0, count, 1)
    p.connect(zero, 0, count, 1)
    stop = p.obj(20, 200, f"moses {NOTES}")
    p.connect(count, 0, stop, 0)
    done = p.obj(120, 200, "t b")       # [until]'s stop inlet takes a bang only
    p.connect(stop, 1, done, 0)
    p.connect(done, 0, until, 1)

    # n -> GUI value -> gamut index -> keyselector, as NOTE_SNAP does.
    split = p.obj(20, 230, "t f f")
    p.connect(stop, 0, split, 0)
    gui = p.obj(20, 260, f"/ {NOTES}")
    p.connect(split, 0, gui, 0)
    last = gui
    if nudge:
        last2 = p.obj(20, 290, "- %g" % nudge)
        p.connect(last, 0, last2, 0)
        last = last2
    scale = p.obj(20, 320, f"* {length * 11}")
    p.connect(last, 0, scale, 0)
    snap = p.obj(20, 350, "f.util.keyselector 0")
    p.connect(scale, 0, snap, 0)
    pack = p.obj(20, 380, "pack f f")
    p.connect(split, 1, pack, 1)
    p.connect(snap, 0, pack, 0)
    p.connect(pack, 0, p.obj(20, 410, "print SNAP"), 0)

    quit = p.msg(300, 110, "\\; pd quit")
    p.connect(fan, 0, quit, 0)
    p.write(path)


def parse(log):
    results = {}
    for line in log.splitlines():
        m = re.match(r"SNAP: (\S+) (\S+)", line.strip())
        if m:
            results[int(round(float(m.group(2))))] = float(m.group(1))
    return results


def nearest(n, gamut, root):
    """The gamut pitches nearest to n (two on an exact tie)."""
    candidates = [g + root + 12 * octave for g in gamut for octave in range(-1, NOTES // 12 + 2)]
    best = min(abs(c - n) for c in candidates)
    return sorted({c for c in candidates if abs(abs(c - n) - best) < 1e-6})


def name(pitch):
    if abs(pitch - round(pitch)) < 1e-6:
        p = int(round(pitch))
        return f"{NAMES[p % 12]}{p // 12 - 1}"
    return "%.2f" % pitch


def check(results, gamut, root, notes):
    """(wrong in-gamut notes, other notes not at their nearest gamut pitch)."""
    in_gamut = {(g + root) % 12 for g in gamut if abs(g - round(g)) < 1e-6}
    moved, off = [], []
    for n in notes:
        if n not in results:
            moved.append((n, None))
            continue
        got = results[n]
        if n % 12 in in_gamut:
            if abs(got - n) > 1e-6:
                moved.append((n, got))
        elif not any(abs(got - c) < 1e-6 for c in nearest(n, gamut, root)):
            off.append((n, got))
    return moved, off


def report(label, results, gamut, root, notes, show_all):
    moved, off = check(results, gamut, root, notes)
    gamut_text = " ".join("%g" % g for g in gamut)
    status = "ok" if not moved else "FAIL"
    print(f"{label:<26} gamut [{gamut_text}] root {NAMES[root % 12]}: "
          f"{status}, {len(moved)} in-gamut notes moved, {len(off)} others not at the nearest gamut pitch")
    if show_all or moved or off:
        row = []
        in_gamut = {(g + root) % 12 for g in gamut if abs(g - round(g)) < 1e-6}
        for n in notes:
            got = results.get(n)
            mark = "*" if n % 12 in in_gamut else ""
            text = "?" if got is None else name(got)
            flag = "" if (n, got) not in moved and (n, got) not in off else "!"
            row.append(f"{name(n)}{mark}->{text}{flag}")
        for i in range(0, len(row), 12):
            print("    " + "  ".join(row[i:i + 12]))
    return bool(moved)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gamut", default=None,
                    help='one gamut instead of the built-in cases, semitones above the root, e.g. "0 2 4 7 9"')
    ap.add_argument("--root", type=int, default=0, help="scale root 0..12 for --gamut (default 0, C)")
    ap.add_argument("--nudge", type=float, default=None,
                    help=f"subtract this from each GUI value (default: run each case exact and "
                         f"with {DEFAULT_NUDGE:g})")
    ap.add_argument("--all-notes", action="store_true",
                    help="check all 132 notes, not just the octave from C4")
    ap.add_argument("--show", action="store_true", help="print every note, not only cases that fail")
    ap.add_argument("--path", default=None,
                    help="directory holding the control abstractions (default: src/pd/controlabstractions)")
    ap.add_argument("--rev", default=None, help="git revision to probe instead of the working tree")
    ap.add_argument("--pd", default=None, help="path to the Pd binary")
    ap.add_argument("--keep", action="store_true", help="keep the generated patches and Pd logs")
    args = ap.parse_args()

    if args.gamut is not None:
        try:
            gamut = [float(v) for v in args.gamut.split()]
        except ValueError:
            sys.exit("--gamut takes numbers, e.g. \"0 2 4 7 9\"")
        if not 1 <= len(gamut) <= 12:
            sys.exit("--gamut takes 1 to 12 pitches")
        cases = {"gamut": (gamut, args.root)}
    else:
        cases = CASES
    nudges = [args.nudge] if args.nudge is not None else [0.0, DEFAULT_NUDGE]
    notes = range(NOTES) if args.all_notes else range(60, 72)

    pd = probe.find_pd(args.pd)
    tmp = tempfile.mkdtemp(prefix="quantise-probe-")
    revtmp = None
    failed = False
    try:
        if args.rev:
            revtmp, search_path = probe.checkout(args.rev)
            label = f"revision {args.rev}"
        else:
            search_path = args.path or probe.ABSTRACTIONS
            label = search_path
        if not os.path.isdir(search_path):
            sys.exit(f"no such directory: {search_path}")
        print(f"quantise probe: {label}")

        for case, (gamut, root) in cases.items():
            for nudge in nudges:
                patch = os.path.join(tmp, f"{case}-{nudge:g}.pd")
                build_patch(patch, gamut, root, nudge)
                log = probe.run(pd, patch, search_path, timeout=30)
                errors = [l.strip() for l in log.splitlines()
                          if "error" in l.lower() or "couldn't create" in l]
                for line in errors[:5]:
                    print(f"  pd: {line}")
                if len(errors) > 5:
                    print(f"  pd: ... {len(errors) - 5} more")
                if args.keep:
                    with open(patch[:-3] + ".log", "w") as fh:
                        fh.write(log)
                results = parse(log)
                if not results:
                    print(f"{case}: no output from Pd (see --keep)")
                    failed = True
                    continue
                tag = case if not nudge else f"{case}, {nudge:g} low"
                failed |= report(tag, results, gamut, root, notes, args.show)
        if args.keep:
            print(f"  kept: {tmp}")
        return 1 if failed else 0
    finally:
        if not args.keep:
            shutil.rmtree(tmp, ignore_errors=True)
        if revtmp:
            shutil.rmtree(revtmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
