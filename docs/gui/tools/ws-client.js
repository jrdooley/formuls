// A headless extra "tablet": connects like a browser client, counts frames,
// answers pings, renders nothing. Used to measure how server cost scales
// with the number of connected clients.
//
//   <app>/Contents/Resources/gui/node ws-client.js probe1
//
// Needs node 22+ for the global WebSocket. SIGINT prints the totals.
const id = process.argv[2] || 'probe';
const port = process.argv[3] || '19001';
let frames = 0, bytes = 0;

const s = new WebSocket(`ws://127.0.0.1:${port}/${id}/`);
s.onopen = () => console.log('open', id);
s.onmessage = (e) => {
  frames++;
  bytes += (e.data && e.data.length) || 0;
  if (e.data === '["ping"]') s.send('["pong"]');
};
s.onclose = () => console.log('closed', id);

process.on('SIGINT', () => {
  console.log(JSON.stringify({ id, frames, bytes }));
  process.exit(0);
});
setInterval(() => {}, 1 << 30);
