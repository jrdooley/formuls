# Session-size rig

Produces the numbers in `../../session-size.md`. Everything runs against a scratch copy
of the app's Open Stage Control package, never the app's own, and on ports 9020+, never
9000/9001.

| file | what it does |
|---|---|
| `instrument_osc.py` | Adds `performance.mark()`s to a copy of the client: `b0` load start, `b0a` after teardown, `b1` after parse, `b2` after the initial value broadcast, `b3` end of build, `s0`/`s1` around state apply. Also exposes `window.__jm` (SessionManager) and `window.__wm` (widget manager) so sessions can be rebuilt in-page. |
| `serve.sh PORT SESSION [STATE]` | One server with the app's own flags. `OSC=<package dir>` selects the (instrumented) package. |
| `rebuild.js` | In-page: `__preload(files)`, then `__runAll(files, rounds)` rebuilds each through the real `SessionManager.load`, after an empty session, and records the phase timings in `__results`. Preload first, because `load()` resets the server's session path and static files 403 afterwards. |
| `bench.js` | In-page: `benchTabs` (switch time to second frame, discarding any switch during which the page was hidden), `msgCost` / `msgCostArr` (one `setValue(…, {sync:true})`, which is what an incoming message costs), `drawCost` (synchronous `draw()`), `census`. |
| `tabrun.js` | Waits for the build marker, settles, then runs `benchTabs` on synth tabs 1–3. |
| `modeprobe.js` + `oscsend.py` | Arms a long-task and `widget-created` counter, then a real OSC message is sent over UDP (`oscsend.py PORT /lfofreq1 1`). |
| `flatten.js` | The first, in-browser flattener (the build uses `src/tools/flatten-session.py`, a Python port that needs no browser): resolves static `@{…}` with o-s-c's own `resolveProp`, inlines clones, and keeps template tabs as empty placeholders so tab indices are unchanged. `__flatten({templateTabs: ['formuls0', 'Widgets']})`. |
| `receiver.py PORT DIR` | POST sink, used to get large JSON out of the page. |
| `fingerprint.js` + `compare_fp.py` | Equivalence check: every widget's resolved props, value, geometry and computed style, compared between two sessions. |
| `modalgeom.js` + `compare_modal.py` | The same for every popup on synth 1, opened in turn. |
| `gen_tabs.py`, `gen_canvas.py` | Session variants: fewer synth tabs; slider compounds and/or sequencer panels as one canvas. |
| `census.py` | Authored vs expanded widget counts per tab, and clone templates. |
| `extract-defaults.js` | Regenerates `src/tools/osc-defaults-<version>.json` from a live client running the clone-based session. Needed when the vendored o-s-c version changes. |
| `patch-experiments.py` | `--tab-show-unforced`, `--multixy-point-interaction`. Anchored and idempotent. Not applied by the build. |

## Outline

    S=<scratch dir>; cp docs/gui/tools/session/* $S
    cp src/gui/_main.json $S/base.json; cp src/gui/_formuls-default.state $S/base.state
    cp -R "$(ls -d /Applications/formuls-*.app | tail -1)/Contents/Resources/gui/open-stage-control" $S/oscp
    python3 $S/instrument_osc.py $S/oscp
    OSC=$S/oscp $S/serve.sh 9020 base.json base.state &
    python3 $S/receiver.py 9030 $S &

Open `http://127.0.0.1:9020`, and in its console:

    eval(await (await fetch('/flatten.js')).text())
    const {session} = __flatten({templateTabs: ['formuls0', 'Widgets']})
    await fetch('http://127.0.0.1:9030/flat.json', {method: 'POST', body: JSON.stringify(session)})

Then serve `flat.json` on another port, run `fingerprint.js` in both, and compare them
with `compare_fp.py`. For in-page rebuilds, copy each session to `NAME.json.js`
(the server refuses `.json` but serves `.js`).

## Things that cost time

- **A hidden page measures nothing.** With the page hidden, `requestAnimationFrame`
  stops: tab switches read as ~2,000 ms, and canvases are never sized, so `draw()`
  returns early (a "draw cost" of ~1 µs). `benchTabs` now discards any switch during
  which the page was hidden. Check `document.visibilityState` before trusting a
  rendering number.
- **Start only after the session has settled.** A tab bench started a few seconds
  after navigation overlapped the first load and reported 1.7 s switches. `tabrun.js`
  waits for the `b3` mark.
- **`canvas` has no `variables` prop.** It is silently dropped, so per-instance
  constants have to go into the scripts.
- **Inside object props, `OSC{/x, 0}` resolves to the string `"0"`**, which is
  truthy. Coerce it.
- **o-s-c's UDP input port is the same as `--port`.** The sandbox blocks both binding
  it and sending to it.
