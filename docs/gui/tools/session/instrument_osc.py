"""Instrument a COPY of the app's Open Stage Control client for benchmarking:
performance.mark() around each phase of SessionManager.load, and window.__jm /
window.__wm handles so sessions can be rebuilt in-page without reloading.
Exact-string anchors; aborts if any is missing."""
import sys, pathlib
p = pathlib.Path(sys.argv[1]) / 'client' / 'index.js'
s = p.read_text()
edits = [
    ('constructor(){super(),this.session=null,this.saveMode="session"',
     'constructor(){super(),window.__jm=this,this.session=null,this.saveMode="session"'),
    ('setTimeout(()=>{try{oc.clear(),this.session=new Session(a,"session")',
     'setTimeout(()=>{performance.mark("b0");try{oc.clear(),this.session=new Session(a,"session")'),
    ('this.setSaveMode(this.session.isFragment?"fragment":"session")}catch(a){',
     'this.setSaveMode(this.session.isFragment?"fragment":"session"),performance.mark("b1"),window.__wm=Fl}catch(a){'),
    ('options:{}});fm.enabled&&(fm.disable(),fm.enable()),ji.dispatchEvent(window,"resize"),setTimeout(()=>{uiLoading(!1)',
     'options:{}});performance.mark("b2");fm.enabled&&(fm.disable(),fm.enable()),ji.dispatchEvent(window,"resize"),performance.mark("b3"),setTimeout(()=>{uiLoading(!1)'),
    ('for(var p in a)h[a[p][0]]=a[p][1];a=h}this.set(a,l),',
     'for(var p in a)h[a[p][0]]=a[p][1];a=h}performance.mark("s0"),this.set(a,l),performance.mark("s1"),'),
    ('u.innerHTML="",pc.reset(),pc.parse(', 'u.innerHTML="",pc.reset(),performance.mark("b0a"),pc.parse('),
]
for a, b in edits:
    if s.count(a) != 1:
        sys.exit(f'anchor count {s.count(a)}: {a[:60]}')
    s = s.replace(a, b)
p.write_text(s)
print('instrumented', p)
