#!/usr/bin/env python3
"""A tiny stand-in for the GitHub releases API, for the Update tool on this computer.
   mock_github.py <dir> [good|badsum|badfile|badpath|none]   serves <dir>'s package and prints PORT n."""
import http.server, json, os, sys, hashlib, tarfile, io, socketserver

root = sys.argv[1]; mode = sys.argv[2] if len(sys.argv) > 2 else "good"
tag = "v2099.01.01.1"

def make_package():
    buf = io.BytesIO()
    files = {"VERSION": (tag + "\n").encode(),
             "install.sh": b"#!/bin/sh\necho installed\n",
             "Tools/rgb30/Demo.pak/launch.sh": b"#!/bin/sh\n"}
    sums = "".join("%s  %s\n" % (hashlib.sha256(v).hexdigest(), k) for k, v in files.items() if k not in ("VERSION", "install.sh"))
    files["SHA256SUMS"] = sums.encode()
    if mode == "badfile":
        files["Tools/rgb30/Demo.pak/launch.sh"] = b"changed after the sums\n"
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        for k, v in files.items():
            i = tarfile.TarInfo(k); i.size = len(v); i.mode = 0o755; t.addfile(i, io.BytesIO(v))
        if mode == "badpath":
            i = tarfile.TarInfo("../escape.txt"); i.size = 1; t.addfile(i, io.BytesIO(b"x"))
    return buf.getvalue()

pkg = make_package()
name = "MinUIPlus-%s.tar.gz" % tag
sha = hashlib.sha256(pkg).hexdigest()

class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        base = "http://127.0.0.1:%d" % self.server.server_address[1]
        if self.path == "/repos/o/r/releases/latest":
            if mode == "none":
                self.send_response(404); self.end_headers(); self.wfile.write(b'{"message":"Not Found"}'); return
            body = json.dumps({"url": base + "/repos/o/r/releases/1", "tag_name": tag, "assets": [
                {"url": base + "/repos/o/r/releases/assets/2", "id": 2, "name": name + ".sha256", "uploader": {"url": base + "/users/x"}, "size": 100},
                {"url": base + "/repos/o/r/releases/assets/1", "id": 1, "name": name, "uploader": {"url": base + "/users/x"}, "size": len(pkg)}]}).encode()
            self.send_response(200); self.send_header("Content-Type", "application/json"); self.end_headers(); self.wfile.write(body); return
        if self.path == "/repos/o/r/releases/assets/1" and "octet-stream" in self.headers.get("Accept", ""):
            self.send_response(302); self.send_header("Location", base + "/blob/pkg"); self.end_headers(); return
        if self.path == "/repos/o/r/releases/assets/2" and "octet-stream" in self.headers.get("Accept", ""):
            body = ((sha if mode != "badsum" else "0" * 64) + "  " + name + "\n").encode()
            if mode == "badpath": body = (hashlib.sha256(pkg).hexdigest() + "  " + name + "\n").encode()
            self.send_response(200); self.end_headers(); self.wfile.write(body); return
        if self.path == "/blob/pkg":
            self.send_response(200); self.send_header("Content-Length", str(len(pkg))); self.end_headers(); self.wfile.write(pkg); return
        self.send_response(404); self.end_headers()

class S(socketserver.TCPServer):
    allow_reuse_address = True
srv = S(("127.0.0.1", 0), H)
print("PORT", srv.server_address[1], flush=True)
srv.serve_forever()
