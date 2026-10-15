#!/usr/bin/env python3
"""Flatten an Open Stage Control session at build time.

src/gui/_main.json is authored with clones whose contents get their ids and
values from @{parent.variables...}. Open Stage Control resolves those at load,
and every widget that carries one registers a global listener that every other
widget's creation calls, so load time grows with the square of the widget
count. Measured in docs/gui/session-size.md: 10.9 s -> 3.6 s on an M5.

This writes an equivalent session in which:

  - every clone is replaced by the widget it renders (the clone's geometry, and
    its own css / interaction:false / visible:false, carried onto it);
  - every reference that cannot change after load is resolved:
    @{parent.variables...}, @{this.variables...}, @{parent.id}, @{this.id};
  - anything dynamic is left exactly as written: OSC{}, JS{}, #{}, VAR{},
    @{someWidget.value}, @{this.value}, and scripts;
  - template-only tabs become empty hidden tabs. Their *position* is kept on
    purpose: Pd reads the selected synth from the root tab index;
  - props equal to the widget type's default are dropped (o-s-c refills them).

Resolution is a line-by-line port of o-s-c 1.31's Widget.resolveProp,
balancedReplace and balanced-match, including its quirks: nested @{} values are
spliced in with String() (objects become "[object Object]"), top-level ones as
raw strings or JSON, a result that looks like JSON is parsed except inside
object props where numbers and true/false stay strings, and `parent` seen from a
clone's content is the clone's parent.

Contract, as for patch-osc-perf.py: anything it does not understand is a hard
failure, never a silent pass-through.

usage: flatten-session.py SRC OUT --defaults osc-defaults-X.json
                          [--osc-package DIR] --template-tab ID [--template-tab ID ...]
"""
import argparse, copy, json, pathlib, re, sys

UNDEF = object()                          # JavaScript `undefined`
MARKERS = re.compile(r'(@|OSC|VAR|IMPORT|JS|#)\{')
STATIC_PROPS = {'variables', 'id'}        # the only props treated as fixed after load
SAFE_TWICE = {'border-radius'}         # css that may sit on both clone and content
CLONE_CARRIED = {'type', 'id', 'widgetId', 'props', 'variables', 'left', 'top', 'width', 'height',
                 'expand', 'visible', 'interaction', 'css', 'comments', 'lock', 'scoped'}


class FlattenError(Exception):
    pass


# ---------------------------------------------------------------------------
# JavaScript value semantics

def js_num(v):
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, int):
        return str(v)
    if v != v:
        return 'NaN'
    if v in (float('inf'), float('-inf')):
        return 'Infinity' if v > 0 else '-Infinity'
    if v.is_integer() and abs(v) < 1e21:
        return str(int(v))
    r = repr(v)
    if 'e' in r:
        mant, exp = r.split('e')
        sign = exp[0] if exp[0] in '+-' else '+'
        r = mant + 'e' + sign + exp.lstrip('+-').lstrip('0')
    return r


def js_json(v):
    """JSON.stringify"""
    if v is UNDEF:
        return UNDEF
    if v is None:
        return 'null'
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, (int, float)):
        return 'null' if (isinstance(v, float) and (v != v or v in (float('inf'), float('-inf')))) else js_num(v)
    if isinstance(v, str):
        return json.dumps(v, ensure_ascii=False)
    if isinstance(v, list):
        return '[' + ','.join('null' if (x is UNDEF) else js_json(x) for x in v) + ']'
    if isinstance(v, dict):
        return '{' + ','.join(json.dumps(k, ensure_ascii=False) + ':' + js_json(x)
                              for k, x in v.items() if x is not UNDEF) + '}'
    raise FlattenError(f'cannot stringify {type(v)}')


def js_string(v):
    """String(v)"""
    if v is UNDEF:
        return 'undefined'
    if v is None:
        return 'null'
    if isinstance(v, (bool, int, float)):
        return js_num(v)
    if isinstance(v, str):
        return v
    if isinstance(v, list):
        return ','.join('' if (x is None or x is UNDEF) else js_string(x) for x in v)
    return '[object Object]'


def str_or_json(v):
    """o-s-c's placeholder substitution: strings raw, everything else JSON.stringify."""
    if isinstance(v, str):
        return v
    j = js_json(v)
    return 'undefined' if j is UNDEF else j


def js_index(v, key):
    """v[key] for the subkey form @{id.prop.key}"""
    if isinstance(v, dict):
        return v.get(key, UNDEF)
    if isinstance(v, (list, str)) and re.fullmatch(r'0|[1-9]\d*', key):
        i = int(key)
        return v[i] if i < len(v) else UNDEF
    if isinstance(v, (list, str)) and key == 'length':
        return len(v)
    return UNDEF


_JS_NUM = re.compile(r'[+-]?(\d+\.?\d*([eE][+-]?\d+)?|\.\d+([eE][+-]?\d+)?|Infinity)|0[xX][0-9a-fA-F]+|0[oO][0-7]+|0[bB][01]+')


def js_isnan_str(s):
    """isNaN(s) for a string"""
    t = s.strip(' \t\n\r\v\f ﻿  ')
    return not (t == '' or _JS_NUM.fullmatch(t))


def is_json(s):
    return s != '' and s[0] in ' \t\n+-eE{([0123456789tfn"'


def _reject_constant(c):
    raise ValueError(c)


def json_parse(s):
    """JSON.parse, or raise ValueError"""
    return json.loads(s, parse_constant=_reject_constant)


def js_equal(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return isinstance(a, bool) and isinstance(b, bool) and a == b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return a == b
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(js_equal(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(js_equal(x, y) for x, y in zip(a, b))
    return type(a) == type(b) and a == b


# ---------------------------------------------------------------------------
# balanced-match and balancedReplace, as bundled in o-s-c 1.31

def _range(a, b, s):
    ai = s.find(a)
    bi = s.find(b, ai + 1)
    i = ai
    result = None
    if ai >= 0 and bi > 0:
        begs, left, right = [], len(s), None
        while i >= 0 and result is None:
            if i == ai:
                begs.append(i)
                ai = s.find(a, i + 1)
            elif len(begs) == 1:
                result = [begs.pop(), bi]
            else:
                beg = begs.pop()
                if beg < left:
                    left, right = beg, bi
                bi = s.find(b, i + 1)
            i = ai if (ai < bi and ai >= 0) else bi
        if begs:
            result = [left, right]
    return result


def balanced(a, b, s):
    r = _range(a, b, s)
    if not r:
        return None
    if r[1] is None:
        # o-s-c would go on with a half-built match; not modelled, so stop.
        raise FlattenError(f'unbalanced braces in: {s[:80]}')
    return {'pre': s[:r[0]], 'body': s[r[0] + len(a):r[1]], 'post': s[r[1] + len(b):], 'start': r[0], 'end': r[1]}


def balanced_replace(prefix, s, fn):
    m = -1
    while True:
        g = s.find(prefix + '{')
        if g < 0 or g == m:
            break
        v = s[g + len(prefix):]
        A = balanced('{', '}', v)
        if A:
            v = A['pre'] + fn(A['body']) + A['post']
            s = s[:g] + v
        m = g
    return s


# ---------------------------------------------------------------------------
# Session tree

class Node:
    __slots__ = ('props', 'parent', 'type', 'widgets', 'tabs', 'inner', 'cache', 'template')

    def __init__(self, props, parent, template=False):
        self.props = props
        self.parent = parent
        self.type = props.get('type')
        self.widgets, self.tabs, self.inner = [], [], None
        self.cache = {}
        self.template = template


class Flattener:
    def __init__(self, session, defaults, template_tabs):
        self.session = session
        self.defaults = defaults
        self.template_tabs = set(template_tabs)
        self.stats = {'clones': 0, 'static_props': 0, 'partial_props': 0, 'stripped': 0, 'undefined_dropped': 0, 'broken_overrides': []}
        self.depth = 0
        self.clone_ids = set()
        # authored tree (clones not expanded): used to find clone targets by resolved id
        self.authored_root = self._build_authored(session['content'], None, False)
        self.targets = {}
        self._index_targets(self.authored_root)

    # --- trees -------------------------------------------------------------
    def _build_authored(self, raw, parent, template):
        props = {k: v for k, v in raw.items() if k not in ('widgets', 'tabs')}
        n = Node(props, parent, template)
        self._check_widget(n)
        for c in raw.get('widgets') or []:
            n.widgets.append(self._build_authored(c, n, template))
        for c in raw.get('tabs') or []:
            n.tabs.append(self._build_authored(c, n, template or (parent is None and c.get('id') in self.template_tabs)))
        return n

    def _index_targets(self, n):
        if n.type != 'clone':
            rid = self.resolve_prop(n, 'id')[0]
            self.targets.setdefault(js_string(rid), []).append(n)
        for c in n.widgets + n.tabs:
            self._index_targets(c)

    def _check_widget(self, n):
        for k in ('script', 'draw', 'touch'):
            if k in n.props:
                raise FlattenError(f'legacy prop "{k}" on {n.props.get("id")}: open and re-save the session in o-s-c first')
        if n.type == 'fragment':
            raise FlattenError('fragment widgets are not supported')
        if n.type == 'clone' and n.props.get('scoped', False) is not False:
            raise FlattenError(f'scoped clone {n.props.get("id")} is not supported')

    def _authored_raw(self, n):
        """A widget's authored data, children included, as o-s-c's clone copies it."""
        d = copy.deepcopy(n.props)
        if n.widgets:
            d['widgets'] = [self._authored_raw(c) for c in n.widgets]
        if n.tabs:
            d['tabs'] = [self._authored_raw(c) for c in n.tabs]
        return d

    def expand(self, raw, parent, force=False):
        """Build the rendered tree: clones get an `inner` node, as o-s-c creates it.
        Template tabs are not expanded (nothing inside them is written out) unless
        `force`, which the reference check uses to learn every id they create."""
        props = {k: v for k, v in raw.items() if k not in ('widgets', 'tabs')}
        n = Node(props, parent)
        self._check_widget(n)
        if not force and parent is not None and parent.parent is None and props.get('id') in self.template_tabs:
            return n
        if n.type == 'clone':
            self.stats['clones'] += 1
            wid, st = self.resolve_prop(n, 'widgetId')
            if not st or not isinstance(wid, str):
                raise FlattenError(f'clone {props.get("id")}: widgetId is not static')
            cands = self.targets.get(wid, [])
            if len(cands) != 1:
                raise FlattenError(f'clone target "{wid}": {len(cands)} candidates, expected 1')
            over, st = self.resolve_prop(n, 'props')
            if not st:
                raise FlattenError(f'clone {props.get("id")}: props override is not static')
            if isinstance(over, (str, list)):
                # o-s-c spreads it: {...target.props, ...override}. A string (an override
                # whose JSON did not parse) or an array becomes index keys, and the intended
                # override silently never applies. Reproduced, and counted.
                if over != '' and over != []:
                    self.stats['broken_overrides'].append(js_string(self.resolve_prop(n, 'id')[0]))
                over = {str(i): ch for i, ch in enumerate(over)}
            elif not isinstance(over, dict):
                over = {}
            data = self._authored_raw(cands[0])
            data.update(copy.deepcopy(over))
            for k in ('script', 'draw', 'touch'):
                if k in data:
                    raise FlattenError(f'clone {props.get("id")}: legacy prop "{k}" after override')
            self.depth += 1
            if self.depth > 20:
                raise FlattenError('clone nesting deeper than 20: circular?')
            n.inner = self.expand(data, n, force)
            self.depth -= 1
            return n
        for c in raw.get('widgets') or []:
            n.widgets.append(self.expand(c, n, force))
        for c in raw.get('tabs') or []:
            n.tabs.append(self.expand(c, n, force))
        return n

    # --- resolution (port of Widget.resolveProp) ---------------------------
    def raw_prop(self, n, key):
        """A prop as the widget holds it after o-s-c's parser: defaults filled in, and
        anything the type does not define (or starting with "_") deleted."""
        d = self.defaults.get(n.type)
        if d is None:
            raise FlattenError(f'no defaults for widget type "{n.type}" (regenerate the defaults table)')
        if key not in d or key.startswith('_'):
            return False, None
        if key in n.props:
            return True, n.props[key]
        return True, copy.deepcopy(d[key])

    def resolve_prop(self, n, key, orig=None):
        """Fully resolved value of a prop -> (value, static)"""
        if key in n.cache:
            return n.cache[key]
        if (n, key) in getattr(self, '_stack', set()):
            raise FlattenError(f'circular resolution of {key} on {n.props.get("id")}')
        self._stack = getattr(self, '_stack', set()) | {(n, key)}
        try:
            ok, raw = self.raw_prop(n, key)
            if not ok:
                res = (UNDEF, True)
            elif key == 'props' and n.type == 'matrix':
                res = (None, False)           # per-child expansion: never needed as a value
            else:
                res = self.resolve_value(n, key, raw, False, orig or (n, key))
        finally:
            self._stack = self._stack - {(n, key)}
        n.cache[key] = res
        return res

    def resolve_value(self, n, key, raw, nested, orig):
        if isinstance(raw, str):
            return self.resolve_string(n, key, raw, nested, orig)
        if isinstance(raw, dict):
            out, st = {}, True
            for k, v in raw.items():
                out[k], s = self.resolve_value(n, key, v, True, orig)
                st = st and s
            return out, st
        if isinstance(raw, list):
            out, st = [], True
            for v in raw:
                r, s = self.resolve_value(n, key, v, True, orig)
                out.append(r)
                st = st and s
            return out, st
        return raw, True

    def target_of(self, n, A):
        if A == 'parent':
            t = n.parent
            if t is None:
                return 'FL'               # the root's parent is the widget manager
            if t.type == 'clone':
                t = t.parent if t.parent is not None else 'FL'
            return t
        if A == 'this':
            return n
        return None                       # another widget: dynamic

    def lookup(self, n, content, orig):
        """One @{content} block -> ('value', v, static) | ('text', s, static)"""
        parts = content.split('.')
        if len(parts) > 1:
            g, m = parts.pop(), None
            if len(parts) > 1:
                m, g = g, parts.pop()
            A = '.'.join(parts)
        else:
            A, g, m = parts[0], 'value', None
        t = self.target_of(n, A)
        if t is None or g not in STATIC_PROPS:
            return ('text', None, False)
        if t == 'FL':
            return ('text', 'undefined', True)
        ok, _ = self.raw_prop(t, g)
        if not ok:
            return ('text', 'undefined', True)
        if g == orig[1] and t is orig[0]:
            return ('text', 'ERR_CIRCULAR_REF', True)
        v, st = self.resolve_prop(t, g, orig)
        if m is not None and v is not UNDEF:
            v = js_index(v, m)
        return ('value', v, st)

    def resolve_string(self, n, key, s, nested, orig):
        placeholders = {}
        state = {'static': True, 'w': 999}

        def at_cb(content):
            if '@{' in content:
                def nested_cb(c):
                    v, st = self.resolve_string(n, key + '-nested', '@{' + c + '}', False, orig)
                    if not st:
                        state['static'] = False
                    return js_string(v)
                content = balanced_replace('@', content, nested_cb)
            kind, v, st = self.lookup(n, content, orig)
            if not st:
                state['static'] = False
                return 'undefined'
            if kind == 'text':
                return v
            name = 'VAR_%d' % state['w']
            state['w'] -= 1
            placeholders[name] = v
            return name

        s = balanced_replace('@', s, at_cb)
        if not state['static'] or re.search(r'(VAR|IMPORT|OSC|JS|#)\{', s):
            return None, False
        for name, v in placeholders.items():
            s = s.replace(name, str_or_json(v))
        if is_json(s) and (key != 'label' or s == 'false') and (not nested or (js_isnan_str(s) and s not in ('true', 'false'))):
            try:
                return json_parse(s), True
            except ValueError:
                pass
        return s, True

    # --- partial substitution for props that stay dynamic -------------------
    def _code_spans(self, s):
        """Spans of top-level blocks whose contents o-s-c treats specially:
        JS{} / #{} -- a ref there is a JS variable, so it is embedded as a JSON literal;
        OSC{} / VAR{} / IMPORT{} -- the contents are split on "," and trimmed *before*
        placeholders are substituted, so only text that cannot change that split may
        be embedded."""
        spans = []
        for m in re.finditer(r'(JS|#|OSC|VAR|IMPORT)\{', s):
            A = balanced('{', '}', s[m.end() - 1:])
            if A:
                spans.append((m.start(), m.end() - 1 + A['end'], 'code' if m.group(1) in ('JS', '#') else 'args'))
        return spans

    def partial(self, n, key, s, orig, mode='top', spans=None):
        if spans is None and mode == 'top':
            spans = self._code_spans(s)
        out, i = [], 0
        while True:
            p = s.find('@{', i)
            if p < 0:
                break
            A = balanced('{', '}', s[p + 1:])
            if not A:
                break
            close = p + 1 + A['end']
            content = self.partial(n, key, A['body'], orig, 'nested')
            emb = None
            if '@{' not in content:
                if mode == 'nested':
                    kind, _, st = self.lookup(n, content, orig)
                    if st:
                        v, st2 = self.resolve_string(n, key + '-nested', '@{' + content + '}', False, orig)
                        if st2:
                            emb = js_string(v)
                else:
                    kind, v, st = self.lookup(n, content, orig)
                    if st:
                        ctx = next((k for a, b, k in (spans or []) if a <= p < b), 'top')
                        if kind == 'text':
                            emb = v
                        elif ctx == 'code':
                            j = js_json(v)
                            emb = 'undefined' if j is UNDEF else j
                        else:
                            emb = str_or_json(v)
                        if ctx == 'args' and (re.search(r'[,{}]', emb) or emb != emb.strip()):
                            emb = None                 # would change the argument split: keep the ref
            if emb is not None and (MARKERS.search(emb) or 'VAR_' in emb):
                raise FlattenError(f'{n.props.get("id")}.{key}: a resolved value contains o-s-c syntax: {emb[:60]}')
            out.append(s[i:p])
            out.append(emb if emb is not None else '@{' + content + '}')
            i = close + 1
        out.append(s[i:])
        return ''.join(out)

    # --- output --------------------------------------------------------------
    def flat_value(self, n, key, raw, nested=False):
        if isinstance(raw, str):
            if '@{' not in raw or re.match(r'on[A-Z]', key):
                return raw                     # scripts and plain strings: verbatim
            orig = (n, key)
            if not (key == 'props' and n.type == 'matrix'):
                v, st = self.resolve_string(n, key, raw, nested, orig)
                if st:
                    if not nested and isinstance(v, (dict, list)):
                        v = js_json(v)         # o-s-c re-parses a JSON string without coercing
                    txt = v if isinstance(v, str) else js_json(v)
                    if isinstance(txt, str) and MARKERS.search(txt):
                        raise FlattenError(f'{n.props.get("id")}.{key}: resolved value contains o-s-c syntax')
                    self.stats['static_props'] += not nested
                    return v
            self.stats['partial_props'] += not nested
            return self.partial(n, key, raw, orig)
        if isinstance(raw, dict):
            return {k: self.flat_value(n, key, v, True) for k, v in raw.items()}
        if isinstance(raw, list):
            return [self.flat_value(n, key, v, True) for v in raw]
        return raw

    def out_props(self, n):
        defaults = self.defaults.get(n.type)
        if defaults is None:
            raise FlattenError(f'no defaults for widget type "{n.type}" (regenerate the defaults table)')
        o = {}
        for k, raw in n.props.items():
            if k not in defaults or k.startswith('_'):
                self.stats['undefined_dropped'] += 1      # the parser deletes these anyway
                continue
            v = self.flat_value(n, k, raw)
            if k not in ('type', 'id') and k in defaults and js_equal(defaults[k], v):
                self.stats['stripped'] += 1
                continue
            o[k] = v
        o['type'] = n.type
        return o

    def out_widget(self, n, root_tab=False):
        if n.type == 'clone':
            return self.out_clone(n)
        if root_tab and n.props.get('id') in self.template_tabs:
            vis, _ = self.resolve_prop(n, 'visible')
            if vis is not False:
                raise FlattenError(f'template tab {n.props.get("id")} is visible: it would disappear')
            label = self.flat_value(n, 'label', self.raw_prop(n, 'label')[1])
            return {'type': 'tab', 'id': self.resolve_prop(n, 'id')[0], 'visible': False, 'label': label}
        o = self.out_props(n)
        if n.type != 'matrix':
            if n.widgets:
                o['widgets'] = [self.out_widget(c) for c in n.widgets]
            if n.tabs:
                o['tabs'] = [self.out_widget(c, root_tab=(n.parent is None)) for c in n.tabs]
        return o

    def out_clone(self, c):
        o = self.out_widget(c.inner)
        defaults = self.defaults['clone']
        for k, raw in c.props.items():
            # Props a clone does not define (every clone here carries a stray `address`
            # and `variables`) are inert in o-s-c. Defined ones must be carried or default.
            if k in defaults and k not in CLONE_CARRIED and not js_equal(defaults[k], raw):
                raise FlattenError(f'clone {c.props.get("id")}: prop "{k}" would be lost')
        self.clone_ids.add(js_string(self.resolve_prop(c, 'id')[0]))
        idefs = self.defaults[c.inner.type]
        for k in ('left', 'top', 'width', 'height', 'expand'):
            ok, raw = self.raw_prop(c, k)
            v = self.flat_value(c, k, raw)
            if js_equal(idefs.get(k, UNDEF), v):
                o.pop(k, None)
            else:
                o[k] = v
        for k in ('visible', 'interaction'):
            v, st = self.resolve_prop(c, k)
            if not st:
                raise FlattenError(f'clone {c.props.get("id")}: dynamic {k} is not supported')
            if v is True:
                continue
            if v is not False:
                raise FlattenError(f'clone {c.props.get("id")}: dynamic {k} is not supported')
            if o.get(k, True) is not True:
                raise FlattenError(f'clone {c.props.get("id")}: {k} set on both clone and content')
            o[k] = False
        css, st = self.resolve_prop(c, 'css')
        if not st:
            raise FlattenError(f'clone {c.props.get("id")}: dynamic css is not supported')
        if css:
            if not isinstance(css, str) or MARKERS.search(css):
                raise FlattenError(f'clone {c.props.get("id")}: dynamic css is not supported')
            inner = o.get('css', '')
            if inner and not isinstance(inner, str):
                raise FlattenError(f'clone {c.props.get("id")}: cannot merge css')
            # Declarations on the clone styled an outer box; merged, they style the content's
            # box, which takes the clone's geometry. Safe unless both set the same property:
            # identical values are fine only where nesting does not compound (opacity
            # multiplies, a percentage font-size scales again).
            decls = lambda t: {x.split(':', 1)[0].strip(): x.split(':', 1)[1].strip()
                               for x in re.split(r'[;\n]', t) if ':' in x}
            dc, di = decls(css), decls(inner) if inner else {}
            for prop in dc.keys() & di.keys():
                if not (prop in SAFE_TWICE and dc[prop] == di[prop]):
                    raise FlattenError(f'clone {c.props.get("id")}: css "{prop}" set on both clone and content')
            o['css'] = css + ';\n' + inner if inner else css
        return o

    def run(self):
        root = self.expand(self.session['content'], None)
        out = self.out_widget(root)
        self.check_references(out, root)
        return {k: v for k, v in self.session.items() if k != 'content'} | {'content': out}

    # --- safety net ------------------------------------------------------------
    def check_references(self, out, root):
        """A dynamic @{id...} or a script's get('id') left in the output must not point
        at a widget that existed at runtime before flattening but no longer does: one
        created inside a template tab (directly or by a clone there), or a clone."""
        out_ids, tpl_ids = set(), set()

        def collect(w):
            out_ids.add(js_string(w.get('id')))
            for c in (w.get('widgets') or []) + (w.get('tabs') or []):
                collect(c)
        collect(out)

        def collect_tpl(n):
            if n.type != 'clone':
                tpl_ids.add(js_string(self.resolve_prop(n, 'id')[0]))
            for c in n.widgets + n.tabs + ([n.inner] if n.inner else []):
                collect_tpl(c)
        clones = self.stats['clones']
        for raw in self.session['content'].get('tabs') or []:
            if raw.get('id') in self.template_tabs:
                collect_tpl(self.expand(raw, root, force=True))
        self.stats['clones'] = clones                 # those expansions are not shipped
        tpl_ids |= self.clone_ids - out_ids          # a clone's own id disappears when it is inlined

        bad = set()

        def scan(v):
            if isinstance(v, str):
                for m in re.finditer(r'@\{([^@{}.]+)[.}]', v):
                    x = m.group(1)
                    if x not in ('parent', 'this') and x not in out_ids and x in tpl_ids:
                        bad.add(x)
                for m in re.finditer(r'''\b(?:get|set|getProp|getVar)\(\s*['"]([^'"]+)['"]''', v):
                    if m.group(1) not in out_ids and m.group(1) in tpl_ids and m.group(1) != 'this':
                        bad.add(m.group(1))
            elif isinstance(v, dict):
                for x in v.values():
                    scan(x)
            elif isinstance(v, list):
                for x in v:
                    scan(x)
        scan(out)
        if bad:
            raise FlattenError(f'references to widgets that only exist in template tabs: {sorted(bad)[:10]}')


def count(w):
    return 1 + sum(count(c) for c in (w.get('widgets') or []) + (w.get('tabs') or []))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('src')
    ap.add_argument('out')
    ap.add_argument('--defaults', required=True)
    ap.add_argument('--osc-package', help='refuse to run if this o-s-c package is not the defaults table\'s version')
    ap.add_argument('--template-tab', action='append', default=[], required=True)
    a = ap.parse_args()

    defaults = json.loads(pathlib.Path(a.defaults).read_text())
    if a.osc_package:
        ver = json.loads((pathlib.Path(a.osc_package) / 'package.json').read_text())['version']
        if ver != defaults['openStageControl']:
            sys.exit(f'flatten-session: o-s-c package is {ver}, defaults table is for {defaults["openStageControl"]}')
    session = json.loads(pathlib.Path(a.src).read_text())
    tabs = [t.get('id') for t in session['content'].get('tabs') or []]
    missing = [t for t in a.template_tab if t not in tabs]
    if missing:
        sys.exit(f'flatten-session: template tab(s) not found at root: {missing}')
    try:
        f = Flattener(session, defaults['types'], a.template_tab)
        out = f.run()
    except FlattenError as e:
        sys.exit(f'flatten-session: {e}')
    text = json.dumps(out, ensure_ascii=False, separators=(',', ':'))
    pathlib.Path(a.out).write_text(text)
    s = f.stats
    print(f'flatten-session: {count(session["content"])} authored -> {count(out["content"])} widgets, '
          f'{s["clones"]} clones inlined, {s["static_props"]} props resolved, {s["partial_props"]} kept dynamic, '
          f'{s["stripped"]} defaults dropped, {len(text) / 1e6:.2f} MB')
    if s['broken_overrides']:
        print(f'flatten-session: note: {len(s["broken_overrides"])} clone props overrides do not parse and never '
              f'applied in o-s-c either (kept as o-s-c behaves): {sorted(set(s["broken_overrides"]))[:8]}')


if __name__ == '__main__':
    main()
