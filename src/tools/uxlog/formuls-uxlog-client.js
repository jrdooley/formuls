/* formuls UX logger -- client half.
 *
 * Injected into the Open Stage Control client page by patch-osc-uxlog.py,
 * only in a build made with FORMULS_UXLOG=1. Records how people touch the
 * interface -- not what they sound like -- and ships the events in batches
 * over the GUI's own WebSocket to formuls-uxlog-module.js, which writes them
 * to disk and stops them before they reach the Pd patch.
 *
 * Every listener is passive and in the capture phase: the logger watches
 * the same events the widgets get, before they get them, and never calls
 * preventDefault or stopPropagation. The tablet's main thread is the budget
 * that drops connections (docs/gui/README.md), so moves are thinned to
 * MOVE_HZ per finger and everything goes out in one message per FLUSH_MS.
 */
(function () {
    'use strict'

    var FLUSH_MS = 500
    var MOVE_HZ = 30
    var ADDRESS = '/formuls/uxlog'

    var session = Date.now().toString(36) + Math.random().toString(36).slice(2, 7)
    var queue = []
    var gestures = {}           // pointerId -> gesture in progress
    var seq = 0

    function ipc() { return window.__formulsIpc }

    function emit(ev) {
        ev.seq = seq++
        ev.t = Date.now()                       // tablet wall clock, ms
        ev.pt = Math.round(performance.now())   // monotonic, for durations
        queue.push(ev)
    }

    function flush() {
        if (!queue.length || !ipc()) return
        var batch = queue
        queue = []
        try {
            // No target: the server adds its default --send target, and the
            // custom module's oscOutFilter swallows this address there.
            ipc().send('sendOsc', { address: ADDRESS, v: JSON.stringify({ s: session, ft: Date.now(), ev: batch }) })
        } catch (e) {
            queue = batch.concat(queue)
        }
    }
    setInterval(flush, FLUSH_MS)
    window.addEventListener('pagehide', function () { emit({ k: 'page-hide' }); flush() })

    // The nearest widget that owns the element: Open Stage Control hangs the
    // widget instance on its container as _widget_instance.
    function widgetAt(el) {
        for (; el && el !== document; el = el.parentNode) {
            if (el._widget_instance) return el._widget_instance
        }
        return null
    }

    function describe(w) {
        if (!w) return null
        var p = function (k) { try { return w.getProp(k) } catch (e) { return undefined } }
        var r = w.container.getBoundingClientRect()
        return {
            id: p('id'), type: p('type'),
            interactive: p('interaction') !== false && !/^(panel|folder|root|tab|text|frame)$/.test(p('type')),
            box: [r.left, r.top, r.width, r.height].map(Math.round)
        }
    }

    // Closest widget a person could have meant to touch: walk up past
    // containers to the first one that takes input.
    function interactiveAt(el) {
        for (var w = widgetAt(el); w; w = widgetAt(w.container.parentNode)) {
            var d = describe(w)
            if (d.interactive) return { w: w, d: d }
        }
        return null
    }

    function value(w) {
        try { var v = w.getValue(); return typeof v === 'object' ? JSON.parse(JSON.stringify(v)) : v }
        catch (e) { return undefined }
    }

    // Value direction flips during one gesture: overshoot-and-correct shows
    // up here. Multi-value widgets (xy pads, multisliders) flip per axis.
    function trackValue(g) {
        var v = value(g.w)
        var a = Array.isArray(v) ? v : [v]
        if (JSON.stringify(a) === JSON.stringify(g.last)) return
        g.changes++
        for (var i = 0; i < a.length; i++) {
            if (typeof a[i] !== 'number' || typeof g.last[i] !== 'number') continue
            var dir = Math.sign(a[i] - g.last[i])
            if (dir && g.dir[i] && dir !== g.dir[i]) g.reversals++
            if (dir) g.dir[i] = dir
        }
        g.last = a
    }

    function viewport() { return [window.innerWidth, window.innerHeight] }

    function down(e) {
        var hit = interactiveAt(e.target)
        var g = {
            id: e.pointerId, x0: e.clientX, y0: e.clientY, x: e.clientX, y: e.clientY,
            t0: performance.now(), lastMove: 0, path: 0, left: false,
            w: hit && hit.w, d: hit && hit.d, changes: 0, reversals: 0, dir: [], last: []
        }
        if (g.w) { var v = value(g.w); g.v0 = v; g.last = Array.isArray(v) ? v : [v] }
        gestures[e.pointerId] = g
        emit({
            k: 'down', p: e.pointerId, pt_type: e.pointerType, x: Math.round(e.clientX), y: Math.round(e.clientY),
            vp: viewport(), active: Object.keys(gestures).length,
            w: g.d ? g.d.id : null, wt: g.d ? g.d.type : null, box: g.d ? g.d.box : null,
            // a touch on dead space: the raw material for cold spots and near-misses
            miss: !g.d, under: g.d ? undefined : (describe(widgetAt(e.target)) || {}).id
        })
    }

    function move(e) {
        var g = gestures[e.pointerId]
        if (!g) return
        g.path += Math.hypot(e.clientX - g.x, e.clientY - g.y)
        g.x = e.clientX; g.y = e.clientY
        if (g.w) {
            trackValue(g)
            var b = g.d.box
            if (!g.left && (g.x < b[0] || g.y < b[1] || g.x > b[0] + b[2] || g.y > b[1] + b[3])) {
                g.left = true
                emit({ k: 'leave', p: e.pointerId, w: g.d.id, x: Math.round(g.x), y: Math.round(g.y) })
            }
        }
        var now = performance.now()
        if (now - g.lastMove < 1000 / MOVE_HZ) return
        g.lastMove = now
        emit({ k: 'move', p: e.pointerId, x: Math.round(g.x), y: Math.round(g.y) })
    }

    function up(e) {
        var g = gestures[e.pointerId]
        if (!g) return
        delete gestures[e.pointerId]
        if (g.w) trackValue(g)
        emit({
            k: e.type === 'pointercancel' ? 'cancel' : 'up', p: e.pointerId,
            x: Math.round(e.clientX), y: Math.round(e.clientY),
            w: g.d ? g.d.id : null, wt: g.d ? g.d.type : null,
            dur: Math.round(performance.now() - g.t0),
            dist: Math.round(Math.hypot(e.clientX - g.x0, e.clientY - g.y0)),
            path: Math.round(g.path), left: g.left,
            v0: g.v0, v1: g.w ? value(g.w) : undefined,
            changes: g.changes, reversals: g.reversals
        })
    }

    var opts = { capture: true, passive: true }
    document.addEventListener('pointerdown', down, opts)
    document.addEventListener('pointermove', move, opts)
    document.addEventListener('pointerup', up, opts)
    document.addEventListener('pointercancel', up, opts)

    // Navigation: tab switches. Open Stage Control switches tabs on press,
    // not on click, so watch the press; the label is the text a person saw.
    document.addEventListener('pointerdown', function (e) {
        var link = e.target.closest && e.target.closest('.tablink')
        if (!link) return
        var parent = widgetAt(link)
        emit({ k: 'tab', label: link.textContent.trim(), in: parent ? describe(parent).id : null })
    }, opts)

    document.addEventListener('visibilitychange', function () {
        emit({ k: document.hidden ? 'hidden' : 'visible' })
    })
    window.addEventListener('resize', function () { emit({ k: 'resize', vp: viewport() }) })
    window.addEventListener('orientationchange', function () { emit({ k: 'orientation', vp: viewport() }) })

    emit({ k: 'start', vp: viewport(), dpr: window.devicePixelRatio, ua: navigator.userAgent })
})()
