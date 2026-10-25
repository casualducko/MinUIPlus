#!/usr/bin/env python3
"""A tiny time server for the Clock tool's tests: every answer (HEAD or GET) carries a fixed Date header.
Prints "PORT n" once it listens. usage: mock_time.py [date header text]"""
import http.server, sys
DATE = sys.argv[1] if len(sys.argv) > 1 else "Sun, 25 Oct 2026 04:30:00 GMT"
class H(http.server.BaseHTTPRequestHandler):
    def date_time_string(self, timestamp=None):
        return DATE
    def do_HEAD(self):
        self.send_response(204)
        self.end_headers()
    do_GET = do_HEAD
    def log_message(self, *a):
        pass
s = http.server.HTTPServer(("127.0.0.1", 0), H)
print("PORT", s.server_address[1], flush=True)
s.serve_forever()
