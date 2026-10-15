// Waits for the session build marker, settles, then runs the tab-switch bench only
// if the page is visible (rAF timings are meaningless in a hidden page).
(async () => {
  await new Promise(r => { const t = setInterval(() => { if (performance.getEntriesByName('b3').length) { clearInterval(t); r(); } }, 250); setTimeout(r, 30000); });
  await new Promise(r => setTimeout(r, 4000));
  // hidden switches are discarded per switch inside benchTabs
  eval(await (await fetch('/bench.js')).text());
  const b = window.__bench;
  const tabs = await b.benchTabs(['formuls1', 'formuls2', 'formuls3'], 5);
  const t = b.tabs(); b.root().setValue(t.findIndex(x => x.getProp('id') === 'formuls1'), { send: false, sync: true });
  await b.raf2(); await b.sleep(300);
  const canv = document.querySelectorAll('#osc-container canvas').length;
  return JSON.stringify({ session: document.title, vis: document.visibilityState, tabs, canvasesOnSynthTab: canv });
})();
