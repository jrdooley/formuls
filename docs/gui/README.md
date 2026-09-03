# The GUI link: why it drops, and what actually fixes it

The control interface becomes unreliable and disconnects when a lot of
parameters are being automated or modulated. This is what is happening,
measured rather than argued, and what each of the proposed fixes is worth.

Measured on an Apple M5, macOS 26.6.2, Open Stage Control 1.31.0, node
v22.17.0 — the versions `build-macOS.sh` downloads. Tooling to reproduce
everything is in `tools/`.

## The path a value takes

    automater --25 Hz--> f.util.oscformat --> to-o-s-c-interface
      --> [netsend -u -b] --UDP--> o-s-c server --WebSocket--> each browser

Two facts about the second half decide everything else.

**One OSC message becomes one WebSocket frame, per client.** `Client.send` in
the o-s-c server is, in full:

```js
send(e,t){var r=JSON.stringify([e,t]);this.connected()?this.socket.send(r):this.queue.push(r)}
```

No coalescing, no `bufferedAmount` check, and the `JSON.stringify` is repeated
per client rather than hoisted out of the loop in `IpcServer.send`.

**Sending OSC bundles does not change this.** The server parses with osc.js,
whose `fireBundleEvents` emits one `message` event per contained packet. A
bundle of 80 messages is unwrapped into 80 messages before anything else
happens, and still becomes 80 WebSocket frames.

## What breaks the connection

The client heartbeat, in `client/index.js`:

```js
this.hearbeat = setInterval(() => {
  this.connected() && (this.socket.send('["ping"]'),
    this.hearbeatTimeout = setTimeout(() => { this.connected() && this.close() }, 3e3))
}, 5e3)
```

It pings every 5 s and **closes the connection if no pong is handled within
3 s**. That deadline is a `setTimeout` on the browser's main thread, and the
pong is cleared by a WebSocket message that has to queue behind every pending
widget update. Saturate the main thread and the client drops *itself*. On
reconnect the server re-pushes the session, which saturates it again — hence
"unreliable" rather than "dead".

The server's own watchdog is far more forgiving: ping every 25 s, close after
5 s of silence. So the client gives up first, which matches the symptom.

## What an update costs the client

Real addresses from the patch, driven at the 25 Hz the GUI throttle
(`[metro 40]` at `_main.pd:221`) sets. The tab was hidden, so
`requestAnimationFrame` was suspended and no painting was measured: **these
are floors.** A tablet repainting its canvases will be well above them.

| message hits | client JS per message |
|---|---|
| an address no widget answers to | **6 µs** |
| a fader | **105 µs** |
| an xy pad (two floats) | **127 µs** |

Transport — the frame, the `JSON.parse`, the event dispatch — is about **5%**
of the cost. The widget update is the other **95%**. That single ratio decides
which fixes can work.

At 84 parameters × 25 Hz = 1,781 msg/s: 100% delivered, 112 KiB/s, and
**13.2% of an M5 main thread with rendering switched off.**

## Where the volume comes from

- The patch pushes **6,320 distinct OSC addresses**. The startup state burst
  is ~6,600 messages inside one second; steady idle traffic is ~67 msg/s.
- 427 `f.seq.automater` instances (`docs/efficiency/tools/census.py`), each
  GUI-throttled to 25 Hz. 151 `f.util.oscformat` and 66 `f.util.oscformatxy`
  instances actually address the GUI.
- `MESSAGE_THROTTLE` in `f.formuls.pd:5` already gates parameter updates to
  the **selected synth instance**. `inter-act` is a two-slot array written by
  `INTERFACE_ACTIVE_MESSAGE_SEND` and both slots are compared, which reads
  like "the last two tabs" — but the `i`/`mod 2` pair alternates the write
  index on every poll reply, and the poll repeats the same tab 25 times a
  second, so both slots converge on the current tab within 80 ms. The second
  slot is a 40 ms grace window across a switch, not a second live instance.
  The practical ceiling is therefore about 36 GUI senders × 25 Hz ≈ **900
  msg/s**, so the 1,781 msg/s load used above is roughly double a realistic
  worst case — pessimistic, which is the safe direction for a cost figure but
  worth knowing. The `inter-act-overide` toggle opens the gate for all six and
  takes it to ~10,700 msg/s, but it is a Pd-side toggle the interface cannot
  reach.
- `GET_INTERFACE` at `_main.pd:148` sends `/GET 127.0.0.1:9000 root` **25
  times a second, forever**, and each one costs a WebSocket frame to every
  client plus a 24-byte reply parsed back through Pd. It looks like pure
  waste. **It is not: it is load-bearing, and it must not be deleted.**

  `route pd GET oscmonitor testtone` in `OPEN_STAGE_CONTROL_RECEIVE_` sends
  its `GET` outlet into `INTERFACE_ACTIVE_MESSAGE_SEND`, whose `route root`
  writes the reply into the two-slot `interface-active` array that becomes
  `inter-act`. So this poll is the *only* thing telling Pd which tab is
  selected, and therefore the only thing driving the gate above.

  Verified that nothing else can do the job: with a sink on the server's
  `--send` target and no Pd running, switching tabs in the client produces
  **no OSC at all**. The root widget does not push its value; it can only be
  polled. Two consequences worth knowing — `inter-act` updates at half the
  poll rate (the `mod 2`/`sel 1` pair reads the array every other reply, so
  12.5 Hz), and if every tablet disconnects the gate freezes on whichever
  two tabs were last selected.

  Lowering the poll rate is possible — tab selection is a human action and
  10 Hz would be imperceptible — but it is worth well under 1% of the load
  and means editing a patch Pd will re-save. Not worth it on its own.

## Sending them as blobs: measured, and it is not the fix

`src/tools/patch-osc-perf.py --batch-ms 20` rewrites `IpcServer.send` to
coalesce broadcasts into one frame per 20 ms. The receiving half needed no
invention: the client has handled a `bundle` event since forever
(`client/callbacks.js`), it is simply that no server release ever sends one.
Same load, same session, one browser client:

| | stock | `--batch-ms 20` |
|---|---|---|
| WebSocket frames for 38k messages | 38,310 | **464** (83 msgs/frame) |
| bytes | 124.5 KiB/s | 107.4 KiB/s (**−14%**) |
| server CPU | 0.99 s | **0.59 s (−40%)** |
| client main-thread JS over the run | 4,164 ms | **5,513 ms (+32%)** |
| handler p50 / p99 | 0.1 / 0.6 ms | 12.2 / 15.9 ms |
| handler calls over 16 ms | 0 | 3 of 464 |

The 6 µs transport figure predicted this: there was only 5% to win on the
client, and batching does not collect it. What it does instead is make the
client do **32% more total work**, delivered in 12 ms slabs rather than 0.1 ms
slivers. Rewriting the shipped `for...in` bundle loop as an indexed loop —
which the patch does whenever batching is on — does not recover the
difference, so the loop was never the cause.

The one number that first looked alarming was a 67.7 ms handler call. That is
a reconnect artefact, not steady state: over a 20 s run the worst steady-state
call is 19.5 ms and only three exceed 16 ms. Worth recording because a max
alone would have condemned the change for the wrong reason.

The server side of the trade grows with tablet count. Three paired rounds at
84 params × 25 Hz with **four** clients connected, median of three:

| | µs of server CPU per message | vs stock |
|---|---|---|
| stock | 51.1 | — |
| serialise once | 48.8 | −4.5% |
| `--batch-ms 20` | **18.6** | **−64%** |

So batching is a real win for the server and the link, and a real loss for the
tablet. Since a saturated tablet main thread is exactly what drops the
connection, **batching ships off by default** and is one flag away.

**Conclusion: packing the messages differently cannot help the client. Sending
fewer of them can, linearly.**

## What shipped

`src/tools/patch-osc-perf.py`, applied to the vendored package by both build
scripts straight after it is unpacked. It follows `brand-osc.sh`'s contract:
matched on the code rather than line numbers, idempotent, and a hard failure
if an anchor is missing, because a patch that silently does nothing is worse
than one that stops the build.

- **Applied always:** `IpcServer.send` serialises the payload once and hands
  every client the same string, instead of `JSON.stringify` per client. Worth
  −4.5% of server CPU at four clients and nothing at one; no behavioural
  change whatsoever.
- **Opt-in, `--batch-ms N`:** the coalescing above. Turn it on when the server
  or the access point is the bottleneck rather than the tablet.

The `/GET` poll was in the original plan for this step and was **not** removed:
it turned out to be load-bearing. See the note above.

## Gating what escaped the gate

The plan for this step was "gate updates on the visible tab rather than the
instance". Two things turned out to be wrong with that.

**The panel has no tabs to gate on.** The whole synth page is one surface:
almost every widget under `maincontainer` is `visible: true` at once. The only
hidden pages are modals.

**And the instance gate is tighter than it looked.** `inter-act` is a two-slot
array written by `INTERFACE_ACTIVE_MESSAGE_SEND`, and the `i`/`mod 2` pair
alternates the write index on every poll reply. Because the poll repeats the
*same* tab 25 times a second, both slots converge on the current tab within
80 ms. It is not "the last two tabs kept open forever" — it is one instance,
with a 40 ms grace window across a switch. The earlier estimate of a
~1,800 msg/s ceiling assumed two instances and was about double the truth.

So the parameter traffic was already gated. What was not:

`SEQPOS_LED___` in `f.util.oscinparse.pd` sent straight to
`to-o-s-c-interface`, bypassing `f.formuls.pd`'s `MESSAGE_THROTTLE`
altogether — so **all six instances streamed their sequencer position at all
times**, regardless of which tab was open. Worse, `seqposN` lives inside the
`sequencerN` *modal*: it is not even on screen unless the performer has that
popup open.

It is also the most expensive message in the instrument. Priced on its own
against a slider on the same panel:

| | client JS per update |
|---|---|
| a slider (`/attack1`) | 76 µs |
| a sequencer position (`/seqpos1`) | **710 µs** |

(Both measured with Pd stopped and only the priced address in flight, which is
why the slider comes out below the 105 µs in the table further up — that one
was taken with the patch running and its background traffic in the average.
The ratio is the point, and it is not close.)

The fix is one object: send to `$1-message-collect` instead, which is the bus
`f.formuls.pd` already reads and gates. Verified headless — with instance 1
selected only `/seqpos1` flows, with instance 4 only `/seqpos4`, with no tab
selected none at all — and then end to end through Pd, o-s-c and a browser,
one synth tab open, instrument idle:

| | before | after |
|---|---|---|
| messages/s to the client | 81 | **46 (−43%)** |
| bytes | 7.0 KiB/s | 4.3 KiB/s (−39%) |
| client main thread | 10.6% | **6.3% (−41%)** |
| handler p50 | 1.2 ms | **0.2 ms** |

After it, the only ungated steady traffic left is the `/GET` poll itself.

### Why gating in Pd is the only lever

It would be reasonable to hope the client already skips work for a panel that
is not showing. It does not, and this is what makes suppression worth its full
face value. o-s-c builds a tab's widgets when you open it and tears them down
when you leave — but the cost of an update does not follow. Measured
back to back with Pd stopped, on the mixer tab, with the synth-1 panel torn
down and `attack1` confirmed absent from the DOM:

| | client JS per update |
|---|---|
| `/attack1` — known to the session, panel torn down | **69 µs** |
| `/nosuchwidget1` — not in the session at all | **2.3 µs** |

A 30× gap. Any address the session defines costs the full update whether or
not its panel exists. There is no discount for hiding something; the only
saving is not sending it.

## What does fix it, in order

| | work | expected |
|---|---|---|
| **1** ~~Delete the `/GET` poll~~; hoist the `stringify`; make batching available — **done**, see "What shipped" | 1–2 days | −4.5% server CPU at four tablets by default; −64% and 83× fewer frames with `--batch-ms 20`. Does not fix client saturation, and the `/GET` half of this was a wrong call |
| **2** Gate the traffic that escapes `inter-act` — **done**, see "Gating what escaped the gate" | one object changed | **−43% of idle messages and −41% of idle client CPU** |
| **3** The blob idea in the place it pays: one packed frame per tick into an o-s-c `canvas` widget with an `onDraw` script, replacing ~72 individual widget updates with one | 1–2 weeks | Attacks the 95% directly. This is the version of "send it as a blob" that works |
| **4** Merge xy pads into `multixy` + mode buttons — `filter` already does this with HiPass/LowPass points | 2–4 weeks | Real, but touches the 1.7 MB `_main.json` (907 authored widgets, 67 clones) and the string-concatenated addresses in `f.util.oscformatxy` / `f.util.oscinparse` |

Steps 1–3 are roughly three weeks.

## The JUCE + WebView + React alternative

One correction first, because it changes the scope entirely.

`juce_gui_extra` in JUCE 9.0.1 has `WebBrowserComponent` and the
`WebSliderRelay` / `WebToggleButtonRelay` / `WebComboBoxRelay` bridges. But
**JUCE contains no HTTP server and no WebSocket server**, and those relays
only wire up the embedded WKWebView inside the app's own window. A JUCE
WebView puts the UI on the host machine's screen; it serves nothing to a
tablet over the network. "Runs as it does now, tablet connects over Wi-Fi" is
not what that gets you — the HTTP and WebSocket servers would both be yours to
write in C++.

It also fixes nothing by itself. A React client taking 1,800 messages a second
and re-rendering hits the same wall. What a rewrite *does* buy is control over
the three things that are actually broken: frame batching, the heartbeat
deadline, and per-widget update cost.

Scope, for one person: an HTTP + WebSocket server in C++; a React
re-implementation of the fader, xy, multixy, matrix, keyboard, menu, modal and
tab widget types with multi-touch; multi-client state sync; and the session
and state handling currently carried by `_main.json` and the 220 KB
`_formuls-default.state`. **3–6 months.**

Worth doing only after steps 1–3, and then for other reasons — at which point
it becomes a port of a UI whose cost model is understood, which is a much
safer rewrite than the one available today.

## Caveats, so nobody repeats the mistakes

**Do not point the rig at ports 9000/9001 while the app is running.** A
headless Pd started from a copy of `_main.pd` still has `connect 127.0.0.1
9001` in it, so its `netsend` attaches to the *live* o-s-c server and pushes
its whole startup state into the running interface. It cannot drive the audio
engine — o-s-c does not re-send values it receives over OSC — but every
on-screen widget jumps. Repoint the three hardcoded ports in the copy first;
`tools/README.md` has the `sed`.

**OSC string padding is `(4 - len % 4) % 4`, not `4 - len % 4`.** The second
form appends four spurious nulls to every address whose length is already a
multiple of 4, which misaligns the type tag and makes osc.js drop the message.
Exactly 24 of 84 test addresses were affected, and the resulting 71.4%
delivery rate looked convincingly like the server dropping input under load —
it held at 71.4% at 200 msg/s and at 1,900 msg/s, which is what gave it away.
A control run into `osc-sink.py` (100% delivery) proved the datagrams were
leaving fine.

**A hidden browser tab cannot measure rendering.** `requestAnimationFrame` is
suspended and timers are clamped to 1 Hz, so any lag probe built on
`setInterval` reports ~900 ms of phantom lag and the paint cost never appears.
Timing the app's own `onmessage` handler works in a hidden tab; everything
downstream of it does not. Every per-message figure here is therefore a floor.

**Send the right arity.** One float into an xy pad is silently ignored by the
client and prices at 2 µs. It looks like a spectacular optimisation and is a
measurement of nothing.

**Background traffic swamps a per-message figure.** Pricing one widget type
against a running Pd mixes ~70 msg/s of `/GET` and `/seqpos` into the average,
and `/seqpos` is ten times the cost of a slider. A visible-versus-hidden
comparison done that way came out identical for the wrong reason and nearly
buried the finding. Stop Pd, then drive only the addresses being priced.

**o-s-c only builds the open tab.** Widgets are created when a tab is opened
and destroyed when it is left, so an address whose panel has never been shown
matches nothing and prices at 2 µs. Open the tab you intend to measure, and
check with `document.querySelectorAll('[data-widget]')` that the widget is
really there — otherwise the number is a measurement of the miss path.
