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
 *   {"k":"in", "st", "m":{addr: value}}      values Pd sends to the GUI that
 *                                            modulate the sound (LFOs, chaos,
 *                                            automation, extmodulation), at most
 *                                            IN_HZ frames a second, changed values only
 *   {"k":"disp", "st", "m":{addr: value}}    display feedback: values Pd sends to
 *                                            the GUI that the player does not set
 *                                            and that change no sound -- sequencer
 *                                            position and step, pitch readouts.
 *                                            Framed the same way, kept apart so
 *                                            they are never read as modulation
 *   {"k":"snap", "why", "vals":{id: value}}  every widget's value: at start, every
 *                                            5 minutes, after a preset load
 *   {"k":"preset", "st", "a", "v"}           a preset load or state command from the app
 *   {"k":"rec-start"|"rec-stop", "c":"app", "file", "first_block_st", ...}
 *                                            session audio (uxaudio-*.ogg in this
 *                                            folder): sample 0 is at first_block_st
 *   {"k":"clock", "c", "s", "off", "st"}     a client's clock offset, st - ft, the
 *                                            first time it is known and whenever it
 *                                            moves: t + off is the event on this
 *                                            computer's clock
 *   {"k":"stats", "st", ...}                 what the logger itself dropped or
 *                                            collapsed, written on unload
 *
 * "t" is the tablet's clock, "st" this computer's; both are ms since the
 * epoch, and "st" is the one to trust when comparing tablets. "at", on an
 * event from the app, is the app's own clock when it sent it: a gap in the
 * spec stream is the app not sending if "at" gaps too, and this server too
 * busy to write if it does not.
 *
 * Nothing here alters what reaches the Pd patch, except that the logger's
 * own messages are dropped instead of forwarded.
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

// Pd -> GUI traffic is dense (LFOs at the GUI frame rate on many widgets),
// so it is framed: the latest value per address, written IN_HZ times a
// second, only where it changed. At 5 Hz, 50 modulated widgets cost about
// 15 MB an hour; replay shows modulation at that rate.
var IN_HZ = Number(process.env.FORMULS_UXLOG_IN_HZ) || 5
var inPending = {}
var inLast = {}
var inAny = false

// Some of what Pd sends the GUI is not modulation at all: the player never
// sets it and it changes no sound. The sequencer playhead and step, and the
// pitch readout, are the interface reporting itself. They are also the
// densest addresses in a log, so counting them as modulation makes every
// gesture look as though it were made into a running network. They go to
// their own stream, "disp", and are never mixed with the modulation in "in".
// Add to this list rather than filtering downstream; each entry matches a
// whole address, with a trailing synth number allowed.
var DISPLAY_FEEDBACK = [
    /^\/seqpos\d*$/,          // the sequencer's playhead
    /^\/sequencerstep\d*$/,   // the step the sequencer is on
    /^\/oscpitchgui\d*$/      // the oscillator pitch readout
]

// Open Stage Control's own housekeeping. /GET is a client asking the server
// for the state of a tab; it carries no value the player set and no value Pd
// produced, and it is relentless -- 7641 of the 12553 param events in the
// 10 November session, 61 %, over five distinct payloads, all ["root", n].
// It is counted and dropped, in both directions.
var OSC_INTERNAL = [
    /^\/GET$/
]

function isOscInternal(address) {
    for (var i = 0; i < OSC_INTERNAL.length; i++) {
        if (OSC_INTERNAL[i].test(address)) return true
    }
    return false
}

function isDisplayFeedback(address) {
    for (var i = 0; i < DISPLAY_FEEDBACK.length; i++) {
        if (DISPLAY_FEEDBACK[i].test(address)) return true
    }
    return false
}

var started = new Date()
var file = path.join(dir, 'uxlog-' + stamp(started) + '.jsonl')
var out = fs.createWriteStream(file, { flags: 'a' })

// A connecting tablet pushes its whole state at once -- thousands of
// messages in a couple of seconds -- and this server is single-threaded, so
// a write per message competed with receiving the app's 5 Hz spec frames:
// the two worst gaps in the 10 November session, 2.6 s and 3.6 s, sit on
// exactly that burst, where every other gap is under a quarter second. Lines
// are collected and written in one go instead, at WRITE_HZ and whenever the
// buffer passes WRITE_MAX bytes. The flush has to stay brisk: announceUxLogEvent
// in the app reads this file back to see whether an event landed.
var WRITE_HZ = Number(process.env.FORMULS_UXLOG_WRITE_HZ) || 10
var WRITE_MAX = 64 * 1024
var pending = []
var pendingBytes = 0

function flushWrites() {
    if (!pending.length) return
    var chunk = pending.join('')
    pending = []
    pendingBytes = 0
    out.write(chunk)
}

function write(obj) {
    var line = JSON.stringify(obj) + '\n'
    pending.push(line)
    pendingBytes += line.length
    if (pendingBytes >= WRITE_MAX) flushWrites()
}

var writeTimer = setInterval(flushWrites, 1000 / WRITE_HZ)

write({ k: 'session', st: started.getTime(), iso: started.toISOString(), file: file,
        host: os.hostname(), version: 1 })
console.log('(INFO) formuls UX logging to ' + file)

// Of the 4912 real param events in the 10 November session, 3908 were the
// connect-time state dump and 1124 of those were an identical (address,
// value) written twice within 50 ms -- the same message reaching this filter
// once per send target. Live playing, the 1004 events after the dump, had
// none at all. So an identical value on the same address from the same client
// inside DUP_MS counts once. Nothing a hand can do arrives twice that fast;
// a button sends 1 then 0, which differ.
var DUP_MS = Number(process.env.FORMULS_UXLOG_DUP_MS) || 50
var dupSeen = {}

function isDuplicate(key, st) {
    var prev = dupSeen[key]
    dupSeen[key] = st
    return prev !== undefined && st - prev < DUP_MS
}

// dupSeen would otherwise hold every address touched in a session. Addresses
// last written more than a second ago cannot collapse anything.
var dupTimer = setInterval(function () {
    var cutoff = Date.now() - 1000
    for (var k in dupSeen) if (dupSeen[k] < cutoff) delete dupSeen[k]
}, 10000)

var stats = { k: 'stats', internal_param: 0, internal_in: 0, collapsed_param: 0 }
var statsWritten = ''

// The appliance is switched off at the wall, so unload() cannot be relied on
// to be the place these counts are written. They go in once a minute as well,
// whenever they have moved: a log cut short by the power still says what the
// logger dropped. The last line of a session wins.
function writeStats() {
    var now = JSON.stringify([stats.internal_param, stats.internal_in, stats.collapsed_param])
    if (now === statsWritten) return
    statsWritten = now
    stats.st = Date.now()
    write(stats)
}

var statsTimer = setInterval(writeStats, 60000)

function round(v) {
    return typeof v === 'number' ? Number(v.toPrecision(5)) : v
}

function noteIn(data) {
    if (isOscInternal(data.address)) { stats.internal_in++; return }
    var v = data.args.map(function (a) { return round(a.value) })
    inPending[data.address] = v.length === 1 ? v[0] : v
    inAny = true
}

var inTimer = setInterval(function () {
    if (!inAny) return
    var m = {}, d = {}, n = 0, dn = 0
    for (var a in inPending) {
        var s = JSON.stringify(inPending[a])
        if (inLast[a] === s) continue
        inLast[a] = s
        if (isDisplayFeedback(a)) { d[a] = inPending[a]; dn++ }
        else { m[a] = inPending[a]; n++ }
    }
    inPending = {}
    inAny = false
    var st = Date.now()
    if (n) write({ k: 'in', st: st, m: m })
    if (dn) write({ k: 'disp', st: st, m: d })
}, 1000 / IN_HZ)

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

// st - ft is the clock offset plus the network delay of that one batch, and
// the delay is small and positive, so the smallest offset seen is the best
// estimate: t + off puts a tablet event on this computer's clock. On the
// appliance the two clocks were 835.4 s apart on 10 November, enough to put
// every gesture 14 minutes before the sound it made. Recorded when first
// known and whenever it moves, so a reader never has to derive it.
var clockOff = {}

function noteClock(session, client, st, ft) {
    if (typeof ft !== 'number') return
    var off = st - ft
    var prev = clockOff[session]
    if (prev !== undefined && off >= prev) return      // a slower batch, not a better offset
    clockOff[session] = off
    if (prev === undefined || prev - off > 100) {
        write({ k: 'clock', st: st, c: client, s: session, off: off })
    }
}

function logOut(data) {
    var st = Date.now()
    if (data.address === ADDRESS) {
        try {
            var batch = JSON.parse(data.args[0].value)
            noteClock(batch.s, data.clientId, st, batch.ft)
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
    // /grab/<address> 0|1 is the GUI telling the patch to stop echoing that
    // address back while a finger is on it (f.util.guiinhibit). It is not a
    // parameter change, so it gets its own stream rather than inflating `param`
    // -- and it is worth keeping, because it is the GUI's own account of when
    // each touch began and ended, against which `down`/`up` can be checked.
    if (data.address.indexOf('/grab/') === 0) {
        write({ k: 'grab', st: st, c: data.clientId, a: data.address.slice(5),
                v: data.args.length ? data.args[0].value : undefined })
        return data
    }
    if (isOscInternal(data.address)) { stats.internal_param++; return data }
    var v = data.args.map(function (a) { return a.value })
    if (isDuplicate(data.clientId + ' ' + data.address + ' ' + JSON.stringify(v), st)) {
        stats.collapsed_param++
        return data
    }
    write({ k: 'param', st: st, c: data.clientId, a: data.address, v: v })
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
        if (data.address.indexOf('/STATE/') === 0) {
            // A preset load from the app window: note it, and have every
            // tablet snapshot its state once the preset has landed.
            write({ k: 'preset', st: Date.now(), a: data.address,
                    v: data.args.map(function (a) { return a.value }) })
            setTimeout(function () { receive('/formuls/uxlog/snap', 1) }, 1000)
        } else {
            noteIn(data)
        }
        return inner.oscInFilter ? inner.oscInFilter(data) : data
    },
    oscOutFilter: function (data) {
        if (data.address === ADDRESS) return logOut(data)
        // formuls-replay-client.js asks every server whether it is a replay;
        // a live session drops the question instead of passing it to Pd
        if (data.address.indexOf('/formuls/') === 0) return
        var res = inner.oscOutFilter ? inner.oscOutFilter(data) : data
        if (res) logOut(res)
        return res
    },
    unload: function () {
        clearInterval(inTimer)
        clearInterval(writeTimer)
        clearInterval(dupTimer)
        clearInterval(statsTimer)
        if (inner.unload) inner.unload()
        stats.st = Date.now()
        write(stats)
        flushWrites()
        out.end()
    }
}
