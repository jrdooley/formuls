// In-page session flattener. Walks the LIVE widget tree of a loaded session and
// emits an equivalent session in which:
//   - clones are replaced by the widget they render (geometry taken from the clone),
//   - every static reference (@{parent.variables...}, @{parent.variables},
//     @{this.variables...}, @{this.id}) is resolved by o-s-c's own resolveProp,
//   - dynamic constructs (OSC{}, JS{}, @{someWidget.value}, @{this.value}) are kept,
//   - template-only tabs are replaced by empty hidden placeholders (tab indices are
//     load-bearing: Pd reads the selected synth from the root tab index).
// window.__flatten({templateTabs}) -> {session, report}
(function () {
  const STATIC = /^(parent\.variables(\.[A-Za-z0-9_]+)*|this\.variables(\.[A-Za-z0-9_]+)*|this\.id|parent\.id)$/;
  const SCRIPT_KEYS = new Set(['onValue', 'onCreate', 'onTouch', 'onDraw', 'onPreload', 'onKeyboard', 'onResize']);
  const report = { clones: 0, partial: 0, unresolved: {}, cloneExtraProps: {}, conflicts: [], stripped: 0 };

  // innermost @{...} (no nested @{ inside)
  function innermost(s, from) {
    let i = s.indexOf('@{', from);
    while (i >= 0) {
      let d = 0, j = i + 1, nested = false;
      for (; j < s.length; j++) {
        if (s[j] === '{') d++;
        else if (s[j] === '}') { d--; if (d === 0) break; }
        if (s[j] === '@' && s[j + 1] === '{' && j !== i) nested = true;
      }
      if (!nested) return [i, j];
      i = s.indexOf('@{', i + 2);
    }
    return null;
  }

  const defCache = {};
  function defaultsOf(w) {
    const t = w.getProp('type');
    if (defCache[t]) return defCache[t];
    const out = {};
    try {
      const d = w.constructor.defaults();
      for (const cat in d) for (const k in d[cat]) if (d[cat][k] && typeof d[cat][k] === 'object' && 'value' in d[cat][k]) out[k] = d[cat][k].value;  // some entries are section-label strings
    } catch (e) {}
    return (defCache[t] = out);
  }
  function resolveSnippet(w, expr) {
    return w.resolveProp('__flat', '@{' + expr + '}', false);
  }

  // Substitute static innermost refs repeatedly; leave dynamic ones verbatim.
  function partial(w, s) {
    let from = 0, guard = 0;
    while (guard++ < 500) {
      const m = innermost(s, from);
      if (!m) break;
      const [i, j] = m; const expr = s.slice(i + 2, j);
      if (STATIC.test(expr)) {
        const v = resolveSnippet(w, expr);
        if (s === '@{' + expr + '}') return v;            // whole prop: keep type
        const rep = String(v);                             // o-s-c embeds with String(): objects become [object Object]
        s = s.slice(0, i) + rep + s.slice(j + 1);
        from = 0;                                         // outer ref may now be innermost
      } else {
        report.unresolved[expr.replace(/\d+/g, '#')] = (report.unresolved[expr.replace(/\d+/g, '#')] || 0) + 1;
        from = i + 2;
      }
    }
    return s;
  }

  function isDynamicString(s) {
    if (/(OSC|JS|VAR|IMPORT)\{|#\{/.test(s)) return true;
    // any @{} that is not purely static after nesting is dynamic
    let probe = s.replace(/@\{(parent\.variables(\.[A-Za-z0-9_]+)*|this\.variables(\.[A-Za-z0-9_]+)*|this\.id|parent\.id)\}/g, 'X');
    for (let k = 0; k < 5; k++) probe = probe.replace(/@\{(parent\.variables(\.[A-Za-z0-9_]+)*|this\.variables(\.[A-Za-z0-9_]+)*|this\.id|parent\.id)\}/g, 'X');
    return /@\{/.test(probe);
  }

  function flatValue(w, k, v, nested) {
    if (typeof v === 'string') {
      if (!/@\{/.test(v)) return v;
      if (SCRIPT_KEYS.has(k)) return v;                   // scripts are not @{}-resolved
      // Whole-prop resolution only at top level, and never for a matrix's per-child
      // template (getProp('props') on a matrix returns the expanded per-child array).
      if (!nested && !(k === 'props' && w.getProp('type') === 'matrix') && !isDynamicString(v)) {
        const r = w.getProp(k);                           // fully static: o-s-c's own result
        // o-s-c coerces strings inside object literals ("true" -> true) but not inside a
        // resolved JSON string, so hand objects back as a JSON string to keep types exact.
        return (r !== null && typeof r === 'object') ? JSON.stringify(r) : r;
      }
      report.partial++;
      return partial(w, v);
    }
    if (Array.isArray(v)) return v.map(x => flatValue(w, k, x, true));
    if (v && typeof v === 'object') { const o = {}; for (const kk in v) o[kk] = flatValue(w, k, v[kk], true); return o; }
    return v;
  }

  function exportWidget(w, tplTabs) {
    const type = w.getProp('type');
    if (type === 'clone') {
      report.clones++;
      const inner = w.children[0];
      if (!inner) return null;
      const o = exportWidget(inner, tplTabs);
      for (const k of ['left', 'top', 'width', 'height', 'expand']) if (k in w.props) o[k] = flatValue(w, k, w.props[k]);
      // container props of the clone itself, moved onto the inlined widget
      if (w.props.visible === false) {
        if (o.visible === true || o.visible === undefined) o.visible = false; else report.conflicts.push(['visible', w.getProp('id'), o.visible]);
      }
      if (w.props.interaction === false) {
        if (o.interaction === true || o.interaction === undefined) o.interaction = false; else report.conflicts.push(['interaction', w.getProp('id'), o.interaction]);
      }
      if (w.props.css) {
        const cc = flatValue(w, 'css', w.props.css);
        if (o.css && /font-size/.test(cc) && /font-size/.test(o.css)) report.conflicts.push(['css', w.getProp('id'), cc, o.css]);
        o.css = o.css ? cc + ';\n' + o.css : cc;   // declarations need the separator
      }
      // anything else set on the clone itself would be lost: record it
      for (const k of Object.keys(w.props)) {
        if (['type', 'id', 'widgetId', 'props', 'variables', 'left', 'top', 'width', 'height', 'expand', 'visible', 'comments', 'lock', 'address', 'scoped', 'css', 'interaction'].includes(k)) continue;
        const def = w.constructor.defaults ? undefined : undefined;
        report.cloneExtraProps[k] = (report.cloneExtraProps[k] || 0) + 1;
      }
      if (w.props.css) report.cloneExtraProps['css:' + w.props.css] = (report.cloneExtraProps['css:' + w.props.css] || 0) + 1;
      if (w.props.interaction === false) report.cloneExtraProps['interaction:false'] = (report.cloneExtraProps['interaction:false'] || 0) + 1;
      return o;
    }
    const o = {};
    const defs = defaultsOf(w);
    for (const k of Object.keys(w.props)) {
      if (k === 'widgets' || k === 'tabs') continue;
      const v = flatValue(w, k, w.props[k]);
      if (k !== 'type' && k !== 'id' && k in defs && JSON.stringify(defs[k]) === JSON.stringify(v)) { report.stripped++; continue; }
      o[k] = v;
    }
    if (type !== 'matrix' && (w.props.widgets || w.props.tabs)) {
      const kids = (w.children || []).filter(c => c && c.getProp);
      const ws = [], ts = [];
      for (const c of kids) {
        if (c.getProp('type') === 'tab') {
          if (tplTabs.includes(c.getProp('id'))) ts.push({ type: 'tab', id: c.getProp('id'), visible: false, label: c.getProp('label'), widgets: [], tabs: [] });
          else ts.push(exportWidget(c, tplTabs));
        } else {
          const e = exportWidget(c, tplTabs); if (e) ws.push(e);
        }
      }
      o.widgets = ws; o.tabs = ts;
    }
    return o;
  }

  window.__flatten = function ({ templateTabs = [] } = {}) {
    let r = document.querySelector('[data-widget]')._widget_instance;
    while (r.parent && typeof r.parent.getProp === 'function') r = r.parent;
    const content = exportWidget(r, templateTabs);
    return { session: { version: '1.31.0', type: 'session', createdWith: 'flatten.js', content }, report };
  };
})();
