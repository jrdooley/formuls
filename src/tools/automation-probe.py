#!/usr/bin/env python3
"""Record an automation gesture into f.seq.automater and log what comes back out.

Builds a throwaway Pd patch that drives one automater the way the running app
does -- master parameter/GUI throttles, a beat on `clockin`, `record 1`, a
gesture, `record 0` -- runs it headless, and reports every discontinuity in the
value the automater sends towards Faust.

Needs no audio device, no GUI, no externals and no built app: f.seq.automater
and everything under it are plain Pd. One run takes about as long as the
simulated timeline (a few seconds).

    python3 src/tools/automation-probe.py
    python3 src/tools/automation-probe.py --gesture triangle --press 520
    python3 src/tools/automation-probe.py --rev 2194d48        # compare history
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ABSTRACTIONS = os.path.join(REPO, "src", "pd", "controlabstractions")

# The gesture the take records, as a function of a 0..1 phase across the take.
# "triangle" ends where it started, so a correct implementation loops it with no
# step at all; "hold" never moves, so any reported jump is the harness's fault.
GESTURES = {
    "ramp": r"expr min(1 \, \$f1)",
    "triangle": r"expr 1 - abs(1 - 2 * min(1 \, \$f1))",
    "hold": r"expr 0.5",
}


def find_pd(override=None):
    """Locate a Pd binary: an explicit path, then PATH, then a macOS bundle."""
    if override:
        if not os.path.isfile(override):
            sys.exit(f"no Pd binary at {override}")
        return override
    found = shutil.which("pd")
    if found:
        return found
    import glob
    bundles = sorted(glob.glob("/Applications/Pd*.app/Contents/Resources/bin/pd"))
    if bundles:
        return bundles[-1]
    sys.exit("Pd not found: install it, or pass --pd /path/to/pd")


class Patch:
    """Minimal Pd patch writer.

    Object indices are positional and count comments and subpatches alike, so
    connections are made through the handles add() returns rather than by
    counting lines. A ';' inside a message box has to reach the file as '\\;' --
    unescaped it parses as a separate record, which is how a stray `; pd quit`
    silently quits Pd at load with no output at all.
    """

    def __init__(self):
        self.objs = []
        self.conns = []

    def add(self, kind, x, y, text):
        self.objs.append(f"#X {kind} {x} {y} {text};")
        return len(self.objs) - 1

    def obj(self, x, y, text):
        return self.add("obj", x, y, text)

    def msg(self, x, y, text):
        return self.add("msg", x, y, text)

    def connect(self, src, outlet, dst, inlet):
        self.conns.append(f"#X connect {src} {outlet} {dst} {inlet};")

    def write(self, path):
        with open(path, "w") as fh:
            fh.write("#N canvas 0 0 900 700 10;\n")
            fh.write("\n".join(self.objs) + "\n")
            fh.write("\n".join(self.conns) + "\n")


def build_patch(path, args):
    p = Patch()
    take_ms = args.release - args.press
    steps = max(1, take_ms // args.throttle)

    load = p.obj(20, 20, "loadbang")
    fan = p.obj(20, 50, "t b b b b b b")
    p.connect(load, 0, fan, 0)

    # The two master throttles the app runs: parameter updates towards Faust,
    # and the slower GUI feedback stream.
    pmetro = p.obj(400, 50, f"metro {args.throttle}")
    p.connect(load, 0, pmetro, 0)
    p.connect(pmetro, 0, p.obj(400, 80, "s master-param-update-throttle"), 0)
    gmetro = p.obj(600, 50, f"metro {args.gui_throttle}")
    p.connect(load, 0, gmetro, 0)
    p.connect(gmetro, 0, p.obj(600, 80, "s master-gui-update-throttle"), 0)

    auto = p.obj(20, 500, "f.seq.automater 999 999-probe 0 0")

    # Log outlet 0 -- the value that reaches Faust -- with a timestamp. [t f b]
    # rather than [t b f]: the bang has to re-measure the timer BEFORE the float
    # opens the pack, or every line carries the previous frame's time.
    split = p.obj(20, 530, "t f b")
    timer = p.obj(760, 50, "timer")
    pack = p.obj(20, 560, "pack f 0")
    p.connect(load, 0, timer, 0)
    p.connect(auto, 0, split, 0)
    p.connect(split, 1, timer, 1)
    p.connect(timer, 0, pack, 1)
    p.connect(split, 0, pack, 0)
    p.connect(pack, 0, p.obj(20, 590, "print VAL"), 0)
    # Outlet 2 pulses once when a take finishes, so the log marks the release.
    p.connect(auto, 2, p.obj(300, 590, "print RELEASE"), 0)

    # active 1, then a beat every --beat ms: automation playback is meant to
    # start on a beat, so the beat grid is what the take is measured against.
    arm = p.obj(20, 90, "del 50")
    armmsg = p.msg(20, 120, "active 1")
    p.connect(fan, 0, arm, 0)
    p.connect(arm, 0, armmsg, 0)
    p.connect(armmsg, 0, auto, 0)

    beat = p.obj(120, 90, f"metro {args.beat}")
    beatmsg = p.msg(120, 120, "clockin bang")
    p.connect(fan, 1, beat, 0)
    p.connect(beat, 0, beatmsg, 0)
    p.connect(beatmsg, 0, auto, 0)

    press = p.obj(240, 90, f"del {args.press}")
    pressmsg = p.msg(240, 120, "record 1")
    p.connect(fan, 2, press, 0)
    p.connect(press, 0, pressmsg, 0)
    p.connect(pressmsg, 0, auto, 0)

    release = p.obj(360, 90, f"del {args.release}")
    releasemsg = p.msg(360, 120, "record 0")
    p.connect(fan, 3, release, 0)
    p.connect(release, 0, releasemsg, 0)
    p.connect(releasemsg, 0, auto, 0)

    # The gesture: a phase counter over the take, shaped, sent as a bare float
    # (the automater's [route] catches those on its `float` branch).
    gest = p.obj(240, 180, f"metro {args.throttle}")
    count = p.obj(240, 210, "f")
    incr = p.obj(300, 210, "+ 1")
    phase = p.obj(240, 240, f"/ {steps}")
    shape = p.obj(240, 270, GESTURES[args.gesture])
    p.connect(press, 0, gest, 0)
    p.connect(gest, 0, count, 0)
    p.connect(count, 0, incr, 0)
    p.connect(incr, 0, count, 1)
    p.connect(count, 0, phase, 0)
    p.connect(phase, 0, shape, 0)
    p.connect(shape, 0, auto, 0)
    stop = p.msg(360, 180, "stop")
    p.connect(release, 0, stop, 0)
    p.connect(stop, 0, gest, 0)

    quit_at = p.obj(700, 90, f"del {args.until}")
    quit_msg = p.msg(700, 120, r"\; pd quit")
    p.connect(fan, 4, quit_at, 0)
    p.connect(quit_at, 0, quit_msg, 0)

    p.write(path)
    return take_ms


def run(pd, patch, search_path, timeout):
    """Run the patch. The patch quits itself; the timeout is a backstop so a
    patch that fails to load can never wedge the tool."""
    cmd = [pd, "-nogui", "-stderr", "-noaudio", "-path", search_path, "-open", patch]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        sys.stderr.write(f"Pd did not quit within {timeout:.0f}s; killed it\n")
        return (exc.stdout or b"").decode("utf-8", "replace") + \
               (exc.stderr or b"").decode("utf-8", "replace")
    return out.stdout + out.stderr


def parse(log):
    """-> (frames, release_index) where frames is [(time_ms, value)]."""
    frames, release_at = [], None
    for line in log.splitlines():
        m = re.match(r"VAL: (-?[\d.e+-]+) ([\d.]+)$", line.strip())
        if m:
            frames.append((float(m.group(2)), float(m.group(1))))
        elif line.startswith("RELEASE:") and release_at is None:
            release_at = len(frames)
    return frames, release_at


def report(frames, release_at, args, take_ms):
    if not frames:
        print("  no output at all -- the automater did not load, or Pd quit early")
        return 1

    held = None
    if release_at is not None and 0 < release_at < len(frames):
        held = frames[release_at][0] - frames[release_at - 1][0]

    print(f"  take {args.press}-{args.release} ms ({take_ms} ms), "
          f"beat every {args.beat} ms, gesture '{args.gesture}'")
    print(f"  {len(frames)} frames out of the automater")
    if release_at is None:
        print("  never saw the end-of-take pulse on outlet 2")
    elif held is not None:
        print(f"  last live value {frames[release_at - 1][1]:.3f} at "
              f"{frames[release_at - 1][0]:.0f} ms; next value "
              f"{frames[release_at][1]:.3f} at {frames[release_at][0]:.0f} ms "
              f"(held {held:.0f} ms)")

    jumps = []
    for i in range(1, len(frames)):
        delta = frames[i][1] - frames[i - 1][1]
        if abs(delta) > args.threshold:
            jumps.append((i, frames[i - 1], frames[i], delta))

    if not jumps:
        print(f"  no step larger than {args.threshold}")
    for i, prev, cur, delta in jumps:
        where = ""
        if release_at is not None:
            where = " <- first step after the take" if i == release_at else ""
        print(f"  step {delta:+.3f}  {prev[1]:.3f} -> {cur[1]:.3f}  "
              f"at {cur[0]:.0f} ms{where}")

    if args.trace:
        print("  --- trace ---")
        for i, (t, v) in enumerate(frames):
            mark = "  <- take ends here" if i == release_at else ""
            print(f"    {t:7.0f} ms  {v:.4f}{mark}")
    return 0


def checkout(rev):
    """Extract one revision's control abstractions so runs can be compared."""
    tmp = tempfile.mkdtemp(prefix="automation-probe-")
    archive = subprocess.run(
        ["git", "archive", rev, "src/pd/controlabstractions"],
        cwd=REPO, capture_output=True,
    )
    if archive.returncode:
        shutil.rmtree(tmp, ignore_errors=True)
        sys.exit(f"git archive {rev} failed: {archive.stderr.decode().strip()}")
    subprocess.run(["tar", "-x", "-C", tmp], input=archive.stdout, check=True)
    return tmp, os.path.join(tmp, "src", "pd", "controlabstractions")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--path", default=None,
                    help="directory holding the control abstractions "
                         "(default: src/pd/controlabstractions)")
    ap.add_argument("--rev", default=None,
                    help="git revision to probe instead of the working tree")
    ap.add_argument("--gesture", choices=sorted(GESTURES), default="ramp",
                    help="shape recorded during the take (default: ramp)")
    ap.add_argument("--press", type=int, default=300, help="record on, ms")
    ap.add_argument("--release", type=int, default=1300, help="record off, ms")
    ap.add_argument("--beat", type=int, default=500, help="clockin interval, ms")
    ap.add_argument("--throttle", type=int, default=10,
                    help="master parameter throttle interval, ms")
    ap.add_argument("--gui-throttle", type=int, default=50,
                    help="master GUI throttle interval, ms")
    ap.add_argument("--until", type=int, default=4000,
                    help="how long to run before quitting, ms")
    ap.add_argument("--threshold", type=float, default=0.05,
                    help="report steps larger than this (default: 0.05)")
    ap.add_argument("--trace", action="store_true", help="print every frame")
    ap.add_argument("--pd", default=None, help="path to the Pd binary")
    ap.add_argument("--keep", action="store_true",
                    help="keep the generated patch and Pd log")
    args = ap.parse_args()

    if args.release <= args.press:
        sys.exit("--release must be after --press")
    if args.until <= args.release:
        sys.exit("--until must be after --release, or playback is never seen")

    pd = find_pd(args.pd)
    tmp = tempfile.mkdtemp(prefix="automation-probe-")
    revtmp = None
    try:
        if args.rev:
            revtmp, search_path = checkout(args.rev)
            label = f"revision {args.rev}"
        else:
            search_path = args.path or ABSTRACTIONS
            label = search_path
        if not os.path.isdir(search_path):
            sys.exit(f"no such directory: {search_path}")

        patch = os.path.join(tmp, "probe.pd")
        take_ms = build_patch(patch, args)
        log = run(pd, patch, search_path, timeout=args.until / 1000.0 + 10)

        print(f"automation probe: {label}")
        # Pd names the object it could not create on the line before the error,
        # so report the pair rather than a bare "couldn't create".
        lines = log.splitlines()
        for i, line in enumerate(lines):
            if "error" not in line.lower() and "couldn't create" not in line:
                continue
            prev = lines[i - 1].strip() if i else ""
            if prev and not prev.startswith(("VAL:", "RELEASE:")):
                print(f"  pd: {prev}: {line.strip()}")
            else:
                print(f"  pd: {line.strip()}")
        frames, release_at = parse(log)
        status = report(frames, release_at, args, take_ms)

        if args.keep:
            with open(os.path.join(tmp, "pd.log"), "w") as fh:
                fh.write(log)
            print(f"  kept: {patch} and {os.path.join(tmp, 'pd.log')}")
        return status
    finally:
        if not args.keep:
            shutil.rmtree(tmp, ignore_errors=True)
        if revtmp:
            shutil.rmtree(revtmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
