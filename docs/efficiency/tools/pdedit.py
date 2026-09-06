#!/usr/bin/env python3
"""Structural edits to Pd patches: delete objects and renumber connections,
preserving the raw text (and line wrapping) of every untouched record."""
import re, sys, os

OBJ_TYPES = {"obj","msg","text","floatatom","symbolatom","listbox","restore","scalar"}

def split_records(data):
    """Yield (raw_text_including_trailing_semicolon, body) preserving original bytes."""
    out=[]; i=0; start=0
    while i < len(data):
        if data[i]==';' and (i==0 or data[i-1]!='\\'):
            out.append(data[start:i+1]); start=i+1
        i+=1
    if start < len(data): out.append(data[start:])
    return out

def toks(raw):
    return raw.rstrip(';').strip().split()

class Doc:
    def __init__(self, path):
        self.path=path
        self.recs = split_records(open(path, encoding='utf-8', errors='replace').read())
        # annotate: for each record index -> (canvas_id, kind, obj_index_or_None)
        self.ann=[]; self.canvases=[]      # canvases[i] = dict(objs=[rec_idx], conns=[rec_idx], name=)
        stack=[]
        for ri,raw in enumerate(self.recs):
            t=toks(raw)
            if len(t)<2: self.ann.append((None,'other',None)); continue
            if t[0]=='#N' and t[1]=='canvas':
                cid=len(self.canvases)
                self.canvases.append(dict(objs=[],conns=[],name=(t[7] if len(t)>7 else 'ROOT'),parent=(stack[-1] if stack else None)))
                stack.append(cid); self.ann.append((cid,'canvas',None)); continue
            if t[0]=='#X' and stack:
                cur=stack[-1]; k=t[1]
                if k=='restore':
                    sub=stack.pop()
                    self.canvases[sub]['name']=' '.join(t[4:]) if len(t)>4 else 'pd'
                    if stack:
                        par=stack[-1]; idx=len(self.canvases[par]['objs'])
                        self.canvases[par]['objs'].append(ri); self.ann.append((par,'restore',idx))
                    else: self.ann.append((sub,'restore',None))
                    continue
                if k=='connect':
                    self.canvases[cur]['conns'].append(ri); self.ann.append((cur,'connect',None)); continue
                if k in OBJ_TYPES or k=='array':
                    idx=len(self.canvases[cur]['objs'])
                    self.canvases[cur]['objs'].append(ri); self.ann.append((cur,'obj',idx)); continue
            self.ann.append((stack[-1] if stack else None,'other',None))

    def canvas_id(self, name):
        hits=[i for i,c in enumerate(self.canvases) if c['name']==name]
        if len(hits)!=1: raise SystemExit(f"canvas {name!r}: {len(hits)} matches in {self.path}")
        return hits[0]

    def obj_text(self, cid, idx):
        return self.recs[self.canvases[cid]['objs'][idx]].strip()

    def set_obj(self, cid, idx, new_body):
        ri=self.canvases[cid]['objs'][idx]; raw=self.recs[ri]
        lead=raw[:len(raw)-len(raw.lstrip())]
        self.recs[ri]=lead+new_body+';'

    def delete(self, cid, indices):
        c=self.canvases[cid]; dead=set(indices)
        for i in dead:
            if self.ann[c['objs'][i]][1]=='restore':
                raise SystemExit(f"refusing to delete subcanvas object #{i} in {c['name']}")
        # renumber map
        keep=[i for i in range(len(c['objs'])) if i not in dead]
        newidx={old:n for n,old in enumerate(keep)}
        # rewrite connections
        newconns=[]
        for ri in c['conns']:
            t=toks(self.recs[ri]); s,so,d,di=int(t[2]),int(t[3]),int(t[4]),int(t[5])
            if s in dead or d in dead: self.recs[ri]=None; continue
            raw=self.recs[ri]; lead=raw[:len(raw)-len(raw.lstrip())]
            self.recs[ri]=lead+f"#X connect {newidx[s]} {so} {newidx[d]} {di};"
        for i in dead: self.recs[c['objs'][i]]=None
        # refresh structure
        self.recs=[r for r in self.recs if r is not None]
        return len(dead)

    def save(self, path=None):
        open(path or self.path,'w',encoding='utf-8').write(''.join(self.recs))

if __name__=='__main__':
    cmd=sys.argv[1]
    if cmd=='show':
        d=Doc(sys.argv[2]); 
        for cid,c in enumerate(d.canvases):
            print(f"--- canvas[{cid}] {c['name']}")
            for i,ri in enumerate(c['objs']):
                print(f"   [{i}] {d.recs[ri].strip()[:110]}")
    elif cmd=='conns':
        d=Doc(sys.argv[2]); cid=int(sys.argv[3])
        for ri in d.canvases[cid]['conns']: print("  "+d.recs[ri].strip())
