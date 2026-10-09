// formuls slider compound, drawn and touched as ONE canvas widget.
//
// Installed once into `globals.FC` by the root's onCreate (src/tools/compile-compounds.py
// inlines this file there); every compound canvas calls it from tiny per-instance
// onDraw / onTouch scripts that pass their own constants (M) and their own script API.
//
// It reproduces the 15 stacked widgets the authored `slider` template builds:
//   7 status LEDs (buttons), the main fader, chaos / lfofreq / lfodepth faders,
//   the 6-cell mod-source matrix, the 1..6 mod label (switch), the moddepth fader
//   and the label button -- same geometry, same drawing algorithm as o-s-c 1.31's
//   compact fader, and the same touch semantics: the topmost *interactive* layer
//   takes the touch, snap-then-relative dragging, sends rounded to `decimals` (2)
//   and suppressed when unchanged, double tap (375 ms / 20 px) resets to default,
//   toggle cells with o-s-c's traversing rule, a tap-mode label.
//
// Value layout (20 slots):
//   0 main  1 chaos  2 lfofreq  3 lfodepth  4 moddepth  5..10 mod cells 0..5
//   11 ymodlabel  12..18 LEDs quantise reverse act trigstep trigseq chaostrig chaostrigseq
//   19 label
(function () {
  var SLOT = { main: 0, chaos: 1, lfofreq: 2, lfodepth: 3, moddepth: 4, mod: 5, ymod: 11, led: 12, label: 19 };

  // Fader layers, bottom to top, as the template stacks them. gp = o-s-c gaugePadding
  // (padding + lineWidth), A = knobSize; mode = which mode input gates it.
  var FADERS = [
    { slot: 0, mode: null,       gp: 5,  A: 10, min: 0,  max: 1, origin: 0, dashed: false, color: null,                fillAlpha: 0.5, stroke: true,  opacity: 1 },
    { slot: 1, mode: 'chaos',    gp: 10, A: 5,  min: 0,  max: 1, origin: 0, dashed: true,  color: 'rgba(255,255,255,1)', fillAlpha: null, stroke: false, opacity: 1 },
    { slot: 2, mode: 'lfofreq',  gp: 10, A: 5,  min: 0,  max: 1, origin: 0, dashed: true,  color: 'rgba(41,95,189,1)',   fillAlpha: null, stroke: false, opacity: 1 },
    { slot: 3, mode: 'lfodepth', gp: 10, A: 5,  min: 0,  max: 1, origin: 0, dashed: true,  color: '#82a5e3',             fillAlpha: null, stroke: false, opacity: 1 },
    { slot: 4, mode: 'moddepth', gp: 10, A: 5,  min: -1, max: 1, origin: 0, dashed: true,  color: 'rgba(255,182,0,1)',   fillAlpha: null, stroke: false, opacity: 0.3 },
  ];
  // LEDs: [left, top, width, height] as fractions of the compound, colour
  var LEDS = [
    [0.45, 0, 0.1, 1, 'rgba(255,119,0,1)'], [0.60, 0, 0.1, 1, '#ffb3d2'], [0.80, 0, 0.1, 1, 'rgba(193,0,79,1)'],
    [0.70, 0, 0.1, 0.5, '#ff4d97'], [0.70, 0.5, 0.1, 0.5, '#ff4d97'], [0.90, 0, 0.1, 0.5, 'rgba(255,255,255,1)'],
    [0.90, 0.5, 0.1, 0.5, 'rgba(255,255,255,1)'],
  ];
  // matrix cells are o-s-c buttons: a 1px border at alphaStroke .5 (::before) and a fill inset
  // 1px at alphaFillOff .15 / alphaFillOn .75 (::after), inside a container at opacity .4
  var MOD = { left: 0.01, width: 0.98, cells: 6, color: 'rgba(255,182,0,1)', stroke: 0.5, off: 0.15, on: 0.75, opacity: 0.4 };
  var DOUBLE_TAP_MS = 375, DOUBLE_TAP_PX = 20;

  function num(v) { var x = parseFloat(v); return isNaN(x) ? (v === true ? 1 : 0) : x; }
  function active(api, id) { return id ? num(api.get(id)) > 0 || api.get(id) === true : false; }
  function round2(x) { return Math.round(x * 100) / 100; }
  function clip(x, a, b) { return Math.max(a, Math.min(b, x)); }
  function pct(f, v) { return (clip(v, f.min, f.max) - f.min) / (f.max - f.min); }

  // o-s-c 1.31 compact fader, horizontal, drawn straight into compound coordinates.
  function drawFader(ctx, W, H, f, value, fill, cs, fillAlpha) {
    var L = W, T = H, gp = f.gp, A = f.A;
    var coord = function (p) { return gp + clip(p, 0, 1) * (L - 2 * gp); };
    var h = coord(pct(f, value)), o = coord(pct(f, f.origin));
    ctx.save();
    ctx.globalAlpha = 1;
    if (fillAlpha) {                                       // the gauge fill
      ctx.globalAlpha = fillAlpha * f.opacity;
      ctx.strokeStyle = fill;
      ctx.lineWidth = Math.round(T - 2 * gp);
      if (f.dashed) ctx.setLineDash([1, 1]);
      ctx.beginPath(); ctx.moveTo(o, T / 2); ctx.lineTo(h, T / 2); ctx.stroke();
      ctx.setLineDash([]);
    }
    ctx.globalAlpha = 1 * f.opacity;                       // the knob, always drawn
    ctx.fillStyle = fill;
    var k = gp + pct(f, value) * (L - 2 * gp - A);
    ctx.fillRect(k, gp, A, T - 2 * gp);
    if (f.stroke) {                                        // outline (main fader only)
      var lw = 5;
      ctx.globalAlpha = 1; ctx.strokeStyle = validColor(ctx, cs.colorStroke) || fill; ctx.lineWidth = lw;
      ctx.strokeRect(lw / 2, lw / 2, W - lw, H - lw);
    }
    ctx.restore();
  }


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
    cacheRect(ctx, locals, W);
    ctx.clearRect(0, 0, W, H);
    var i, f;
    // LEDs (bottom layer)
    for (i = 0; i < 7; i++) if (num(v[SLOT.led + i])) {
      var L = LEDS[i];
      ctx.globalAlpha = 0.4; ctx.fillStyle = L[4];
      ctx.fillRect(W * L[0], H * L[1], W * L[2], H * L[3]);
    }
    ctx.globalAlpha = 1;
    // faders, in stacking order; moddepth sits above the matrix and the mod label
    for (i = 0; i < 4; i++) {
      f = FADERS[i];
      var fillAlpha = f.mode ? (active(api, M.modes[f.mode]) ? 1 : 0) : f.fillAlpha;
      drawFader(ctx, W, H, f, num(v[f.slot]), f.color || validColor(ctx, cs.colorFill) || validColor(ctx, cs.colorWidget) || '#6db5fd', cs, fillAlpha);
    }
    // mod-source matrix: 6 toggle cells
    var mx = W * MOD.left, mw = W * MOD.width / MOD.cells;
    // the matrix container itself has the panel background at the matrix's opacity: it
    // dims every layer below it across its width -- where that background is a real
    // colour. Under fxa/fxb it is not, and the original matrix is transparent there.
    var bg = validColor(ctx, cs.colorBg);
    if (bg) { ctx.globalAlpha = MOD.opacity; ctx.fillStyle = bg; ctx.fillRect(mx, 0, W * MOD.width, H); }
    ctx.fillStyle = MOD.color; ctx.strokeStyle = MOD.color; ctx.lineWidth = 1;
    for (i = 0; i < MOD.cells; i++) {
      ctx.globalAlpha = MOD.opacity * (num(v[SLOT.mod + i]) ? MOD.on : MOD.off);
      ctx.fillRect(mx + i * mw + 1, 1, mw - 2, H - 2);
      ctx.globalAlpha = MOD.opacity * MOD.stroke;
      ctx.strokeRect(mx + i * mw + 0.5, 0.5, mw - 1, H - 1);
    }
    // mod label: digits 1..6, faint
    ctx.globalAlpha = 0.3; ctx.fillStyle = 'rgba(255,255,255,0.58)';
    ctx.font = '500 ' + M.digitFont + ' Roboto, sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    for (i = 0; i < 6; i++) ctx.fillText(String(i + 1), W * (i + 0.5) / 6, H / 2 + 1);   // +1: DOM text centring
    // moddepth fader
    f = FADERS[4];
    drawFader(ctx, W, H, f, num(v[f.slot]), f.color, cs, active(api, M.modes.moddepth) ? 1 : 0);
    // label button
    ctx.globalAlpha = 1;
    // the label is a tap-mode button: it never shows a held state, whatever its value
    ctx.fillStyle = 'rgba(255,255,255,1)';                         // the template panel's colorText
    ctx.font = '500 ' + M.labelFont + ' Roboto, sans-serif';
    if ('letterSpacing' in ctx) ctx.letterSpacing = '1px';
    ctx.fillText(M.label, W / 2, H / 2 + 1);
    if ('letterSpacing' in ctx) ctx.letterSpacing = '0px';
  }

  // Which layer takes a touch at (x, y): the topmost interactive one, as stacking does.
  function layerAt(x, y, W, M, api) {
    if (active(api, M.modes.sel)) return { kind: 'label' };
    if (active(api, M.modes.moddepth)) return { kind: 'fader', f: FADERS[4] };
    if (active(api, M.modes.mod) && x >= W * MOD.left && x < W * (MOD.left + MOD.width))
      return { kind: 'mod' };
    if (active(api, M.modes.lfodepth)) return { kind: 'fader', f: FADERS[3] };
    if (active(api, M.modes.lfofreq)) return { kind: 'fader', f: FADERS[2] };
    if (active(api, M.modes.chaos)) return { kind: 'fader', f: FADERS[1] };
    return { kind: 'fader', f: FADERS[0] };
  }

  function cellAt(x, W) {
    var i = Math.floor((x - W * MOD.left) / (W * MOD.width / MOD.cells));
    return i >= 0 && i < MOD.cells ? i : -1;
  }

  // Set one slot locally (no packed send) and send the sub-widget's own message.
  // A widget elsewhere on the same address (M.mirror, from the compiler) is set too,
  // as o-s-c kept same-id widgets in step on one tablet.
  function emit(api, value, slot, x, M, sendIt) {
    var v = value.slice(); v[slot] = x;
    api.set('this', v, { send: false });
    var mir = M.mirror && M.mirror[slot];
    for (var i = 0; mir && i < mir.length; i++) api.set(mir[i], x, { send: false });
    if (sendIt) api.send(M.a[slot], round2(x));
    return v;
  }

  function faderTo(api, value, f, x, M, dragged) {
    var old = num(value[f.slot]), nx = clip(x, f.min, f.max);
    // o-s-c: when dragged, a value equal at `decimals` is not sent
    var same = old.toFixed(2) === nx.toFixed(2);
    return emit(api, value, f.slot, nx, M, !(dragged && same));
  }

  function touch(event, value, W, H, M, locals, api) {
    var id = event.pointerId || 0, t = locals.touches || (locals.touches = {});
    var x = event.offsetX, y = event.offsetY, now = Date.now();
    // cells are chosen where the browser's hit test would land
    var hx = hitPoint(event, locals)[0];
    if (event.type === 'start') {
      var layer = layerAt(hx, y, W, M, api);
      var st = t[id] = { layer: layer };
      if (layer.kind === 'label') {
        api.send(M.a[SLOT.label], 1);                                // tap mode: sends its on value only
        return;
      }
      if (layer.kind === 'mod') {
        var c = cellAt(hx, W); st.first = c >= 0 ? num(value[SLOT.mod + c]) : null; st.cells = {};
        if (c >= 0) { st.cells[c] = 1; emit(api, value, SLOT.mod + c, st.first ? 0 : 1, M, true); }
        return;
      }
      var f = layer.f;
      // snap to the touch, as the fader's draginit does with snap: true
      st.percent = (x - f.gp) / (W - 2 * f.gp) * 100;               // unclipped, as o-s-c keeps it
      var nv = faderTo(api, value, f, f.min + st.percent / 100 * (f.max - f.min), M, true);
      // double tap on the same layer: reset to its default after the snap
      var last = locals.lastTap;
      if (last && now - last.t < DOUBLE_TAP_MS && Math.abs(last.x - event.pageX) < DOUBLE_TAP_PX &&
          Math.abs(last.y - event.pageY) < DOUBLE_TAP_PX && last.slot === f.slot) {
        locals.lastTap = null;
        // o-s-c resets to the layer's default (0 for all five) with fromLocal, which
        // also suppresses an unchanged value; a drag that continues starts from there
        faderTo(api, nv, f, 0, M, true);
        st.percent = pct(f, 0) * 100;
      } else {
        locals.lastTap = { t: now, x: event.pageX, y: event.pageY, slot: f.slot };
      }
      return;
    }
    var s = t[id];
    if (!s) return;
    if (event.type === 'move') {
      if (s.layer.kind === 'mod') {
        var cc = cellAt(hx, W);
        if (cc >= 0 && !s.cells[cc]) {                              // traversing rule
          s.cells[cc] = 1;
          if (num(value[SLOT.mod + cc]) === s.first) emit(api, value, SLOT.mod + cc, s.first ? 0 : 1, M, true);
        }
        return;
      }
      if (s.layer.kind !== 'fader') return;
      var ff = s.layer.f, inertia = event.ctrlKey ? 10 : 1;
      // accumulates unclipped: only the value is clipped (percentToValue)
      s.percent = s.percent + event.movementX / (W - 2 * (ff.gp + ff.A / 2)) * 100 / inertia;
      faderTo(api, value, ff, ff.min + s.percent / 100 * (ff.max - ff.min), M, true);
      return;
    }
    if (event.type === 'stop') delete t[id];
  }

  // Inbound patch from the mailbox: [canvasId, slot, value, slot, value, ...], or
  // ['@var', canvasId, name, index, value] for state that is not the widget's value
  // (the sequencer's per-step dimming), followed by a redraw.
  function patch(api, args) {
    if (args[0] === '@var') {
      var o = api.getVar(args[1], args[2]) || {};
      for (var j = 3; j + 1 < args.length; j += 2) o[args[j]] = args[j + 1];
      api.setVar(args[1], args[2], o);
      api.updateCanvas(args[1]);
      return;
    }
    var c = args[0], v = api.get(c);
    if (!v || !v.length) return;
    v = v.slice();
    for (var i = 1; i + 1 < args.length; i += 2) v[args[i]] = args[i + 1];
    api.set(c, v, { send: false });
  }

  globals.FC = { draw: draw, touch: touch, patch: patch, SLOT: SLOT };
})();
