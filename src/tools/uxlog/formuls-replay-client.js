/* formuls replay -- client half.
 *
 * Loaded into the GUI page of every FORMULS_UXLOG=1 build, and idle unless
 * the server is formuls-replay-module.js, which answers /formuls/replay/hi.
 * In replay it:
 *   - draws the followed tablet's touches over the widgets: a dot per finger
 *     with a fading trail, a blue ring where a touch hit dead space, a red
 *     flash where a drag slid off its widget
 *   - switches tabs as that tablet did, so its view is the one on screen
 *   - shows a small panel: tablet picker, overlay toggle, session time
 * Values themselves arrive from the server as ordinary OSC; this script
 * never sets a widget value apart from tab switches.
 */
(function () {
    'use strict'

    var info = null
    var follow = null
    var overlayOn = true
    var pointers = {}       // p -> {x, y, trail: [[x, y, time]], w, box, live}
    var marks = []          // [{x, y, kind, time}]
    var canvas, ctx, panel, timeLabel, picker

    function ipc() { return window.__formulsIpc }

    function send(address, value) {
        ipc().send('sendOsc', { address: address, v: value })
    }

    // --- widgets by id -------------------------------------------------
    var byId = {}
    function widget(id) {
        var w = byId[id]
        if (w && document.body.contains(w.container)) return w
        byId = {}
        var nodes = document.querySelectorAll('[data-widget]')
        for (var i = 0; i < nodes.length; i++) {
            var inst = nodes[i]._widget_instance
            if (!inst) continue
            try { var wid = inst.getProp('id'); if (!(wid in byId)) byId[wid] = inst } catch (e) {}
        }
        return byId[id]
    }

    // Logged positions are in the tablet's pixels. A touch on a widget is
    // placed at the same relative spot on that widget here, wherever and
    // however large it is now; anything else is scaled by the viewport.
    function place(ptr, x, y, vp) {
        if (ptr && ptr.w && ptr.box) {
            var w = widget(ptr.w)
            if (w) {
                var r = w.container.getBoundingClientRect()
                if (r.width && r.height) {
                    return [r.left + (x - ptr.box[0]) / ptr.box[2] * r.width,
                            r.top + (y - ptr.box[1]) / ptr.box[3] * r.height]
                }
            }
        }
        vp = vp || (ptr && ptr.vp) || [window.innerWidth, window.innerHeight]
        return [x / vp[0] * window.innerWidth, y / vp[1] * window.innerHeight]
    }

    // --- events from the replay server ---------------------------------
    function onVis(msg) {
        if (msg.reset) { pointers = {}; marks = [] }
        var now = performance.now()
        msg.ev.forEach(function (ev) {
            if (ev.s !== follow) return
            if (ev.k === 'tab') {
                var panelWidget = widget(ev.in)
                if (panelWidget && ev.idx >= 0) {
                    try { panelWidget.setValue(ev.idx, { sync: true, send: false }) } catch (e) {}
                }
                return
            }
            var ptr = pointers[ev.p]
            if (ev.k === 'down') {
                ptr = pointers[ev.p] = { w: ev.w, box: ev.box, vp: ev.vp, trail: [], live: true }
                if (ev.miss) marks.push({ xy: place(null, ev.x, ev.y, ev.vp), kind: 'miss', time: now })
            }
            if (!ptr) return
            var xy = place(ptr, ev.x, ev.y)
            if (ev.k === 'leave') marks.push({ xy: xy, kind: 'leave', time: now })
            ptr.trail.push([xy[0], xy[1], now])
            if (ev.k === 'up' || ev.k === 'cancel') { ptr.live = false; ptr.ended = now }
        })
    }

    function onHello(msg) {
        info = msg
        follow = msg.follow
        buildPanel()
    }

    function onOsc(d) {
        if (!d || typeof d.address !== 'string' || d.address.indexOf('/formuls/replay/') !== 0) return
        var arg = Array.isArray(d.args) ? d.args[0] : d.args
        try {
            if (d.address === '/formuls/replay/hello') onHello(JSON.parse(arg))
            else if (d.address === '/formuls/replay/vis') onVis(JSON.parse(arg))
            else if (d.address === '/formuls/replay/time' && timeLabel) timeLabel.textContent = clock(Number(arg))
        } catch (e) { console.warn('formuls replay:', e) }
    }

    function clock(ms) {
        var s = Math.max(0, Math.floor(ms / 1000))
        var p = function (n) { return String(n).padStart(2, '0') }
        return Math.floor(s / 3600) + ':' + p(Math.floor(s / 60) % 60) + ':' + p(s % 60)
    }

    // --- drawing --------------------------------------------------------
    var TRAIL_MS = 1200, MARK_MS = 2500

    function draw() {
        requestAnimationFrame(draw)
        if (!canvas) return
        var dpr = window.devicePixelRatio || 1
        if (canvas.width !== innerWidth * dpr || canvas.height !== innerHeight * dpr) {
            canvas.width = innerWidth * dpr; canvas.height = innerHeight * dpr
        }
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
        ctx.clearRect(0, 0, innerWidth, innerHeight)
        if (!overlayOn) return
        var now = performance.now()

        for (var p in pointers) {
            var ptr = pointers[p]
            ptr.trail = ptr.trail.filter(function (q) { return now - q[2] < TRAIL_MS })
            if (!ptr.live && now - ptr.ended > TRAIL_MS) { delete pointers[p]; continue }
            for (var i = 1; i < ptr.trail.length; i++) {
                var a = 1 - (now - ptr.trail[i][2]) / TRAIL_MS
                ctx.strokeStyle = 'rgba(255,170,40,' + (a * 0.8) + ')'
                ctx.lineWidth = 4
                ctx.beginPath()
                ctx.moveTo(ptr.trail[i - 1][0], ptr.trail[i - 1][1])
                ctx.lineTo(ptr.trail[i][0], ptr.trail[i][1])
                ctx.stroke()
            }
            var last = ptr.trail[ptr.trail.length - 1]
            if (last && ptr.live) {
                ctx.fillStyle = 'rgba(255,170,40,0.55)'
                ctx.strokeStyle = 'rgba(255,255,255,0.9)'
                ctx.lineWidth = 2
                ctx.beginPath(); ctx.arc(last[0], last[1], 18, 0, 2 * Math.PI); ctx.fill(); ctx.stroke()
            }
        }

        marks = marks.filter(function (m) { return now - m.time < MARK_MS })
        marks.forEach(function (m) {
            var a = 1 - (now - m.time) / MARK_MS
            ctx.strokeStyle = m.kind === 'miss' ? 'rgba(60,200,255,' + a + ')' : 'rgba(255,60,60,' + a + ')'
            ctx.lineWidth = 3
            ctx.beginPath(); ctx.arc(m.xy[0], m.xy[1], m.kind === 'miss' ? 22 : 14, 0, 2 * Math.PI); ctx.stroke()
        })
    }

    // --- panel ----------------------------------------------------------
    function buildPanel() {
        if (!canvas) {
            canvas = document.createElement('canvas')
            canvas.style.cssText = 'position:fixed;left:0;top:0;width:100vw;height:100vh;pointer-events:none;z-index:99998'
            document.body.appendChild(canvas)
            ctx = canvas.getContext('2d')
            requestAnimationFrame(draw)
        }
        if (!panel) {
            panel = document.createElement('div')
            panel.style.cssText = 'position:fixed;right:8px;bottom:8px;z-index:99999;background:rgba(0,0,0,.8);' +
                'color:#eee;font:12px system-ui,sans-serif;padding:8px 10px;border-radius:6px;display:flex;gap:10px;align-items:center'
            panel.innerHTML = '<b style="color:#fa2">REPLAY</b><span class=t>0:00:00</span>' +
                '<select class=p style="max-width:220px"></select>' +
                '<label><input type=checkbox class=o checked> touches</label>'
            document.body.appendChild(panel)
            timeLabel = panel.querySelector('.t')
            picker = panel.querySelector('.p')
            picker.addEventListener('change', function () { send('/formuls/replay/follow', picker.value) })
            panel.querySelector('.o').addEventListener('change', function (e) { overlayOn = e.target.checked })
        }
        picker.innerHTML = ''
        info.sessions.forEach(function (s, i) {
            var o = document.createElement('option')
            o.value = s.s
            o.textContent = 'tablet ' + (i + 1) + (s.vp ? ' (' + s.vp[0] + '×' + s.vp[1] + ')' : '') + ', ' + s.n + ' events'
            o.selected = s.s === follow
            picker.appendChild(o)
        })
    }

    // Ask whether this is a replay server; a logging server swallows the
    // question, so in a live session nothing ever answers and this stays idle.
    var tries = 0
    var hook = setInterval(function () {
        if (!ipc()) return
        if (tries === 0) ipc().on('receiveOsc', onOsc)
        if (info || ++tries > 10) { clearInterval(hook); return }
        send('/formuls/replay/hi', 1)
    }, 1000)
})()
