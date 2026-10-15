// Fingerprint every live widget for an equivalence check between two sessions:
// resolved props (getProp over every default + authored key), and, per visible tab,
// on-screen geometry and the computed styles that clone containers could affect.
// Template tabs and clone wrappers are skipped so the walk orders line up.
(function () {
  const SKIP = new Set(['widgets', 'tabs', 'comments', 'lock', '__flat']);
  const raf2 = () => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)));
  function root() { let r = document.querySelector('[data-widget]')._widget_instance; while (r.parent && typeof r.parent.getProp === 'function') r = r.parent; return r; }
  function keysOf(w) {
    const ks = new Set(Object.keys(w.props));
    try { const d = w.constructor.defaults(); for (const c in d) for (const k in d[c]) ks.add(k); } catch (e) {}
    return [...ks].filter(k => !SKIP.has(k)).sort();
  }
  window.__fingerprint = async function (templateTabs) {
    const r = root(); const out = {}; const seen = {}; const tabOf = new Map();
    (function walk(w, tab) {
      const t = w.getProp('type');
      if (t === 'tab' && templateTabs.includes(w.getProp('id'))) return;
      const myTab = t === 'tab' ? w : tab;
      if (t !== 'clone') {
        const id = w.getProp('id'); const n = seen[id] = (seen[id] || 0) + 1; const key = id + '#' + n;
        const p = {}; for (const k of keysOf(w)) { try { p[k] = w.getProp(k); } catch (e) { p[k] = 'ERR'; } }
        out[key] = { type: t, props: p, value: w.getValue ? w.getValue() : undefined };
        tabOf.set(key, [myTab, w]);
      }
      for (const c of (w.children || [])) if (c && c.getProp) walk(c, myTab);
    })(r, null);
    // geometry + styles, tab by tab
    const tabs = r.children.filter(c => c && c.getProp && c.getProp('type') === 'tab');
    for (let i = 0; i < tabs.length; i++) {
      if (templateTabs.includes(tabs[i].getProp('id')) || tabs[i].getProp('visible') === false) continue;
      r.setValue(i, { send: false, sync: true }); await raf2(); await new Promise(res => setTimeout(res, 50));
      for (const [key, [tab, w]] of tabOf) {
        if (tab !== tabs[i] || !w.container) continue;
        const b = w.container.getBoundingClientRect(); const cs = getComputedStyle(w.container);
        out[key].geom = [Math.round(b.left), Math.round(b.top), Math.round(b.width), Math.round(b.height)];
        out[key].style = [cs.fontSize, cs.opacity, cs.pointerEvents, cs.display, cs.visibility, cs.borderRadius];
      }
    }
    r.setValue(0, { send: false, sync: true });
    return out;
  };
})();
