# GUI load rig

Helpers used to produce the numbers in `../README.md`. They assume Pd 0.56 at
`/Applications/Pd-0.56-2.app` and a built app bundle, whose `gui/node` binary
and `gui/open-stage-control` package are reused rather than downloaded again.

| file | what it does |
|---|---|
| `osc-sink.py` | Binds a UDP port in place of the o-s-c server and counts what Pd sends: datagrams/s, messages/s, bytes/s, and a per-address histogram. Optionally writes the address list the other tools drive. |
| `pick-addresses.py` | Splits an `osc-sink.py` capture into per-widget-type lists (fader, xy, both, and a control list of addresses no widget answers to). |
| `osc-load.py` | Drives those addresses into an o-s-c server: `throttled` (N addresses at 25 Hz, the realistic instrument load), `steady` (one message every GAP seconds, for pricing a single widget update), or `packed` (one message of N floats to a canvas, for pricing the blob idea). |
| `ws-probe.js` | Pasted into the client's console: wraps `WebSocket` to count frames and time the app's own message handler. |
| `ws-probe-read.js` | Defines `__reset()` and `__read(sent)` to read those counters back. |
| `ws-client.js` | A headless extra "tablet" -- connects, counts frames, answers pings, renders nothing. For measuring how server cost scales with client count. |

## Running the whole thing

Never point any of this at the ports the app uses (9000/9001) while the app is
running — see the caveat about that in `../README.md`. Everything below uses
19000/19001 instead.

**1. Capture what the patch sends.** Copy the patch tree somewhere scratch,
add the externals from a built bundle, and repoint its two hardcoded ports:

    APP=formuls-0.3.0-beta.app/Contents/Resources
    mkdir -p /tmp/pdtest && cp -R src/pd/* /tmp/pdtest/
    cp -R "$APP/pd/externals" /tmp/pdtest/externals
    sed -i '' -e 's/listen 9000/listen 19000/' \
              -e 's/connect 127\.0\.0\.1 9001/connect 127.0.0.1 19001/' \
              -e 's/127\.0\.0\.1:9000 root/127.0.0.1:19000 root/' /tmp/pdtest/_main.pd

    python3 docs/gui/tools/osc-sink.py 19001 /tmp/addrs.txt &
    /Applications/Pd-0.56-2.app/Contents/Resources/bin/pd -nogui -noaudio \
        -path /tmp/pdtest/controlabstractions -path /tmp/pdtest/externals \
        -path /tmp/pdtest/audioabstractions -open /tmp/pdtest/_main.pd &
    sleep 25 && kill %2 && kill -INT %1

    python3 docs/gui/tools/pick-addresses.py /tmp/addrs.txt /tmp

**2. Start a server to measure, and instrument a client.**

    "$APP/gui/node" "$APP/gui/open-stage-control" --send 127.0.0.1:19000 \
        --port 19001 --load "$APP/gui/_main.json" \
        --client-options framerate=25 hdpi=0 --no-qrcode &

Open `http://127.0.0.1:19001`, paste `ws-probe.js` then `ws-probe-read.js`
into the console, and **restart the server** so the client reconnects through
the wrapper. Confirm with
`window.__ST.sockets.at(-1).onmessage.__wrapped === true`.

**3. Measure.** `__reset()` before each run, `__read(sent)` after.

    # realistic instrument load
    python3 docs/gui/tools/osc-load.py throttled 19001 /tmp/addrs-params.txt \
        --n 84 --secs 20 --hz 25

    # transport-only control: same rate, nothing to update
    python3 docs/gui/tools/osc-load.py throttled 19001 /tmp/addrs-unmatched.txt \
        --n 84 --secs 20 --hz 25

    # price one widget type (note --arity, and --no-filter for /seqpos)
    python3 docs/gui/tools/osc-load.py steady 19001 /tmp/addrs-fader.txt \
        --count 6000 --gap 0.001 --arity 1
    python3 docs/gui/tools/osc-load.py steady 19001 /tmp/addrs-xy.txt \
        --count 6000 --gap 0.001 --arity 2

    python3 docs/gui/tools/osc-load.py steady 19001 /tmp/addrs-seqpos.txt \
        --count 2000 --gap 0.003 --arity 1 --no-filter

Server CPU comes from `ps -o time= -p <node pid>` either side of a run.

Stop Pd before pricing a single widget type. Its ~70 msg/s of background
traffic is dominated by `/seqpos`, which costs ten times a slider, and it will
swamp the average you are trying to read. And open the tab the widget lives on
first: o-s-c builds only the visible tab, so an address whose panel has never
been shown matches nothing and prices at 2 µs.

Read `__hist()` as well as `__read()`. The max alone is misleading: a
reconnect burst produces one outlier several times the steady-state worst.

**4. The server A/B.** The patch that ships is `src/tools/patch-osc-perf.py`.
Apply each variant to its own copy and run each in place of the stock server:

    cp -R "$APP/gui/open-stage-control" /tmp/osc-hoist
    cp -R "$APP/gui/open-stage-control" /tmp/osc-batch
    python3 src/tools/patch-osc-perf.py /tmp/osc-hoist
    python3 src/tools/patch-osc-perf.py /tmp/osc-batch --batch-ms 20

Serialising once only pays with more than one client, so add headless ones
before the run and stop them with SIGINT after:

    "$APP/gui/node" docs/gui/tools/ws-client.js probe1 &
    "$APP/gui/node" docs/gui/tools/ws-client.js probe2 &

Alternate the variants across at least three rounds and take medians -- a
single pair sits well inside the run-to-run spread.

**5. Pricing the packed-canvas idea.** Add a canvas to a *copy* of the layout
and drive it, against the same number of values sent individually:

    python3 - <<'EOF'
    import json
    d = json.load(open('src/gui/_main.json'))
    f1 = [t for t in d['content']['tabs'] if t['id'] == 'formuls1'][0]
    f1.setdefault('widgets', []).append({
        "type": "canvas", "id": "modblob", "address": "auto",
        "left": "0%", "top": "0%", "width": "40%", "height": "20%",
        "valueLength": 72, "autoClear": True, "continuous": False,
        "onDraw": ("var w = width / value.length;\n"
                   "ctx.fillStyle = cssVars.colorFill;\n"
                   "for (var i = 0; i < value.length; i++) {\n"
                   "  var h = value[i] * height;\n"
                   "  ctx.fillRect(i * w, height - h, w - 1, h);\n}\n")})
    json.dump(d, open('/tmp/_main-blob.json', 'w'))
    EOF

Serve `/tmp/_main-blob.json`, open the synth-1 tab, then compare 3,600 values
sent each way:

    python3 docs/gui/tools/osc-load.py steady 19001 /tmp/addrs-panel.txt \
        --count 3600 --gap 0.002 --arity 1
    python3 docs/gui/tools/osc-load.py packed 19001 /modblob \
        --count 50 --gap 0.02 --values 72

Read `__stats(3600)` after each, so both are normalised per *value* rather
than per message.

A canvas in a hidden browser pane never actually paints -- `requestAnimationFrame`
is suspended, and `getImageData` on it comes back all zeroes. Time `widget.draw()`
directly to price drawing; it is synchronous, unlike `batchDraw()`.
