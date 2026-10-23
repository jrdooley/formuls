/* formuls UX logger -- server half (an Open Stage Control custom module).
 *
 * Bundled only in a FORMULS_UXLOG=1 build; OpenStageControlProcess passes
 * --custom-module when it finds this file next to _main.json. Writes one
 * JSON object per line to ~/Music/formuls/uxlog/uxlog-<date>-<time>.jsonl:
 *
 *   {"k":"session", ...}                     once, when the server starts
 *   {"k":"client", "c":<id>, ...}            a tablet connects
 *   {"k":"down"|"move"|"up"|..., "c", "s", "t", "st", ...}
 *                                            touch events from the client half
 *   {"k":"param", "c", "st", "a", "v"}       every value the GUI sends to Pd
 *   {"k":"rec-start"|"rec-stop", "c":"app", "file", "first_block_st", ...}
 *                                            session audio (uxaudio-*.ogg in this
 *                                            folder): sample 0 is at first_block_st
 *
 * "t" is the tablet's clock, "st" this computer's; both are ms since the
 * epoch, and "st" is the one to trust when comparing tablets. Nothing here
 * alters what reaches the Pd patch, except that the logger's own messages
 * are dropped instead of forwarded.
 */
var fs = nativeRequire('fs')
var path = nativeRequire('path')
var os = nativeRequire('os')

var ADDRESS = '/formuls/uxlog'
var APP_ADDRESS = '/formuls/uxlog/event'
var dir = process.env.FORMULS_UXLOG_DIR || path.join(os.homedir(), 'Music', 'formuls', 'uxlog')
fs.mkdirSync(dir, { recursive: true })

function stamp(d) {
    var p = function (n) { return String(n).padStart(2, '0') }
    return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate()) +
        '-' + p(d.getHours()) + p(d.getMinutes()) + p(d.getSeconds())
}

var started = new Date()
var file = path.join(dir, 'uxlog-' + stamp(started) + '.jsonl')
var out = fs.createWriteStream(file, { flags: 'a' })

function write(obj) {
    out.write(JSON.stringify(obj) + '\n')
}

write({ k: 'session', st: started.getTime(), iso: started.toISOString(), file: file,
        host: os.hostname(), version: 1 })
console.log('(INFO) formuls UX logging to ' + file)

app.on('open', function (data, client) {
    write({ k: 'client', st: Date.now(), c: client.id, addr: client.address })
})
app.on('close', function (data, client) {
    write({ k: 'client-gone', st: Date.now(), c: client.id })
})

// Open Stage Control loads a single --custom-module. If the build also ships
// formuls-module.js (the compound-slider translator), this module wraps it:
// its filters run first, and the logger sees what it would have sent.
var inner = {}
try { inner = require('formuls-module.js') || {} } catch (e) { inner = {} }

function logOut(data) {
    var st = Date.now()
    if (data.address === ADDRESS) {
        try {
            var batch = JSON.parse(data.args[0].value)
            for (var i = 0; i < batch.ev.length; i++) {
                var ev = batch.ev[i]
                ev.c = data.clientId
                ev.s = batch.s
                ev.st = st
            ev.ft = batch.ft   // tablet clock when the batch left: st - ft is network delay only
                write(ev)
            }
        } catch (e) {
            write({ k: 'bad-batch', st: st, c: data.clientId, err: String(e) })
        }
        return   // swallowed: the Pd patch never sees logger traffic
    }
    write({ k: 'param', st: st, c: data.clientId, a: data.address,
            v: data.args.map(function (a) { return a.value }) })
    return data
}

module.exports = {
    init: function () { if (inner.init) return inner.init.apply(this, arguments) },
    oscInFilter: function (data) {
        // Events from the formuls app itself (rec-start / rec-stop), sent over
        // UDP to this server's OSC port; logged here and not passed to the GUI.
        if (data.address === APP_ADDRESS) {
            try {
                var ev = JSON.parse(data.args[0].value)
                ev.st = Date.now()
                ev.c = 'app'
                write(ev)
            } catch (e) {
                write({ k: 'bad-app-event', st: Date.now(), err: String(e) })
            }
            return
        }
        return inner.oscInFilter ? inner.oscInFilter(data) : data
    },
    oscOutFilter: function (data) {
        if (data.address === ADDRESS) return logOut(data)
        var res = inner.oscOutFilter ? inner.oscOutFilter(data) : data
        if (res) logOut(res)
        return res
    },
    unload: function () { if (inner.unload) inner.unload(); out.end() }
}
