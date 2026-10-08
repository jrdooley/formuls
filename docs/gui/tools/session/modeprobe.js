// Arms a probe: records every long task and the total main-thread time of the
// websocket message handler until __modeRead() is called. Send the OSC message
// from outside after arming.
(function () {
  const tasks = []; window.__modeTasks = tasks;
  try { new PerformanceObserver(l => { for (const e of l.getEntries()) tasks.push(Math.round(e.duration)); }).observe({ type: 'longtask' }); } catch (e) {}
  // count widgets re-created: widget-created events after arming
  let created = 0; window.__wm.on('widget-created', () => created++, { context: window });
  const t0 = performance.now();
  window.__modeRead = () => ({ longtasks: tasks.slice(), created, sinceArm: Math.round(performance.now() - t0) });
})();
