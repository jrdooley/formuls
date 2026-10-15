// Injected into an Open Stage Control client. Measures tab-switch cost and
// per-message cost against whatever session is loaded.
(function () {
  const raf2 = () => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)));
  const sleep = ms => new Promise(r => setTimeout(r, ms));

  function root() {
    let w = document.querySelector('[data-widget]')._widget_instance;
    while (w.parent && typeof w.parent.getProp === 'function') w = w.parent;
    return w;
  }
  function count(w, acc = { n: 0, t: {} }) {
    acc.n++; const ty = w.getProp('type'); acc.t[ty] = (acc.t[ty] || 0) + 1;
    for (const c of (w.children || [])) if (c && c.getProp) count(c, acc);
    return acc;
  }
  function tabs() { return root().children.filter(c => c && c.getProp && c.getProp('type') === 'tab'); }

  // Long-task observer: total main-thread blocking attributable to a window.
  let lt = [];
  try {
    new PerformanceObserver(l => { for (const e of l.getEntries()) lt.push(e); })
      .observe({ type: 'longtask', buffered: false });
  } catch (e) {}

  // Switch to tab index i; report sync JS time and time to second frame.
  async function switchTo(i) {
    const r = root();
    lt = [];
    let hid = document.visibilityState !== 'visible';
    const onVis = () => { if (document.visibilityState !== 'visible') hid = true; };
    document.addEventListener('visibilitychange', onVis);
    const t0 = performance.now();
    r.setValue(i, { send: false, sync: true });
    const t1 = performance.now();
    await raf2();
    const t2 = performance.now();
    await sleep(50);
    const blocking = lt.reduce((a, e) => a + e.duration, 0);
    document.removeEventListener('visibilitychange', onVis);
    if (document.visibilityState !== 'visible') hid = true;
    return { sync: +(t1 - t0).toFixed(1), toPaint: +(t2 - t0).toFixed(1), longtask: +blocking.toFixed(1), hidden: hid };
  }

  // Cycle through the given tab ids n times; median of each metric.
  async function benchTabs(ids, rounds = 5) {
    const all = tabs(); const idx = ids.map(id => all.findIndex(t => t.getProp('id') === id));
    const res = [];
    await switchTo(idx[idx.length - 1]); await sleep(300);
    for (let k = 0; k < rounds; k++) for (const i of idx) { res.push(await switchTo(i)); await sleep(150); }
    const ok = res.filter(x => !x.hidden);          // a switch that saw the page hidden is discarded
    const med = key => { const v = ok.map(x => x[key]).sort((a, b) => a - b); return v[Math.floor(v.length / 2)]; };
    const p90 = key => { const v = ok.map(x => x[key]).sort((a, b) => a - b); return v[Math.floor(v.length * 0.9)]; };
    return { n: ok.length, discardedHidden: res.length - ok.length, syncMed: med('sync'), toPaintMed: med('toPaint'), toPaintP90: p90('toPaint') };
  }

  function census() {
    const r = root(); const c = count(r);
    return {
      total: c.n, types: c.t,
      perTab: tabs().map(t => [t.getProp('id'), count(t).n]),
      domNodes: document.getElementsByTagName('*').length,
      canvases: document.querySelectorAll('canvas').length,
      heapMB: performance.memory ? Math.round(performance.memory.usedJSHeapSize / 1e6) : null,
    };
  }

  // Load timing: navigation start -> session built (first widget present).
  window.__bench = { root, count, tabs, switchTo, benchTabs, census, sleep, raf2 };
})();
// Per-update cost of a widget receiving a value the way an incoming OSC message
// delivers it (sync:true is what triggers the global value-changed broadcast).
window.__bench.msgCost = function (id, n = 2000) {
  let w = null;
  const walk = x => { if (!w && x.getProp && x.getProp('id') === id) w = x; for (const c of (x.children || [])) if (c && c.getProp) walk(c); };
  walk(window.__bench.root());
  if (!w) return null;
  const t0 = performance.now();
  for (let i = 0; i < n; i++) w.setValue((i % 100) / 100, { send: false, sync: true, fromExternal: true });
  const t1 = performance.now();
  return +((t1 - t0) * 1000 / n).toFixed(1);   // µs per update
};
// Synchronous draw cost: widget.draw() runs the paint immediately (batchDraw waits for rAF).
window.__bench.drawCost = function (ids, n = 300) {
  const found = [];
  const walk = x => { if (x.getProp && ids.includes(x.getProp('id'))) found.push(x); for (const c of (x.children || [])) if (c && c.getProp) walk(c); };
  walk(window.__bench.root());
  const t0 = performance.now();
  for (let i = 0; i < n; i++) for (const w of found) w.draw();
  return { widgets: found.map(w => w.getProp('id')), usPerDraw: +((performance.now() - t0) * 1000 / n).toFixed(1) };
};
// Per-update cost for a multi-value widget (e.g. a 13-value canvas compound).
window.__bench.msgCostArr = function (id, len, n = 2000) {
  let w = null;
  const walk = x => { if (!w && x.getProp && x.getProp('id') === id) w = x; for (const c of (x.children || [])) if (c && c.getProp) walk(c); };
  walk(window.__bench.root());
  if (!w) return null;
  const v = new Array(len).fill(0);
  const t0 = performance.now();
  for (let i = 0; i < n; i++) { v[i % len] = (i % 100) / 100; w.setValue(v.slice(), { send: false, sync: true, fromExternal: true }); }
  return +((performance.now() - t0) * 1000 / n).toFixed(1);
};
