// check-gestures.mjs -- do the canvas compounds send exactly what o-s-c's own widgets send?
//
// The build replaces each slider compound and each 128-step sequencer panel with one
// canvas whose script reproduces o-s-c's widget behaviour (snap, relative drag, double
// tap, traversing, hit-testing). Nothing checks that copy by itself, so this does: it
// builds two one-compound sessions from src/gui/_main.json -- one with the native
// widgets, one compiled to a canvas -- serves each with the vendored o-s-c, drives the
// same real mouse gestures in headless Chrome, records what each sends to "Pd" over
// UDP, and compares the two, gesture by gesture. Run it after changing a compound
// library, the compiler or the module, and after any Open Stage Control upgrade.
//
// usage: <o-s-c node> src/tools/check-gestures.mjs [--osc-dir DIR] [--chrome PATH]
//                                                  [--port N] [--keep] [--verbose]
//
//   --osc-dir  a directory holding `node` and `open-stage-control` (default: build/gui,
//              else the newest formuls-*.app bundle in the repo root)
//   --chrome   Chrome or Chromium (default: /Applications/Google Chrome.app)
//   --port     first of four ports used: two servers, two UDP recorders (default 9061;
//              never 9000/9001, the app's own)
//
// Needs node 22+ (built-in WebSocket); the vendored o-s-c node is one. No npm packages.
// Exit status: 0 identical, 1 a difference, 2 the harness itself failed.

import { spawn, execFileSync } from 'node:child_process'
import dgram from 'node:dgram'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..')
const argv = process.argv.slice(2)
const opt = (name, dflt) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : dflt }
const flag = (name) => argv.includes(name)
const VERBOSE = flag('--verbose')
const BASE_PORT = parseInt(opt('--port', '9061'))
const VIEW = { width: 778, height: 457 }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const log = (...a) => console.log('check-gestures:', ...a)

function die(msg) { console.error('check-gestures: ERROR: ' + msg); cleanup(); process.exit(2) }

// ---------------------------------------------------------------- the gestures
// Coordinates are fractions of the compound's own rectangle, so both variants get the
// same pixels. `osc` lines are what Pd would send to the server first (mode switches).
const cell = (c, r) => [(c + 0.5) / 8, (r + 0.5) / 16]
const SLIDER = [
  { name: 'tap', act: [['tap', 0.5, 0.5]] },
  { name: 'snap and drag', act: [['drag', 0.257, 0.5, 0.771, 0.5]] },
  { name: 'double tap', act: [['dbl', 0.643, 0.5]] },
  { name: 'drag back past the end', act: [['drag', 0.6, 0.5, -0.2, 0.5], ['drag', 0.3, 0.5, 0.5, 0.5]] },
  { name: 'chaos layer', osc: [['/chaos1', 1]], act: [['drag', 0.257, 0.5, 0.771, 0.5]], after: [['/chaos1', 0]] },
  { name: 'LFO freq layer', osc: [['/lfofreq1', 1]], act: [['drag', 0.386, 0.5, 0.643, 0.5]], after: [['/lfofreq1', 0]] },
  { name: 'LFO depth layer', osc: [['/lfodepth1', 1]], act: [['drag', 0.771, 0.5, 0.45, 0.5]], after: [['/lfodepth1', 0]] },
  { name: 'mod depth layer', osc: [['/moddepth1', 1]], act: [['drag', 0.257, 0.5, 0.835, 0.5]], after: [['/moddepth1', 0]] },
  { name: 'mod cells', osc: [['/mod1', 1]],
    act: [['tap', 0.416, 0.5], ['tap', 0.416, 0.5], ['tap', 0.746, 0.5], ['drag', 0.09, 0.5, 0.578, 0.5]] },
  // the cells are now mixed: a drag across all of them must toggle each one it enters
  { name: 'mod cells: drag across mixed states', act: [['drag', 0.95, 0.5, 0.05, 0.5]], after: [['/mod1', 0]] },
  { name: 'parameter select', osc: [['/modeselector1', 1]], act: [['tap', 0.5, 0.5]], after: [['/modeselector1', 0]] },
]
const SEQ = [
  { name: 'tap a step twice', act: [['tap', ...cell(0, 0)], ['tap', ...cell(0, 0)]] },
  { name: 'drag along a row', act: [['drag', ...cell(0, 1), ...cell(6, 1)]] },
  { name: 'drag down a column', act: [['drag', ...cell(2, 3), ...cell(2, 8)]] },
  // row 1 is set except its last step: starting there, a drag must skip the set ones
  { name: 'drag from an unset step across set ones', act: [['drag', ...cell(7, 1), ...cell(0, 1)]] },
  { name: 'drag from a set step across unset ones', act: [['drag', ...cell(2, 5), ...cell(6, 5)]] },
  { name: 'quantised: drag a step fader', osc: [['/quantiseseq1', 1]], act: [['drag', 1.1 / 8, 2.5 / 16, 1.9 / 8, 2.5 / 16]] },
  { name: 'quantised: tap a step fader', act: [['tap', ...cell(3, 2)]] },
  { name: 'quantised: double tap a step fader', act: [['dbl', ...cell(4, 2)]] },
  { name: 'quantised: drag across step faders', act: [['drag', 0.2 / 8, 4.5 / 16, 5.4 / 8, 4.5 / 16]], after: [['/quantiseseq1', 0]] },
]

// ---------------------------------------------------------------- sessions
function oscDir() {
  const d = opt('--osc-dir')
  const cands = d ? [d] : [path.join(ROOT, 'build/gui'),
    ...fs.readdirSync(ROOT).filter((f) => /^formuls-.*\.app$/.test(f)).sort().reverse()
      .map((f) => path.join(ROOT, f, 'Contents/Resources/gui'))]
  for (const c of cands) if (fs.existsSync(path.join(c, 'node')) && fs.existsSync(path.join(c, 'open-stage-control'))) return c
  die('no o-s-c found (build the app first, or pass --osc-dir)')
}

function* walk(w) { yield w; for (const c of [...(w.widgets || []), ...(w.tabs || [])]) yield* walk(c) }
const find = (root, pred) => { for (const w of walk(root)) if (pred(w)) return w; return null }
const clone = (x) => JSON.parse(JSON.stringify(x))

function session(version, widgets) {
  return { version, type: 'session', content: { type: 'root', id: 'root', tabs: [
    { type: 'tab', id: 'formuls1', label: 'one', widgets, tabs: [] }] } }
}

function buildSessions(dir, osc) {
  const py = (script, args) => execFileSync('python3', [path.join(ROOT, 'src/tools', script), ...args], { stdio: VERBOSE ? 'inherit' : 'pipe' })
  const flat = path.join(dir, 'flat.json')
  py('flatten-session.py', [path.join(ROOT, 'src/gui/_main.json'), flat,
    '--defaults', path.join(ROOT, 'src/tools/osc-defaults-1.31.0.json'), '--osc-package', path.join(osc, 'open-stage-control'),
    '--template-tab', 'formuls0', '--template-tab', 'Widgets'])
  const d = JSON.parse(fs.readFileSync(flat, 'utf8'))
  const full = { left: 0, top: 0, width: '100%', height: '100%', expand: false }
  const hidden = (id) => { const w = clone(find(d.content, (x) => x.id === id && x.type !== 'panel')); if (!w) die(`no ${id}`); return Object.assign(w, { visible: false, left: 0, top: 0, width: 1, height: 1 }) }

  const comp = clone(find(d.content, (w) => w.id === 'slider' && (w.widgets || []).some((c) => c.id === 'attack1')))
  Object.assign(comp, full, { height: '40%' })
  const modes = ['lfofreq1', 'lfodepth1', 'chaos1', 'mod1', 'moddepth1', 'modeselector1'].map(hidden)
  const seq = clone(find(d.content, (w) => w.id === 'seqsteppanel' && (w.widgets || []).some((c) => (c.widgets || []).some((g) => g.id === 'seqwrite1-0'))))
  Object.assign(seq, full)
  const out = {}
  for (const [name, widgets] of [['slider', [comp, ...modes]], ['seq', [seq, hidden('quantiseseq1')]]]) {
    const native = path.join(dir, `${name}-native.json`)
    fs.writeFileSync(native, JSON.stringify(session(d.version, widgets)))
    const cdir = path.join(dir, `${name}-canvas`); fs.mkdirSync(cdir)
    fs.writeFileSync(path.join(dir, 'empty.state'), '{}')
    py('compile-compounds.py', [native, path.join(dir, 'empty.state'), path.join(cdir, '_main.json'),
      path.join(cdir, 'x.state'), path.join(cdir, 'compounds.json'),
      '--lib', path.join(ROOT, 'src/gui/compounds/slider-lib.js'), '--seq-lib', path.join(ROOT, 'src/gui/compounds/sequencer-lib.js')])
    fs.copyFileSync(path.join(ROOT, 'src/gui/formuls-module.js'), path.join(cdir, 'formuls-module.js'))
    out[name] = { native: { session: native }, canvas: { session: path.join(cdir, '_main.json'), module: path.join(cdir, 'formuls-module.js') } }
  }
  return out
}

// ---------------------------------------------------------------- processes
const children = []
let tmpRoot = null
function cleanup() {
  for (const c of children) try { c.kill('SIGKILL') } catch (e) {}
  if (tmpRoot && !flag('--keep')) fs.rmSync(tmpRoot, { recursive: true, force: true })
}
process.on('exit', cleanup)
process.on('SIGINT', () => { cleanup(); process.exit(2) })

async function startServer(osc, port, udp, variant) {
  const args = [path.join(osc, 'open-stage-control'), '--port', String(port), '--send', `127.0.0.1:${udp}`,
    '--load', variant.session, '--read-only', '--no-qrcode', '--client-options', 'framerate=25', 'hdpi=0']
  if (variant.module) args.push('--custom-module', variant.module)
  const p = spawn(path.join(osc, 'node'), args, { stdio: ['ignore', 'pipe', 'pipe'] })
  children.push(p)
  let outText = ''
  p.stdout.on('data', (b) => { outText += b }); p.stderr.on('data', (b) => { outText += b })
  for (let i = 0; i < 100; i++) {
    if (/Server started/.test(outText)) return p
    if (p.exitCode !== null) break
    await sleep(100)
  }
  die(`o-s-c on ${port} did not start:\n${outText}`)
}

function recorder(port) {
  const msgs = []
  const sock = dgram.createSocket('udp4')
  const str = (b, i) => { const j = b.indexOf(0, i); return [b.toString('utf8', i, j), (j + 4) & ~3] }
  const decode = (b) => {
    if (b.toString('utf8', 0, 7) === '#bundle') {
      for (let i = 16; i < b.length;) { const n = b.readInt32BE(i); decode(b.subarray(i + 4, i + 4 + n)); i += 4 + n }
      return
    }
    let [address, i] = str(b, 0); let tags; [tags, i] = str(b, i)
    const args = []
    for (const t of tags.slice(1)) {
      if (t === 'f') { args.push(+b.readFloatBE(i).toFixed(6)); i += 4 }
      else if (t === 'i') { args.push(b.readInt32BE(i)); i += 4 }
      else if (t === 'd') { args.push(b.readDoubleBE(i)); i += 8 }
      else if (t === 's') { let s; [s, i] = str(b, i); args.push(s) }
      else args.push(t)
    }
    msgs.push(`${address} ${tags} ${JSON.stringify(args)}`)
  }
  sock.on('message', decode)
  return new Promise((res) => sock.bind(port, '127.0.0.1', () => res({ msgs, close: () => sock.close() })))
}

function sendOsc(port, address, value) {
  const s = (x) => { const b = Buffer.from(x + '\0'); return Buffer.concat([b, Buffer.alloc((4 - b.length % 4) % 4)]) }
  const v = Buffer.alloc(4); v.writeFloatBE(value)
  const sock = dgram.createSocket('udp4')
  return new Promise((res) => sock.send(Buffer.concat([s(address), s(',f'), v]), port, '127.0.0.1', () => { sock.close(); res() }))
}

// ---------------------------------------------------------------- Chrome over CDP
async function openPage(chrome, url) {
  const prof = fs.mkdtempSync(path.join(tmpRoot, 'chrome-'))
  const p = spawn(chrome, ['--headless=new', '--remote-debugging-port=0', `--user-data-dir=${prof}`, '--no-first-run',
    '--no-default-browser-check', '--disable-gpu', `--window-size=${VIEW.width},${VIEW.height}`, url], { stdio: 'ignore' })
  children.push(p)
  let port
  for (let i = 0; i < 100 && !port; i++) {
    try { port = fs.readFileSync(path.join(prof, 'DevToolsActivePort'), 'utf8').split('\n')[0] } catch (e) { await sleep(100) }
  }
  if (!port) die('Chrome did not start (pass --chrome)')
  let page
  for (let i = 0; i < 50 && !page; i++) {
    const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()
    page = list.find((t) => t.type === 'page' && t.url.startsWith(url))
    if (!page) await sleep(100)
  }
  const ws = new WebSocket(page.webSocketDebuggerUrl)
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej })
  let id = 0; const pending = new Map()
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data)
    if (m.id && pending.has(m.id)) { const { res, rej } = pending.get(m.id); pending.delete(m.id); m.error ? rej(new Error(m.error.message)) : res(m.result) }
  }
  const send = (method, params = {}) => { const i = ++id; ws.send(JSON.stringify({ id: i, method, params })); return new Promise((res, rej) => pending.set(i, { res, rej })) }
  const evaluate = async (expression) => {
    const r = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text)
    return r.result.value
  }
  await send('Emulation.setDeviceMetricsOverride', { ...VIEW, deviceScaleFactor: 1, mobile: false })
  return { send, evaluate, close: () => { ws.close(); p.kill('SIGKILL') } }
}

const PAGE_HELPERS = `
  window.__root = () => { const e = document.querySelector('[data-widget]'); if (!e) return null; let r = e._widget_instance; while (r.parent && typeof r.parent.getProp === 'function') r = r.parent; return r };
  window.__find = (id) => { const r = __root(); if (!r) return null; const st = [r]; while (st.length) { const w = st.pop(); if (w.getProp && w.getProp('id') === id) return w; (w.children || []).forEach((c) => c && st.push(c)) } return null };
  true`

// the compound's rectangle: the canvas, or the native compound's panel
const RECT = {
  slider: { native: `__find('attack1') && __find('attack1').parent`, canvas: `__find('attack1_c')` },
  seq: { native: `__find('seqwrite1-0') && __find('seqwrite1-0').parent.parent`, canvas: `__find('seqsteps1_c')` },
}

async function rectOf(page, expr) {
  for (let i = 0; i < 100; i++) {
    await page.evaluate(PAGE_HELPERS)
    const r = await page.evaluate(`(() => { const w = ${expr}; if (!w) return null; const b = (w.container || w.widget).getBoundingClientRect(); return b.width > 0 ? {left: b.left, top: b.top, width: b.width, height: b.height} : null })()`)
    if (r) return r
    await sleep(100)
  }
  die(`compound not found on the page: ${expr}`)
}

async function mouse(page, type, x, y, extra = {}) {
  await page.send('Input.dispatchMouseEvent', { type, x, y, button: type === 'mouseMoved' && !extra.buttons ? 'none' : 'left', ...extra })
}

async function perform(page, rect, a) {
  const px = (fx, fy) => [rect.left + fx * rect.width, rect.top + fy * rect.height]
  if (a[0] === 'tap' || a[0] === 'dbl') {
    const [x, y] = px(a[1], a[2])
    await mouse(page, 'mouseMoved', x, y)
    for (let n = 1; n <= (a[0] === 'dbl' ? 2 : 1); n++) {
      await mouse(page, 'mousePressed', x, y, { buttons: 1, clickCount: n }); await sleep(30)
      await mouse(page, 'mouseReleased', x, y, { buttons: 0, clickCount: n }); await sleep(60)
    }
  } else if (a[0] === 'drag') {
    const [x0, y0] = px(a[1], a[2]), [x1, y1] = px(a[3], a[4]), steps = 12
    await mouse(page, 'mouseMoved', x0, y0)
    await mouse(page, 'mousePressed', x0, y0, { buttons: 1, clickCount: 1 }); await sleep(30)
    for (let i = 1; i <= steps; i++) {
      await mouse(page, 'mouseMoved', x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, { buttons: 1 }); await sleep(16)
    }
    await mouse(page, 'mouseReleased', x1, y1, { buttons: 0, clickCount: 1 })
  }
  await sleep(450)                 // past the 375 ms double-tap window before the next action
}

// ---------------------------------------------------------------- one variant
async function record(osc, chrome, kind, which, variant, port, udp, gestures) {
  const rec = await recorder(udp)
  const server = await startServer(osc, port, udp, variant)
  const page = await openPage(chrome, `http://127.0.0.1:${port}/`)
  const rect = await rectOf(page, RECT[kind][which])
  await sleep(800)                                         // let the initial value burst pass
  const out = []
  for (const g of gestures) {
    for (const [a, v] of g.osc || []) await sendOsc(port, a, v)
    if (g.osc) await sleep(250)
    const start = rec.msgs.length
    for (const a of g.act) await perform(page, rect, a)
    await sleep(300)
    out.push(rec.msgs.slice(start))
    for (const [a, v] of g.after || []) await sendOsc(port, a, v)
    if (g.after) await sleep(250)
  }
  page.close(); server.kill('SIGKILL'); rec.close()
  return out
}

// ---------------------------------------------------------------- main
async function main() {
  const osc = oscDir()
  const chrome = opt('--chrome', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
  if (!fs.existsSync(chrome)) die(`no Chrome at ${chrome} (pass --chrome)`)
  if (typeof WebSocket === 'undefined') die(`node ${process.version} has no WebSocket; run with node 22+ (e.g. ${osc}/node)`)
  tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'check-gestures-'))
  log(`o-s-c ${JSON.parse(fs.readFileSync(path.join(osc, 'open-stage-control/package.json'), 'utf8')).version} from ${osc}`)
  const sessions = buildSessions(tmpRoot, osc)

  let failed = 0, total = 0
  for (const [kind, gestures] of [['slider', SLIDER], ['seq', SEQ]]) {
    const native = await record(osc, chrome, kind, 'native', sessions[kind].native, BASE_PORT, BASE_PORT + 2, gestures)
    const canvas = await record(osc, chrome, kind, 'canvas', sessions[kind].canvas, BASE_PORT + 1, BASE_PORT + 3, gestures)
    gestures.forEach((g, i) => {
      total++
      const a = native[i], b = canvas[i]
      const same = a.length === b.length && a.every((m, j) => m === b[j])
      const empty = a.length === 0                         // a gesture that sends nothing proves nothing
      if (same && !empty) { log(`ok    ${kind}: ${g.name} (${a.length} messages)`); if (VERBOSE) a.forEach((m) => log('        ' + m)); return }
      failed++
      log(`FAIL  ${kind}: ${g.name}${empty ? ' -- the native widgets sent nothing; the gesture missed' : ''}`)
      const n = Math.max(a.length, b.length)
      for (let j = 0; j < n; j++) log(`        ${a[j] === b[j] ? ' ' : '!'} native: ${(a[j] || '-').padEnd(34)} canvas: ${b[j] || '-'}`)
    })
  }
  log(failed ? `${failed} of ${total} gestures differ` : `all ${total} gestures identical`)
  if (flag('--keep')) log(`sessions kept in ${tmpRoot}`)
  cleanup()
  process.exit(failed ? 1 : 0)
}

main().catch((e) => die(e.stack || String(e)))
