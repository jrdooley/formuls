#!/usr/bin/env python3
"""Make an Open Stage Control server coalesce receiveOsc into one WS frame.

Open Stage Control ships as a webpack bundle, so this is a string edit on the
minified source rather than a patch file - and it is applied to a *copy*,
never to the tree the app runs from:

    cp -R "<app>/Contents/Resources/gui/open-stage-control" /tmp/osc-batched
    python3 batch-frames.py /tmp/osc-batched
    "<app>/Contents/Resources/gui/node" /tmp/osc-batched --send 127.0.0.1:19000 \
        --port 19001 --load "<app>/Contents/Resources/gui/_main.json" \
        --client-options framerate=25 hdpi=0 --no-qrcode

The client has handled a "bundle" event since forever (client/callbacks.js:
`bundle: n => { for (let o in n) e.receive(n[o]) }`) but no server release
ever sends one, so the receiving half needs no change.

Measured effect is in ../README.md: 81x fewer frames and -23% server CPU,
but the client gets *slower* per message. This exists to reproduce that
result, not as a recommended patch - see "Two changes worth shipping".

Targets o-s-c 1.31.0. If the anchor string is gone, the bundle changed;
re-derive it from Client.send in server/node/index.js.
"""
import os, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "."
PATH = os.path.join(ROOT, "server", "node", "index.js")

OLD = ('send(e,t){var r=JSON.stringify([e,t]);'
       'this.connected()?this.socket.send(r):this.queue.push(r)}')

NEW = ('send(e,t){'
       'if(e==="receiveOsc"){'
         'if(!this._b)this._b=[];'
         'this._b.push(t);'
         'if(!this._bt)this._bt=setTimeout(()=>{'
           'this._bt=null;var b=this._b;this._b=[];'
           'var r=JSON.stringify(["bundle",b]);'
           'this.connected()?this.socket.send(r):this.queue.push(r)'
         '},20);'
         'return}'
       # flush pending values before any other event, so ordering is preserved
       'if(this._b&&this._b.length){'
         'clearTimeout(this._bt);this._bt=null;var q=this._b;this._b=[];'
         'var rb=JSON.stringify(["bundle",q]);'
         'this.connected()?this.socket.send(rb):this.queue.push(rb)}'
       'var r=JSON.stringify([e,t]);'
       'this.connected()?this.socket.send(r):this.queue.push(r)}')

src = open(PATH, encoding="utf8", errors="replace").read()
n = src.count(OLD)
if n != 1:
    sys.exit(f"expected exactly one Client.send in {PATH}, found {n} "
             "- the bundle has changed, re-derive the anchor")
open(PATH, "w").write(src.replace(OLD, NEW))
print(f"patched {PATH}: receiveOsc coalesced into one 'bundle' frame per 20 ms")
