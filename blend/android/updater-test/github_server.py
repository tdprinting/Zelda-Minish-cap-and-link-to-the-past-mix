# Stand-in GitHub releases API for the updater tests: wants a token on the API host and none after the redirect.
import http.server, sys, json, hashlib
ZIP = sys.argv[1]
data = open(ZIP,'rb').read()
rel = {"tag_name": "v1.2", "name": "v1.2", "assets": [
  {"url": "http://127.0.0.1:8766/repos/tee/builds/releases/assets/1", "id": 1, "name": "notes.txt", "digest": "sha256:" + "0"*64},
  {"url": "http://127.0.0.1:8766/repos/tee/builds/releases/assets/2", "id": 2, "name": "picori-seal-v1.2.zip",
   "browser_download_url": "http://x/y", "digest": "sha256:" + hashlib.sha256(data).hexdigest()}]}
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(s):
        s.log_message("%s host=%s auth=%s accept=%s", s.path, s.headers.get('Host'), s.headers.get('Authorization'), s.headers.get('Accept'))
        if s.path == '/repos/tee/builds/releases/latest':
            if s.headers.get('Authorization') != 'Bearer secret-token': s.send_response(404); s.end_headers(); return
            b = json.dumps(rel, indent=2).encode(); s.send_response(200); s.send_header('Content-Length', str(len(b))); s.end_headers(); s.wfile.write(b); return
        if s.path == '/repos/tee/builds/releases/assets/2':
            if s.headers.get('Accept') != 'application/octet-stream' or s.headers.get('Authorization') != 'Bearer secret-token':
                s.send_response(415); s.end_headers(); return
            s.send_response(302); s.send_header('Location', 'http://localhost:8766/blob/zip?X-Amz-Signature=xyz'); s.end_headers(); return
        if s.path.startswith('/blob/zip?X-Amz-Signature=xyz'):
            if s.headers.get('Authorization'): s.send_response(400); s.end_headers(); return
            s.send_response(200); s.send_header('Content-Length', str(len(data))); s.end_headers(); s.wfile.write(data); return
        s.send_response(404); s.end_headers()
http.server.HTTPServer(('127.0.0.1', 8766), H).serve_forever()
