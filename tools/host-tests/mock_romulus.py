#!/usr/bin/env python3
"""A stand-in romulus server for tests, following ROMULUS_HANDOFF.md: bearer token,
GET /api/platforms, /api/roms (platform, platforms, q, offset, limit, one) and
/api/roms/{id}/download. Prints "PORT n" once it listens.
usage: mock_romulus.py TOKEN"""
import io, json, re, sys, time, zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

TOKEN = sys.argv[1] if len(sys.argv) > 1 else "secret"

ROMS = []
def add(platform, n, ext, region="USA"):
    for i in range(n):
        rid = len(ROMS) + 1
        ROMS.append(dict(id=rid, name=f"{platform.upper()} Game {i:03d}", platform=platform, region=region if i % 3 else "",
                         file_name=f"{platform.upper()} Game {i:03d}.{ext}"))
add("nes", 70, "nes"); add("gba", 30, "gba"); add("gb", 20, "gb"); add("ps2", 5, "iso")
# a few special ones
for r in ROMS:
    if r["platform"] == "gb" and r["id"] % 5 == 0: r["is_dir"] = True # disc sets: counted, never listed
ROMS.append(dict(id=900, name="Zipped (World)", platform="nes", region="World", file_name="Zipped (World).zip"))
ROMS.append(dict(id=901, name="Big Slow", platform="gba", region="", file_name="Big Slow.gba"))
ROMS.append(dict(id=902, name="Café テスト \U0001F600", platform="nes", region="", file_name="Café テスト.nes"))
ROMS.append(dict(id=903, name="Evil", platform="nes", region="", file_name="../../etc/Evil Name?.nes"))

def body_for(rom):
    if rom["id"] == 901:
        return bytes((i * 7) & 255 for i in range(3 * 1024 * 1024))
    if rom["id"] == 900:
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("readme.txt", "hello")
            z.writestr("roms/Zipped (World).nes", b"NES\x1a" + bytes(range(256)) * 40)
        return buf.getvalue()
    return (b"ROM%d-" % rom["id"]) * 700

for r in ROMS:
    r["size"] = len(body_for(r))

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def send_json(self, obj, code=200):
        data = json.dumps(obj).encode()
        self.send_response(code); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
    def do_GET(self):
        u = urlparse(self.path); q = parse_qs(u.query, keep_blank_values=True)
        if not u.path.startswith("/api/"):
            return self.send_json({"error": "not found"}, 404)
        if self.headers.get("Authorization") != "Bearer " + TOKEN:
            return self.send_json({"error": "unauthorized"}, 401)
        if u.path == "/api/platforms":
            counts = {}
            for r in ROMS: counts[r["platform"]] = counts.get(r["platform"], 0) + 1
            return self.send_json([dict(slug=s, count=c, name=s.upper()) for s, c in counts.items()])
        if u.path == "/api/roms":
            plats = None
            if "platform" in q: plats = {q["platform"][0]}
            elif "platforms" in q: plats = set(q["platforms"][0].split(","))
            text = q.get("q", [""])[0].lower()
            offset = int(q.get("offset", ["0"])[0]); limit = int(q.get("limit", ["50"])[0])
            rows = [r for r in ROMS if (plats is None or r["platform"] in plats) and text in r["name"].lower()]
            page = [r for r in rows[offset:offset + limit] if not r.get("is_dir")] # cut first, then drop disc sets, as the real one does
            return self.send_json({"total": len(rows), "roms": page})
        m = re.fullmatch(r"/api/roms/(\d+)/download", u.path)
        if m:
            rom = next((r for r in ROMS if r["id"] == int(m.group(1))), None)
            if not rom: return self.send_json({"error": "no such rom"}, 404)
            data = body_for(rom)
            self.send_response(200); self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data))); self.end_headers()
            try:
                if rom["id"] == 901: # slow: 3 MB at about 6 MB/s... in pieces, so a cancel has time to happen
                    for i in range(0, len(data), 65536):
                        self.wfile.write(data[i:i + 65536]); self.wfile.flush(); time.sleep(0.05)
                else:
                    self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass
            return
        self.send_json({"error": "not found"}, 404)

srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
print("PORT %d" % srv.server_address[1], flush=True)
srv.serve_forever()
