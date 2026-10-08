"""Simulated Pod Player API (same JSON as the firmware) for previewing and testing the web UI.

Run:   python3 tools/ui/mock_player.py        (serves http://127.0.0.1:8099/)
Test:  node tools/ui/ui_test.mjs               (drives the page in headless Chrome; macOS path to Chrome)
The page is read from player/web_page.h on every request, so edits show up on reload.
Preview helper: /__set?state=playing&link=no_sd&volume=11 changes the simulated state.
"""
from pathlib import Path

def page():
    src = (Path(__file__).resolve().parents[2] / "player" / "web_page.h").read_text()
    return src.split('R"HTML(', 1)[1].rsplit(')HTML"', 1)[0]

import json, time, sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs

st = dict(track=3, count=8, playCmd=3, link="ok", error=0, volume=8, volumeMax=15, state="stopped",
          repeat="off", wifi="sta", ip="172.20.10.2", queue=0)
started = [0.0]; paused_accum = [0.0]; paused_at = [0.0]

def elapsed():
    if st["state"] == "stopped": return 0
    until = paused_at[0] if st["state"] == "paused" else time.time()
    return max(0, int(until - started[0] - paused_accum[0]))

def body(msg=None):
    d = dict(st, elapsed=elapsed())
    if msg: d["msg"] = msg
    return json.dumps(d)

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, data, ctype="application/json"):
        self.send_response(code); self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data.encode()))); self.end_headers(); self.wfile.write(data.encode())
    def do_GET(self):
        u = urlparse(self.path)
        if u.path == "/": return self.send(200, page(), "text/html")
        if u.path == "/api/status": return self.send(200, body())
        if u.path == "/api/health":
            return self.send(200, json.dumps(dict(uptime=7500, heapFree=182000, heapMin=160000, rssi=-58, reset="power-on",
                                                  boots=12, crashes=0, wifi="sta")))
        if u.path == "/__set":      # preview helper: /__set?link=no_sd&state=playing ...
            for k, v in parse_qs(u.query).items():
                st[k] = int(v[0]) if v[0].lstrip("-").isdigit() else v[0]
            if st["state"] == "playing" and not started[0]: started[0] = time.time() - 83
            return self.send(200, body())
        self.send(404, "not found", "text/plain")
    def do_POST(self):
        u = urlparse(self.path); q = parse_qs(u.query)
        if self.headers.get("X-Pod") != "1": return self.send(403, "forbidden", "text/plain")
        p = u.path
        if p == "/api/play":
            if st["state"] == "paused": paused_accum[0] += time.time() - paused_at[0]
            elif st["state"] == "stopped": started[0] = time.time(); paused_accum[0] = 0
            st["state"] = "playing"
        elif p == "/api/pause" and st["state"] == "playing": paused_at[0] = time.time(); st["state"] = "paused"
        elif p == "/api/stop": st["state"] = "stopped"
        elif p in ("/api/next", "/api/prev"):
            d = 1 if p.endswith("next") else -1
            st["track"] = (st["track"] - 1 + d) % st["count"] + 1
            st["state"] = "playing"; started[0] = time.time(); paused_accum[0] = 0
        elif p == "/api/volume": st["volume"] = max(0, min(st["volumeMax"], int(q["v"][0])))
        elif p == "/api/repeat": st["repeat"] = {"off": "all", "all": "one", "one": "off"}[st["repeat"]]
        elif p == "/api/track": st["track"] = int(q["n"][0]); st["state"] = "playing"; started[0] = time.time(); paused_accum[0] = 0
        return self.send(200, body())

HTTPServer(("127.0.0.1", 8099), H).serve_forever()
