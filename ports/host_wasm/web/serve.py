#!/usr/bin/env python3
"""Local static server with COOP/COEP headers for the PicoMite web bundle.

SharedArrayBuffer (needed by the worker + pthreads build) requires the
page to be served in a "cross-origin isolated" context. That means
every response must carry:

    Cross-Origin-Opener-Policy: same-origin
    Cross-Origin-Embedder-Policy: require-corp

`python3 -m http.server` does not set these, so we subclass its handler
and layer the headers on top. Everything else — MIME types, directory
listings, range requests — is inherited unchanged.

Usage: ./serve.py [port]   (defaults to 8000).
"""
import http.server
import os
import sys


class COOPCOEPHandler(http.server.SimpleHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/__smoke_echo":
            self.send_error(404, "Not Found")
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = 0
        body = self.rfile.read(max(0, length))
        payload = (
            f"METHOD=POST\n"
            f"CONTENT_TYPE={self.headers.get('Content-Type', '')}\n"
            f"BODY="
        ).encode("utf-8") + body
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def end_headers(self):
        # Two isolation headers that unlock SharedArrayBuffer:
        #   - COOP: same-origin prevents other origins from embedding us
        #     in a way that could share state.
        #   - COEP: require-corp ensures every subresource opts in to
        #     cross-origin embedding. Same-origin resources (which is
        #     all we serve) satisfy it automatically.
        self.send_header("Cross-Origin-Opener-Policy",  "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # Disable caching so edits during development are picked up on
        # plain reload — no need to hard-refresh after every tweak.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    print(f"Serving {os.getcwd()} at http://localhost:{port}/  (COOP/COEP on)")
    http.server.ThreadingHTTPServer(("127.0.0.1", port), COOPCOEPHandler).serve_forever()


if __name__ == "__main__":
    main()
