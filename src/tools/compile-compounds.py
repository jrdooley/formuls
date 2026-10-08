#!/usr/bin/env python3
"""Compile formuls' multi-widget compounds into single canvas widgets (build step).

Runs after flatten-session.py, on its output. Each `slider` compound -- 15 stacked
widgets (7 LED buttons, 5 faders, a 6-cell matrix, a label switch, a label button)
-- becomes ONE canvas whose 20-value array holds every sub-widget's value, drawn and
touched by src/gui/compounds/slider-lib.js. Measured in docs/gui/session-size.md
(option B): fewer widgets, listeners and canvases per tab.

Pd is untouched. Its messages are translated by the o-s-c server module
src/gui/formuls-module.js, from the map this writes:

  Pd -> GUI   /attackchaos1 0.4   becomes a single-slot patch for the canvas,
              applied in the client by a mailbox `variable` (never a whole-array
              overwrite, so an LED flashing while a fader is dragged cannot race it).
  GUI -> Pd   a gesture sends exactly the sub-widget message it sends today
              (/attack1, /attackmod1/2, /attacklabel1 ...) from the canvas script;
              a state recall sends the packed array once and the module unpacks it
              into today's per-address burst, in today's order.

It also rewrites references from outside a compound to a sub-widget
(@{saturation1.value} -> @{saturation1_c.value.0}), migrates the state file to the
packed values, and gives the mode inputs (chaos1, lfofreq1, ...) an onValue that
redraws the canvases they gate (updateCanvas, no broadcast).

Hard failure on anything it does not model.

usage: compile-compounds.py SESSION_IN STATE_IN SESSION_OUT STATE_OUT MAP_OUT --lib slider-lib.js
"""
import argparse, json, pathlib, re, sys

SLIDER_SHAPE = ['button'] * 7 + ['fader'] * 4 + ['matrix', 'switch', 'fader', 'button']
# child index -> canvas slot (LEDs 0..6 -> 12..18; faders; matrix cells 5..10; switch 11; label 19)
SLOT_OF_CHILD = {0: 12, 1: 13, 2: 14, 3: 15, 4: 16, 5: 17, 6: 18, 7: 0, 8: 1, 9: 2, 10: 3, 12: 11, 13: 4, 14: 19}
MATRIX_CHILD, MOD_SLOT, NSLOTS = 11, 5, 20
MODE_OF_CHILD = {8: 'chaos', 9: 'lfofreq', 10: 'lfodepth', 11: 'mod', 13: 'moddepth'}
PATCH_ADDRESS, PATCH_ID = '/fc_patch', 'fc_patch'
FONT = '16.5px'                      # o-s-c 1.31: 11px widget font x the template's 150%


class CompileError(Exception):
    pass


def kids(w):
    return (w.get('widgets') or []) + (w.get('tabs') or [])


def walk(w, fn, parent=None):
    fn(w, parent)
    for c in kids(w):
        walk(c, fn, w)


def address_of(w):
    a = w.get('address', 'auto')
    if a == 'auto':
        return '/' + str(w['id'])
    if not isinstance(a, str) or '@{' in a or a.startswith('#{'):
        raise CompileError(f'{w["id"]}: dynamic address {a!r} is not supported')
    return a


def osc_address(prop):
    """'OSC{/chaos1, 0}' -> '/chaos1'"""
    m = re.fullmatch(r'OSC\{\s*(/[^,}]*?)\s*(,[^}]*)?\}', str(prop))
    if not m:
        raise CompileError(f'expected an OSC{{}} mode gate, found {prop!r}')
    return m.group(1)


def value_ref(prop):
    """'@{modeselector1.value}' -> 'modeselector1'"""
    m = re.fullmatch(r'@\{([^@{}.]+)\.value\}', str(prop))
    if not m:
        raise CompileError(f'expected @{{id.value}}, found {prop!r}')
    return m.group(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    for n in ('session_in', 'state_in', 'session_out', 'state_out', 'map_out'):
        ap.add_argument(n)
    ap.add_argument('--lib', required=True)
    a = ap.parse_args()
    try:
        run(a)
    except CompileError as e:
        sys.exit(f'compile-compounds: {e}')


def run(a):
    session = json.loads(pathlib.Path(a.session_in).read_text())
    state = json.loads(pathlib.Path(a.state_in).read_text())
    lib = pathlib.Path(a.lib).read_text()
    root = session['content']

    compounds = []                                   # (parent, index, panel)
    def find(w, parent):
        for i, c in enumerate(w.get('widgets') or []):
            if c.get('type') == 'panel' and c.get('id') == 'slider':
                compounds.append((w, i, c))
    walk(root, find)
    if not compounds:
        raise CompileError('no slider compounds found')

    used_ids = set()
    walk(root, lambda w, p: used_ids.add(str(w.get('id'))))
    sub_to_slot = {}                                 # sub-widget id -> (canvas id, slot)
    canvases_by_mode_input = {}                      # input id -> [canvas ids]
    mapping = {'canvases': {}, 'inbound': {}}
    new_state_entries = {}                           # first sub key -> (canvas id, value)
    drop_keys = set()

    skipped = []
    def static_wiring(ch):
        # a compound whose mode gates or label still hold @{} (the keyboard velocity
        # slider: its n is an object) is left exactly as authored -- today's behaviour
        props = [ch[ci].get('interaction') for ci in MODE_OF_CHILD] + [ch[14].get('interaction'), ch[14].get('label', '')]
        return not any(isinstance(x, str) and re.search(r'@\{[^}]*@\{|OSC\{[^}]*@\{', x) for x in props) and \
               all('@{' not in str(c.get('id', '')) for c in ch)
    k = -1
    for (parent, idx, panel) in compounds:
        ch = panel.get('widgets') or []
        if [c.get('type') for c in ch] != SLIDER_SHAPE:
            raise CompileError(f'slider compound: unexpected shape {[c.get("type") for c in ch]}')
        if not static_wiring(ch):
            skipped.append(str(ch[7].get('id'))); continue
        k += 1
        if panel.get('tabs'):
            raise CompileError(f'compound #{k}: has tabs')
        main_id = str(ch[7]['id'])
        cid = main_id + '_c'
        n = 2
        while cid in used_ids:
            cid = f'{main_id}_c{n}'; n += 1
        used_ids.add(cid)
        caddr = f'/fc/{k}'

        addrs = [None] * NSLOTS
        sub_ids = [None] * NSLOTS
        for ci, slot in SLOT_OF_CHILD.items():
            addrs[slot] = address_of(ch[ci]); sub_ids[slot] = str(ch[ci]['id'])
        matrix = ch[MATRIX_CHILD]
        if matrix.get('quantity', 0) != 6 or matrix.get('widgetType', 'button') != 'button':
            raise CompileError(f'{cid}: mod matrix is not 6 buttons')
        maddr = address_of(matrix)
        for i in range(6):
            addrs[MOD_SLOT + i] = f'{maddr}/{i}'; sub_ids[MOD_SLOT + i] = f'{matrix["id"]}/{i}'

        modes = {name: osc_address(ch[ci].get('interaction'))[1:] for ci, name in MODE_OF_CHILD.items()}
        modes['sel'] = value_ref(ch[14].get('interaction'))
        for mid in modes.values():
            canvases_by_mode_input.setdefault(mid, []).append(cid)
        label = ch[14].get('label', '')
        if not isinstance(label, str) or '@{' in label:
            raise CompileError(f'{cid}: dynamic label is not supported')

        # values: state if present, else the sub-widget's own value/default, else 0
        values, recall = [], []
        for slot in range(NSLOTS):
            sid = sub_ids[slot]
            if sid in state:
                values.append(state[sid]); recall.append(slot); drop_keys.add(sid)
            else:
                mat = state.get(str(matrix['id']))
                if MOD_SLOT <= slot < MOD_SLOT + 6 and isinstance(mat, list) and len(mat) == 6:
                    values.append(mat[slot - MOD_SLOT])
                else:
                    values.append(0)
        if str(matrix['id']) in state:
            drop_keys.add(str(matrix['id']))
        values = [v if isinstance(v, (int, float)) and not isinstance(v, bool) else (1 if v is True else 0) for v in values]
        first = next((sub_ids[s] for s in range(NSLOTS) if sub_ids[s] in state), None)
        if first:                                # compounds sharing ids share state keys: all get it
            new_state_entries.setdefault(first, []).append((cid, values))

        M = {'n': None, 'label': label, 'a': addrs, 'modes': modes, 'labelFont': FONT, 'digitFont': FONT}
        mjs = json.dumps(M, ensure_ascii=False)
        canvas = {
            'type': 'canvas', 'id': cid, 'address': caddr,
            'left': panel.get('left', 'auto'), 'top': panel.get('top', 'auto'),
            'width': panel.get('width', 'auto'), 'height': panel.get('height', 'auto'),
            'valueLength': NSLOTS, 'default': values, 'autoClear': False, 'padding': 0,
            'onDraw': f'var M = {mjs};\nglobals.FC && globals.FC.draw(ctx, width, height, value, cssVars, M, {{get: get}})',
            'onTouch': f'var M = {mjs};\nglobals.FC && globals.FC.touch(event, value, width, height, M, locals, {{get: get, set: set, send: send}})',
        }
        for p in ('expand', 'visible', 'css', 'interaction'):
            if p in panel:
                canvas[p] = panel[p]
        parent['widgets'][idx] = canvas

        # recall in the state file's key order -- the order today's widgets send in
        key_pos = {k2: i for i, k2 in enumerate(state)}
        recall.sort(key=lambda s: key_pos[sub_ids[s]])
        mapping['canvases'][caddr] = {'id': cid, 'recall': [[s, addrs[s]] for s in recall]}
        for slot in range(NSLOTS):
            sub_to_slot[sub_ids[slot]] = (cid, slot)
            mapping['inbound'].setdefault(addrs[slot], []).append([cid, slot, 1])
        # no aggregate: o-s-c ignores /attackmod1 carrying 6 values (verified), so must we

    # references from outside the compounds into their sub-widgets
    refs = {'rewritten': 0}
    def rewrite(v):
        if isinstance(v, str):
            def sub(m):
                x = m.group(1)
                if x in sub_to_slot:
                    refs['rewritten'] += 1
                    c, s = sub_to_slot[x]
                    return '@{%s.value.%d}' % (c, s)
                return m.group(0)
            v2 = re.sub(r'@\{([^@{}.]+)(?:\.value)?\}', sub, v)
            for x in re.findall(r'@\{([^@{}.]+)\.', v2):
                if x in sub_to_slot:
                    raise CompileError(f'reference to a compound sub-widget prop other than value: {x}')
            for x in re.findall(r'''\b(?:get|set|getProp)\(\s*['"]([^'"]+)['"]''', v2):
                if x in sub_to_slot:
                    raise CompileError(f'script reference to a compound sub-widget: {x}')
            return v2
        if isinstance(v, dict):
            return {k: rewrite(x) for k, x in v.items()}
        if isinstance(v, list):
            return [rewrite(x) for x in v]
        return v
    def rewrite_widget(w, p):
        for k in list(w):
            if k not in ('widgets', 'tabs'):
                w[k] = rewrite(w[k])
    walk(root, rewrite_widget)

    # inbound addresses that something else in the session still listens to pass through
    remaining = set()
    def addr_of(w, p):
        if w.get('type') in ('panel', 'tab', 'root', 'modal'):
            return
        try:
            remaining.add(address_of(w))
        except CompileError:
            pass
    walk(root, addr_of)
    mapping['passthrough'] = sorted(a for a in mapping['inbound'] if a in remaining)

    # mode inputs redraw the canvases they gate
    hooked = set()
    def hook(w, p):
        if w.get('type') == 'input' and str(w.get('id')) in canvases_by_mode_input:
            calls = ''.join(f"updateCanvas({json.dumps(c)});" for c in canvases_by_mode_input[str(w['id'])])
            prev = w.get('onValue', '')
            w['onValue'] = (prev + '\n' if prev else '') + '// compile-compounds: redraw the canvases this mode gates\n' + calls
            hooked.add(str(w['id']))
    walk(root, hook)

    # the mailbox, and the shared library
    tabs = root.get('tabs') or []
    if not tabs:
        raise CompileError('session has no tabs to host the mailbox')
    tabs[0].setdefault('widgets', []).append({
        'type': 'variable', 'id': PATCH_ID, 'address': PATCH_ADDRESS,
        'onValue': 'globals.FC && globals.FC.patch({get: get, set: set}, value)'})
    prev = root.get('onCreate', '')
    root['onCreate'] = '// compile-compounds: src/gui/compounds/slider-lib.js\n' + lib + ('\n' + prev if prev else '')

    # state: packed values, in the position of each compound's first sub key. A sub key
    # whose id another widget still carries (reverb1 is also the effect's own toggle)
    # is kept, so that widget keeps getting its state.
    still_there = set()
    walk(root, lambda w, p: still_there.add(str(w.get('id'))))
    drop_keys -= still_there
    out_state = {}
    for key, v in state.items():
        for cid, vals in new_state_entries.get(key, []):
            out_state[cid] = vals
        if key not in drop_keys:
            out_state[key] = v

    pathlib.Path(a.session_out).write_text(json.dumps(session, ensure_ascii=False, separators=(',', ':')))
    pathlib.Path(a.state_out).write_text(json.dumps(out_state, ensure_ascii=False, indent=1))
    pathlib.Path(a.map_out).write_text(json.dumps(mapping, ensure_ascii=False))
    nwid = [0]; walk(root, lambda w, p: nwid.__setitem__(0, nwid[0] + 1))
    if skipped:
        print(f'compile-compounds: note: {len(skipped)} compounds with non-static wiring left as authored: {skipped[:6]}')
    print(f'compile-compounds: {k + 1} of {len(compounds)} slider compounds -> canvases, {refs["rewritten"]} outside references '
          f'rewritten, {len(hooked)} mode inputs hooked, {len(mapping["passthrough"])} shared addresses passed through, '
          f'{nwid[0]} authored widgets, state {len(state)} -> {len(out_state)} keys')


if __name__ == '__main__':
    main()
