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
- `MESSAGE_THROTTLE` in `f.formuls.pd:5` already gates updates to the **two
  most recently selected synth instances** — `inter-act` is a two-slot array
  written by `INTERFACE_ACTIVE_MESSAGE_SEND`, and both slots are compared. So
  the practical ceiling is roughly 2 × 36 GUI senders × 25 Hz ≈ **1,800
  msg/s**, which is why the load above was chosen at that level. The
  `inter-act-overide` toggle opens the gate for all six and takes it to
  ~10,700 msg/s, but it is a Pd-side toggle the interface cannot reach.
- `GET_INTERFACE` at `_main.pd:148` sends `/GET 127.0.0.1:9000 root` **25
  times a second, forever**. Each one costs a WebSocket frame to every client
  and a 24-byte reply parsed back through Pd's `oscparse`/`route` chain. It
  polls a widget whose value is always 0.

## Sending them as blobs: measured, and it is not the fix

`tools/batch-frames.py` rewrites `Client.send` in a copy of the bundle to
coalesce into one frame per 20 ms. No client change was needed: the client has
handled a `bundle` event since forever (`client/callbacks.js`:
`bundle: n => { for (let o in n) e.receive(n[o]) }`) but no server release
ever sends one. Same load, same client:

| | stock | batched |
|---|---|---|
| WebSocket frames for 36k messages | 35,618 | **438** (83 msgs/frame) |
| bytes | 111.7 KiB/s | 91.3 KiB/s (**−18%**) |
| server CPU | 0.82 s | **0.63 s (−23%)** |
| client JS per message | 115 µs | **133 µs (worse)** |
| longest uninterrupted handler | 15.3 ms | 23.7 ms |

The client gets *slower*: `for...in` over an array is slow, and batching also
removes the natural yielding between messages, which is why the longest
uninterrupted run of JS grows by half. The 6 µs transport figure predicts this
exactly — there is only 5% there to win.

Batching is still worth something for the **link**: 81× fewer frames and 18%
fewer bytes means a far shorter head-of-line queue on a marginal Wi-Fi hop.
But the client's main thread is where the 3-second deadline lives, and
batching does not move it.

**Conclusion: packing the messages differently cannot help. Sending fewer of
them can, linearly.**

## Two changes worth shipping on their own

Both are small, and neither is the real fix.

- **Delete the `/GET` poll.** 25 frames/s per client and 25 Hz of `oscparse`
  work in Pd, for a value that is always 0.
- **Hoist the `JSON.stringify` out of `IpcServer.send`'s per-client loop.**
  It currently re-serialises the same payload once per connected tablet.

The batching patch is worth shipping *if* the client half is rewritten at the
same time — a `for...of` loop, and a flush that yields between chunks so one
frame cannot monopolise the main thread for 24 ms. As it stands it trades
client responsiveness for server CPU, which is the wrong direction.

## What does fix it, in order

| | work | expected |
|---|---|---|
| **1** Delete the `/GET` poll; hoist the `stringify`; ship the batching patch *with* a rewritten client handler, applied to the vendored o-s-c in `build-macOS.sh` | 1–2 days | −23% server CPU, 81× fewer frames, better behaviour on marginal Wi-Fi. Does not fix client saturation |
| **2** Gate updates on the **visible tab**, not just the instance. `inter-act` already proves the pattern; o-s-c can tell Pd which tab is active | ~1 week, all in the patch | Biggest win per unit of effort; plausibly 3–10× fewer steady-state messages |
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
