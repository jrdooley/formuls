#!/usr/bin/env python3
"""analyse-uxlog.py <log.jsonl>... [--out DIR] [--regrab-ms 2000] [--audio]

Turns UX logs from a FORMULS_UXLOG=1 build into numbers about the
interaction design. Standard library only. Writes to DIR (default
./uxlog-report):

  widgets.csv   one row per widget: how often it is touched (hot / cold),
                and the signs of difficulty --
                  reversal_mean  value direction flips per gesture
                                 (overshoot, then correct)
                  leave_rate     share of drags whose finger slid off the widget
                  regrab_rate    share of touches that follow a touch on the
                                 same widget within --regrab-ms (a second go at it)
                  short_rate     share of touches that changed nothing
  misses.csv    every touch on dead space, with the widget under it if any --
                clusters next to a widget mean its target is too small
  tabs.csv      navigation: every tab switch and how long the user stayed
  gestures.csv  one row per completed gesture, for your own statistics
  heatmap.html  touch-down positions over the screen, per client session,
                misses in a different colour
  sound.csv     with --audio: for every gesture, the sound in the session
                recording (uxaudio-*.ogg, found next to the log) before,
                during and after it -- loudness (RMS dBFS) and brightness
                (spectral centroid, Hz) -- and how much the gesture changed
                them. Needs: pip install numpy soundfile

Timing: the log's "st" stamps arrive in 500 ms batches, so they are late.
Each batch also carries "ft", the tablet's clock as it was sent, so st - ft
is the clock offset plus network delay alone; its minimum over a session
estimates the offset, and every gesture is placed at t + offset (logs
without "ft" fall back to st - t, which is up to 500 ms less precise). Sample 0 of the recording is rec-start's first_block_st.

Widgets never touched at all are cold spots too: compare widgets.csv with
the widget ids in src/gui/_main.json (pass --layout to list them).
"""
import argparse
import collections
import csv
import json
import pathlib
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


def layout_ids(path):
    ids = set()

    def walk(node):
        if isinstance(node, dict):
            if node.get('type') not in (None, 'root', 'panel', 'folder', 'tab', 'text', 'frame') and 'id' in node:
                ids.add(node['id'])
            for v in node.values():
                walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)
    walk(json.load(open(path)))
    return ids


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--out', default='uxlog-report')
    ap.add_argument('--regrab-ms', type=int, default=2000)
    ap.add_argument('--layout', help='_main.json, to list widgets nobody touched')
    ap.add_argument('--audio', action='store_true', help='measure the sound around each gesture')
    ap.add_argument('--pre-ms', type=int, default=500, help='audio window before a gesture')
    ap.add_argument('--post-ms', type=int, default=1000, help='audio window after a gesture')
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    events = sorted(load(a.logs), key=lambda e: (e.get('st', 0), e.get('seq', 0)))
    gestures, misses, tabs, downs = [], [], [], collections.defaultdict(list)
    last_touch = {}       # session -> (widget, server time of the up)
    viewports = {}

    for e in events:
        s = e.get('s')
        if e['k'] in ('start', 'resize', 'orientation'):
            viewports[s] = e.get('vp')
        elif e['k'] == 'down':
            downs[s].append(e)
            if e.get('miss'):
                misses.append(e)
        elif e['k'] in ('up', 'cancel') and e.get('w'):
            prev = last_touch.get(s)
            e['regrab'] = bool(prev and prev[0] == e['w'] and e['t'] - e['dur'] - prev[1] <= a.regrab_ms)
            last_touch[s] = (e['w'], e['t'])
            gestures.append(e)
        elif e['k'] == 'tab':
            tabs.append(e)

    by = collections.defaultdict(list)
    for g in gestures:
        by[g['w']].append(g)

    rows = []
    for w, gs in by.items():
        drags = [g for g in gs if g['path'] > 10]
        rows.append({
            'widget': w, 'type': gs[0].get('wt'), 'touches': len(gs),
            'sessions': len({g['s'] for g in gs}),
            'dur_median_ms': int(statistics.median(g['dur'] for g in gs)),
            'reversal_mean': round(statistics.mean(g['reversals'] for g in gs), 2),
            'leave_rate': round(sum(g['left'] for g in drags) / len(drags), 2) if drags else '',
            'regrab_rate': round(sum(g['regrab'] for g in gs) / len(gs), 2),
            'short_rate': round(sum(g['changes'] == 0 for g in gs) / len(gs), 2),
            'cancelled': sum(g['k'] == 'cancel' for g in gs),
        })
    rows.sort(key=lambda r: -r['touches'])
    write_csv(out / 'widgets.csv', rows)

    write_csv(out / 'misses.csv', [
        {'st': m['st'], 'session': m['s'], 'x': m['x'], 'y': m['y'],
         'vw': m['vp'][0], 'vh': m['vp'][1], 'under': m.get('under') or ''} for m in misses])

    tab_rows = []
    for i, t in enumerate(tabs):
        nxt = next((u for u in tabs[i + 1:] if u['s'] == t['s'] and u.get('in') == t.get('in')), None)
        tab_rows.append({'st': t['st'], 'session': t['s'], 'group': t.get('in'), 'tab': t['label'],
                         'stay_s': round((nxt['st'] - t['st']) / 1000, 1) if nxt else ''})
    write_csv(out / 'tabs.csv', tab_rows)

    write_csv(out / 'gestures.csv', [
        {k: g.get(k) for k in ('st', 's', 'c', 'w', 'wt', 'k', 'dur', 'dist', 'path', 'left',
                               'changes', 'reversals', 'regrab', 'v0', 'v1')} for g in gestures])

    heatmap(out / 'heatmap.html', downs, viewports)

    if a.audio:
        sound(out / 'sound.csv', events, gestures, a)

    print(f'{len(gestures)} gestures, {len(misses)} misses, {len(tabs)} tab switches '
          f'over {len(downs)} client session(s) -> {out}/')
    if a.layout:
        cold = sorted(layout_ids(a.layout) - set(by))
        (out / 'untouched.txt').write_text('\n'.join(cold) + '\n')
        print(f'{len(cold)} widgets never touched -> {out}/untouched.txt')


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
                     'dur': g['dur'], 'reversals': g['reversals'], 'v0': g.get('v0'), 'v1': g.get('v1'),
                     'audio_file': rec['file'], 'audio_s': round((start - rec['first_block_st']) / 1000, 3),
                     'db_pre': pre[0], 'db_during': dur[0], 'db_post': post[0], 'db_change': delta(0),
                     'centroid_pre': pre[1], 'centroid_during': dur[1], 'centroid_post': post[1],
                     'centroid_change': delta(1)})
    write_csv(path, rows)
    print(f'{len(rows)} gestures matched to session audio -> {path}')


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
