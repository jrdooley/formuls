# Embedded GUI server (prototype)

Branch `ws-prototype`. The JUCE app serves the control GUI itself, so node and
the Open Stage Control server no longer run. This is the first step towards a
plugin build (VST3/AU/CLAP), where a child process is not an option and the
GUI still has to reach a tablet's browser.

## What changed

| | Before | Prototype |
|---|---|---|
| Browser client | o-s-c 1.31.0 `client/` | the same files, served by the app |
| Server | node + o-s-c, child process | `EmbeddedGuiServer` + `WebSocketServer`, in the app |
| `formuls-module.js` | o-s-c `--custom-module` | ported to C++ (`module*` in `EmbeddedGuiServer.cpp`) |
| GUI → Pd | UDP OSC → `OscBridge` (9000) | `OscBridge::post()`, in-process, drained on the message thread |
| Pd → GUI | `PdOscOut` → UDP 9001 | `PdOscOut`'s packet sink → `EmbeddedGuiServer::receivePatchPacket()` |
| App → GUI (state save/open) | UDP 9001 | `EmbeddedGuiServer::receiveFromApp()` |

No OSC port is opened: the only socket is the GUI's own (tcp 9001). The
patch is still never run from a network thread: GUI messages are queued and
handed to libpd on the message thread by the same `OscBridge::forward()` the
UDP path used, and the patch's packets still leave Pd through `PdOscOut`'s
lock-free FIFO.

Set `FORMULS_NODE_GUI=1` in the environment to start node + o-s-c with the
UDP ports, as before.

## The protocol that is reimplemented

One WebSocket per tablet at `/<clientId>/<auth>`. Each message is a JSON
array `["event", data]`. The server handles:

- `ping` → `["pong"]`. The client drops the link if this takes over 3 s.
- `open` → `connected`, `sessionList`, `clipboard`, `serverTargets`, `sessionOpen`.
- `sessionOpened` → `stateLoad` (the default state; only the first client
  sends it on to Pd), `stateSend` to the others if asked, `setTitle`.
- `addWidget` / `removeWidget`: the per-client widget table. The browser
  sends only `{h, v}` and the server fills in address, preArgs, target and
  typeTags from it.
- `sendOsc`: merge with the table, `args = preArgs ++ v`, route to each target
  (default `127.0.0.1:9000`, `"self"`, `null`), then `receiveOsc` to every
  *other* client unless `noSync`.
- `syncOsc`: the sync half only.
- `stateSave` / `stateOpen`: used by the app's Save/Open state commands.
- `clipboard`, `errorLog`.

The editor's events (`sessionSave`, `listDir`, `fragmentLoad`, ...) are
ignored: the GUI is served read-only.

## Sending to a slow tablet

Sends never wait for the network. `Connection::sendText` appends the frame to
that connection's queue and returns; a writer thread per connection drains
it in order. So the thread that calls `deliver()` (the message thread, which
in a plugin is the host's UI thread) is never held up by a tablet, and one
slow tablet no longer holds up the others while `EmbeddedGuiServer`'s lock
is held.

A tablet that cannot keep up is **disconnected**, not sent a stream with
holes in it, when either:

- more than `maxQueuedBytes` (8 MB, about six patch resets of ~9,000
  messages, ~1.3 MB each) is waiting for it, or
- its socket accepts nothing for `stallTimeoutMs` (10 s).

The o-s-c client reconnects by itself within a second and is resynced (below). Dropping messages
instead would leave controls silently showing stale values.

Before this change the frames were written inline with a 5 s wait for
buffer space. In practice it seldom waited: JUCE's `waitUntilReady` only
try-locks the socket's read lock, which the connection's reader thread holds
for almost all of its own 200 ms wait. So it usually returned -1 at once, and
a full buffer (about 1 MB into a reset on macOS loopback) dropped the tablet
mid-reset. It waited the full 5 s only when it hit the gap between reads.
The writer now waits with `poll`/`WSAPoll` on the raw handle.
`src/tools/ws-stall-test` checks all of this.

## Resync on reconnect

The o-s-c client asks for the session and state (`open`) only when its page
loads. On reconnect it just reopens the socket. Anything delivered while it was
away would otherwise be missing until the next change or a page reload. That
covers a Wi-Fi dropout, and a tablet disconnected for falling behind (above).

So `deliver()` keeps the last broadcast frame per control, and when a known
client id reconnects it is sent those frames, in the order they were last
sent. This is display only: they are `receiveOsc` frames, exactly as
delivered, and nothing goes to the patch. A control is identified by:

- `/fc_patch` (the compounds module packs every canvas slot onto this one
  address): its cid and first slot, or `"@var"`, cid, variable and count;
- anything else: the address plus the longest `preArgs` of a registered
  widget the arguments start with, which is what o-s-c matches on. The formuls
  session uses no `preArgs`, so in practice this is the address alone.

`/STATE/SET` (a restored GUI state) empties the cache and becomes its first
entry, and later changes replay on top of it. `/GET` (the patch's poll) and
the other `/STATE/*` commands are never kept: replaying `/STATE/SAVE` would
write the file again. Frames sent to one client only are not shared state
and are not kept either.

A page *load* that reuses an id also gets the replay, before its session
exists; the client ignores it, then loads state as usual.

Checked with plugin-probe's `PROBE_AWAY`. A tablet away from 2.12 s to 4 s
missed most of a global reset. It got back 7,982 frames, and its last value
for each of the 7,980 controls matched a tablet that never left. Without the
replay, 6,854 of them were stale or missing.

## Known gaps

- Clients are never forgotten (o-s-c drops a client after 60 s offline).
- GUI messages are still encoded as `juce::OSCMessage` and the patch's
  replies as OSC bytes (by `[oscformat]`), so the patch is unchanged. A
  plugin could skip the encoding, but it is not the cost that matters.
- A plugin needs one GUI port per instance.
- No authentication or TLS (formuls never used them).
