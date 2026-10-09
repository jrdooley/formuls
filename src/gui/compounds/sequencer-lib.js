// formuls 128-step sequencer panel, drawn and touched as ONE canvas widget.
//
// Installed once into `globals.FS` by the root's onCreate (compile-compounds.py
// inlines it). It reproduces the `seqsteppanel` grid -- 8 columns x 16 rows of step
// cells, each a toggle button (seqwriteN-i) under a quantised fader (seqwriteoffN-i)
// -- with o-s-c 1.31's geometry, drawing and touch semantics:
//
//   - the fader layer takes touches only while the quantise button (quantiseseqN) is
//     held; otherwise the button layer does
//   - the panel's `traversing: smart`: a drag affects only widgets of the type it
//     started on; buttons toggle only if their state equals the first one's; faders in
//     a traversing container always snap, and a fader entered during a drag takes its
//     position from the pointer relative to the *first* fader (so it usually clips)
//   - faders: 10 steps, relative drag, unchanged-value suppression compared *before*
//     step-quantising, double tap resets to 0
//   - each button's opacity follows /aseqwriteN-i (the dimmed steps beyond the
//     sequence length); a dimmed step is still touchable, as today
//
// Value layout (256): 0..127 step toggles, 128..255 step fader values.
// Dimming is not state today (an OSC{} receiver), so it lives in the widget variable
// `op`, written by the mailbox: getVar('this', 'op')[i], default M.op0[i].
(function () {
  var COLS = 8, ROWS = 16, N = 128, OFF = 128;
  var F = { gp: 3, A: 10, steps: 10 };
  var DOUBLE_TAP_MS = 375, DOUBLE_TAP_PX = 20;

  function num(v) { var x = parseFloat(v); return isNaN(x) ? (v === true ? 1 : 0) : x; }
  function clip(x, a, b) { return Math.max(a, Math.min(b, x)); }
  function round2(x) { return Math.round(x * 100) / 100; }
  // o-s-c Slider.setSteps / setValue: steps = u/(n-1), nearest wins, an exact tie goes
  // to the FIRST (lower) step (indexOf(Math.min(...)))
  var STEPS = []; for (var u = 0; u < F.steps; u++) STEPS.push(u / (F.steps - 1) * (1 - 0) + 0);
  function quant(x) {
    var v = clip(x, 0, 1), d = STEPS.map(function (s) { return Math.abs(s - v); });
    return STEPS[d.indexOf(Math.min.apply(null, d))];
  }
  function cell(W, H, i) {
    var cw = W / COLS, ch = H / ROWS, c = i % COLS, r = Math.floor(i / COLS);
    // o-s-c sizes each fader canvas with parseInt of its CSS size
    return { x: c * cw, y: r * ch, w: cw, h: ch, fw: Math.floor(cw), fh: Math.floor(ch) };
  }
  function cellAt(W, H, x, y) {
    var c = Math.floor(x / (W / COLS)), r = Math.floor(y / (H / ROWS));
    return c >= 0 && c < COLS && r >= 0 && r < ROWS ? r * COLS + c : -1;
  }
  function active(api, id) { var v = api.get(id); return num(v) > 0 || v === true; }


  // Where the browser's hit test lands, in canvas CSS pixels. o-s-c traverses by the
  // pointer event's target; the browser rounds the pointer to a whole pixel, and the
  // canvas' event.offsetX is floored, so neither offsetX nor pageX - offsetX will do:
  // use the canvas' real edge, cached at draw time, unless it has moved (a scrolled
  // panel), in which case fall back to the event's own estimate.

  // A colour the canvas really accepts, or null. o-s-c can hand a canvas an unusable
  // colour -- under fxa/fxb, whose colorBg is "@{this}", cssVars.colorBg is "-1" -- and
  // an invalid fillStyle is silently ignored, so the previous fill would be reused.
  function validColor(ctx, c) {
    if (!c || typeof c !== 'string') return null;
    var prev = ctx.fillStyle; ctx.fillStyle = '#010203'; ctx.fillStyle = c;
    var ok = ctx.fillStyle !== '#010203' || c.replace(/\s/g, '').toLowerCase() === '#010203';
    ctx.fillStyle = prev;
    return ok ? c : null;
  }

  function hitPoint(event, locals) {
    var ex = event.pageX - event.offsetX, ey = event.pageY - event.offsetY;
    var lx = (locals.left != null && Math.abs(ex - locals.left) < 1) ? locals.left : ex;
    var ly = (locals.top != null && Math.abs(ey - locals.top) < 1) ? locals.top : ey;
    return [Math.round(event.pageX) - lx, Math.round(event.pageY) - ly];
  }
  function cacheRect(ctx, locals, W) {
    if (!locals) return;
    var r = ctx.canvas.getBoundingClientRect();
    locals.left = r.left; locals.top = r.top; locals.cssW = r.width; locals.cssH = r.height; locals.pw = W;
  }

  function draw(ctx, W, H, v, cs, M, api, locals) {
    cacheRect(ctx, locals, W);                             // for touch hit-testing
    var bg = validColor(ctx, cs.colorBg);
    // the step labels inherit the text colour (white here); cssVars.colorText does not
    // (it resolves to the synth colour), so read what CSS actually inherits
    if (locals && !locals.textColor) locals.textColor = ctx.canvas.ownerDocument.defaultView.getComputedStyle(ctx.canvas).color;
    var text = (locals && validColor(ctx, locals.textColor)) || validColor(ctx, cs.colorText) || 'rgb(216,222,233)';
    var fill = validColor(ctx, cs.colorFill) || validColor(ctx, cs.colorWidget) || 'rgb(109,181,253)';
    var stroke = validColor(ctx, cs.colorStroke) || fill;
    ctx.clearRect(0, 0, W, H);
    var op = api.getVar('this', 'op') || {};
    ctx.font = '500 11px Roboto, sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    if ('letterSpacing' in ctx) ctx.letterSpacing = '1px';
    for (var i = 0; i < N; i++) {
      var c = cell(W, H, i), on = num(v[i]), o = (i in op) ? num(op[i]) : M.op0[i];
      if (bg) { ctx.globalAlpha = 1; ctx.fillStyle = bg; ctx.fillRect(c.x, c.y, c.w, c.h); }   // step panel
      if (o > 0) {                                                            // the button
        ctx.globalAlpha = o * (on ? 0.75 : 0.15); ctx.fillStyle = fill;
        ctx.fillRect(c.x + 3, c.y + 3, c.w - 6, c.h - 6);
        ctx.globalAlpha = o * 0.5; ctx.strokeStyle = stroke; ctx.lineWidth = 3;
        ctx.strokeRect(c.x + 1.5, c.y + 1.5, c.w - 3, c.h - 3);
        ctx.globalAlpha = o; ctx.fillStyle = on ? (bg || 'rgb(33,37,43)') : text;   // colorTextOn auto = background
        ctx.fillText(String(i + 1), c.x + c.w / 2, c.y + c.h / 2 + 1);
      }
      ctx.globalAlpha = 1; ctx.fillStyle = 'rgba(0,0,0,1)';                   // the fader's knob
      var p = clip(num(v[OFF + i]), 0, 1);
      ctx.fillRect(c.x + F.gp + p * (c.fw - 2 * F.gp - F.A), c.y + F.gp, F.A, c.fh - 2 * F.gp);
    }
    if ('letterSpacing' in ctx) ctx.letterSpacing = '0px';
  }

  function setSlot(api, value, slot, x) {
    var v = value.slice(); v[slot] = x; api.set('this', v, { send: false }); return v;
  }

  // o-s-c Slider.setValue for a 10-step fader driven by a drag
  function faderTo(api, st, value, i, raw, M) {
    var cur = num(value[OFF + i]), u = clip(raw, 0, 1);
    var send = cur.toFixed(2) !== u.toFixed(2);                // compared before quantising
    var q = quant(u);
    st.value = setSlot(api, st.value || value, OFF + i, q);
    if (send) api.send(M.w[i], round2(q));
  }

  function touch(event, value, W, H, M, locals, api) {
    // offsetX/Y are CSS pixels and the grid's cells are fractional (770.4 / 8), while
    // width/height are the canvas' integer size: hit-test in the CSS size onDraw saw.
    W = locals.cssW || W; H = locals.cssH || H;
    var id = event.pointerId || 0, t = locals.touches || (locals.touches = {});
    var x = event.offsetX, y = event.offsetY;
    // cells are chosen where the browser's hit test lands (292.5 hits the cell starting
    // at 292.75); values still use offsetX, as o-s-c does
    var hp = hitPoint(event, locals), hx = hp[0], hy = hp[1];
    if (event.type === 'start') {
      var i = cellAt(W, H, hx, hy);
      if (i < 0) return;
      var faders = active(api, M.q), c = cell(W, H, i);
      var st = t[id] = { kind: faders ? 'fader' : 'button', first: i, cur: i, seen: {}, value: value };
      st.seen[i] = 1;
      if (!faders) {
        st.state = num(value[i]);
        st.value = setSlot(api, value, i, st.state ? 0 : 1);
        api.send(M.b[i], st.state ? 0 : 1);
        return;
      }
      st.fx = c.x; st.fw = c.fw;                               // the first fader's canvas
      st.percent = (x - c.x - F.gp) / (c.fw - 2 * F.gp);
      faderTo(api, st, value, i, st.percent, M);
      var last = locals.lastTap, now = Date.now();
      if (last && last.i === i && now - last.t < DOUBLE_TAP_MS &&
          Math.abs(last.x - event.pageX) < DOUBLE_TAP_PX && Math.abs(last.y - event.pageY) < DOUBLE_TAP_PX) {
        locals.lastTap = null;
        faderTo(api, st, st.value, i, 0, M);                   // double tap: back to 0
        st.percent = 0;
      } else locals.lastTap = { t: now, i: i, x: event.pageX, y: event.pageY };
      return;
    }
    var s = t[id];
    if (!s) return;
    if (event.type === 'stop') { delete t[id]; return; }
    var j = cellAt(W, H, hx, hy);
    if (j >= 0 && j !== s.cur && !s.seen[j]) {                 // traversing into a new cell
      s.seen[j] = 1; s.cur = j;
      if (s.kind === 'button') {
        if (num(s.value[j]) === s.state) {
          s.value = setSlot(api, s.value, j, s.state ? 0 : 1);
          api.send(M.b[j], s.state ? 0 : 1);
        }
      } else {
        s.percent = (x - s.fx - F.gp) / (s.fw - 2 * F.gp);     // relative to the first fader
        faderTo(api, s, s.value, j, s.percent, M);
      }
      return;
    }
    if (s.kind === 'fader' && j === s.cur) {                   // relative drag of the current fader
      var inertia = event.ctrlKey ? 10 : 1;
      s.percent = s.percent + event.movementX / (s.fw - 2 * (F.gp + F.A / 2)) / inertia;   // unclipped, as o-s-c
      faderTo(api, s, s.value, s.cur, s.percent, M);
    }
  }

  globals.FS = { draw: draw, touch: touch };
})();
