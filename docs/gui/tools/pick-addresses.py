#!/usr/bin/env python3
"""Build per-widget-type address lists from an osc-sink.py capture.

The capture holds every address the patch pushes to the GUI, but they are
not interchangeable: a fader takes one float and an xy pad takes two, and
the address families differ in cost. Pricing one widget type means driving
only that type, with the right arity.

    python3 pick-addresses.py addrs.txt outdir/

Writes outdir/addrs-fader.txt, addrs-xy.txt, addrs-params.txt (both together,
the realistic mix) and addrs-unmatched.txt (addresses no widget answers to -
the control that isolates transport cost from widget-update cost).
"""
import os, sys

CAPTURE = sys.argv[1] if len(sys.argv) > 1 else "addrs.txt"
OUT     = sys.argv[2] if len(sys.argv) > 2 else "."

# f.util.oscformat instances in f.util.oscinparse.pd: one float each
FADER = ["panning", "volume", "reverb", "attack", "decay", "release",
         "velocity", "delaytime"]
# f.util.oscformatxy instances: two floats each
XY    = ["saturationbitcrush", "chorusphaser", "fmfreqdepth",
         "noisefreqdepth", "oscsliderangetime", "delayfeedbacksend"]

have = set(l.rstrip("\n").split("\t")[-1] for l in open(CAPTURE))


def expand(families):
    return [f"/{f}{i}" for f in families for i in range(1, 7) if f"/{f}{i}" in have]


def write(name, addrs):
    path = os.path.join(OUT, name)
    with open(path, "w") as fh:
        fh.write("".join(f"1\t{a}\n" for a in addrs))
    print(f"{len(addrs):>4}  {path}")


fader, xy = expand(FADER), expand(XY)
write("addrs-fader.txt", fader)
write("addrs-xy.txt", xy)
write("addrs-params.txt", fader + xy)
write("addrs-unmatched.txt", [f"/nosuchwidget{i}" for i in range(500)])
