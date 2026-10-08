// Open Stage Control server module for formuls (loaded with --custom-module).
//
// The shipped session draws each `slider` compound as ONE canvas holding 20 values
// (src/tools/compile-compounds.py). Pd still speaks per-parameter addresses, so this
// translates between the two, from the map compile-compounds.py writes next to the
// session (compounds.json):
//
//   Pd -> GUI  a sub-widget address (/attackchaos1 0.4)
//              becomes a patch [canvasId, slot, value, ...] sent to the client mailbox
//              /fc_patch, which writes just those slots. No server-side copy of the
//              values exists, so nothing here can overwrite a slot the player is moving.
//   GUI -> Pd  a canvas's own address is only ever sent by a state recall; it is
//              unpacked into the per-address messages the stacked widgets sent, in the
//              same order, as floats. Gestures send their sub-widget messages directly
//              from the canvas script and pass through untouched.
//
// The sequencer's per-step dimming (/aseqwriteN-i) is not widget state today, so it
// becomes a variable patch ['@var', canvasId, 'op', step, value] instead.
//
// Anything not in the map passes through unchanged.
var map = loadJSON('compounds.json', function (e) {
    console.error('[formuls-module] cannot load compounds.json: ' + e)
}) || {canvases: {}, inbound: {}, passthrough: []}
// A fader with `steps` snaps an incoming value to the nearest step, an exact tie going
// to the lower one (o-s-c Slider.setValue: indexOf(Math.min(...))). Range 0..1.
function quantise(v, n) {
    var best = 0, bd = Infinity
    for (var u = 0; u < n; u++) {
        var s = u / (n - 1) * (1 - 0) + 0, d = Math.abs(s - v)
        if (d < bd) { bd = d; best = s }
    }
    return best
}

var passthrough = {}
for (var i = 0; i < (map.passthrough || []).length; i++) passthrough[map.passthrough[i]] = true

module.exports = {
    oscInFilter: function (data) {
        var targets = map.inbound[data.address]
        if (!targets) return data
        for (var t = 0; t < targets.length; t++) {
            var cid = targets[t][0], slot = targets[t][1], count = targets[t][2]
            if (typeof slot === 'string') {          // a widget variable, e.g. sequencer dimming
                if (data.args.length) receive('/fc_patch', '@var', cid, slot, count, data.args[0].value)
                continue
            }
            var args = [cid]
            var steps = targets[t][3]
            for (var k = 0; k < count && k < data.args.length; k++) {
                var v = data.args[k].value
                if (steps) v = quantise(Math.max(0, Math.min(1, Number(v))), steps)
                args.push(slot + k, v)
            }
            if (args.length > 1) receive('/fc_patch', ...args)
        }
        return passthrough[data.address] ? data : null
    },
    oscOutFilter: function (data) {
        var c = map.canvases[data.address]
        if (!c) return data
        for (var r = 0; r < c.recall.length; r++) {
            var slot = c.recall[r][0], address = c.recall[r][1], a = data.args[slot]
            if (a === undefined) continue
            send(data.host, data.port, address, {type: 'f', value: Number(a.value)})
        }
        return null
    },
}
