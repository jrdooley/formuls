import json, sys, collections, re
a = json.loads(json.load(open(sys.argv[1]))) if False else json.load(open(sys.argv[1]))
b = json.load(open(sys.argv[2]))
a, b = a['out'], b['out']
print('entries', len(a), len(b), 'only-a', len(set(a) - set(b)), 'only-b', len(set(b) - set(a)))
d = collections.Counter(); ex = {}
for k in set(a) & set(b):
    for f in ('geom', 'style'):
        if a[k][f] != b[k][f]:
            t = f + ' ' + re.sub(r'\d+', '#', k.split('#')[0]); d[t] += 1; ex.setdefault(t, (k, a[k][f], b[k][f]))
print('diff kinds', len(d), 'total', sum(d.values()))
for t, n in d.most_common(15): print(n, t, ex[t])
