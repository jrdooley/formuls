// In-page session rebuild timer (needs the instrumented client: window.__jm, marks b0..b3).
// __rebuild(file) fetches a session JSON from the server's session dir, loads it
// through the real SessionManager.load, and returns the phase timings.
(function () {
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  function phases() {
    const m = {}; for (const e of performance.getEntriesByType('mark')) m[e.name] = e.startTime;
    return {
      teardown: +(m.b0a - m.b0).toFixed(0),
      parse: +(m.b1 - m.b0a).toFixed(0),            // widget construction + DOM
      initialValueBroadcast: +(m.b2 - m.b1).toFixed(0), // value-changed for every widget
      resize: +(m.b3 - m.b2).toFixed(0),
      total: +(m.b3 - m.b0).toFixed(0),
    };
  }
  function census() {
    const wm = window.__wm; const ids = Object.keys(wm.widgets);
    let listeners = null;
    try { listeners = (wm._listeners || wm.listeners || {})['value-changed']; listeners = listeners ? listeners.length : null; } catch (e) {}
    return { widgets: ids.length, valueChangedListeners: listeners };
  }
  window.__rebuild = async function (file, rounds = 1) {
    const data = window.__cache[file];  // preloaded: load() resets the server's session path, after which static files 403
    const out = [];
    for (let i = 0; i < rounds; i++) {
      await new Promise(res => window.__jm.load(window.__empty, res));
      await sleep(300);
      performance.clearMarks();
      await new Promise(res => window.__jm.load(data, res));
      await sleep(400);
      out.push(phases());
    }
    return { file, ...census(), runs: out };
  };
  window.__phases = phases; window.__census = census;
})();
// Fire-and-forget sweep; poll window.__results / window.__done.
window.__runAll = function (files, rounds = 2) {
  window.__results = []; window.__done = false;
  (async () => {
    for (const f of files) {
      const r = await window.__rebuild(f, rounds);
      window.__results.push({ f: r.file, w: r.widgets, vcl: r.valueChangedListeners, runs: r.runs });
    }
    window.__done = true;
  })();
  return 'started';
};

window.__cache = window.__cache || {};
window.__preload = async function (files) {
  for (const f of files) window.__cache[f] = JSON.parse(await (await fetch('/' + f + '.js')).text());
  return Object.keys(window.__cache);
};

window.__empty = {version:'1.31.0', type:'session', content:{type:'root', id:'root', tabs:[], widgets:[]}};
