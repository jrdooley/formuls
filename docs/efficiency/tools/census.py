#!/usr/bin/env python3
"""Expand the Pd abstraction tree from _main.pd and count runtime objects."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pdparse import parse
from collections import defaultdict

ROOT = sys.argv[1] if len(sys.argv)>1 else '.'
SEARCH = [ROOT, os.path.join(ROOT,'controlabstractions'), os.path.join(ROOT,'audioabstractions')]

def find_abs(name):
    for d in SEARCH:
        p = os.path.join(d, name + '.pd')
        if os.path.exists(p): return p
    return None

cache = {}
def objs_of(path):
    if path not in cache:
        _, cv = parse(path)
        cache[path] = [o for c in cv for o in c.objs]
    return cache[path]

counts = defaultdict(int)
absinst = defaultdict(int)
visiting = set()

def walk(path, mult, depth=0):
    if depth > 12: return
    for o in objs_of(path):
        if o.kind != 'obj' or not o.args: continue
        cls = o.args[0]
        counts[cls] += mult
        if cls == 'clone':
            # clone <abstraction> <n> ...   or  clone -x ...
            a = [x for x in o.args[1:] if not x.startswith('-')]
            if len(a) >= 2:
                nm = a[0]; 
                try: n = int(float(a[1]))
                except: n = 1
                p2 = find_abs(nm)
                if p2:
                    absinst[nm] += mult*n
                    walk(p2, mult*n, depth+1)
            continue
        p2 = find_abs(cls)
        if p2 and p2 != path:
            absinst[cls] += mult
            walk(p2, mult, depth+1)

walk(os.path.join(ROOT,'_main.pd'), 1)

TILDE = {k:v for k,v in counts.items() if k.endswith('~')}
print("=== RUNTIME ABSTRACTION INSTANCES ===")
for k,v in sorted(absinst.items(), key=lambda x:-x[1]):
    if v>=2: print(f"  {v:7d}  {k}")
print("\n=== RUNTIME SIGNAL (~) OBJECTS ===")
tot=0
for k,v in sorted(TILDE.items(), key=lambda x:-x[1]):
    print(f"  {v:7d}  {k}"); tot+=v
print(f"  {tot:7d}  TOTAL signal objects")
print("\n=== RUNTIME CONTROL OBJECTS (top 30) ===")
ctot=0
for k,v in sorted(counts.items(), key=lambda x:-x[1]):
    if k.endswith('~'): continue
    ctot+=v
for k,v in sorted(((k,v) for k,v in counts.items() if not k.endswith('~')), key=lambda x:-x[1])[:30]:
    print(f"  {v:7d}  {k}")
print(f"  {ctot:7d}  TOTAL control objects")
