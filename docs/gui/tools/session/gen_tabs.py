"""Variants of _main.json that keep only the first K synth tabs (formuls1..K).
formuls0 (the hidden template every synth tab clones) is always kept."""
import json, sys, os
S = os.path.dirname(os.path.abspath(__file__))
base = json.load(open(os.path.join(S, 'base.json')))
for k in (0, 1, 3):
    d = json.loads(json.dumps(base))
    keep = {f'formuls{i}' for i in range(k + 1, 7)}
    d['content']['tabs'] = [t for t in d['content']['tabs'] if t['id'] not in keep]
    json.dump(d, open(os.path.join(S, f'v_tabs{k}.json'), 'w'))
    print(k, [t['id'] for t in d['content']['tabs']])
