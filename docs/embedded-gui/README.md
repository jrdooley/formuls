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
| GUI → Pd | UDP OSC → `OscBridge` (9000) | unchanged |
| Pd → GUI | `PdOscOut` → UDP 9001 | unchanged |

Set `FORMULS_NODE_GUI=1` in the environment to start node + o-s-c as before.

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

## Known gaps

- A slow tablet slows the others down: messages are sent inline, under one lock.
- Clients are never forgotten (o-s-c drops a client after 60 s offline).
- The OSC hop to Pd remains. A plugin should call libpd directly, and also
  needs one port per instance.
- No authentication or TLS (formuls never used them).
