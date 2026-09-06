#!/usr/bin/env python3
"""patch-osc-perf.py <open-stage-control-dir> [--batch-ms N]

Performance surgery on the Open Stage Control package the build scripts
download. Everything here is measured in docs/gui/README.md.

Applied always
--------------
Hoist the JSON serialisation out of IpcServer's per-client loop. Stock,
every connected tablet re-serialises the same payload:

    for (var s in i) ... i[s].send(e, t)      // JSON.stringify per client

With N tablets on stage that is N stringify calls for one parameter move.
Serialising once and handing every client the same string is free: no
behavioural difference, no change to what any client receives.

Opt-in, and off by default: --batch-ms N
----------------------------------------
Coalesce broadcast value updates into one WebSocket frame per N ms, using
the "bundle" event the client has always handled but that no server release
ever sends. Measured at 84 parameters x 25 Hz, one browser client:

                             stock      --batch-ms 20
    server CPU / 38k msgs    0.99 s     0.59 s   (-40%)
    WebSocket frames        38,310         464   (83x fewer)
    bytes                 124.5 KiB/s 107.4 KiB/s (-14%)
    client main-thread JS    4,164 ms   5,513 ms (+32%)
    handler p50 / p99      0.1/0.6 ms 12.2/15.9 ms

It is off by default because it makes the client do *more* total work, and
a saturated client main thread is precisely what drops the connection --
the client closes the socket if it cannot handle a pong within 3 seconds.
Batching buys server CPU and a much shorter link queue, and charges the one
budget that is already the binding constraint. Rewriting the client's
bundle loop from `for...in` to an indexed loop (which this script does
whenever batching is enabled) does not recover the difference.

Turn it on if the server or the Wi-Fi link is the bottleneck rather than
the tablet -- several tablets connected, or a marginal access point.

Opt-in, and off by default: --skip-prearg-search
------------------------------------------------
Every message carrying more than one value walks every possible split of
its arguments into (preArgs, value) -- N+1 address lookups for N values --
preferring the longest preArgs match. `_main.json` declares preArgs on no
widget at all, so every one of those lookups is a guaranteed miss and the
walk always falls through to the plain address. Trying the plain address
first is then exactly equivalent, which --session verifies before this is
applied (and refuses if the layout ever gains a preArgs widget).

It is worth having only alongside packed messages. Measured back to back
in the real layout, same session, same tab:

                                 stock    --skip-prearg-search
    an xy pad (2 values)         130 us   166 us  (WORSE, +35 us)
    a packed 72-value message    694 us   226 us  (-468 us)

Short-circuiting adds one address lookup that hits and removes N that
miss; a hit costs more than a miss, so at two values the trade is a loss
-- and xy pads are the only multi-value messages the instrument sends
today. This becomes worth applying if the packed-canvas work in
docs/gui/README.md is ever done, and not before.

Matched on the code rather than on line numbers or byte offsets, because
this is a webpack bundle and both move the moment Open Stage Control is
updated. A patch that silently does nothing is worse than one that stops
the build, so a missing anchor is a hard failure.

Idempotent: re-running over an already-patched tree is a no-op, so rebuilds
over an existing build/ directory are safe.

Targets Open Stage Control 1.31.0.
"""
import argparse
import os
import sys

SERVER = ("server", "node", "index.js")
CLIENT = ("client", "callbacks.js")

# Something this script introduces and nothing else contains. Do not use a
# bare "sendRaw": the bundled osc.js defines a sendRaw of its own on UDPPort,
# so that matches a pristine package and the patch silently skips.
MARKER = 'send(e,t){this.sendRaw(JSON.stringify([e,t]))}'

# --- Client gains sendRaw, so a caller can serialise once and send to many
CLIENT_SEND_OLD = (
    'send(e,t){var r=JSON.stringify([e,t]);'
    'this.connected()?this.socket.send(r):this.queue.push(r)}'
)
CLIENT_SEND_NEW = (
    'sendRaw(r){this.connected()?this.socket.send(r):this.queue.push(r)}'
    'send(e,t){this.sendRaw(JSON.stringify([e,t]))}'
)

IPC_SEND_OLD = (
    'send(e,t,r,n){var i=r?[this.clients[r]]:this.clients;'
    'for(var s in i)n&&this.clients[s]==this.clients[n]||i[s]&&i[s].send(e,t)}'
)

# --- serialise once, hand every client the same string
IPC_SEND_HOIST = (
    'send(e,t,r,n){'
    'var p=JSON.stringify([e,t]);'
    'var i=r?[this.clients[r]]:this.clients;'
    'for(var s in i)n&&this.clients[s]==this.clients[n]||i[s]&&i[s].sendRaw(p)}'
)

# --- as above, plus: plain broadcasts of values queue for up to N ms and go
#     out as one frame. Messages aimed at one client, or broadcast excluding
#     one client (the client-to-client sync path), still go immediately, and
#     flush anything pending first so ordering holds.
IPC_SEND_BATCH = (
    'send(e,t,r,n){'
    'if(e==="receiveOsc"&&!r&&!n){'
    '(this._oscQueue||(this._oscQueue=[])).push(t);'
    'if(!this._oscTimer)this._oscTimer=setTimeout(()=>{'
    'this._oscTimer=null;this._flushOsc()'
    '},{BATCH_MS});'
    'return}'
    'this._flushOsc();'
    'var p=JSON.stringify([e,t]);'
    'var i=r?[this.clients[r]]:this.clients;'
    'for(var s in i)n&&this.clients[s]==this.clients[n]||i[s]&&i[s].sendRaw(p)}'
    '_flushOsc(){'
    'if(this._oscTimer){clearTimeout(this._oscTimer);this._oscTimer=null}'
    'var b=this._oscQueue;'
    'if(!b||!b.length)return;'
    'this._oscQueue=[];'
    'var p=JSON.stringify(["bundle",b]);'
    'for(var s in this.clients){var c=this.clients[s];if(c)c.sendRaw(p)}}'
)

# --- the client's bundle loop ships as `for...in` over an array
CLIENT_BUNDLE_OLD = 'bundle:function(n){for(let o in n)e.receive(n[o])}'
CLIENT_BUNDLE_NEW = 'bundle:function(n){for(var i=0;i<n.length;i++)e.receive(n[i])}'

# --- the preArgs search, in client/index.js
#
# Every message carrying more than one value goes through this, and it walks
# every possible split of the arguments into (preArgs, value) -- N+1 address
# lookups for an N-value message -- preferring the longest preArgs match. When
# no widget in the session declares preArgs, every one of those lookups is a
# guaranteed miss, and the walk always falls through to the plain address.
#
# Trying the plain address first is then exactly equivalent, and skips the
# walk. It is NOT equivalent in a session that does use preArgs, because the
# order of preference reverses -- which is why applying this requires
# --session and a check that the layout declares none.
CLIENT_LOOKUP_OLD = (
    'getWidgetByAddressAndArgs(a,l){var u=a,h=l;'
    'if("object"==typeof l&&null!=l)'
    'for(var p=l.length;p>=0;p--){'
    'var g=this.createAddressRef(null,l.slice(0,p),a);'
    'if(this.getWidgetByAddress(g).length){u=g,h=l.slice(p,l.length);break}}'
    'else h=l;'
)
CLIENT_LOOKUP_NEW = (
    'getWidgetByAddressAndArgs(a,l){var u=a,h=l;'
    'if("object"==typeof l&&null!=l){'
    'if(!this.getWidgetByAddress(a).length)'
    'for(var p=l.length;p>=0;p--){'
    'var g=this.createAddressRef(null,l.slice(0,p),a);'
    'if(this.getWidgetByAddress(g).length){u=g,h=l.slice(p,l.length);break}}}'
    'else h=l;'
)


def fail(*lines):
    for line in lines:
        print(line, file=sys.stderr)
    sys.exit(1)


def count_prearg_widgets(session_path):
    """How many widgets in the layout declare a non-empty preArgs."""
    import json
    try:
        doc = json.load(open(session_path, encoding="utf8"))
    except Exception as e:
        fail(f"ERROR: could not read the session file {session_path}: {e}")
    n = 0
    stack = [doc]
    while stack:
        node = stack.pop()
        if isinstance(node, dict):
            pa = node.get("preArgs")
            if pa not in (None, "", []):
                n += 1
            stack.extend(node.values())
        elif isinstance(node, list):
            stack.extend(node)
    return n


def edit(path, pairs):
    """Apply (old, new) replacements to path; each old must appear exactly once."""
    src = open(path, encoding="utf8", errors="replace").read()
    for old, new in pairs:
        n = src.count(old)
        if n != 1:
            fail(f"ERROR: expected exactly one match in {path}, found {n} for:",
                 f"       {old[:70]}...",
                 "       The Open Stage Control bundle must have changed.",
                 "       Re-derive the anchors in src/tools/patch-osc-perf.py from",
                 "       Client.send / IpcServer.send / the bundle callback.")
        src = src.replace(old, new)
    open(path, "w").write(src)


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("osc_dir", help="unpacked Open Stage Control package")
    ap.add_argument("--batch-ms", type=int, default=0, metavar="N",
                    help="coalesce broadcast value updates into one frame per "
                         "N ms (default 0: off -- see the header of this file "
                         "for why)")
    ap.add_argument("--skip-prearg-search", action="store_true",
                    help="short-circuit the client's preArgs search. Only pays "
                         "for messages carrying many values, and COSTS ~35 us on "
                         "a two-value one -- see the header. Requires --session, "
                         "which is checked for preArgs before applying")
    ap.add_argument("--session", metavar="PATH",
                    help="the .json layout this build ships; required by "
                         "--skip-prearg-search so the precondition can be checked")
    a = ap.parse_args()

    if a.skip_prearg_search and not a.session:
        fail("ERROR: --skip-prearg-search requires --session <layout.json>",
             "       so the no-preArgs precondition can be verified.")

    server = os.path.join(a.osc_dir, *SERVER)
    client = os.path.join(a.osc_dir, *CLIENT)

    for p in (server, client):
        if not os.path.isfile(p):
            fail(f"ERROR: no such file: {p}",
                 "       (expected an unpacked Open Stage Control package)")

    if MARKER in open(server, encoding="utf8", errors="replace").read():
        print("Open Stage Control already patched; leaving it alone")
        return

    if a.batch_ms > 0:
        ipc_new = IPC_SEND_BATCH.replace("{BATCH_MS}", str(a.batch_ms))
    else:
        ipc_new = IPC_SEND_HOIST

    edit(server, [(CLIENT_SEND_OLD, CLIENT_SEND_NEW),
                  (IPC_SEND_OLD, ipc_new)])
    if a.batch_ms > 0:
        edit(client, [(CLIENT_BUNDLE_OLD, CLIENT_BUNDLE_NEW)])

    if a.skip_prearg_search:
        n = count_prearg_widgets(a.session)
        if n:
            fail(f"ERROR: {a.session} declares preArgs on {n} widget(s).",
                 "       Short-circuiting the search would reverse which match",
                 "       wins for those widgets. Not applying.")
        edit(os.path.join(a.osc_dir, "client", "index.js"),
             [(CLIENT_LOOKUP_OLD, CLIENT_LOOKUP_NEW)])
        print("patched Open Stage Control: preArgs search short-circuited "
              "(the layout declares none)")

    # Confirm it took, rather than trusting the replace calls.
    if MARKER not in open(server, encoding="utf8", errors="replace").read():
        fail(f"ERROR: patch did not take effect in {server}")

    if a.batch_ms > 0:
        print("patched Open Stage Control: serialise once per broadcast, "
              f"coalesce value updates into one frame per {a.batch_ms} ms")
    else:
        print("patched Open Stage Control: serialise once per broadcast "
              "(frame batching off; --batch-ms N to enable)")


if __name__ == "__main__":
    main()
