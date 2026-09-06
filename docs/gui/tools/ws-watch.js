// Connects to a running app's Open Stage Control server as an extra headless
// "tablet" and logs the frames it receives, so you can see exactly what the
// server tells a widget to do while you drive the real interface by hand.
//
// The server broadcasts widget updates to every connected client, so this sees
// the same messages the real client sees without touching it, without a browser
// console, and without going near the OSC ports (9000/9001 udp) that
// osc-sink.py must not be pointed at while the app is running.
//
//   <app>/Contents/Resources/gui/node ws-watch.js resonantfilterfreqq
//   <app>/Contents/Resources/gui/node ws-watch.js resonantfilterfreqq 9001 watch
//
// Arguments: a substring to match against each frame (omit or pass "" for
// everything), the server's --port (9001 in the shipped app), and a client id.
// Needs node 22+ for the global WebSocket; the bundled gui/node is 22.
//
// Every matching frame is printed as
//   <ms since first match>  <address> [args]
// falling back to the raw frame text when it is not an address/args pair.
// SIGINT prints totals.

const match = process.argv[2] === undefined ? '' : process.argv[2];
const port = process.argv[3] || '9001';
const id = process.argv[4] || 'ws-watch';

let frames = 0, matched = 0, t0 = null;

function describe(text) {
  // o-s-c frames are JSON arrays; the shapes carrying widget values put the
  // address and args either in an object or in positional slots. Try both,
  // then give up and show the frame as it arrived.
  let parsed;
  try { parsed = JSON.parse(text); } catch { return text; }
  if (!Array.isArray(parsed)) return text;
  const body = parsed[1];
  if (body && typeof body === 'object' && !Array.isArray(body)) {
    const addr = body.address ?? body.path ?? body.id;
    if (addr !== undefined) {
      const args = body.args ?? body.value ?? body.v;
      const vals = Array.isArray(args)
        ? args.map((a) => (a && typeof a === 'object' && 'value' in a ? a.value : a))
        : args;
      return `${parsed[0]} ${addr} ${JSON.stringify(vals)}`;
    }
  }
  return text;
}

const s = new WebSocket(`ws://127.0.0.1:${port}/${id}/`);

s.onopen = () => {
  console.log(`connected to ws://127.0.0.1:${port}/${id}/`);
  console.log(match ? `logging frames containing "${match}"` : 'logging every frame');
  console.log('drive the interface now; ctrl-c to stop\n');
};

s.onmessage = (e) => {
  const text = typeof e.data === 'string' ? e.data : String(e.data);
  frames++;
  if (text === '["ping"]') { s.send('["pong"]'); return; }
  if (match && !text.includes(match)) return;
  if (t0 === null) t0 = Date.now();
  matched++;
  console.log(`${String(Date.now() - t0).padStart(7)} ms  ${describe(text)}`);
};

s.onerror = (e) => {
  console.error('websocket error -- is the app running?', e.message || '');
};

s.onclose = () => {
  console.log('\nconnection closed');
  process.exit(0);
};

process.on('SIGINT', () => {
  console.log(`\n${matched} matching frames of ${frames} received`);
  process.exit(0);
});

setInterval(() => {}, 1 << 30);
