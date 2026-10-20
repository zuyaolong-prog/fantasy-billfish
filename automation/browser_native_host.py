"""One-shot Edge Native Messaging bridge to the current-user Billfish pipe.

Framing follows Chromium's nativeMessaging host example (BSD-3-Clause):
https://github.com/GoogleChrome/chrome-extensions-samples/tree/main/api-samples/nativeMessaging/host
No web URL or browser-provided argument is evaluated as shell text.
"""

import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
import uuid

# The registered wrapper uses -I. Only add our installed automation directory.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from billfish_api.errors import ApiError
from billfish_api.online import OnlineClient
from urllib.parse import urlsplit

ALLOWED_ORIGIN = "chrome-extension://aloippladlgkfkcabiknfpokgcppcjkp/"
MAX_FRAME = 1024 * 1024
INSTALL_ROOT = Path(__file__).resolve().parents[1]


def read_exact(stream, size):
    chunks = bytearray()
    while len(chunks) < size:
        part = stream.read(size - len(chunks))
        if not part:
            raise ApiError("invalid_frame", "Native message frame was truncated.")
        chunks.extend(part)
    return bytes(chunks)


def read_message(stream):
    size, = struct.unpack("<I", read_exact(stream, 4))
    if not 0 < size <= MAX_FRAME:
        raise ApiError("invalid_frame", "Native message must contain 1 to 1048576 bytes.")
    raw = read_exact(stream, size)

    def unique_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate key.")
            result[key] = value
        return result

    try:
        return json.loads(raw.decode("utf-8"), object_pairs_hook=unique_pairs,
                          parse_constant=lambda value: (_ for _ in ()).throw(ValueError("Invalid number.")))
    except (UnicodeError, ValueError, RecursionError):
        raise ApiError("invalid_json", "Native message requires valid UTF-8 JSON with unique keys.") from None


def write_message(stream, response):
    raw = json.dumps(response, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
    if len(raw) > MAX_FRAME:
        raise ValueError("Native response exceeds 1 MiB.")
    stream.write(struct.pack("<I", len(raw)))
    stream.write(raw)
    stream.flush()


def validate_request(message):
    if not isinstance(message, dict):
        raise ApiError("invalid_arguments", "Native message must be an object.")
    if message.get("action") != "captureVideo":
        raise ApiError("unsupported_action", "Only captureVideo is supported.")
    if set(message) - {"action", "url", "page_url", "title", "media_url"}:
        raise ApiError("invalid_arguments", "Native message contains unsupported fields.")
    arguments = {}
    for key in ("url", "page_url", "media_url"):
        if key not in message and key != "url":
            continue
        value = message.get(key)
        try:
            if not isinstance(value, str) or not value or len(value) > 32768 or any(ord(char) < 32 for char in value):
                raise ValueError("Invalid URL.")
            parsed = urlsplit(value)
            if parsed.scheme.lower() not in ("http", "https") or not parsed.hostname or parsed.username is not None or parsed.password is not None:
                raise ValueError("Invalid URL.")
            parsed.port  # Reject malformed ports before dispatch.
        except ValueError:
            raise ApiError("invalid_arguments", f"{key} requires an HTTP(S) URL without credentials.") from None
        arguments[key] = value
    if "title" in message:
        value = message["title"]
        if not isinstance(value, str) or len(value) > 4096 or "\0" in value:
            raise ApiError("invalid_arguments", "title must be a string of at most 4096 characters.")
        arguments["title"] = value
    return arguments


def launch_billfish(root):
    launcher = root / "Billfish.exe"
    if not launcher.is_file():
        raise ApiError("launcher_missing", "The unified Billfish launcher is missing.")
    options = {"stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL,
               "cwd": str(root)}
    if os.name == "nt":
        options["creationflags"] = subprocess.CREATE_NO_WINDOW
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        options["startupinfo"] = startup
    subprocess.Popen([str(launcher)], **options)


def ensure_ready(client, root, *, launch=launch_billfish, wait_seconds=45):
    try:
        client.call("host.info", {}, timeout=1)
        return
    except ApiError as exc:
        if exc.code not in {"online_unavailable", "online_timeout"}:
            raise
    launch(root)
    deadline = time.monotonic() + wait_seconds
    while time.monotonic() < deadline:
        try:
            client.call("host.info", {}, timeout=min(1, max(0.01, deadline - time.monotonic())))
            return
        except ApiError as exc:
            if exc.code not in {"online_unavailable", "online_timeout"}:
                raise
        time.sleep(min(0.2, max(0, deadline - time.monotonic())))
    raise ApiError("host_not_ready", "Billfish did not become ready. Open Billfish and try again.")


def handle_message(message, origin, *, client=None, root=INSTALL_ROOT, launch=launch_billfish):
    if origin != ALLOWED_ORIGIN:
        raise ApiError("origin_not_allowed", "This extension is not allowed to use the Billfish capture host.")
    arguments = validate_request(message)
    client = client if client is not None else OnlineClient(root)
    ensure_ready(client, root, launch=launch)
    request_id = str(uuid.uuid4())
    try:
        data = client.call("video.capture", arguments, timeout=15, request_id=request_id)
    except ApiError as exc:
        # Any capture transport error may follow a successful submission. Never replay.
        if exc.code in {"online_timeout", "online_unavailable", "online_protocol_error"}:
            raise ApiError(exc.code, "Capture outcome is unknown. Query jobs.get using job_id before retrying.",
                           {"job_id": request_id}) from None
        raise
    if not isinstance(data, dict) or data.get("accepted") is not True or not isinstance(data.get("job_id"), str) or not data["job_id"]:
        raise ApiError("online_protocol_error", "Billfish did not return a capture job. Query jobs.get before retrying.",
                       {"job_id": request_id})
    return {"ok": True, "accepted": True, "job_id": data["job_id"]}


def error_response(exc):
    # Host errors may include source URLs; return no backend messages or diagnostics.
    own_codes = {"invalid_frame", "invalid_json", "invalid_arguments", "unsupported_action", "origin_not_allowed",
                 "launcher_missing", "host_not_ready"}
    message = exc.message if exc.code in own_codes else "Billfish could not accept capture. Check Billfish for details."
    response = {"ok": False, "error": {"code": exc.code, "message": message}}
    if isinstance(exc.details, dict) and isinstance(exc.details.get("job_id"), str):
        response["job_id"] = exc.details["job_id"]
        response["error"]["message"] = "Capture outcome is unknown. Query jobs.get using job_id before retrying."
    return response


def main(argv=None, stdin=None, stdout=None):
    argv = sys.argv[1:] if argv is None else argv
    stdin = sys.stdin.buffer if stdin is None else stdin
    stdout = sys.stdout.buffer if stdout is None else stdout
    if os.name == "nt":
        import msvcrt
        for stream in (stdin, stdout):
            if hasattr(stream, "fileno"):
                msvcrt.setmode(stream.fileno(), os.O_BINARY)
    try:
        origin = argv[0] if argv else None
        if origin != ALLOWED_ORIGIN:
            raise ApiError("origin_not_allowed", "This extension is not allowed to use the Billfish capture host.")
        response = handle_message(read_message(stdin), origin)
    except ApiError as exc:
        response = error_response(exc)
    except Exception:
        response = {"ok": False, "error": {"code": "capture_host_failed", "message": "The Billfish capture host could not complete the request."}}
    write_message(stdout, response)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
