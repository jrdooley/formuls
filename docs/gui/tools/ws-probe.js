// Instrument an Open Stage Control client to price incoming messages.
//
// Paste into the browser console on the o-s-c page, then bounce the server
// (kill and restart node) so the client reconnects through the wrapper. The
// app assigns socket.onmessage inside its own onopen handler, so the probe
// wraps it one turn later.
//
// Read the counters with ws-probe-read.js. Reset them between runs.
//
// What this measures: the JS the client spends turning a message into a
// widget value. What it does NOT measure: painting. A hidden or backgrounded
// tab has requestAnimationFrame suspended and timers clamped to 1 Hz, so the
// figures are floors, and any timer-based lag probe is meaningless there.

(() => {
  const Native = window.__Native || window.WebSocket;
  window.__Native = Native;

  const ST = window.__ST = {
    frames: 0, bytes: 0, opens: 0, closes: 0,
    handlerMs: 0, maxHandlerMs: 0, hist: [], sockets: [], t0: Date.now()
  };

  const Wrapped = function (url, proto) {
    const s = new Native(url, proto);
    ST.sockets.push(s);
    s.addEventListener('message', e => {
      ST.frames++;
      ST.bytes += (e.data && e.data.length) || 0;
    });
    s.addEventListener('open', () => {
      ST.opens++;
      setTimeout(() => {
        const orig = s.onmessage;
        if (orig && !orig.__wrapped) {
          const wrap = function (ev) {
            const t = performance.now();
            const r = orig.call(this, ev);
            const d = performance.now() - t;
            ST.handlerMs += d;
            ST.hist.push(d);
            if (d > ST.maxHandlerMs) ST.maxHandlerMs = d;
            return r;
          };
          wrap.__wrapped = true;
          s.onmessage = wrap;
        }
      }, 50);
    });
    s.addEventListener('close', () => { ST.closes++; });
    return s;
  };
  Wrapped.prototype = Native.prototype;
  ['CONNECTING', 'OPEN', 'CLOSING', 'CLOSED'].forEach((k, i) => Wrapped[k] = i);
  window.WebSocket = Wrapped;

  return 'installed - now restart the o-s-c server so the client reconnects';
})()
