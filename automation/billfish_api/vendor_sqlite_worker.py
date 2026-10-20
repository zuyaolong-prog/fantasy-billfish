"""Private pipe worker. Intentionally never imports Python's sqlite3 module."""

import json
from pathlib import Path
import sys

# Launched with Python -I and this exact file path; add only our package root.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from billfish_api.errors import ApiError
from billfish_api.vendor_sqlite import _DirectVendorDatabase, _wire_decode, _wire_encode, _error


def main():
    database = None
    try:
        for line in sys.stdin:
            closing = False
            try:
                request = _wire_decode(json.loads(line))
                operation = request.pop("op")
                if operation == "open" and database is None:
                    database = _DirectVendorDatabase(**request)
                    result = None
                elif database is None:
                    raise _error("The isolated database is not open.")
                elif operation in ("query", "execute", "attach"):
                    result = getattr(database, operation)(**request)
                elif operation == "begin":
                    if not database._sqlite.sqlite3_get_autocommit(database._db):
                        raise _error("Nested database transactions are not supported.")
                    result = database.execute("BEGIN" if database.readonly else "BEGIN IMMEDIATE")
                elif operation in ("commit", "rollback"):
                    result = database.execute(operation.upper())
                elif operation == "close":
                    database.close()
                    database = None
                    result, closing = None, True
                else:
                    raise _error("Unsupported private database operation.")
                reply = {"result": result}
            except ApiError as exc:
                reply = {"error": {**exc.to_dict(), "status": exc.status}}
            except Exception as exc:
                reply = {"error": {"code": "native_database_unavailable",
                                   "message": "The isolated database operation failed.",
                                   "details": {"reason": str(exc)}, "status": 503}}
            sys.stdout.write(json.dumps(_wire_encode(reply), ensure_ascii=True) + "\n")
            sys.stdout.flush()
            if closing:
                return
    finally:
        if database is not None:
            database.close()


if __name__ == "__main__":
    main()
