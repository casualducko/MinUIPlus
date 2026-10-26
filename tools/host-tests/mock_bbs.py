#!/usr/bin/env python3
"""A tiny stand-in for the Lexaloffle BBS for the Pico Search tool's tests and the Mac harness.
   Prints "PORT n" once it is listening. Serves:
     /bbs/?cat=7&carts_tab=1&page=N&search=Q   the fixture listing on page 1 (more pages if MOCK_PAGES), nothing after
     /bbs/?tid=T                               the fixture thread page (404 for tid 404)
     /bbs/cposts/1/15133.p8.png                a valid PNG, big enough to look like a cart
"""
import http.server, os, random, struct, sys, urllib.parse, zlib

here = os.path.dirname(os.path.abspath(__file__))
def fixture(name): return open(os.path.join(here, "fixtures", name), "rb").read()

def png(w=128, h=128):
    rng = random.Random(1)
    raw = b"".join(b"\x00" + bytes(rng.randrange(256) for x in range(w)) for y in range(h))
    def chunk(t, d): c = struct.pack(">I", len(d)) + t + d; return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")

class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, body, ctype="text/html; charset=utf-8"):
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        u = urllib.parse.urlparse(self.path); q = urllib.parse.parse_qs(u.query, keep_blank_values=True)
        if u.path == "/bbs/cposts/1/15133.p8.png": return self.send(200, png(), "image/png")
        if u.path == "/bbs/" and "tid" in q:
            if q["tid"][0] == "404": return self.send(404, b"not found")
            return self.send(200, fixture("bbs_thread.html"))
        if u.path == "/bbs/" and "search" in q:
            page = int(q.get("page", ["1"])[0])
            if page == 1: return self.send(200, fixture("bbs_search.html"))
            return self.send(200, b"<html><script>pdat=[\n\t];</script></html>")
        self.send(404, b"nope")

srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
print("PORT", srv.server_address[1], flush=True)
srv.serve_forever()
