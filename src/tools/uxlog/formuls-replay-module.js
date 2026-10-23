/* formuls replay -- an Open Stage Control custom module.
 *
 * Loaded instead of formuls-uxlog-module.js when the app is in Replay mode
 * (OpenStageControlProcess::start with a log file). It reads the session log
 * named by FORMULS_REPLAY_LOG and plays it back into the GUI:
 *
 *   the app        sends /formuls/replay/pos <audio position, ms> about 20
 *                  times a second while the session audio plays or is moved
 *   this module    sends the GUI every logged value up to that moment, as if
 *                  Pd had sent it (so compound canvases and the translator in
 *                  formuls-module.js behave exactly as they did live), and
 *                  forwards touches and tab switches to formuls-replay-client.js,
 *                  which draws the touches and follows the chosen tablet's tabs
 *
 * Seeking (a jump back, or forward by more than SEEK_MS) restores the latest
 * state snapshot before the new position, then applies every value logged
 * between that snapshot and the position, latest value per address.
 *
 * There is no --send target in replay: nothing reaches a sound engine, and
 * anything a viewer does in the GUI is dropped here.
 */
var fs = nativeRequire('fs')

var SEEK_MS = 1500

var inner = {}
try { inner = require('formuls-module.js') || {} } catch (e) { inner = {} }

var file = process.env.FORMULS_REPLAY_LOG
var lines = fs.readFileSync(file, 'utf8').split('\n')
var events = []
for (var i = 0; i < lines.length; i++) {
    if (!lines[i]) continue
    try { events.push(JSON.parse(lines[i])) } catch (e) { /* a line cut short by a crash */ }
}
lines = null

// Tablet clocks -> this computer's clock (see analyse-uxlog.py, "Timing").
var offset = {}
events.forEach(function (e) {
    if (e.t !== undefined && e.s !== undefined && e.st !== undefined) {
        var o = e.st - (e.ft !== undefined ? e.ft : e.t)
        if (!(e.s in offset) || o < offset[e.s]) offset[e.s] = o
    }
})
function when(e) { return e.t !== undefined && e.s in offset ? e.t + offset[e.s] : e.st }

var values = []     // {T, a, v}: what the GUI was sent, from either direction
var snaps = []      // {T, s, vals}
var visual = []     // {T, ev}: touches and tab switches
var sessions = {}
var rec = null
var VIS = { down: 1, move: 1, up: 1, cancel: 1, leave: 1, tab: 1 }

events.forEach(function (e) {
    if (e.k === 'param') values.push({ T: e.st, a: e.a, v: e.v })
    else if (e.k === 'in') for (var a in e.m) values.push({ T: e.st, a: a, v: e.m[a] })
    else if (e.k === 'snap') snaps.push({ T: when(e), s: e.s, vals: e.vals })
    else if (e.k === 'rec-start' && !rec) rec = e
    else if (VIS[e.k]) {
        var ev = {}
        for (var key in e) if (key !== 'st' && key !== 'ft' && key !== 'c') ev[key] = e[key]
        ev.T = when(e)
        visual.push({ T: ev.T, ev: ev })
    }
    if (e.s !== undefined) {
        var s = sessions[e.s] || (sessions[e.s] = { s: e.s, c: e.c, n: 0, vp: e.vp, ua: e.ua })
        s.n++
        if (e.k === 'start') { s.vp = e.vp; s.ua = e.ua; s.from = when(e) }
    }
})
events = null
var byT = function (x, y) { return x.T - y.T }
values.sort(byT); snaps.sort(byT); visual.sort(byT)

var t0 = rec ? rec.first_block_st : (values.length ? values[0].T : 0)
var sessionList = Object.keys(sessions).map(function (k) { return sessions[k] })
    .sort(function (x, y) { return y.n - x.n })
var follow = sessionList.length ? sessionList[0].s : null   // the busiest tablet
var cur = null

console.log('(INFO) formuls replay: ' + file + ' -- ' + values.length + ' values, ' +
    snaps.length + ' snapshots, ' + visual.length + ' touch/tab events, ' +
    sessionList.length + ' tablet session(s)' + (rec ? '' : ' (no rec-start: timing from log start)'))

// first index with list[i].T > T
function after(list, T) {
    var lo = 0, hi = list.length
    while (lo < hi) { var mid = (lo + hi) >> 1; if (list[mid].T <= T) lo = mid + 1; else hi = mid }
    return lo
}

function toGui(address, v) {
    var vs = Array.isArray(v) ? v : [v]
    var data = { address: address, host: '127.0.0.1', port: 0,
                 args: vs.map(function (x) { return { type: typeof x === 'number' ? 'f' : 's', value: x } }) }
    var res = inner.oscInFilter ? inner.oscInFilter(data) : data
    if (res) receive.apply(null, [res.address].concat(res.args.map(function (a) { return a.value })))
}

function sendValues(from, to) {
    // collapse to the latest value per address: a burst between two position
    // ticks need only land once
    var latest = {}, order = []
    for (var i = from; i < to; i++) {
        if (!(values[i].a in latest)) order.push(values[i].a)
        latest[values[i].a] = values[i].v
    }
    order.forEach(function (a) { toGui(a, latest[a]) })
}

function sendVisual(list, reset) {
    if (list.length || reset)
        receive('/formuls/replay/vis', JSON.stringify({ reset: !!reset, ev: list }))
}

function seek(T) {
    var snap = null
    for (var i = after(snaps, T) - 1; i >= 0; i--) {
        if (!follow || snaps[i].s === follow) { snap = snaps[i]; break }
    }
    if (snap) receive('/STATE/SET', JSON.stringify(snap.vals), 0)
    sendValues(snap ? after(values, snap.T) : 0, after(values, T))

    // the followed tablet's tab on every tab group, as of T
    var tabs = {}
    for (var j = 0, end = after(visual, T); j < end; j++) {
        var ev = visual[j].ev
        if (ev.k === 'tab' && ev.s === follow) tabs[ev.in] = ev
    }
    sendVisual(Object.keys(tabs).map(function (k) { return tabs[k] }), true)
}

function advance(from, to) {
    sendValues(after(values, from), after(values, to))
    var list = []
    for (var i = after(visual, from), end = after(visual, to); i < end; i++) list.push(visual[i].ev)
    sendVisual(list, false)
}

function hello() {
    receive('/formuls/replay/hello', JSON.stringify({
        file: file, sessions: sessionList, follow: follow, t0: t0,
        audio: rec ? rec.file : null
    }))
}

function position(T) {
    if (cur === null || T < cur || T - cur > SEEK_MS) seek(T)
    else if (T > cur) advance(cur, T)
    cur = T
    receive('/formuls/replay/time', T - t0)
}

module.exports = {
    init: function () { if (inner.init) return inner.init.apply(this, arguments) },
    oscInFilter: function (data) {
        if (data.address === '/formuls/replay/pos') {
            position(t0 + Number(data.args[0].value))
            return
        }
        // /STATE/... and anything else from the app still reach the GUI
        return inner.oscInFilter ? inner.oscInFilter(data) : data
    },
    oscOutFilter: function (data) {
        if (data.address === '/formuls/replay/hi') hello()
        else if (data.address === '/formuls/replay/follow') {
            follow = String(data.args[0].value)
            hello()
            if (cur !== null) seek(cur)
        }
        return   // replay sends nothing anywhere
    },
    unload: function () { if (inner.unload) inner.unload() }
}
