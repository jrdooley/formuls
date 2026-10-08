#!/usr/bin/env python3
"""Two experimental client patches for a vendored Open Stage Control 1.31 package,
measured in ../../session-size.md. Same contract as src/tools/patch-osc-perf.py:
matched on code rather than line numbers, idempotent, hard failure on a missing anchor.
Neither is applied by the build.

  --tab-show-unforced
      Tab.show() calls check(widget, true), which resizes (reallocates and redraws)
      every canvas on the tab even when its size has not changed. Passing false resizes
      only canvases whose size changed while the tab was hidden. Measured: synth-tab
      switch 58.6 -> 48.7 ms median on the flattened session; canvases updated while
      their tab was hidden were verified up to date on show.

  --multixy-point-interaction
      Adds a per-point `interaction` key to multixy `pointsAttr`. A point whose value is
      false, 0, "0", "false" or "" stays visible but cannot be grabbed; touches go to
      the nearest interactive point. Accepts OSC{} (pointsAttr is a dynamic prop, so a
      mode change updates in place with no widget rebuild).

usage: patch-experiments.py OSC_PACKAGE_DIR [--tab-show-unforced] [--multixy-point-interaction]
"""
import sys, pathlib

PATCHES = {
    '--tab-show-unforced': [
        ('this.detached=!1,this.setVisibility(),check(this.widget,!0))}',
         'this.detached=!1,this.setVisibility(),check(this.widget,!1))}'),
    ],
    '--multixy-point-interaction': [
        ('-1==Object.values(this.touchMap).indexOf(g)&&this.pads[g].getProp("visible")&&(u=',
         '-1==Object.values(this.touchMap).indexOf(g)&&this.pads[g].getProp("visible")'
         '&&!(this.pointsAttr[g]&&[!1,0,"0","false",""].includes(this.pointsAttr[g].interaction))&&(u='),
        ('void 0===p.label&&(p.label=u?this.getProp("points")[h]:String(h)),',
         'void 0===p.label&&(p.label=u?this.getProp("points")[h]:String(h)),'
         'this.pointsAttr[h]&&(this.pointsAttr[h].interaction=p.interaction),'),
    ],
}

def main():
    if len(sys.argv) < 3 or any(a not in PATCHES for a in sys.argv[2:]):
        sys.exit(__doc__)
    js = pathlib.Path(sys.argv[1]) / 'client' / 'index.js'
    s = js.read_text()
    for flag in sys.argv[2:]:
        for old, new in PATCHES[flag]:
            if s.count(new) == 1:
                print(f'{flag}: already applied'); continue
            if s.count(old) != 1:
                sys.exit(f'{flag}: anchor found {s.count(old)} times, expected 1: {old[:70]}')
            s = s.replace(old, new)
            print(f'{flag}: applied')
    js.write_text(s)

if __name__ == '__main__':
    main()
