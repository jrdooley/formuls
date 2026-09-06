#!/usr/bin/env python3
"""Minimal Pure Data patch parser + static analyser."""
import re, sys, os, json
from collections import defaultdict

# object-creating record types (these consume an index in `connect` numbering)
OBJ_TYPES = {"obj","msg","text","floatatom","symbolatom","listbox","restore","scalar"}

def records(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as fh:
        data = fh.read()
    # split on unescaped ';'
    parts = re.split(r'(?<!\\);', data)
    for p in parts:
        p = p.strip()
        if p:
            yield p.replace('\n', ' ').replace('\r', ' ')

def tokenize(rec):
    return rec.split()

class Obj:
    __slots__ = ('idx','kind','x','y','args','canvas','line')
    def __init__(self, idx, kind, x, y, args, canvas):
        self.idx=idx; self.kind=kind; self.x=x; self.y=y; self.args=args; self.canvas=canvas
    @property
    def cls(self):
        return self.args[0] if self.args else ''
    def __repr__(self):
        return f"[{self.idx}] {self.kind} {' '.join(self.args)[:60]}"

class Canvas:
    def __init__(self, name, parent, path):
        self.name=name; self.parent=parent; self.path=path
        self.objs=[]; self.conns=[]   # conns: (src,outlet,dst,inlet)
    def label(self):
        return self.name

def parse(path):
    stack=[]; canvases=[]; root=None
    for rec in records(path):
        t = tokenize(rec)
        if len(t)<2: continue
        if t[0]=='#N' and t[1]=='canvas':
            name = t[7] if len(t)>7 else 'ROOT'
            c = Canvas(name, stack[-1] if stack else None, path)
            canvases.append(c)
            if stack is not None and len(stack)==0: root=c
            stack.append(c)
        elif t[0]=='#X':
            k=t[1]
            if not stack: continue
            cur=stack[-1]
            if k=='restore':
                sub=stack.pop()
                if stack:
                    par=stack[-1]
                    name=' '.join(t[4:]) if len(t)>4 else 'pd'
                    o=Obj(len(par.objs),'restore',t[2] if len(t)>2 else 0,t[3] if len(t)>3 else 0,t[4:],par)
                    o.args = t[4:] if len(t)>4 else ['pd']
                    par.objs.append(o)
                    sub.name = name
            elif k=='connect':
                try:
                    cur.conns.append((int(t[2]),int(t[3]),int(t[4]),int(t[5])))
                except Exception: pass
            elif k in ('coords','declare','array','f'):
                if k=='array':
                    cur.objs.append(Obj(len(cur.objs),'array',0,0,t[1:],cur))
            elif k in OBJ_TYPES:
                cur.objs.append(Obj(len(cur.objs),k,t[2] if len(t)>2 else 0,t[3] if len(t)>3 else 0,t[4:],cur))
        elif t[0]=='#A':
            pass
    return root, canvases

# ---- classification -------------------------------------------------------
SEND_CLS   = {'s','send','throw~','s~','send~','value','v'}
RECV_CLS   = {'r','receive','catch~','r~','receive~','value','v'}
# classes that legitimately have no outgoing connection
SINK_CLS   = {'s','send','throw~','s~','send~','print','outlet','outlet~','dac~','writesf~',
              'soundfiler','tabwrite~','tabwrite','declare','namecanvas','pdcontrol','bng','tgl',
              'nbx','hsl','vsl','hradio','vradio','vu','cnv','table','array','text','key','keyup',
              'keyname','savestate','block~','switch~','del','delay','set','clear','textfile','qlist'}
# classes that legitimately have no incoming connection
SRC_CLS    = {'r','receive','catch~','r~','receive~','loadbang','inlet','inlet~','adc~','key','keyup',
              'keyname','notein','ctlin','bendin','pgmin','touchin','polytouchin','midiin','sysexin',
              'netreceive','oscparse','declare','namecanvas','table','array','bng','tgl','nbx','hsl',
              'vsl','hradio','vradio','vu','cnv','text','block~','switch~','savestate','openpanel',
              'realtime','cputime','random','msg'}

def analyse(path):
    root, canvases = parse(path)
    issues=[]
    for c in canvases:
        indeg=defaultdict(int); outdeg=defaultdict(int)
        for (s,so,d,di) in c.conns:
            outdeg[s]+=1; indeg[d]+=1
        for o in c.objs:
            if o.kind=='text': continue
            cls=o.cls
            if cls.startswith('#'): continue
            has_in = indeg[o.idx]>0; has_out=outdeg[o.idx]>0
            if not has_in and not has_out:
                if cls in SINK_CLS or cls in SRC_CLS or o.kind=='restore': continue
                issues.append((path,c.label(),o.idx,'ISOLATED',str(o)))
            elif not has_out and cls not in SINK_CLS and o.kind in ('obj','msg') and cls not in ('bng','tgl'):
                issues.append((path,c.label(),o.idx,'NO_OUTPUT_USE',str(o)))
            elif not has_in and cls not in SRC_CLS and o.kind in ('obj','msg'):
                issues.append((path,c.label(),o.idx,'NO_INPUT',str(o)))
    return root, canvases, issues

def collect_sr(canvases, path):
    sends=[]; recvs=[]
    for c in canvases:
        for o in c.objs:
            if o.kind!='obj' or not o.args: continue
            cls=o.args[0]
            if len(o.args)<2: continue
            sym=o.args[1]
            if cls in ('s','send','throw~','s~','send~'):
                sends.append((sym,path,c.label(),o.idx))
            elif cls in ('r','receive','catch~','r~','receive~'):
                recvs.append((sym,path,c.label(),o.idx))
    return sends,recvs

if __name__=='__main__':
    mode=sys.argv[1]; files=sys.argv[2:]
    if mode=='dead':
        for f in files:
            _,_,iss=analyse(f)
            for i in iss: print(f"{os.path.basename(i[0])}\t{i[1]}\t#{i[2]}\t{i[3]}\t{i[4]}")
    elif mode=='sr':
        allS=defaultdict(list); allR=defaultdict(list)
        for f in files:
            _,cv=parse(f)[0],parse(f)[1]
            s,r=collect_sr(cv,f)
            for sym,p,c,i in s: allS[sym].append((os.path.basename(p),c,i))
            for sym,p,c,i in r: allR[sym].append((os.path.basename(p),c,i))
        print("=== SENT but NEVER RECEIVED ===")
        for sym in sorted(allS):
            if sym not in allR: print(f"  {sym}\t<- {allS[sym][:3]}")
        print("=== RECEIVED but NEVER SENT ===")
        for sym in sorted(allR):
            if sym not in allS: print(f"  {sym}\t-> {allR[sym][:3]}")
    elif mode=='count':
        tot=defaultdict(int)
        for f in files:
            _,cv=parse(f)[0],parse(f)[1]
            n=sum(len(c.objs) for c in cv)
            nt=sum(1 for c in cv for o in c.objs if o.kind=='text')
            print(f"{os.path.basename(f)}\tcanvases={len(cv)}\tobjects={n}\tcomments={nt}\treal={n-nt}")
            for c in cv:
                for o in c.objs:
                    if o.kind=='obj' and o.args: tot[o.args[0]]+=1
        print("--- class histogram (top 40) ---")
        for k,v in sorted(tot.items(), key=lambda x:-x[1])[:40]: print(f"  {v:5d}  {k}")
