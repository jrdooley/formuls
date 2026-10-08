// Opens every modal on one tab in turn and records the on-screen geometry and key
// computed styles of everything inside it. Keys are id#occurrence, as in fingerprint.js.
(async function () {
  const raf2 = () => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)));
  let r = document.querySelector('[data-widget]')._widget_instance;
  while (r.parent && typeof r.parent.getProp === 'function') r = r.parent;
  const tabs = r.children.filter(c => c && c.getProp && c.getProp('type') === 'tab');
  const ti = tabs.findIndex(t => t.getProp('id') === 'formuls1');
  r.setValue(ti, { send: false, sync: true }); await raf2(); await new Promise(z => setTimeout(z, 300));
  const modals = []; (function walk(w) { if (w.getProp('type') === 'modal') modals.push(w); for (const c of (w.children || [])) if (c && c.getProp && c.getProp('type') !== 'clone') walk(c); else if (c && c.getProp) walk(c); })(tabs[ti]);
  const out = {}; const seen = {};
  for (const m of modals) {
    m.setValue(1, { send: false, sync: false }); await raf2(); await new Promise(z => setTimeout(z, 120));
    (function walk(w) {
      if (w.getProp('type') !== 'clone' && w.container) {
        const id = 'M:' + m.getProp('id') + '/' + w.getProp('id'); const n = seen[id] = (seen[id] || 0) + 1;
        const b = w.container.getBoundingClientRect(); const cs = getComputedStyle(w.container);
        out[id + '#' + n] = { geom: [Math.round(b.left), Math.round(b.top), Math.round(b.width), Math.round(b.height)], style: [cs.fontSize, cs.opacity, cs.pointerEvents, cs.display, cs.borderRadius] };
      }
      for (const c of (w.children || [])) if (c && c.getProp) walk(c);
    })(m);
    m.setValue(0, { send: false, sync: false }); await raf2();
  }
  return JSON.stringify({ modals: modals.length, entries: Object.keys(out).length, out });
})();
