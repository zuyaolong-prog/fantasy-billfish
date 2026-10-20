"""Explicitly started, authenticated loopback HTTP adapter."""

import hmac
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from .errors import ApiError

MAX_BODY = 1024 * 1024
SOCKET_TIMEOUT = 10


def load_token(token_file=None):
    try:
        token = Path(token_file).read_text(encoding="utf-8-sig").strip() if token_file else os.environ.get("BILLFISH_API_TOKEN", "").strip()
    except (OSError, UnicodeError) as exc:
        raise ApiError("invalid_arguments", "Cannot read API token file.", {"reason": str(exc)}) from exc
    if len(token) < 32 or not token.isascii() or any(char.isspace() for char in token):
        raise ApiError("invalid_arguments", "API token must contain at least 32 random ASCII characters without whitespace.")
    return token


def _envelope(data=None, error=None):
    if error is None:
        return {"ok": True, "data": data}
    return {"ok": False, "error": {"code": error.code, "message": error.message, "details": error.details}}


def create_server(service, token, port=27393):
    if len(token) < 32 or not token.isascii() or any(char.isspace() for char in token):
        raise ApiError("invalid_arguments", "API token must contain at least 32 random ASCII characters without whitespace.")

    class Handler(BaseHTTPRequestHandler):
        server_version = "BillfishAPI/1"
        sys_version = ""

        def setup(self):
            super().setup()
            self.connection.settimeout(SOCKET_TIMEOUT)

        def log_message(self, format, *args):
            # Do not log request paths, headers, bodies, or credentials.
            pass

        def _send(self, status, data=None, error=None, raw=False):
            payload = json.dumps(data if raw else _envelope(data, error), ensure_ascii=False, allow_nan=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            self.wfile.write(payload)

        def _authenticate(self):
            if self.headers.get_all("Origin"):
                raise ApiError("origin_forbidden", "Browser Origin requests are not accepted.", status=403)
            auth = self.headers.get_all("Authorization", [])
            if len(auth) != 1 or not hmac.compare_digest(auth[0].encode("utf-8"), ("Bearer " + token).encode("ascii")):
                raise ApiError("unauthorized", "A valid Bearer token is required.", status=401)

        def _body(self):
            if self.headers.get_all("Transfer-Encoding"):
                raise ApiError("invalid_request", "Transfer-Encoding is not supported.")
            lengths = self.headers.get_all("Content-Length", [])
            if len(lengths) != 1 or not lengths[0].isascii() or not lengths[0].isdigit():
                raise ApiError("invalid_request", "One valid Content-Length is required.", status=411)
            if len(lengths[0]) > 10:
                raise ApiError("request_too_large", "Request body exceeds 1 MiB.", status=413)
            length = int(lengths[0])
            if length > MAX_BODY:
                raise ApiError("request_too_large", "Request body exceeds 1 MiB.", status=413)
            if self.headers.get_content_type() != "application/json":
                raise ApiError("invalid_request", "Content-Type must be application/json.", status=415)
            try:
                raw = self.rfile.read(length)
                if len(raw) != length:
                    raise ApiError("invalid_request", "Incomplete request body.")
                def pairs(values):
                    obj = {}
                    for key, value in values:
                        if key in obj:
                            raise ValueError("Duplicate object key.")
                        obj[key] = value
                    return obj
                def invalid_constant(value):
                    raise ValueError("Non-finite numbers are not valid JSON.")
                body = json.loads(raw.decode("utf-8"), object_pairs_hook=pairs, parse_constant=invalid_constant)
            except (UnicodeError, ValueError, RecursionError) as exc:
                raise ApiError("invalid_request", "Body must be valid UTF-8 JSON.") from exc
            if not isinstance(body, dict) or set(body) - {"operation", "arguments"}:
                raise ApiError("invalid_request", "Body must be an object with operation and arguments.")
            if not isinstance(body.get("operation"), str) or not body["operation"].strip():
                raise ApiError("invalid_request", "operation must be a nonempty string.")
            if "arguments" not in body or not isinstance(body["arguments"], dict):
                raise ApiError("invalid_request", "arguments must be an object.")
            return body

        def _dispatch(self, method):
            try:
                self._authenticate()
                if method == "GET":
                    if self.path == "/health":
                        data = {"status": "ok"}
                    elif self.path == "/v1/capabilities":
                        data = service.capabilities()
                    elif self.path == "/openapi.json":
                        data = service.openapi()
                    else:
                        raise ApiError("not_found", "Endpoint not found.", status=404)
                elif method == "POST" and self.path == "/v1/execute":
                    body = self._body()
                    data = service.call(body["operation"], body["arguments"])
                elif method == "POST":
                    raise ApiError("not_found", "Endpoint not found.", status=404)
                else:
                    raise ApiError("method_not_allowed", "HTTP method not allowed.", status=405)
                self._send(200, data=data, raw=method == "GET" and self.path == "/openapi.json")
            except ApiError as exc:
                self._send(exc.status, error=exc)
            except TimeoutError:
                self._send(408, error=ApiError("request_timeout", "Request body read timed out.", status=408))
            except (BrokenPipeError, ConnectionResetError):
                self.close_connection = True
            except Exception as exc:
                print(f"Billfish API error: {type(exc).__name__}", file=sys.stderr)
                self._send(500, error=ApiError("internal_error", "An unexpected error occurred.", status=500))

        def do_GET(self):
            self._dispatch("GET")

        def do_POST(self):
            self._dispatch("POST")

        def do_OPTIONS(self):
            self._dispatch("OPTIONS")

        def do_PUT(self):
            self._dispatch("PUT")

        def do_DELETE(self):
            self._dispatch("DELETE")

        def do_PATCH(self):
            self._dispatch("PATCH")

        def do_HEAD(self):
            self._dispatch("HEAD")

    return ThreadingHTTPServer(("127.0.0.1", port), Handler)


def serve(service, token, port=27393):
    with create_server(service, token, port) as server:
        print(f"Billfish API listening on http://127.0.0.1:{server.server_port}", file=sys.stderr)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
