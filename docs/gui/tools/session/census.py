"""Census of _main.json: authored widgets per tab, clone templates, and expanded
(built) widget counts once clones are resolved to their template subtrees."""
import json, sys, re, collections

d = json.load(open(sys.argv[1]))
root = d['content']

byid = {}
def walk(w, path, fn):
    fn(w, path)
    for c in w.get('widgets', []) or []:
        walk(c, path + [w.get('id')], fn)
    for c in w.get('tabs', []) or []:
        walk(c, path + [w.get('id')], fn)

def reg(w, p):
    byid.setdefault(w.get('id'), []).append((w, p))
walk(root, [], reg)

def subtree_count(w, types=None, depth=0):
    """Expanded count: clone widgets count as themselves + their template's subtree."""
    if depth > 20:
        return 1
    n = 1
    if types is not None:
        types[w['type']] += 1
    if w['type'] == 'clone':
        tgt = w.get('widgetId', '')
        tgt = re.sub(r'^#\{|\}$', '', str(tgt))
        cands = byid.get(tgt) or []
        if cands:
            t = cands[0][0]
            n += subtree_count(t, types, depth + 1)
        else:
            n += 0
    for c in (w.get('widgets') or []) + (w.get('tabs') or []):
        n += subtree_count(c, types, depth + 1)
    return n

authored = collections.Counter()
def cnt(w, p): authored[w['type']] += 1
walk(root, [], cnt)
print('authored total', sum(authored.values()), dict(authored.most_common()))

# clone targets
clones = collections.Counter()
def cl(w, p):
    if w['type'] == 'clone':
        clones[str(w.get('widgetId'))] += 1
walk(root, [], cl)
print('\nclone targets (instances):')
for k, v in clones.most_common():
    tgt = re.sub(r'^#\{|\}$', '', k)
    t = byid.get(tgt)
    if t:
        tc = collections.Counter()
        n = subtree_count(t[0][0], tc)
        print(f'  {k:40s} x{v:3d}  template subtree={n:3d} {dict(tc)}  at {"/".join(map(str,t[0][1][-3:]))}')
    else:
        print(f'  {k:40s} x{v:3d}  (target not found)')

print('\nper tab: authored / expanded')
for t in root['tabs']:
    a = collections.Counter(); walk(t, [], lambda w, p: a.update([w['type']]))
    e = collections.Counter(); n = subtree_count(t, e)
    print(f'  {t["id"]:10s} authored={sum(a.values()):5d} expanded={n:5d} {dict(e.most_common(8))}')
