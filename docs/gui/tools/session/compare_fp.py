"""Compare two fingerprint dumps (fingerprint.js): every widget's resolved props,
value, on-screen geometry and computed style. Prints a summary of differences,
grouped by prop and by id pattern so systematic causes stand out."""
import json, sys, re, collections
a = json.load(open(sys.argv[1])); b = json.load(open(sys.argv[2]))
ka, kb = set(a), set(b)
print('widgets', len(a), len(b), 'only-in-a', len(ka - kb), 'only-in-b', len(kb - ka))
for k in sorted(ka - kb)[:10]: print('  only a:', k)
for k in sorted(kb - ka)[:10]: print('  only b:', k)
diffs = collections.Counter(); examples = {}
for k in sorted(ka & kb):
    x, y = a[k], b[k]
    if x['type'] != y['type']:
        diffs['TYPE'] += 1; examples.setdefault('TYPE', (k, x['type'], y['type'])); continue
    for p in set(x['props']) | set(y['props']):
        vx, vy = x['props'].get(p, '<missing>'), y['props'].get(p, '<missing>')
        if vx != vy:
            tag = 'prop:' + p + ' @ ' + re.sub(r'\d+', '#', k.split('#')[0])
            diffs[tag] += 1; examples.setdefault(tag, (k, str(vx)[:120], str(vy)[:120]))
    for f in ('value', 'geom', 'style'):
        if x.get(f) != y.get(f):
            tag = f + ' @ ' + re.sub(r'\d+', '#', k.split('#')[0])
            diffs[tag] += 1; examples.setdefault(tag, (k, str(x.get(f))[:120], str(y.get(f))[:120]))
print('distinct diff kinds', len(diffs), 'total', sum(diffs.values()))
for tag, n in diffs.most_common(int(sys.argv[3]) if len(sys.argv) > 3 else 40):
    print(f'{n:6d}  {tag}\n        {examples[tag]}')
