"""Persistent read-only animation snapshots for one bound Billfish library."""
import json
import sys
from pathlib import Path

# Bundled Python -I excludes the script directory. Import only this installation.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from billfish_api.catalog import Catalog
from billfish_api.errors import ApiError

MAX_REQUEST_BYTES = 8 * 1024 * 1024


def respond(request, library_root):
    request_id = request.get("requestId") if isinstance(request, dict) else None
    try:
        if (not isinstance(request, dict) or set(request) != {"requestId", "scopeRoot"}
                or not isinstance(request_id, str) or not request_id.isascii()
                or not request_id.isdecimal() or not 1 <= len(request_id) <= 20):
            raise ApiError("invalid_arguments", "Expected a requestId and scopeRoot.")
        # Each snapshot opens and closes its own read-only transaction. No cached
        # result or open connection can conceal catalog or disk membership changes.
        data = Catalog(library_root).animation_snapshot(request["scopeRoot"])
        return {"requestId": request_id, "ok": True, "data": data}
    except ApiError as exc:
        return {"requestId": request_id, "ok": False, "error": exc.to_dict()}
    except Exception:
        return {"requestId": request_id, "ok": False, "error": {"code": "helper_failed", "message": "The animation catalog could not complete the request."}}


def main():
    if len(sys.argv) != 2 or not Path(sys.argv[1]).is_absolute() or "\0" in sys.argv[1]:
        return 2
    library_root = sys.argv[1]
    while True:
        raw = sys.stdin.buffer.readline(MAX_REQUEST_BYTES + 1)
        if not raw:
            return 0
        # Invalid framing terminates the worker; no partial request is executed.
        if len(raw) > MAX_REQUEST_BYTES or not raw.endswith(b"\n"):
            return 2
        try:
            request = json.loads(raw.decode("utf-8"))
        except (UnicodeError, ValueError):
            return 2
        response = respond(request, library_root)
        sys.stdout.buffer.write((json.dumps(response, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8"))
        sys.stdout.buffer.flush()


if __name__ == "__main__":
    raise SystemExit(main())
