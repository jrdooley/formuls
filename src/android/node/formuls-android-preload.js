/*
 * formuls-android-preload.js
 *
 * Loaded with `node -r` ahead of Open Stage Control on Android only (see
 * OpenStageControlProcess.cpp). It smooths over the ways nodejs-mobile on
 * Android differs from a desktop node, without modifying the downloaded
 * Open Stage Control package itself.
 */

'use strict'

const os = require('os')
const fs = require('fs')

// os.networkInterfaces() can throw on Android: newer releases restrict the
// netlink queries libuv uses to enumerate interfaces. Open Stage Control only
// calls it to print the addresses it is reachable at (the formuls app lists
// those itself), so an empty answer is harmless where an exception would stop
// the server from starting.
const networkInterfaces = os.networkInterfaces.bind(os)

os.networkInterfaces = function () {
    try {
        return networkInterfaces()
    } catch (err) {
        console.log('(formuls) os.networkInterfaces() unavailable: ' + err.message)
        return {}
    }
}

// Open Stage Control keeps its settings under XDG_CONFIG_HOME on Linux-like
// systems, which the app points into its private storage. It logs an error
// if the folder is missing, so make sure it exists.
for (const dir of [process.env.XDG_CONFIG_HOME, process.env.TMPDIR]) {
    if (dir) {
        try { fs.mkdirSync(dir, { recursive: true }) } catch (err) { /* reported by o-s-c */ }
    }
}

// Keep a stray error in the server from ending the process (and with it the
// GUI) where a desktop user would not have been affected either -- but still
// log it, so it shows up in `adb logcat -s formuls-gui`.
process.on('uncaughtException', (err) => {
    console.error('(formuls) uncaught exception in the GUI server:', err && err.stack || err)
})
