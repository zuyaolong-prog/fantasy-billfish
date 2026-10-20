"""Online client public framing and discovery contracts without running Billfish."""

import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from billfish_api import online
from billfish_api.errors import ApiError


class FakePipe:
    calls = []
    response = None
    failure = None

    def __init__(self, name, deadline):
        self.name = name

    def exchange(self, request, deadline, max_response):
        body = json.loads(request.decode("utf-8"))
        self.calls.append(body)
        if self.failure:
            raise self.failure
        return self.response if self.response is not None else (json.dumps({"id": body["id"], "ok": True, "data": {"name": "素材"}}, ensure_ascii=False) + "\n").encode("utf-8")

    def close(self):
        pass


class OnlineTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name) / "install"
        self.path = Path(self.directory.name) / "Billfish" / "automation" / "host.json"
        self.path.parent.mkdir(parents=True)
        self.descriptor = {"protocol": 1, "pid": 1234, "pipe_name": "BillfishAutomation-v1-1234", "install_root": str(self.root)}
        self.save_descriptor()
        self.environment = patch.dict(os.environ, {"LOCALAPPDATA": self.directory.name})
        self.environment.start()
        self.addCleanup(self.environment.stop)
        self.pipe = patch.object(online, "_PipeConnection", FakePipe)
        self.pipe.start()
        self.addCleanup(self.pipe.stop)
        FakePipe.calls, FakePipe.response, FakePipe.failure = [], None, None
        self.client = online.OnlineClient(self.root)

    def save_descriptor(self):
        self.path.write_text(json.dumps(self.descriptor), encoding="utf-8")

    def test_frame_and_library_handshake(self):
        self.assertTrue(self.client.available())
        self.assertEqual(self.client.call("host.info", {"query": "中文"}, library_root="X:\\素材", request_id="test-id"), {"name": "素材"})
        self.assertEqual(FakePipe.calls, [{"id": "test-id", "operation": "host.info", "arguments": {"query": "中文"}, "library_root": "X:\\素材"}])

    def test_invalid_descriptor_never_connects(self):
        for key, value in [("protocol", True), ("protocol", 2), ("pid", -1), ("pid", "1234"), ("pipe_name", "\\\\server\\other"), ("pipe_name", "BillfishAutomation-v1-1235"), ("install_root", str(self.root / "other"))]:
            with self.subTest(key=key, value=value):
                previous = self.descriptor[key]
                self.descriptor[key] = value
                self.save_descriptor()
                self.assertFalse(self.client.available())
                with self.assertRaises(ApiError) as caught:
                    self.client.call("host.info", {})
                self.assertEqual(caught.exception.code, "online_unavailable")
                self.descriptor[key] = previous
        self.assertEqual(FakePipe.calls, [])

    def test_response_identity_and_json_validation(self):
        for frame in [b'{"id":"wrong","ok":true,"data":{}}\n', b'{"id":"id","ok":1,"data":{}}\n', b'{"id":"id","ok":true}\n', b'{"id":"id","ok":false,"error":{}}\n', b'{"id":"id","ok":true,"data":NaN}\n', b'{"id":"id","id":"id","ok":true,"data":{}}\n', b"[]\n", b"\xff\n", b"{}", b"{}\n{}\n"]:
            with self.subTest(frame=frame):
                FakePipe.response = frame
                with self.assertRaises(ApiError) as caught:
                    self.client.call("host.info", {}, request_id="id")
                self.assertEqual(caught.exception.code, "online_protocol_error")

    def test_host_failure_and_library_mismatch_are_not_retried(self):
        for code in ("library_mismatch", "unknown_operation", "partial_failure"):
            FakePipe.calls = []
            FakePipe.response = (json.dumps({"id": "id", "ok": False, "error": {"code": code, "message": "Read current library.", "details": {"completed": 2}}}) + "\n").encode()
            with self.assertRaises(ApiError) as caught:
                self.client.call("items.update", {}, request_id="id")
            self.assertEqual(caught.exception.code, code)
            self.assertEqual(caught.exception.details, {"completed": 2, "request_id": "id", "job_id": "id"})
            self.assertEqual(len(FakePipe.calls), 1)
        FakePipe.response = None
        for code in ("online_timeout", "online_unavailable"):
            FakePipe.calls = []
            FakePipe.failure = ApiError(code, "Unavailable.")
            with self.assertRaises(ApiError) as caught:
                self.client.call("items.update", {})
            self.assertEqual(caught.exception.code, code)
            self.assertEqual(len(FakePipe.calls), 1)
            identifier = FakePipe.calls[0]["id"]
            self.assertEqual(caught.exception.details["request_id"], identifier)
            self.assertEqual(caught.exception.details["job_id"], identifier)
            self.assertIn("outcome is unknown", caught.exception.message)

    def test_request_and_response_bounds(self):
        for kwargs in [{"timeout": 0}, {"timeout": float("nan")}, {"request_id": ""}, {"arguments": {"score": float("nan")}}, {"arguments": {"text": "x" * online.MAX_REQUEST}}]:
            arguments = kwargs.pop("arguments", {})
            with self.subTest(kwargs=kwargs), self.assertRaises(ApiError) as caught:
                self.client.call("host.info", arguments, **kwargs)
            self.assertEqual(caught.exception.code, "invalid_arguments")
        frame = (json.dumps({"id": "id", "ok": True, "data": "x" * online.MAX_REQUEST}) + "\n").encode()
        FakePipe.response = frame
        with self.assertRaises(ApiError) as caught:
            self.client.call("host.info", {}, request_id="id")
        self.assertEqual(caught.exception.code, "online_protocol_error")
        self.assertEqual(len(self.client.call("diagnostics.objects", {}, request_id="id")), online.MAX_REQUEST)
        for operation in ("ui.snapshot", "ui.capture"):
            with self.subTest(operation=operation):
                self.assertEqual(len(self.client.call(operation, {}, request_id="id")), online.MAX_REQUEST)

    def test_large_selection_is_complete_or_explicitly_rejected(self):
        selection = {"items": [{"id": "1", "note": "x" * (1024 * 1024 + 1)}], "total": 1,
                     "library_root": str(self.root)}
        FakePipe.response = (json.dumps({"id": "selection", "ok": True, "data": selection}) + "\n").encode()
        result = self.client.call("items.selected", {}, request_id="selection")
        self.assertEqual(result, selection)

        selection["items"][0]["note"] = "x" * (16 * 1024 * 1024)
        FakePipe.response = (json.dumps({"id": "selection", "ok": True, "data": selection}) + "\n").encode()
        with self.assertRaises(ApiError) as oversized:
            self.client.call("items.selected", {}, request_id="selection")
        self.assertEqual(oversized.exception.code, "selection_too_large")
        self.assertEqual(oversized.exception.details["limit_bytes"], 16 * 1024 * 1024)
        self.assertEqual(oversized.exception.details["request_id"], "selection")


if __name__ == "__main__":
    unittest.main()
