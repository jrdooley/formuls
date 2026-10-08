import sys, collections
def load(p): return [l.split(' ', 1)[1].strip() for l in open(p) if l.strip()]
a, b = load(sys.argv[1]), load(sys.argv[2])
print('messages', len(a), len(b))
ca, cb = collections.Counter(a), collections.Counter(b)
print('multiset identical:', ca == cb)
for k in list((ca - cb).items())[:8]: print('  only/more in flat:', k)
for k in list((cb - ca).items())[:8]: print('  only/more in compiled:', k)
print('sequence identical:', a == b)
# per-address order preserved?  compare the relative order of each compound's messages
if a != b:
    first = next(i for i, (x, y) in enumerate(zip(a, b)) if x != y)
    print('first divergence at', first, '\n  flat    :', a[first:first+4], '\n  compiled:', b[first:first+4])
