# Stand-in latest.json host for the updater tests (serves a folder; /redirect bounces to the zip).
import http.server, sys, os
ROOT = sys.argv[1]
class H(http.server.SimpleHTTPRequestHandler):
    def __init__(s, *a, **k): super().__init__(*a, directory=ROOT, **k)
    def do_GET(s):
        s.log_message("auth=%s", s.headers.get('Authorization'))
        if s.path.startswith('/redirect'):
            s.send_response(302); s.send_header('Location', 'http://127.0.0.1:8765/picori-seal-v1.2.zip?sig=abc'); s.end_headers(); return
        if '?' in s.path: s.path = s.path.split('?')[0]
        super().do_GET()
http.server.HTTPServer(('127.0.0.1', 8765), H).serve_forever()
