"""Public transport contract tests using an isolated fake service."""

import contextlib
from concurrent.futures import ThreadPoolExecutor
import http.client
import io
import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from billfish_api import cli
from billfish_api.errors import ApiError
from billfish_api.http_server import MAX_BODY, create_server, load_token


class FakeService:
    def __init__(self, library=None, install_root=None):
        self.library = library

    def call(self, operation, arguments):
        if operation == "missing":
            raise ApiError("unknown_operation", "Unknown operation.", status=404)
        if operation in {"invalid_argument", "invalid_arguments"}:
            raise ApiError(operation, "Invalid argument.", status=400)
        return {"operation": operation, "arguments": arguments, "library": self.library, "name": "素材"}

    def capabilities(self):
        return {"operations": ["items.search"]}

    def openapi(self):
        return {"openapi": "3.1.0", "paths": {}}


class CliTests(unittest.TestCase):
    def invoke(self, arguments, stdin=""):
        output, diagnostics = io.StringIO(), io.StringIO()
        with patch.object(cli, "Service", FakeService), patch.object(sys, "stdin", io.StringIO(stdin)), contextlib.redirect_stdout(output), contextlib.redirect_stderr(diagnostics):
            code = cli.main(arguments)
        return code, json.loads(output.getvalue()), diagnostics.getvalue()

    def test_search_and_unicode(self):
        code, body, _ = self.invoke(["--library", "中文库", "search", "纹理", "--limit", "7", "--offset", "2", "--folder-id", "3", "--tag-id", "4", "--ext", "png", "--ext", "jpg"])
        self.assertEqual(code, 0)
        self.assertEqual(body["data"]["library"], "中文库")
        self.assertEqual(body["data"]["arguments"], {"query": "纹理", "limit": 7, "offset": 2, "folder_id": "3", "tag_id": "4", "extensions": ["png", "jpg"]})

    def test_public_command_mapping(self):
        for command, operation, arguments in [("info", "library.info", {}), ("selected", "items.selected", {}), ("folders", "folders.list", {}), ("tags", "tags.list", {}), ("item abc", "items.get", {"item_id": "abc"})]:
            with self.subTest(command=command):
                code, body, _ = self.invoke(command.split())
                self.assertEqual(code, 0)
                self.assertEqual(body["data"]["operation"], operation)
                self.assertEqual(body["data"]["arguments"], arguments)

    def test_call_sources_and_schema(self):
        for args, stdin in [(["call", "items.update", "--json", '{"note":"中文"}'], ""), (["call", "items.update", "--input", "-"], '{"note":"中文"}')]:
            code, body, _ = self.invoke(args, stdin)
            self.assertEqual(code, 0)
            self.assertEqual(body["data"]["arguments"], {"note": "中文"})
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "args.json"
            source.write_text('{"note":"中文"}', encoding="utf-8")
            self.assertEqual(self.invoke(["call", "items.update", "--input", str(source)])[0], 0)
        self.assertEqual(self.invoke(["schema"])[1]["data"], FakeService().capabilities())
        self.assertEqual(self.invoke(["openapi"])[1]["data"], FakeService().openapi())
        self.assertEqual(self.invoke(["--version"])[1]["data"]["version"], cli.VERSION)
        self.assertIn("help", self.invoke(["--help"])[1]["data"])

    def test_argument_and_service_exit_codes(self):
        for args in [[], ["no-command"], ["item"], ["search", "--limit", "bad"], ["call", "x", "--json", "[]"], ["call", "x", "--json", "{"], ["call", "x", "--json", "{}", "--input", "-"], ["call", "x", "--json", '{"note":1,"note":2}'], ["call", "x", "--json", '{"score":NaN}'], ["call", "x", "--json", '{"score":Infinity}']]:
            with self.subTest(args=args):
                code, body, _ = self.invoke(args)
                self.assertEqual(code, 2)
                self.assertFalse(body["ok"])
        code, body, _ = self.invoke(["call", "missing"])
        self.assertEqual(code, 1)
        self.assertEqual(body["error"]["code"], "unknown_operation")

    def test_service_argument_error_codes_across_transports(self):
        for error_code in ("invalid_argument", "invalid_arguments"):
            with self.subTest(error_code=error_code):
                code, body, _ = self.invoke(["call", error_code])
                self.assertEqual(code, 2)
                self.assertEqual(body["error"]["code"], error_code)
                with create_server(FakeService(), HttpTests.token, 0) as server:
                    thread = threading.Thread(target=server.handle_request)
                    thread.start()
                    connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=3)
                    try:
                        connection.request("POST", "/v1/execute", json.dumps({"operation": error_code, "arguments": {}}), {"Authorization": "Bearer " + HttpTests.token, "Content-Type": "application/json"})
                        response = connection.getresponse()
                        self.assertEqual(response.status, 400)
                        self.assertEqual(json.loads(response.read())["error"], body["error"])
                    finally:
                        connection.close()
                        thread.join()

    def test_interrupted_operation_reports_uncertain_outcome(self):
        with patch.object(FakeService, "call", side_effect=KeyboardInterrupt):
            code, body, _ = self.invoke(["call", "items.update", "--json", "{}"])
        self.assertEqual(code, 130)
        self.assertFalse(body["ok"])
        self.assertEqual(body["error"]["code"], "interrupted")
        self.assertIn("Read back", body["error"]["message"])


class HttpTests(unittest.TestCase):
    token = "a-test-token-" + "a" * 40

    @classmethod
    def setUpClass(cls):
        cls.server = create_server(FakeService(), cls.token, 0)
        cls.thread = threading.Thread(target=cls.server.serve_forever)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.thread.join()
        cls.server.server_close()

    def request(self, method, path, body=None, headers=None):
        merged = {"Authorization": "Bearer " + self.token}
        if headers:
            merged.update(headers)
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
        try:
            connection.request(method, path, body=body, headers=merged)
            response = connection.getresponse()
            status, response_headers, raw = response.status, dict(response.getheaders()), response.read()
            return status, response_headers, json.loads(raw.decode("utf-8"))
        finally:
            connection.close()

    def test_auth_origin_and_endpoints(self):
        self.assertEqual(self.server.server_address[0], "127.0.0.1")
        for headers, status in [({"Authorization": ""}, 401), ({"Authorization": "Bearer wrong"}, 401), ({"Origin": "http://example.com"}, 403)]:
            self.assertEqual(self.request("GET", "/health", headers=headers)[0], status)
        for path in ("/health", "/v1/capabilities", "/openapi.json"):
            status, headers, body = self.request("GET", path)
            self.assertEqual(status, 200)
            if path == "/openapi.json":
                self.assertEqual(body, FakeService().openapi())
            else:
                self.assertTrue(body["ok"])
            self.assertNotIn("Access-Control-Allow-Origin", headers)
        self.assertEqual(self.request("GET", "/unknown")[0], 404)
        self.assertEqual(self.request("OPTIONS", "/v1/execute")[0], 405)

    def test_execute_utf8_and_unknown_operation(self):
        for operation, expected in [("items.update", 200), ("missing", 404)]:
            raw = json.dumps({"operation": operation, "arguments": {"note": "中文"}}, ensure_ascii=False).encode("utf-8")
            status, _, body = self.request("POST", "/v1/execute", raw, {"Content-Type": "application/json"})
            self.assertEqual(status, expected)
            if expected == 200:
                self.assertEqual(body["data"]["arguments"]["note"], "中文")
            else:
                self.assertEqual(body["error"]["code"], "unknown_operation")

    def test_invalid_body_and_limits(self):
        for raw in (b"[]", b"{}", b"{", b"\xff", b'{"operation":"x","arguments":[],"extra":1}', b'{"operation":"x","arguments":null}', b'{"operation":"x","operation":"y","arguments":{}}', b'{"operation":"x","arguments":{"score":NaN}}'):
            with self.subTest(raw=raw):
                self.assertEqual(self.request("POST", "/v1/execute", raw, {"Content-Type": "application/json"})[0], 400)
        self.assertEqual(self.request("POST", "/v1/execute", "{}")[0], 415)
        self.assertEqual(self.request("POST", "/v1/execute", b"", {"Content-Type": "application/json", "Content-Length": str(MAX_BODY + 1)})[0], 413)
        self.assertEqual(self.request("POST", "/v1/execute", b"", {"Content-Type": "application/json", "Content-Length": "-1"})[0], 411)
        self.assertEqual(self.request("POST", "/v1/execute", b"", {"Transfer-Encoding": "chunked"})[0], 400)

    def test_token_configuration(self):
        with patch.dict("os.environ", {"BILLFISH_API_TOKEN": self.token}):
            self.assertEqual(load_token(), self.token)
        for token in ("", "short", "a" * 32 + "\n", "中" * 32):
            with self.subTest(token=token), self.assertRaises(ApiError):
                create_server(FakeService(), token, 0)

    def test_job_status_remains_available_during_long_operation(self):
        started, release = threading.Event(), threading.Event()

        class WaitingService(FakeService):
            def call(self, operation, arguments):
                if operation == "plugins.run":
                    started.set()
                    if not release.wait(10):
                        raise ApiError("test_timeout", "Long task was not released.")
                    return {"status": "completed"}
                if operation == "jobs.get":
                    return {"status": "running" if not release.is_set() else "completed"}
                return super().call(operation, arguments)

        with create_server(WaitingService(), self.token, 0) as server:
            server_thread = threading.Thread(target=server.serve_forever)
            server_thread.start()

            def execute(operation):
                connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=3)
                try:
                    connection.request("POST", "/v1/execute", json.dumps({"operation": operation, "arguments": {}}), {"Authorization": "Bearer " + self.token, "Content-Type": "application/json"})
                    response = connection.getresponse()
                    return response.status, json.loads(response.read())
                finally:
                    connection.close()

            with ThreadPoolExecutor(max_workers=1) as executor:
                pending = executor.submit(execute, "plugins.run")
                try:
                    self.assertTrue(started.wait(2))
                    status, body = execute("jobs.get")
                    self.assertEqual(status, 200)
                    self.assertEqual(body["data"]["status"], "running")
                    self.assertFalse(pending.done())
                finally:
                    release.set()
                    server.shutdown()
                    server_thread.join()
                self.assertEqual(pending.result()[1]["data"]["status"], "completed")


if __name__ == "__main__":
    unittest.main()
