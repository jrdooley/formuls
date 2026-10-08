# Tiny POST sink: writes the request body to OUT. Used to get large JSON out of the page.
import http.server, sys
OUT = sys.argv[2]
class H(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers['Content-Length']); body = self.rfile.read(n)
        name = self.path.strip('/') or 'out.json'
        open(OUT + '/' + name, 'wb').write(body)
        self.send_response(200); self.send_header('Access-Control-Allow-Origin', '*'); self.end_headers(); self.wfile.write(b'ok')
    def log_message(self, *a): pass
http.server.HTTPServer(('127.0.0.1', int(sys.argv[1])), H).serve_forever()
