// Regenerates src/tools/osc-defaults-<version>.json for flatten-session.py.
//
// Paste into the console of an Open Stage Control client running the
// clone-based src/gui/_main.json -- not the flattened build output, which has no
// clones (the flattener needs the clone defaults too) -- then save the
// printed JSON as src/tools/osc-defaults-<version>.json and point the build
// scripts' --defaults at it. Each type's defaults come from its widget class,
// <Class>.defaults(), which only exists inside the browser bundle.
(function () {
  const version = (document.title.match(/v(\d+\.\d+\.\d+)/) || [])[1] || 'UNKNOWN';
  let r = document.querySelector('[data-widget]')._widget_instance;
  while (r.parent && typeof r.parent.getProp === 'function') r = r.parent;
  const byType = {};
  (function walk(w) {
    const t = w.getProp('type');
    if (!byType[t]) {
      const d = w.constructor.defaults(), out = {};
      for (const cat in d) for (const k in d[cat]) {
        const e = d[cat][k];                       // some entries are section-label strings
        if (e && typeof e === 'object' && 'value' in e) out[k] = e.value;
      }
      byType[t] = out;
    }
    for (const c of (w.children || [])) if (c && c.getProp) walk(c);
  })(r);
  const json = JSON.stringify({
    openStageControl: version,
    note: 'Per-type prop defaults from <WidgetClass>.defaults(), extracted from a live session ' +
          '(docs/gui/tools/session/extract-defaults.js). Regenerate when the vendored o-s-c ' +
          'version changes; flatten-session.py refuses a mismatched package.',
    types: byType,
  }, null, 1);
  console.log(json);
  return Object.keys(byType).sort().join(', ');
})();
