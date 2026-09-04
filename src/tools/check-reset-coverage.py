#!/usr/bin/env python3
"""Check the reset parameter table against the interface it resets.

Every parameter with chaos/LFO/mod sub-widgets is declared twice: once in
gui/_main.json, where the interface builds the widgets, and once in
f.util.reset.pd, where Pd zeroes them on reset. Pd has no way to derive the
second from the first, so the two drift -- that drift is what left the
Sequencer Add/Drop, chorus, saturation, pitchshift, gate and flam widgets
untouched by a reset, and it was invisible because a message sent to an
address no widget owns fails silently at both ends.

Run from the repository root:  python3 src/tools/check-reset-coverage.py
Exits non-zero if the table and the interface disagree.
"""
import json, re, sys, os

GUI   = 'src/gui/_main.json'
RESET = 'src/pd/controlabstractions/f.util.reset.pd'

# widget template -> (abstraction that resets it, does it have a mod matrix)
KIND = {
    'slider':  ('f.util.lfoguireset',   1),
    'split':   ('f.util.lfoguireset',   1),
    'menu':    ('f.util.lfoguireset',   0),
    'xya':     ('f.util.lfoguixyreset', 1),
    'xyb':     ('f.util.lfoguixyreset', 1),
    'xyosc':   ('f.util.lfoguixyreset', 1),
    'multixy': ('f.util.lfoguixyreset', 0),
}
# global controls that live outside a synth instance and are not reset per-synth
EXEMPT = {'tempo'}


def gui_parameters(path):
    """name -> widget template, for every cloned parameter in the interface."""
    out = {}
    def walk(n):
        if n.get('type') == 'clone' and isinstance(n.get('props'), str):
            m = re.search(r'"name"\s*:\s*"([^"]+)"', n['props'])
            if m:
                out[m.group(1)] = n.get('widgetId')
        for k in ('widgets', 'tabs'):
            for c in (n.get(k) or []):
                walk(c)
    walk(json.load(open(path))['content'])
    for e in EXEMPT:
        out.pop(e, None)
    return out


def reset_table(path):
    """name -> (abstraction, mod flag) as declared in pd LFO_MOD_CHAOS."""
    out = {}
    src = open(path, encoding='utf-8', errors='replace').read().replace('\\$', '$')
    for m in re.finditer(r'#X obj \d+ \d+ (f\.util\.lfogui(?:xy)?reset) \$1 (\S+?) (\d)\s*;', src):
        out[m.group(2)] = (m.group(1), int(m.group(3)))
    return out


def main():
    root = os.getcwd()
    for f in (GUI, RESET):
        if not os.path.exists(os.path.join(root, f)):
            sys.exit('run this from the repository root (%s not found)' % f)

    gui, table = gui_parameters(GUI), reset_table(RESET)
    problems = []

    for name, tpl in sorted(gui.items()):
        if tpl not in KIND:
            problems.append('%-24s interface uses template %r, which this checker '
                            'does not know; teach it in KIND' % (name, tpl))
            continue
        want = KIND[tpl]
        have = table.get(name)
        if have is None:
            problems.append('%-24s in the interface (%s) but not reset at all; add '
                            '[%s $1 %s %d]' % (name, tpl, want[0], name, want[1]))
        elif have != want:
            problems.append('%-24s is a %s, so it needs [%s $1 %s %d]; the table says '
                            '[%s $1 %s %d]' % (name, tpl, want[0], name, want[1],
                                               have[0], name, have[1]))

    for name in sorted(set(table) - set(gui)):
        problems.append('%-24s reset, but no widget of that name exists in the '
                        'interface; the messages go nowhere' % name)

    if problems:
        print('reset table and interface disagree:\n')
        for p in problems:
            print('  ' + p)
        print('\n%d problem(s); %d parameters in the interface, %d in the table'
              % (len(problems), len(gui), len(table)))
        return 1
    print('ok: all %d interface parameters are reset with the right widget kind' % len(gui))
    return 0


if __name__ == '__main__':
    sys.exit(main())
