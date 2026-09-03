# GUI load rig

Helpers used to produce the numbers in `../README.md`. They assume Pd 0.56 at
`/Applications/Pd-0.56-2.app` and a built app bundle, whose `gui/node` binary
and `gui/open-stage-control` package are reused rather than downloaded again.

| file | what it does |
|---|---|
| `osc-sink.py` | Binds a UDP port in place of the o-s-c server and counts what Pd sends: datagrams/s, messages/s, bytes/s, and a per-address histogram. Optionally writes the address list the other tools drive. |
| `pick-addresses.py` | Splits an `osc-sink.py` capture into per-widget-type lists (fader, xy, both, and a control list of addresses no widget answers to). |
| `osc-load.py` | Drives those addresses into an o-s-c server, either `throttled` (N addresses at 25 Hz, the realistic instrument load) or `steady` (one message every GAP seconds, for pricing a single widget update). |
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

    # price one widget type (note --arity)
    python3 docs/gui/tools/osc-load.py steady 19001 /tmp/addrs-fader.txt \
        --count 6000 --gap 0.001 --arity 1
    python3 docs/gui/tools/osc-load.py steady 19001 /tmp/addrs-xy.txt \
        --count 6000 --gap 0.001 --arity 2

Server CPU comes from `ps -o time= -p <node pid>` either side of a run.

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
