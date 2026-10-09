# Draft: upstream proposal to Open Stage Control

To post as a GitHub issue on `jean-emmanuel/open-stage-control`, or on the project
forum. Not yet posted. It proposes upstreaming the two changes `src/tools/patch-osc-perf.py`
always applies, so the script can shrink. The numbers come from
[`session-size.md`](session-size.md) and [`README.md`](README.md).

Before posting:
- add your own details; the draft describes formuls only in general terms;
- keep the 4.5% figure tied to four clients, the count it was measured at.

---

**Title:** Two small performance changes: skip forced canvas resize on tab show, and serialise broadcasts once

Hi, and thanks for Open Stage Control. I use it as the tablet interface for a synthesiser written in Pure Data. The session is large: about 13,000 widgets, around 400 canvases per tab, and often several tablets connected at once. While profiling it, I found two small changes in 1.31.0 that help in both cases. Neither changes behaviour as far as I can tell. I'm proposing them here rather than keeping them as local patches.

### 1. `Tab.show()`: don't force-resize canvases whose size hasn't changed

When a tab is shown, it's re-attached and then calls `check(this.widget, true)`. The `true` forces every canvas in the tab to resize: it reallocates the backing store and redraws, even when the size is unchanged since the tab was hidden. With `false`, only canvases whose size actually changed are resized.

The change, in the built client:

```diff
- this.detached=!1,this.setVisibility(),check(this.widget,!0))}
+ this.detached=!1,this.setVisibility(),check(this.widget,!1))}
```

**Measured:** switching between two tabs of about 400 canvases each went from **58.6 ms to 48.7 ms** (median of 15 switches, Chromium, Apple M5). On a tablet the absolute saving should be larger.

**Why I believe it's safe:** a canvas whose value changed while its tab was hidden is still drawn up to date, because drawing runs whether or not the canvas is attached. I checked this after each switch by comparing every canvas's displayed bitmap with a fresh `draw()`, and they matched. A negative control (clearing one canvas by hand) was detected, so the comparison can fail.

What I'm unsure of is why `true` was chosen originally. If some case needs the forced resize, such as fonts or CSS loading while a tab is hidden, a narrower fix could force it only in that case.

### 2. `IpcServer.send()`: serialise once, not once per client

The server broadcast calls `JSON.stringify` for every connected client inside the loop, so with N tablets every message is serialised N times. Serialising once and sending the same string to every client gives identical frames:

```js
send(event, data, clientId, excludeId) {
    var payload = JSON.stringify([event, data])
    var clients = clientId ? [this.clients[clientId]] : this.clients
    for (var id in clients) {
        if (excludeId && this.clients[id] == this.clients[excludeId]) continue
        if (clients[id]) clients[id].sendRaw(payload)
    }
}
```

The client wrapper then needs a `sendRaw(string)` beside `send()`; `send()` becomes `this.sendRaw(JSON.stringify([e, t]))`.

**Measured:** **about 4.5% less server CPU with four clients connected**, under steady automation traffic. The saving grows with the number of tablets. Each client receives byte-identical frames.

### Context

I currently apply both as string-replacement patches to the built package at install time. That works, but it breaks with every release, which is why I'd rather see them upstream. I'm happy to open a pull request against the source if you prefer, and to share the measurement setup.
