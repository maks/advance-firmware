#!/usr/bin/env python3
"""
SPDX-License-Identifier: BSD-3-Clause

Development HTTP server for picoTracker WebAssembly port.
Serves static assets with Cross-Origin-Opener-Policy and
Cross-Origin-Embedder-Policy headers required for SharedArrayBuffer / pthreads.
"""

import argparse
import http.server
import mimetypes
import os
import sys
from pathlib import Path

# Ensure proper MIME types for WASM and JS
mimetypes.add_type("application/wasm", ".wasm")
mimetypes.add_type("text/javascript", ".js")
mimetypes.add_type("text/html", ".html")
mimetypes.add_type("text/css", ".css")
mimetypes.add_type("application/json", ".json")


class WasmHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, web_dir=None, public_wasm_dir=None, **kwargs):
        self.web_dir = Path(web_dir).resolve() if web_dir else Path("web").resolve()
        self.public_wasm_dir = (
            Path(public_wasm_dir).resolve()
            if public_wasm_dir
            else (self.web_dir / "public" / "wasm").resolve()
        )
        super().__init__(*args, directory=str(self.web_dir), **kwargs)

    def end_headers(self):
        # Mandatory isolation headers for SharedArrayBuffer and WASM pthreads
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def translate_path(self, path):
        from urllib.parse import urlparse
        clean_path = urlparse(path).path

        # Route /wasm/* to web/public/wasm/*
        if clean_path.startswith("/wasm/"):
            rel = clean_path[len("/wasm/"):]
            target = (self.public_wasm_dir / rel).resolve()
            # Prevent path traversal
            if str(target).startswith(str(self.public_wasm_dir)):
                return str(target)

        # Fallback to standard web_dir resolution
        return super().translate_path(clean_path)


def main():
    parser = argparse.ArgumentParser(description="Serve picoTracker WASM with isolation headers")
    parser.add_argument("--port", "-p", type=int, default=8000, help="Port to listen on (default: 8000)")
    parser.add_argument("--host", "-H", type=str, default="127.0.0.1", help="Host address (default: 127.0.0.1)")
    parser.add_argument("--dir", "-d", type=str, default="web", help="Web directory (default: web)")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    web_dir = (repo_root / args.dir).resolve()
    wasm_dir = (web_dir / "public" / "wasm").resolve()

    if not web_dir.exists():
        print(f"Error: web directory not found at {web_dir}", file=sys.stderr)
        sys.exit(1)

    handler = lambda *h_args, **h_kwargs: WasmHandler(
        *h_args, web_dir=web_dir, public_wasm_dir=wasm_dir, **h_kwargs
    )

    server_address = (args.host, args.port)
    http.server.ThreadingHTTPServer.allow_reuse_address = True
    with http.server.ThreadingHTTPServer(server_address, handler) as httpd:
        print(f"Serving picoTracker Web on http://{args.host}:{args.port}/ (directory: {web_dir})")
        print("COOP/COEP isolation headers enabled.")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down server.")


if __name__ == "__main__":
    main()
