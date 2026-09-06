// Read (and optionally reset) the ws-probe counters.
//
//   window.__read()        report since the last reset
//   window.__read(35616)   report against a known number of OSC messages sent,
//                          which is what gives msgsPerFrame and us-per-OSC-message
//                          when the server is batching several into one frame
//   window.__hist()        distribution of handler durations. Read this, not
//                          maxHandlerMs: a reconnect burst produces one huge
//                          outlier that says nothing about steady state
//   window.__reset()       zero the counters; call before every run

(() => {
  window.__reset = () => {
    const S = window.__ST;
    S.frames = 0; S.bytes = 0; S.closes = 0; S.opens = 0;
    S.handlerMs = 0; S.maxHandlerMs = 0; S.hist = []; S.t0 = Date.now();
    return 'reset';
  };

  window.__hist = () => {
    const h = [...window.__ST.hist].sort((a, b) => a - b);
    if (!h.length) return {n: 0};
    const q = p => +h[Math.min(h.length - 1, Math.floor(p * h.length))].toFixed(1);
    return {
      n: h.length, p50: q(.5), p90: q(.9), p99: q(.99),
      max: +h[h.length - 1].toFixed(1),
      over16ms: h.filter(x => x > 16).length,
      over50ms: h.filter(x => x > 50).length,
      totalMs: +h.reduce((a, b) => a + b, 0).toFixed(0)
    };
  };

  window.__read = (oscSent) => {
    const S = window.__ST, el = (Date.now() - S.t0) / 1000;
    const n = oscSent || S.frames;
    return {
      elapsed: +el.toFixed(1),
      wsFrames: S.frames,
      oscSent: oscSent || null,
      deliveredPct: oscSent ? +(100 * S.frames / oscSent).toFixed(1) : null,
      msgsPerFrame: oscSent ? +(oscSent / Math.max(1, S.frames)).toFixed(1) : 1,
      kibPerSec: +(S.bytes / 1024 / el).toFixed(1),
      usPerMsg: +(S.handlerMs * 1000 / Math.max(1, n)).toFixed(0),
      mainThreadPct: +(100 * S.handlerMs / (el * 1000)).toFixed(1),
      maxHandlerMs: +S.maxHandlerMs.toFixed(1),
      closes: S.closes
    };
  };

  return 'ready: __reset() before a run, __read(sent) after';
})()
