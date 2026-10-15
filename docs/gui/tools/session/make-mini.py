"""make-mini.py FLAT.json OUT.json [MAIN_ID] -- a one-compound test session: the slider
compound whose main fader is MAIN_ID (default attack1), filling the window width, plus
the hidden mode inputs it reads. Gesture coordinates in it are exact, which is what
the equivalence harness needs."""
import json, sys, copy, re
d = json.load(open(sys.argv[1])); main = sys.argv[3] if len(sys.argv) > 3 else 'attack1'
n = re.search(r'(\d+)$', main).group(1)
def find(w, pred):
    if pred(w): return w
    for c in (w.get('widgets') or []) + (w.get('tabs') or []):
        r = find(c, pred)
        if r: return r
comp = copy.deepcopy(find(d['content'], lambda w: w.get('id') == 'slider' and any(c.get('id') == main for c in w.get('widgets') or [])))
comp.update({'left': 0, 'top': 0, 'width': '100%', 'height': '40%', 'expand': False})
modes = [copy.deepcopy(find(d['content'], lambda w, i=i: w.get('id') == i + n and w.get('type') == 'input'))
         for i in ('lfofreq', 'lfodepth', 'chaos', 'mod', 'moddepth', 'modeselector')]
tab = {'type': 'tab', 'id': 'formuls' + n, 'label': 'one', 'widgets': [comp] + modes, 'tabs': []}
json.dump({'version': d['version'], 'type': 'session', 'content': {'type': 'root', 'id': 'root', 'tabs': [tab]}}, open(sys.argv[2], 'w'))
