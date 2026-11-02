#!/usr/bin/env python3
"""Generate (or check) the OSC address inventory the control interface exposes.

Pd and the GUI agree on an address per parameter, and nothing declares that
agreement anywhere: addresses are *widget ids*. 943 of the 964 authored widgets
have `address: "auto"`, which Open Stage Control resolves to '/' + the widget's
id, so renaming a widget renames a parameter and a message sent to an address no
widget owns fails silently at both ends -- the same failure mode
check-reset-coverage.py exists for.

This writes that agreement down, derived from the session rather than by hand:
one row per address, with how many widgets sit on it, whether anything can send
it, its range, quantisation and rounding, and its default from the state file.
It is the table a replacement front-end would be built and tested against.

It runs the real flattener first (flatten-session.py, imported, same template
tabs as build-macOS.sh), because the authored session names most of its widgets
`attack@{parent.variables.n}` inside a clone and those ids do not exist until
o-s-c resolves them. Anything still unresolved after flattening is a hard
failure, not a row: a template id in the table would be worse than no table.

Run from the repository root:

    python3 src/tools/osc-contract.py                      # write docs/gui/osc-contract.csv
    python3 src/tools/osc-contract.py --check              # CI: fail if it is out of date

--check exits non-zero and names what moved, so a widget rename that silently
changes a parameter's address stops a build instead of a performance.
"""
import argparse, collections, csv, importlib.util, io, json, os, re, sys

GUI      = 'src/gui/_main.json'
STATE    = 'src/gui/_formuls-default.state'
DEFAULTS = 'src/tools/osc-defaults-1.31.0.json'
FLATTEN  = 'src/tools/flatten-session.py'
OUT      = 'docs/gui/osc-contract.csv'

# as build-macOS.sh and build-linux.sh pass them: tabs that only hold templates.
# Their position is kept on purpose -- Pd reads the selected synth from the root
# tab index -- so they are emptied, not removed.
TEMPLATE_TABS = ['formuls0', 'Widgets']

# Widgets that carry a value, and so an address. Everything in the flattened
# session is in one of these two sets; a type in neither is a hard failure,
# because a new value-carrying type is a new row in the contract.
VALUE_TYPES = {
    'button', 'fader', 'knob', 'slider', 'encoder', 'range', 'switch', 'menu',
    'dropdown', 'input', 'textarea', 'xy', 'multixy', 'pad', 'rgb', 'canvas',
    'matrix', 'keyboard', 'led', 'text', 'file', 'variable', 'plot', 'eq',
    'visualizer',
}
CONTAINER_TYPES = {
    'session', 'root', 'panel', 'tab', 'modal', 'clone', 'fragment', 'folder',
    'patchbay', 'frame', 'html', 'image', 'svg', 'script',
}

# Modulation-layer suffixes, longest first so `chaostrigseq` is not read as
# `chaostrig` + "seq". f.util.controlparse routes $3$2, $3chaos$2, $3lfofreq$2,
# $3lfodepth$2, $3label$2 ($2 = instance, $3 = parameter); f.util.widgetparse
# takes the rest.
LAYERS = ['chaostrigseq', 'chaostrig', 'trigstep', 'trigseq', 'lfofreq',
          'lfodepth', 'moddepth', 'overlay', 'quantise', 'reverse', 'label',
          'chaos', 'act', 'mod']
LAYER_RE = re.compile(r'^(.*?)(' + '|'.join(LAYERS) + r')(\d*)$')
TRAILING_INDEX = re.compile(r'(\d+)$')

# What an id may contain to be usable as one OSC address segment. OSC 1.0
# reserves ' # * , / ? [ ] { }', and o-s-c puts the id straight after the '/'.
# An id outside this is not an address Pd can ever route, however it got there:
# the velocity parameter on a5396e7 was named `velocityact{"n":1,...}`, its
# clone's whole variables object spliced in by the resolution quirk
# flatten-session.py documents, and Pd's /velocity1 messages went nowhere.
ADDRESS_SEGMENT = re.compile(r'^[^ \t\n#*,/?\[\]{}]+$')

DEFAULT_RANGE = '{"min": 0, "max": 1}'   # o-s-c's, where none is set
DEFAULT_DECIMALS = '2'                   # o-s-c's; rounding is part of the contract

COLUMNS = ['address', 'views', 'types', 'direction', 'param', 'layer',
           'instance', 'range', 'steps', 'decimals', 'in_state', 'default']


class ContractError(Exception):
    pass


def load_flattener(path):
    """flatten-session.py as a module (its name is not an identifier)."""
    spec = importlib.util.spec_from_file_location('flatten_session', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def kids(w):
    return (w.get('widgets') or []) + (w.get('tabs') or [])


def value_widgets(session):
    """Every value-carrying widget in a flattened session, in document order."""
    out = []

    def walk(w, parent):
        t = w.get('type')
        if t is None:
            raise ContractError('a widget with no type, under %r' % (parent or {}).get('id'))
        if t not in VALUE_TYPES and t not in CONTAINER_TYPES:
            raise ContractError(
                'widget type %r is in neither VALUE_TYPES nor CONTAINER_TYPES; decide '
                'whether it carries an address and teach this script' % t)
        if t in VALUE_TYPES:
            out.append((w, parent))
        for c in kids(w):
            walk(c, w)

    walk(session['content'], None)
    return out


def address_of(w):
    a = w.get('address', 'auto')
    if a == 'auto':
        return '/' + w['id']
    return str(a)


def rows(session, state):
    by = collections.OrderedDict()
    for w, parent in value_widgets(session):
        wid = w.get('id')
        if not wid:
            continue                      # o-s-c gives these no address either
        if not isinstance(wid, str):
            raise ContractError('widget id %r is not a string' % (wid,))
        if '@{' in wid or '{{' in wid:
            raise ContractError(
                'widget id %r is still a template after flattening; the inventory would '
                'list an id that never exists at runtime' % wid)
        if not ADDRESS_SEGMENT.match(wid):
            raise ContractError(
                'widget id %r cannot be an OSC address segment, so no message can reach '
                'it; this is a bug in the session, not in the table' % wid)
        by.setdefault(address_of(w), []).append(w)

    out = []
    for address, widgets in by.items():
        name = address.lstrip('/')
        m = LAYER_RE.match(name)
        if m:
            param, layer, instance = m.group(1), m.group(2), m.group(3)
        else:
            idx = TRAILING_INDEX.search(name)
            param, layer, instance = name, '', (idx.group(1) if idx else '')

        def first(prop):
            for w in widgets:
                v = w.get(prop)
                if v not in (None, ''):
                    return v
            return ''

        rng = first('range')
        out.append({
            'address': address,
            'views': len(widgets),
            'types': '|'.join(sorted({w['type'] for w in widgets})),
            # an address with no interactive widget on it is one Pd only writes to
            'direction': ('both' if any(w.get('interaction', True) is not False
                                        for w in widgets) else 'pd->gui'),
            'param': param,
            'layer': layer,
            'instance': instance,
            'range': json.dumps(rng) if rng != '' else DEFAULT_RANGE,
            'steps': first('steps'),
            'decimals': first('decimals') or DEFAULT_DECIMALS,
            'in_state': 'yes' if name in state else 'no',
            'default': json.dumps(state[name]) if name in state else '',
        })
    out.sort(key=lambda r: r['address'])
    return out


def to_csv(table):
    buf = io.StringIO()
    w = csv.DictWriter(buf, fieldnames=COLUMNS, lineterminator='\n')
    w.writeheader()
    w.writerows(table)
    return buf.getvalue()


def summarise(table):
    direction = collections.Counter(r['direction'] for r in table)
    layers = collections.Counter(r['layer'] for r in table)
    shared = [r for r in table if r['views'] > 1]
    return ('%d addresses: %d both ways, %d pd->gui only; %d on more than one widget '
            '(max %d); %d parameter addresses and %d layer addresses across %d layers'
            % (len(table), direction['both'], direction['pd->gui'], len(shared),
               max((r['views'] for r in table), default=0), layers[''],
               len(table) - layers[''], len(layers) - 1))


def build():
    defaults = json.load(open(DEFAULTS))
    session = json.load(open(GUI))
    state = json.load(open(STATE))

    tabs = [t.get('id') for t in session['content'].get('tabs') or []]
    missing = [t for t in TEMPLATE_TABS if t not in tabs]
    if missing:
        raise ContractError('template tab(s) not found at the root: %s; TEMPLATE_TABS is '
                            'out of step with the build scripts' % missing)

    flatten = load_flattener(FLATTEN)
    try:
        flat = flatten.Flattener(session, defaults['types'], TEMPLATE_TABS).run()
    except flatten.FlattenError as e:
        raise ContractError('flatten-session: %s' % e)

    return rows(flat, state), state


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--out', default=OUT, help='where to write the inventory (default: %s)' % OUT)
    ap.add_argument('--check', action='store_true',
                    help='do not write; exit non-zero if --out is not what this would write')
    a = ap.parse_args()

    for f in (GUI, STATE, DEFAULTS, FLATTEN):
        if not os.path.exists(f):
            sys.exit('run this from the repository root (%s not found)' % f)

    try:
        table, state = build()
    except ContractError as e:
        sys.exit('osc-contract: %s' % e)

    text = to_csv(table)

    if not a.check:
        os.makedirs(os.path.dirname(a.out) or '.', exist_ok=True)
        with open(a.out, 'w') as f:
            f.write(text)
        print('osc-contract: wrote %s' % a.out)
        print('osc-contract: %s' % summarise(table))
        # The rest of the state file is container ids and widgets o-s-c saves but
        # Pd never addresses; the gap is expected, its size is worth watching.
        with_default = sum(1 for r in table if r['in_state'] == 'yes')
        print('osc-contract: %d addresses have a default; %d of the %d state entries '
              'match no address in the session'
              % (with_default, len(state) - with_default, len(state)))
        return 0

    if not os.path.exists(a.out):
        print('osc-contract: %s does not exist; run without --check to create it' % a.out)
        return 1

    old = {r['address']: r for r in csv.DictReader(open(a.out))}
    new = {r['address']: {k: str(v) for k, v in r.items()} for r in table}
    if old == new:
        print('osc-contract: %s is up to date (%s)' % (a.out, summarise(table)))
        return 0

    added, removed = sorted(set(new) - set(old)), sorted(set(old) - set(new))
    changed = [a_ for a_ in sorted(set(old) & set(new)) if old[a_] != new[a_]]

    print('%s is out of date: %d address(es) added, %d removed, %d changed.\n'
          % (a.out, len(added), len(removed), len(changed)))
    for label, items in (('added', added), ('removed', removed)):
        for addr in items[:20]:
            print('  %-8s %s' % (label, addr))
        if len(items) > 20:
            print('  %-8s ... and %d more' % (label, len(items) - 20))
    for addr in changed[:20]:
        diff = ', '.join('%s %r -> %r' % (k, old[addr][k], new[addr][k])
                         for k in COLUMNS if old[addr][k] != new[addr][k])
        print('  changed  %s: %s' % (addr, diff))
    if len(changed) > 20:
        print('  changed  ... and %d more' % (len(changed) - 20))

    print('\nA removed or changed address is a change to the interface Pd talks to. If it '
          'was deliberate, regenerate with:\n\n    python3 src/tools/osc-contract.py\n')
    return 1


if __name__ == '__main__':
    sys.exit(main())
