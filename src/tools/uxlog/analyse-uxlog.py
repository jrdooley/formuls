#!/usr/bin/env python3
"""analyse-uxlog.py <log.jsonl>... [--out DIR] [--regrab-ms 2000] [--audio]

Turns UX logs from a FORMULS_UXLOG=1 build into numbers about the
interaction design. Standard library only. Writes to DIR (default
./uxlog-report):

  widgets.csv   one row per widget: how often it is touched (hot / cold),
                how often it is swept, and the signs of difficulty, measured
                over adjustments (touches that are not sweeps):
                  sweeps          deliberate back-and-forth playing: at least
                                  --sweep-ms long, finger travel at least
                                  --sweep-ratio times its net movement, and
                                  at least --sweep-turns direction changes
                  sent_rev_*      reversals in the values the participant's
                                  own tablet sent (overshoot, then correct),
                                  mean and median
                  finger_turns_*  direction changes of the finger itself,
                                  mean and median (logs from 2026-10-24 on)
                  leave_rate     share of drags whose finger slid off the widget
                  regrab_rate    share of touches that follow a touch on the
                                 same widget within --regrab-ms (a second go at it)
                  short_rate     share of touches that changed nothing
                Both reversal measures ignore the widget's displayed value,
                which the sound engine's echoes overwrite mid-drag
                (gestures.csv keeps it as display_reversals, for comparison).
  misses.csv    every touch on dead space, with the widget under it if any --
                clusters next to a widget mean its target is too small.
                A tap on a tab bar is navigation, not dead space: builds
                before 2026-10-23 logged one as a miss, and those are
                dropped here (and from heatmap.html) by matching them to
                the tab switch they caused, within --tab-miss-ms
  tabs.csv      navigation: every tab switch and how long the user stayed
  sliders.csv   the slider canvases, per parameter and per LAYER: a slider
                is one canvas whose main fader, chaos / LFO frequency / LFO
                depth / mod depth faders, six mod-source cells and label are
                chosen by the synth's mode buttons, so each (parameter,
                layer) gets the same difficulty measures as widgets.csv,
                with regrab_rate counted on the same parameter and layer
  modes.csv     the same measures summed per layer (main, chaos, lfofreq,
                lfodepth, moddepth, mod, label) across all parameters, and
                how often several modes were on at once
  gestures.csv  one row per completed gesture, for your own statistics, each
                tagged with the REGIME it was made in (see below)
  widgets-by-regime.csv
                widgets.csv split by regime, because a median over touches
                made in different regimes describes neither of them
  heatmap.html  touch-down positions over the screen, per client session,
                misses in a different colour
  sound.csv     with --audio, and tagged with the gesture's regime -- read the
                before-and-after of a "static" row, and treat an "active" one
                as the network's sound rather than the gesture's:
                for every gesture, the sound in the session
                recording (uxaudio-*.ogg, found next to the log) before,
                during and after it -- loudness (RMS dBFS) and brightness
                (spectral centroid, Hz) -- and how much the gesture changed
                them. Needs: pip install numpy soundfile

Regime: whether the engine was modulating the sound while the gesture was
made. "static" means no address in the "in" stream changed within
--regime-ms of it, so the player's hand was the only thing moving the sound
and the before-and-after in sound.csv attributes the change to the gesture.
"active" means modulation was running (mod_n counts the addresses), so that
difference mixes the gesture with the network and -- where the network feeds
back on itself -- cannot be separated from it at all. Pool the two and the
result describes neither, which is why widgets-by-regime.csv exists and why
sound.csv carries the tag. Display feedback (the sequencer playhead, the step
number, the pitch readout) is not modulation and never counts: builds from
2026-10-27 log it separately as "disp", and in an older log the addresses
listed in DISPLAY_FEEDBACK below are moved out of "in" here, which the run
reports.

Timing: the log's "st" stamps arrive in 500 ms batches, so they are late.
Each batch also carries "ft", the tablet's clock as it was sent, so st - ft
is the clock offset plus network delay alone; its minimum over a session
estimates the offset, and every gesture is placed at t + offset (logs
without "ft" fall back to st - t, which is up to 500 ms less precise). Sample 0 of the recording is rec-start's first_block_st.

  untouched.txt widgets that were there but were never touched: cold spots.
                The list is built from the ids seen in the logs themselves --
                the state snapshots (every widget in the page, resolved) plus
                every widget touched -- because the ids in _main.json are
                mostly templates (@{parent.variables...}) filled in per synth
                at run time, and cannot be read off the file. With --layout,
                its literal ids are added and its templated ones counted in
                the file's header. A widget in a tab no session ever opened
                may be missing from the list: open every tab in a pilot run.
"""
import argparse
import collections
import csv
import bisect
import json
import pathlib
import re
import statistics


def load(paths):
    for p in paths:
        with open(p) as f:
            for line in f:
                line = line.strip()
                if line:
                    try:
                        yield json.loads(line)
                    except json.JSONDecodeError:
                        pass  # a line cut short when the app was killed


# What the module writes to the "disp" stream from 2026-10-27 (28439b1): values
# the player never sets and that change no sound, so never evidence that the
# engine was modulating. Kept here only to reclassify logs recorded before that
# change, where they still arrive in "in". Keep in step with DISPLAY_FEEDBACK in
# formuls-uxlog-module.js.
DISPLAY_FEEDBACK = (
    re.compile(r'^/seqpos\d*$'),
    re.compile(r'^/sequencerstep\d*$'),
    re.compile(r'^/oscpitchgui\d*$'),
)


def is_display_feedback(address):
    return any(r.match(address) for r in DISPLAY_FEEDBACK)


def regimes(events, gestures, window_ms):
    """Tag each gesture with the engine's state while it was made.

    Returns the number of addresses reclassified out of a pre-28439b1 log.
    """
    split_log = any(e['k'] == 'disp' for e in events)
    moved = set()
    frames = []
    for e in events:
        if e['k'] != 'in':
            continue
        addrs = set(e.get('m') or {})
        if not split_log:
            display = {a for a in addrs if is_display_feedback(a)}
            moved |= display
            addrs -= display
        if addrs:
            frames.append((e['st'], addrs))
    frames.sort()
    stamps = [f[0] for f in frames]
    off = clock_offsets(events)
    for g in gestures:
        shift = off.get(g['s'])
        end = g['t'] + shift if shift is not None else g['st']
        lo, hi = end - g['dur'] - window_ms, end + window_ms
        active = set()
        for i in range(bisect.bisect_left(stamps, lo), bisect.bisect_right(stamps, hi)):
            active |= frames[i][1]
        g['mod_n'] = len(active)
        g['regime'] = 'active' if active else 'static'
    return len(moved)


def layout_ids(path):
    """The widget ids in a session file, split into (literal, templated).

    Most of formuls's ids are templates -- "@{parent.variables.name}act@
    {parent.variables.n}" -- that o-s-c fills in per clone at run time, so
    only the literal ones name a widget that exists. Resolving the rest
    would mean reimplementing o-s-c's clone and variable handling, and a
    half-resolved id is worse than a missing one: it would read as a real
    widget nobody touched.
    """
    ids, templated = set(), set()

    def walk(node):
        if isinstance(node, dict):
            if node.get('type') not in (None, 'root', 'panel', 'folder', 'tab', 'text', 'frame') and 'id' in node:
                (templated if '@{' in str(node['id']) else ids).add(node['id'])
            for v in node.values():
                walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)
    walk(json.load(open(path)))
    return ids, templated


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--out', default='uxlog-report')
    ap.add_argument('--regrab-ms', type=int, default=2000)
    ap.add_argument('--sweep-ms', type=int, default=1500, help='a sweep lasts at least this long')
    ap.add_argument('--sweep-ratio', type=float, default=3.0,
                    help='finger travel of a sweep, as a multiple of its net movement')
    ap.add_argument('--sweep-turns', type=int, default=4, help='direction changes a sweep has at least')
    ap.add_argument('--layout', help='_main.json, to add its literal widget ids to untouched.txt')
    ap.add_argument('--regime-ms', type=int, default=1000,
                    help='modulation this close to a gesture makes its regime "active"')
    ap.add_argument('--tab-miss-ms', type=int, default=250,
                    help='a pre-2026-10-23 miss this close to a tab switch is that tab tap')
    ap.add_argument('--audio', action='store_true', help='measure the sound around each gesture')
    ap.add_argument('--pre-ms', type=int, default=500, help='audio window before a gesture')
    ap.add_argument('--post-ms', type=int, default=1000, help='audio window after a gesture')
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    events = sorted(load(a.logs), key=lambda e: (e.get('st', 0), e.get('seq', 0)))
    gestures, misses, tabs, downs = [], [], [], collections.defaultdict(list)
    seen_ids = set()      # every widget id the logs show was really in the page
    last_touch = {}       # session -> (widget, server time of the up)
    last_layer = {}       # session -> ((parameter, layer), server time of the up)
    open_downs = {}       # (session, pointer) -> its down event
    viewports = {}

    for e in events:
        s = e.get('s')
        if e['k'] in ('start', 'resize', 'orientation'):
            viewports[s] = e.get('vp')
        elif e['k'] == 'down':
            downs[s].append(e)
            open_downs[(s, e.get('p'))] = e
            if e.get('miss'):
                misses.append(e)
        elif e['k'] in ('up', 'cancel') and e.get('w'):
            d = open_downs.pop((s, e.get('p')), {})
            for k in ('layer', 'cell', 'modes'):
                if d.get(k) is not None:
                    e[k] = d[k]
            if e.get('param') and e.get('layer'):
                key = (e['param'], e['layer'])
                prev = last_layer.get(s)
                e['layer_regrab'] = bool(prev and prev[0] == key and e['t'] - e['dur'] - prev[1] <= a.regrab_ms)
                last_layer[s] = (key, e['t'])
            prev = last_touch.get(s)
            e['regrab'] = bool(prev and prev[0] == e['w'] and e['t'] - e['dur'] - prev[1] <= a.regrab_ms)
            last_touch[s] = (e['w'], e['t'])
            gestures.append(e)
        elif e['k'] == 'tab':
            tabs.append(e)
        elif e['k'] == 'snap':
            seen_ids |= set(e.get('vals') or {})

    # A tap on a tab bar hits no widget, so builds before 2026-10-23 logged
    # it as dead space -- which would count the interface's most successful
    # interaction as a failure. Such a tap is the one that caused the tab
    # switch logged at the same moment, so match them and drop them. Clearing
    # the flag as well keeps heatmap.html in step with misses.csv.
    tab_misses = 0
    for m in misses:
        if any(t.get('s') == m.get('s') and abs(t['st'] - m['st']) <= a.tab_miss_ms for t in tabs):
            m['miss'] = False
            m['tab_tap'] = True
            tab_misses += 1
    misses = [m for m in misses if not m.get('tab_tap')]

    sent_values(events, gestures)
    for g in gestures:
        turns = g.get('finger_turns')
        turns = turns if turns is not None else g.get('sent_rev')
        g['sweep'] = bool(g['dur'] >= a.sweep_ms and g['path'] >= a.sweep_ratio * max(g['dist'], 1)
                          and (turns or 0) >= a.sweep_turns)

    moved = regimes(events, gestures, a.regime_ms)
    if moved:
        print(f'{moved} display-feedback address(es) moved out of the "in" stream: '
              f'log predates formuls 28439b1')

    by = collections.defaultdict(list)
    for g in gestures:
        by[g['w']].append(g)

    rows = [dict({'widget': w, 'type': gs[0].get('wt')}, **measures(gs, 'regrab')) for w, gs in by.items()]
    rows.sort(key=lambda r: -r['touches'])
    write_csv(out / 'widgets.csv', rows)

    wr = collections.defaultdict(list)
    for g in gestures:
        wr[(g['w'], g['regime'])].append(g)
    wrows = [dict({'widget': w, 'type': gs[0].get('wt'), 'regime': reg,
                   'mod_n_median': int(statistics.median(g['mod_n'] for g in gs))},
                  **measures(gs, 'regrab')) for (w, reg), gs in wr.items()]
    wrows.sort(key=lambda r: (-r['touches'], r['widget'], r['regime']))
    write_csv(out / 'widgets-by-regime.csv', wrows)

    sliders = collections.defaultdict(list)
    for g in gestures:
        if g.get('param') and g.get('layer'):
            sliders[(g['param'], g['layer'])].append(g)
    srows = []
    for (param, layer), gs in sliders.items():
        m = re.match(r'/(.*?)(\d+)$', param)
        srows.append(dict({'parameter': m.group(1) if m else param, 'synth': m.group(2) if m else '',
                           'layer': layer,
                           'cells': ' '.join(str(c) for c in sorted({g['cell'] for g in gs if g.get('cell') is not None}))},
                          **measures(gs, 'layer_regrab')))
    srows.sort(key=lambda r: (r['parameter'], r['synth'], LAYERS.index(r['layer']) if r['layer'] in LAYERS else 99))
    write_csv(out / 'sliders.csv', srows)

    layers = collections.defaultdict(list)
    for gs in sliders.values():
        for g in gs:
            layers[g['layer']].append(g)
    write_csv(out / 'modes.csv', [dict({'layer': l,
                                        'parameters': len({g['param'] for g in layers[l]}),
                                        'several_modes_on': sum(len(g.get('modes') or []) > 1 for g in layers[l])},
                                       **measures(layers[l], 'layer_regrab'))
                                  for l in LAYERS if l in layers])

    write_csv(out / 'misses.csv', [
        {'st': m['st'], 'session': m['s'], 'x': m['x'], 'y': m['y'],
         'vw': m['vp'][0], 'vh': m['vp'][1], 'under': m.get('under') or ''} for m in misses])

    tab_rows = []
    for i, t in enumerate(tabs):
        nxt = next((u for u in tabs[i + 1:] if u['s'] == t['s'] and u.get('in') == t.get('in')), None)
        tab_rows.append({'st': t['st'], 'session': t['s'], 'group': t.get('in'),
                         'tab': t['label'] or '#%s' % t.get('idx'),   # icon-only tabs logged before the fix
                         'stay_s': round((nxt['st'] - t['st']) / 1000, 1) if nxt else ''})
    write_csv(out / 'tabs.csv', tab_rows)

    write_csv(out / 'gestures.csv', [
        dict({k: g.get(k) for k in ('st', 's', 'c', 'w', 'wt', 'param', 'layer', 'cell', 'addr', 'k', 'dur',
                                    'dist', 'path', 'left', 'changes', 'sweep', 'regime', 'mod_n',
                                    'sent_n', 'sent_rev', 'finger_turns', 'regrab', 'v0', 'v1')},
             display_reversals=g.get('reversals')) for g in gestures])

    heatmap(out / 'heatmap.html', downs, viewports)

    if a.audio:
        sound(out / 'sound.csv', events, gestures, a)

    static = sum(g['regime'] == 'static' for g in gestures)
    print(f'{len(gestures)} gestures ({static} static, {len(gestures) - static} with the '
          f'engine modulating), {len(misses)} misses '
          f'({tab_misses} tab tap(s) dropped), {len(tabs)} tab switches '
          f'over {len(downs)} client session(s) -> {out}/')

    touched = set(by)
    universe = seen_ids | touched
    note = [f'# widgets that were there but were never touched, of {len(universe)} seen',
            f'# {len(seen_ids)} from the state snapshots, {len(touched)} touched']
    if a.layout:
        literal, templated = layout_ids(a.layout)
        universe |= literal
        note.append(f'# {len(literal)} literal ids from {a.layout}; '
                    f'{len(templated)} of its ids are templates and cannot be resolved here')
    note.append('# a widget in a tab no session opened may be missing: open every tab in a pilot run')
    cold = sorted(universe - touched)
    (out / 'untouched.txt').write_text('\n'.join(note + cold) + '\n')
    print(f'{len(cold)} of {len(universe)} widgets never touched -> {out}/untouched.txt')


def clock_offsets(events):
    off = {}
    for e in events:
        if 't' in e and 's' in e and 'st' in e:
            off[e['s']] = min(off.get(e['s'], float('inf')), e['st'] - e.get('ft', e['t']))
    return off


def sound(path, events, gestures, a):
    try:
        import numpy as np
        import soundfile as sf
    except ImportError:
        print('--audio needs numpy and soundfile: pip install numpy soundfile')
        return
    recs = [e for e in events if e['k'] == 'rec-start' and e.get('first_block_st')]
    if not recs:
        print('--audio: no rec-start event in the log(s)')
        return
    logdirs = {pathlib.Path(p).resolve().parent for p in a.logs}
    off = clock_offsets(events)
    files = {}
    rows = []

    def feats(x, sr):
        if len(x) < 256:
            return None, None
        rms = float(np.sqrt(np.mean(x ** 2)))
        spec = np.abs(np.fft.rfft(x * np.hanning(len(x))))
        freqs = np.fft.rfftfreq(len(x), 1 / sr)
        cen = float((spec * freqs).sum() / spec.sum()) if spec.sum() > 0 else 0.0
        return round(20 * np.log10(max(rms, 1e-9)), 1), round(cen)

    for g in gestures:
        if g['s'] not in off:
            continue
        end = g['t'] + off[g['s']]          # gesture end on the computer's clock
        start = end - g['dur']
        rec = max((r for r in recs if r['first_block_st'] <= start), key=lambda r: r['first_block_st'], default=None)
        if rec is None:
            continue
        if rec['file'] not in files:
            f = next((d / rec['file'] for d in logdirs if (d / rec['file']).exists()), None)
            files[rec['file']] = sf.SoundFile(str(f)) if f else None
        f = files[rec['file']]
        if f is None:
            continue
        sr = f.samplerate

        def clip(t0, t1):
            i0 = int((t0 - rec['first_block_st']) * sr / 1000)
            i1 = int((t1 - rec['first_block_st']) * sr / 1000)
            if i0 < 0 or i1 > f.frames or i1 <= i0:
                return np.zeros(0)
            f.seek(i0)
            return f.read(i1 - i0, dtype='float32', always_2d=True).mean(axis=1)

        pre = feats(clip(start - a.pre_ms, start), sr)
        dur = feats(clip(start, end), sr)
        post = feats(clip(end, end + a.post_ms), sr)
        delta = lambda i: round(post[i] - pre[i], 1) if None not in (pre[i], post[i]) else ''
        rows.append({'st': round(start), 'session': g['s'], 'widget': g['w'], 'type': g.get('wt'),
                     'regime': g.get('regime'), 'mod_n': g.get('mod_n'),
                     'dur': g['dur'], 'reversals': g['reversals'], 'v0': g.get('v0'), 'v1': g.get('v1'),
                     'audio_file': rec['file'], 'audio_s': round((start - rec['first_block_st']) / 1000, 3),
                     'db_pre': pre[0], 'db_during': dur[0], 'db_post': post[0], 'db_change': delta(0),
                     'centroid_pre': pre[1], 'centroid_during': dur[1], 'centroid_post': post[1],
                     'centroid_change': delta(1)})
    write_csv(path, rows)
    print(f'{len(rows)} gestures matched to session audio -> {path}')


# slider layers in slider-lib.js's stacking order, bottom to top
LAYERS = ['main', 'chaos', 'lfofreq', 'lfodepth', 'moddepth', 'mod', 'label']


def mean_median(xs):
    xs = [x for x in xs if x is not None]
    return (round(statistics.mean(xs), 2), statistics.median(xs)) if xs else ('', '')


def measures(gs, regrab_key):
    adj = [g for g in gs if not g['sweep']]           # adjustments: what difficulty is measured on
    drags = [g for g in adj if g['path'] > 10]
    rev_mean, rev_median = mean_median(g.get('sent_rev') for g in adj)
    turn_mean, turn_median = mean_median(g.get('finger_turns') for g in adj)
    n = len(adj)
    return {
        'touches': len(gs),
        'sessions': len({g['s'] for g in gs}),
        'sweeps': len(gs) - n,
        'dur_median_ms': int(statistics.median(g['dur'] for g in adj)) if adj else '',
        'sent_rev_mean': rev_mean, 'sent_rev_median': rev_median,
        'finger_turns_mean': turn_mean, 'finger_turns_median': turn_median,
        'leave_rate': round(sum(g['left'] for g in drags) / len(drags), 2) if drags else '',
        'regrab_rate': round(sum(bool(g.get(regrab_key)) for g in adj) / n, 2) if n else '',
        'short_rate': round(sum(g['changes'] == 0 for g in adj) / n, 2) if n else '',
        'cancelled': sum(g['k'] == 'cancel' for g in gs),
    }


# slider layer -> its address, from the parameter's main address, for logs that
# predate the addr field: /velocity1 + chaos -> /velocitychaos1, mod cell 3 -> /velocitymod1/3
def control_address(g):
    if g.get('addr'):
        return g['addr']
    if g.get('param') and g.get('layer'):
        if g['layer'] == 'main':
            return g['param']
        m = re.match(r'(/.*?)(\d+)$', g['param'])
        if m:
            a = m.group(1) + g['layer'] + m.group(2)
            return a + '/%s' % g['cell'] if g['layer'] == 'mod' else a
    w = g.get('w') or ''
    return None if w.startswith('tab:') else '/' + w


def flips(seq):
    # direction changes in a run of values; multi-value controls per element
    n, last, sign = 0, None, {}
    for v in seq:
        vs = v if isinstance(v, list) else [v]
        if last is not None:
            for i, x in enumerate(vs):
                if i < len(last) and isinstance(x, (int, float)) and isinstance(last[i], (int, float)):
                    d = (x > last[i]) - (x < last[i])
                    if d and sign.get(i) and d != sign[i]:
                        n += 1
                    if d:
                        sign[i] = d
        last = vs
    return n


def sent_values(events, gestures):
    """Attach to each gesture what its own tablet sent on its control's
    address while the finger was down (sent_n, sent_rev), and the finger's
    own direction changes (finger_turns). Sends are timed by the computer's
    clock (st); the gesture is placed on that clock with its tablet's offset."""
    off = clock_offsets(events)
    sends = collections.defaultdict(list)          # (client, address) -> [(st, value)]
    for e in events:
        if e['k'] == 'param':
            v = e['v'][0] if len(e['v']) == 1 else e['v']
            sends[(e.get('c'), e['a'])].append((e['st'], v))
    for g in gestures:
        if g.get('fturns_x') is not None:
            g['finger_turns'] = g['fturns_x'] + g.get('fturns_y', 0)
        g['addr'] = control_address(g)
        if g['s'] not in off or not g['addr']:
            continue
        end = g['t'] + off[g['s']]
        start = end - g['dur']
        # sends leave the tablet as the finger moves; allow for the network
        seq = [v for st, v in sends.get((g.get('c'), g['addr']), []) if start - 30 <= st <= end + 150]
        g['sent_n'] = len(seq)
        g['sent_rev'] = flips(seq)


def write_csv(path, rows):
    with open(path, 'w', newline='') as f:
        if not rows:
            return
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)


def heatmap(path, downs, viewports):
    data = {s: {'vp': viewports.get(s) or d[0]['vp'],
                'pts': [[e['x'], e['y'], 1 if e.get('miss') else 0] for e in d]} for s, d in downs.items()}
    path.write_text("""<!doctype html><meta charset=utf-8><title>formuls touch heatmap</title>
<style>body{font:14px system-ui;margin:16px;background:#111;color:#ddd}canvas{background:#000;max-width:100%;border:1px solid #444}</style>
<p>Touch-downs per client session. <span style="color:#f80">Orange</span> on widgets, <span style="color:#3cf">blue</span> missed (dead space). Brighter = more.</p>
<div id=c></div><script>
const D=""" + json.dumps(data) + """;
for(const [s,{vp,pts}] of Object.entries(D)){
  const h=document.createElement('h3');h.textContent='session '+s+' ('+vp[0]+'x'+vp[1]+', '+pts.length+' touches)';c.append(h);
  const cv=document.createElement('canvas');cv.width=vp[0];cv.height=vp[1];c.append(cv);
  const g=cv.getContext('2d');g.globalCompositeOperation='lighter';
  for(const [x,y,m] of pts){const r=g.createRadialGradient(x,y,0,x,y,24);
    r.addColorStop(0,m?'rgba(50,200,255,.35)':'rgba(255,130,0,.25)');r.addColorStop(1,'rgba(0,0,0,0)');
    g.fillStyle=r;g.fillRect(x-24,y-24,48,48);}
}
</script>""")


if __name__ == '__main__':
    main()
