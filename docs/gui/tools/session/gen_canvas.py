"""Benchmark variants of the flattened session with bespoke canvas compounds.

  flat_cs.json   slider compounds (panel 'slider': 22 widgets incl. matrix children)
                 -> ONE canvas each
  flat_cq.json   + each 128-step sequencer panel (385 widgets) -> ONE canvas each

The canvases draw the same information the stacked widgets show (main value with
the synth-colour gradient, the four colour-coded mode values, seven status LEDs,
mod-source cells, label) and handle touch for whichever mode is active, read with
get() at touch time so they hold no linked props. Value layout of a slider canvas:
  [main, chaos, lfofreq, lfodepth, moddepth, modsrc, q, rev, act, tstep, tseq, ctrig, ctseq]
I/O is NOT wired to Pd: these exist to price build, tab-switch, draw and update cost.
"""
import json, os, sys, copy
S = os.path.dirname(os.path.abspath(__file__))

SLIDER_DRAW = r"""
var v = value, W = width, H = height;
var main = v[0] || 0;
// main value: gradient fill like the stacked fader (white -> synth colour)
var g = ctx.createLinearGradient(0, 0, W, 0);
g.addColorStop(0, cssVars.colorWidget); g.addColorStop(1, 'white');
ctx.globalAlpha = 0.5; ctx.fillStyle = g; ctx.fillRect(0, 0, W * main, H);
ctx.globalAlpha = 1; ctx.fillStyle = 'white'; ctx.fillRect(W * main - 2, 0, 4, H);
// mode values, colour coded; filled only while their mode is active
var cols = ['rgba(255,255,255,1)', 'rgba(41,95,189,1)', '#82a5e3', 'rgba(255,182,0,1)'];
var modes = ['chaos', 'lfofreq', 'lfodepth', 'mod'];
var n = (getProp('this', 'variables') || {}).n;
ctx.setLineDash([4, 4]); ctx.lineWidth = 3;
for (var i = 0; i < 4; i++) {
  var x = i === 3 ? (v[4] + 1) / 2 : v[i + 1];
  if (typeof x !== 'number') continue;
  var active = get(modes[i] + n);
  ctx.strokeStyle = cols[i];
  ctx.globalAlpha = active ? 1 : 0.6;
  ctx.beginPath(); ctx.moveTo(0, H * (0.2 + 0.15 * i)); ctx.lineTo(W * x, H * (0.2 + 0.15 * i)); ctx.stroke();
  if (active) { ctx.globalAlpha = 0.25; ctx.fillStyle = cols[i]; ctx.fillRect(0, 0, W * x, H); }
}
ctx.setLineDash([]); ctx.globalAlpha = 1;
// status LEDs at the same columns the seven buttons occupied
var leds = [[0.45, 0, 0.1, 1, 'rgba(255,119,0,1)'], [0.6, 0, 0.1, 1, '#ffb3d2'], [0.8, 0, 0.1, 1, 'rgba(193,0,79,1)'],
            [0.7, 0, 0.1, 0.5, '#ff4d97'], [0.7, 0.5, 0.1, 0.5, '#ff4d97'], [0.9, 0, 0.1, 0.5, 'white'], [0.9, 0.5, 0.1, 0.5, 'white']];
for (var k = 0; k < 7; k++) if (v[6 + k]) {
  var L = leds[k]; ctx.globalAlpha = 0.4; ctx.fillStyle = L[4]; ctx.fillRect(W * L[0], H * L[1], W * L[2], H * L[3]);
}
ctx.globalAlpha = 1;
// mod-source cells while mod mode is on
if (get('mod' + n)) {
  ctx.font = (H * 0.3) + 'px sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  for (var m = 0; m < 6; m++) {
    ctx.fillStyle = (v[5] === m + 1) ? 'rgba(255,182,0,0.6)' : 'rgba(255,182,0,0.15)';
    ctx.fillRect(W * (0.01 + m * 0.163), 0, W * 0.155, H);
    ctx.fillStyle = 'white'; ctx.fillText(String(m + 1), W * (0.01 + m * 0.163 + 0.078), H / 2);
  }
}
// label
ctx.fillStyle = cssVars.colorText; ctx.font = '150% sans-serif'; ctx.font = Math.round(H * 0.28) + 'px sans-serif';
ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText(String((getProp('this', 'variables') || {}).uilabel), W / 2, H / 2);
"""

SLIDER_TOUCH = r"""
if (event.type === 'start' || event.type === 'move') {
  var n = (getProp('this', 'variables') || {}).n;
  var slot = get('chaos' + n) ? 1 : get('lfofreq' + n) ? 2 : get('lfodepth' + n) ? 3 : get('mod' + n) ? 4 : 0;
  var x = Math.max(0, Math.min(1, event.offsetX / width));
  var v = value.slice();
  if (slot === 4 && event.type === 'start' && event.offsetY < height * 0.5) v[5] = 1 + Math.floor(x * 6);
  else v[slot] = slot === 4 ? x * 2 - 1 : x;
  set('this', v);
}
"""

SEQ_DRAW = r"""
var v = value, W = width, H = height, N = 128, len = (getProp('this', 'variables') || {}).len || 16;
var bw = W / 32, rows = 4, rh = H / rows;
for (var i = 0; i < N; i++) {
  var r = Math.floor(i / 32), c = i % 32, x = c * bw, y = r * rh;
  ctx.globalAlpha = i < len ? 1 : 0.15;
  ctx.fillStyle = 'rgba(255,255,255,0.08)'; ctx.fillRect(x + 1, y + 1, bw - 2, rh - 2);
  var h = (v[i] || 0) * (rh - 2);
  ctx.fillStyle = cssVars.colorWidget; ctx.fillRect(x + 1, y + rh - 1 - h, bw - 2, h);
  if (v[N + i]) { ctx.fillStyle = 'white'; ctx.fillRect(x + 1, y + 1, bw - 2, 4); }
}
ctx.globalAlpha = 1;
"""

SEQ_TOUCH = r"""
if (event.type === 'start' || event.type === 'move') {
  var bw = width / 32, rh = height / 4;
  var c = Math.floor(event.offsetX / bw), r = Math.floor(event.offsetY / rh);
  if (c >= 0 && c < 32 && r >= 0 && r < 4) {
    var i = r * 32 + c, v = value.slice();
    v[i] = Math.max(0, Math.min(1, 1 - (event.offsetY - r * rh) / rh));
    set('this', v);
  }
}
"""

def walk(w, fn, parent=None):
    fn(w, parent)
    for c in list(w.get('widgets') or []) + list(w.get('tabs') or []):
        walk(c, fn, w)

def vars_of(w):
    v = w.get('variables')
    if isinstance(v, str):
        try: v = json.loads(v)
        except Exception: v = {}
    return v if isinstance(v, dict) else {}

def slider_canvas(p):
    v = vars_of(p); name, n = v.get('name', 'x'), v.get('n', 0)
    return {
        'type': 'canvas', 'id': f'{name}{n}_c', 'address': f'/{name}{n}_c',
        'left': p.get('left', 0), 'top': p.get('top', 0), 'width': p.get('width', 'auto'), 'height': p.get('height', 'auto'),
        'expand': p.get('expand', False), 'css': p.get('css', ''), 'visible': p.get('visible', True),
        'interaction': p.get('interaction', True),
        'variables': json.dumps({'name': name, 'n': n, 'uilabel': v.get('uilabel', name)}),
        'valueLength': 13, 'default': [0.5, 0, 0, 0, 0, 0] + [0] * 7,
        # canvas has no `variables` prop: bake the instance constants into the scripts
        'onDraw': SLIDER_DRAW.replace("(getProp('this', 'variables') || {}).n", json.dumps(n)).replace("String((getProp('this', 'variables') || {}).uilabel)", json.dumps(str(v.get('uilabel', name)))),
        'onTouch': SLIDER_TOUCH.replace("(getProp('this', 'variables') || {}).n", json.dumps(n)),
    }

def seq_canvas(p, n):
    return {
        'type': 'canvas', 'id': f'seqsteps{n}_c', 'address': f'/seqsteps{n}_c',
        'left': p.get('left', 0), 'top': p.get('top', 0), 'width': p.get('width', 'auto'), 'height': p.get('height', 'auto'),
        'expand': p.get('expand', False), 'visible': p.get('visible', True),
        'variables': json.dumps({'n': n, 'len': 16}),
        'valueLength': 256, 'default': [0] * 256,
        'onDraw': SEQ_DRAW.replace("(getProp('this', 'variables') || {}).len", '16'), 'onTouch': SEQ_TOUCH,
    }

def build(src, out, do_slider, do_seq):
    d = json.load(open(os.path.join(S, src)))
    counts = {'slider': 0, 'seq': 0, 'removed': 0}
    def count(w):
        c = [0]; walk(w, lambda x, p: c.__setitem__(0, c[0] + 1)); return c[0]
    def fn(w, parent):
        kids = w.get('widgets') or []
        for i, c in enumerate(kids):
            if do_slider and c.get('type') == 'panel' and c.get('id') == 'slider':
                counts['removed'] += count(c); kids[i] = slider_canvas(c); counts['slider'] += 1
            elif do_seq and c.get('type') == 'panel' and c.get('id') == 'seqsteppanel':
                n = vars_of(c).get('n', vars_of(w).get('n', 0))
                counts['removed'] += count(c); kids[i] = seq_canvas(c, n); counts['seq'] += 1
    walk(d['content'], fn)
    json.dump(d, open(os.path.join(S, out), 'w'))
    json.dump(d, open(os.path.join(S, out + '.js'), 'w'))
    print(out, counts)

build('flat.json', 'flat_cs.json', True, False)
build('flat.json', 'flat_cq.json', True, True)
