"""Browser framing and the single-submit video boundary, without touching Edge."""

import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import browser_native_host as host
from billfish_api.errors import ApiError


def frame(data):
    raw = json.dumps(data, ensure_ascii=False).encode("utf-8")
    return struct.pack("<I", len(raw)) + raw


class FragmentedInput(io.BytesIO):
    def read(self, size=-1):
        return super().read(min(size, 2))


class NativeHostTests(unittest.TestCase):
    def test_utf8_frame_roundtrip_and_short_reads(self):
        data = {"title": "视频\n分辨率", "url": "https://example.test/v?a=1&b=2"}
        self.assertEqual(host.read_message(FragmentedInput(frame(data))), data)
        output = io.BytesIO()
        host.write_message(output, data)
        self.assertEqual(host.read_message(io.BytesIO(output.getvalue())), data)
        self.assertEqual(struct.unpack("<I", output.getvalue()[:4])[0], len(output.getvalue()) - 4)

    def test_frame_limits_and_truncation(self):
        for raw in (b"", b"\x01\x00", struct.pack("<I", 0), struct.pack("<I", host.MAX_FRAME + 1), struct.pack("<I", 5) + b"{}"):
            with self.subTest(raw=raw), self.assertRaises(ApiError) as caught:
                host.read_message(io.BytesIO(raw))
            self.assertEqual(caught.exception.code, "invalid_frame")
        # Exact boundary accepted without reading beyond the declared payload.
        raw = b'"' + b'a' * (host.MAX_FRAME - 2) + b'"'
        self.assertEqual(len(host.read_message(io.BytesIO(struct.pack("<I", len(raw)) + raw))), host.MAX_FRAME - 2)

    def test_invalid_json(self):
        for raw in (b'{}{}', b'{"action":1,"action":2}', b'{"value":NaN}', b'"\xff"'):
            with self.subTest(raw=raw), self.assertRaises(ApiError) as caught:
                host.read_message(io.BytesIO(struct.pack("<I", len(raw)) + raw))
            self.assertEqual(caught.exception.code, "invalid_json")

    def test_origin_action_and_input_rejected_before_connection(self):
        client = Mock()
        for origin in (None, "chrome-extension://other/", host.ALLOWED_ORIGIN.rstrip("/"), host.ALLOWED_ORIGIN + "evil"):
            with self.assertRaises(ApiError) as caught:
                host.handle_message({"action": "captureVideo", "url": "https://example.test/v"}, origin, client=client)
            self.assertEqual(caught.exception.code, "origin_not_allowed")
        invalid = [None, [], {}, {"action": "execute", "url": "https://example.test"},
                   {"action": "captureVideo", "url": "file:///C:/v.mp4"},
                   {"action": "captureVideo", "url": "https://user:secret@example.test/v"},
                   {"action": "captureVideo", "url": "https://example.test:broken/v"},
                   {"action": "captureVideo", "url": "https://example.test/v", "page_url": None},
                   {"action": "captureVideo", "url": "https://example.test/v", "media_url": "javascript:alert(1)"},
                   {"action": "captureVideo", "url": "https://example.test/v", "title": 123},
                   {"action": "captureVideo", "url": "https://example.test/v", "cookies": "secret"}]
        for message in invalid:
            with self.subTest(message=message), self.assertRaises(ApiError):
                host.handle_message(message, host.ALLOWED_ORIGIN, client=client)
        client.call.assert_not_called()

    def test_correct_forwarding_and_backend_job_identity(self):
        client = Mock()
        client.call.side_effect = [{"pid": 1}, {"accepted": True, "job_id": "actual-video-job"}]
        message = {"action": "captureVideo", "url": "https://example.test/watch?v=1", "page_url": "https://example.test/watch?v=1",
                   "media_url": "https://cdn.example.test/v.mp4?sig=abc", "title": "视频"}
        response = host.handle_message(message, host.ALLOWED_ORIGIN, client=client)
        self.assertEqual(response, {"ok": True, "accepted": True, "job_id": "actual-video-job"})
        self.assertEqual(client.call.call_args.args, ("video.capture", {key: value for key, value in message.items() if key != "action"}))
        self.assertTrue(client.call.call_args.kwargs["request_id"])
        self.assertEqual(client.call.call_count, 2)

    def test_timeout_never_replays_capture_and_preserves_query_identity(self):
        for code in ("online_timeout", "online_unavailable", "online_protocol_error"):
            client = Mock()
            client.call.side_effect = [{"pid": 1}, ApiError(code, "secret URL https://example.test/?token=secret")]
            with self.subTest(code=code), self.assertRaises(ApiError) as caught:
                host.handle_message({"action": "captureVideo", "url": "https://example.test/v"}, host.ALLOWED_ORIGIN, client=client)
            response = host.error_response(caught.exception)
            self.assertEqual(response["job_id"], client.call.call_args.kwargs["request_id"])
            self.assertNotIn("token", json.dumps(response))
            self.assertEqual([call.args[0] for call in client.call.call_args_list], ["host.info", "video.capture"])

    def test_cold_start_only_unified_launcher_and_readiness_before_capture(self):
        client = Mock()
        launch = Mock()
        client.call.side_effect = [ApiError("online_unavailable", "absent"), {"pid": 1}, {"accepted": True, "job_id": "cold-job"}]
        root = Path("C:/isolated/Billfish")
        self.assertEqual(host.handle_message({"action": "captureVideo", "url": "https://example.test/v"}, host.ALLOWED_ORIGIN,
                                             client=client, root=root, launch=launch)["job_id"], "cold-job")
        launch.assert_called_once_with(root)
        self.assertEqual([call.args[0] for call in client.call.call_args_list], ["host.info", "host.info", "video.capture"])

    def test_process_stdio_binary_frame_without_diagnostics_on_stdout(self):
        # Run the actual isolated entrypoint. Validation avoids any Billfish operation.
        request = {"action": "arbitraryOperation", "url": "https://example.test/?secret=hidden"}
        completed = subprocess.run([sys.executable, "-I", str(Path(host.__file__)), host.ALLOWED_ORIGIN],
                                   input=frame(request), capture_output=True, check=True)
        response = host.read_message(io.BytesIO(completed.stdout))
        self.assertFalse(response["ok"])
        self.assertEqual(response["error"]["code"], "unsupported_action")
        self.assertEqual(len(completed.stdout), 4 + struct.unpack("<I", completed.stdout[:4])[0])
        self.assertNotIn(b"hidden", completed.stderr)

    @unittest.skipUnless(os.name == "nt", "The registered .cmd wrapper requires Windows.")
    def test_windows_cmd_wrapper_preserves_binary_stdio(self):
        template = host.INSTALL_ROOT / "browser-extension" / "native-host" / "billfish-capture-host.cmd.in"
        wrapper = template.read_text(encoding="utf-8").replace("@@PYTHON@@", sys.executable).replace("@@SCRIPT@@", str(Path(host.__file__)))
        with tempfile.TemporaryDirectory(prefix="Billfish Native Host 空格 ") as directory:
            wrapper_path = Path(directory) / "capture host.cmd"
            wrapper_path.write_bytes(wrapper.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
            completed = subprocess.run(["cmd.exe", "/d", "/c", str(wrapper_path), host.ALLOWED_ORIGIN],
                                       input=frame({"action": "arbitraryOperation", "url": "https://example.test/?secret=hidden"}),
                                       capture_output=True, check=True)
        self.assertEqual(host.read_message(io.BytesIO(completed.stdout))["error"]["code"], "unsupported_action")
        self.assertEqual(len(completed.stdout), 4 + struct.unpack("<I", completed.stdout[:4])[0])
        self.assertNotIn(b"hidden", completed.stderr)


if __name__ == "__main__":
    unittest.main()
