"""Deliver one explicitly opened package to the installed current-user host."""

import argparse
import json
from pathlib import Path
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "automation"))
from billfish_api.errors import ApiError
from billfish_api.online import OnlineClient


def deliver(client, source, ready_timeout=60, install_timeout=120):
    """Retry only readiness reads; send exactly one installation mutation."""
    deadline = time.monotonic() + ready_timeout
    while True:
        try:
            client.call("host.info", {}, timeout=min(2, max(.01, deadline - time.monotonic())))
            break
        except ApiError as exc:
            if exc.code not in {"online_unavailable", "online_timeout"} or time.monotonic() >= deadline:
                raise
            time.sleep(min(.1, max(0, deadline - time.monotonic())))
    job_id = str(uuid.uuid4())
    try:
        result = client.call("plugins.install", {"source_path": str(source)},
                             timeout=install_timeout, request_id=job_id)
    except ApiError as exc:
        details = dict(exc.details or {})
        details.update(job_id=job_id, source_path=str(source))
        raise ApiError(exc.code, exc.message, details, exc.status) from exc
    try:
        client.call("ui.settings.open", {}, timeout=10)
    except ApiError as exc:
        raise ApiError("plugin_installed_ui_unavailable",
                       "The package was installed, but its management page could not be opened. Do not reinstall; open Billfish plugin settings.",
                       {"installation": result, "job_id": job_id, "ui_error": exc.to_dict()}) from exc
    return {"installation": result, "job_id": job_id}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-root", required=True)
    parser.add_argument("--source-path", required=True)
    args = parser.parse_args()
    source = Path(args.source_path)
    try:
        if source.suffix.lower() not in {".eagleplugin", ".zip"} or not source.is_file():
            raise ApiError("invalid_package", "Select an existing local .eagleplugin or .zip file.")
        data = deliver(OnlineClient(args.install_root), source.resolve())
        print(json.dumps({"ok": True, "data": data}, ensure_ascii=True))
        return 0
    except ApiError as exc:
        print(json.dumps({"ok": False, "error": exc.to_dict()}, ensure_ascii=True))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
