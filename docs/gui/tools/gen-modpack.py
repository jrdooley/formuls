#!/usr/bin/env python3
"""Generate f.gui.modpack.pd -- one packed GUI message per sub-panel per tick.

    python3 docs/gui/tools/gen-modpack.py src/pd/controlabstractions/f.gui.modpack.pd

Hand-numbering Pd connections is how you get silent miswirings, so the patch
is generated and the indices are managed here. Two bugs this caught only
because the output was tested rather than read:

  - the receive was never wired to the route at all;
  - arity-1 parameters were wired to route outlet 0 instead of their own.

PARAMS lists the Pd-side parameter names, which are NOT the layout's clone
ids: the fxa panel's clones are oscslide/pitchshift/saturationbitcrush/
chorusphaser, but the names Pd sends under come from each clone's "name"
variable. Two of these pads arrive as a pair from f.util.oscformatxy; the
pitchshift pad is assembled in the patch from two separate scalar
f.util.oscformat instances, so it arrives as two arity-1 entries.
"""
import sys

# fxa: four xy pads, two values each -> eight slots
# fxa holds four xy pads. Two come straight from f.util.oscformatxy and
# arrive as a pair; the pitchshift pad is assembled in the patch from two
# separate f.util.oscformat instances, so it arrives as two scalars.
PANEL = "fxa"
PARAMS = [("oscsliderangetime", 2), ("pitchshift", 1), ("pitchshiftmix", 1),
          ("saturationbitcrush", 2), ("chorusphaser", 2)]
SLOTS = sum(n for _, n in PARAMS)

objs, conns = [], []
def obj(x, y, text):
    objs.append(("obj", x, y, text)); return len(objs) - 1
def msg(x, y, text):
    objs.append(("msg", x, y, text)); return len(objs) - 1
def link(a, ao, b, bo):
    conns.append((a, ao, b, bo))

ARR = f"\\$0-pack-{PANEL}"

# --- incoming values -------------------------------------------------------
rin   = obj(20,  20, "r \\$1-modvalue")
# [list prepend] upstream makes these "list <name> ...", whose selector is
# "list" -- [route] matches selectors, so it needs [list trim] first. Testing
# the abstraction with a message box hides this: a message box already
# produces the selector form.
trim  = obj(20,  50, "list trim")
route = obj(20,  80, "route " + " ".join(n for n, _ in PARAMS))
link(rin, 0, trim, 0)
link(trim, 0, route, 0)

# --- one pair of tabwrites per parameter, indices fixed at load ------------
lb = obj(560, 20, "loadbang")
slot = 0
for i, (name, arity) in enumerate(PARAMS):
    x = 20 + i * 175
    src, srcout = route, i
    if arity == 2:
        src = obj(x, 90, "unpack f f"); srcout = 0
        link(route, i, src, 0)
    for j in range(arity):
        tw  = obj(x + j * 85, 130, f"tabwrite {ARR}")
        idx = msg(x + j * 85, 160, str(slot))
        link(src, j if arity == 2 else srcout, tw, 0)
        link(lb, 0, idx, 0)
        link(idx, 0, tw, 1)
        slot += 1

# --- dirty flag: only emit when something moved ---------------------------
dirty_set = msg(960, 60, "1")
for i in range(len(PARAMS)):
    link(route, i, dirty_set, 0)

fflag = obj(560, 120, "f")
link(dirty_set, 0, fflag, 1)

tick  = obj(720,  20, "r master-gui-update-throttle")
tbb   = obj(720,  50, "t b b")
link(tick, 0, tbb, 0)
link(tbb, 1, fflag, 0)                        # right outlet fires first: read
clear = msg(720, 90, "0")
link(tbb, 0, clear, 0)                        # then clear
link(clear, 0, fflag, 1)

sel1  = obj(560, 150, "sel 1")
link(fflag, 0, sel1, 0)

# --- read the array out and send it as one OSC message --------------------
aget  = obj(560, 190, f"array get {ARR}")
link(sel1, 0, aget, 0)
ofmt  = obj(560, 220, f"oscformat mod{PANEL}\\$1")
link(aget, 0, ofmt, 0)
send  = obj(560, 250, "s \\$1-message-collect")
link(ofmt, 0, send, 0)

adef  = obj(20, 300, f"array define {ARR} {SLOTS}")

lines = [f"#N canvas 60 60 980 420 10;"]
for kind, x, y, text in objs:
    lines.append(f"#X {kind} {x} {y} {text};")
for a, ao, b, bo in conns:
    lines.append(f"#X connect {a} {ao} {b} {bo};")

out = sys.argv[1]
open(out, "w").write("\n".join(lines) + "\n")
print(f"wrote {out}: {len(objs)} objects, {len(conns)} connections, "
      f"{SLOTS} slots for {PANEL}")
