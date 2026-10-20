"""Internal SQLite bridge for the frozen Windows x64 Billfish runtime.

All handles and statements belong to the shipped SQLite, including the handle
passed to Billfish's private FTS registration function. Never pass Python
sqlite3 connection pointers here. This module is not an arbitrary SQL API.
"""

from contextlib import contextmanager
import ctypes as C
import hashlib
import os
from pathlib import Path
import platform
import re
import struct
import base64
import json
import subprocess
import sys

from .errors import ApiError


FROZEN_HASHES = {
    "sqlite3.dll": "e845a94029d9fc238631950ca66439c62246573396747643a58ab665f19ba5d9",
    "libBFDataCenter.dll": "302c9b755b81ad97cd863ddfeb3caf58dded6dd662c9ea486c02e5d626459ce6",
}
TOKENIZER_REGISTRATION_RVA = 0xEA110
SQLITE_VERSION = "3.38.5"
_TRANSIENT = C.c_void_p(-1)


def _error(message, details=None):
    return ApiError("native_database_unavailable", message, details, 503)


def _runtime_paths(install_root):
    if os.name != "nt" or C.sizeof(C.c_void_p) != 8 or platform.machine().upper() not in ("AMD64", "X86_64"):
        raise _error("The frozen database bridge requires Windows AMD64 Python.")
    result = {}
    for name, digest in FROZEN_HASHES.items():
        path = Path(install_root).absolute() / name
        try:
            data = path.read_bytes()
            pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
            machine = struct.unpack_from("<H", data, pe_offset + 4)[0]
            signature = data[pe_offset:pe_offset + 4]
        except (OSError, ValueError, struct.error) as exc:
            raise _error("A required frozen runtime file could not be verified.", {"file": name}) from exc
        if (hashlib.sha256(data).hexdigest() != digest or data[:2] != b"MZ"
                or signature != b"PE\0\0" or machine != 0x8664):
            raise _error("The database bridge does not support this runtime identity.", {"file": name})
        result[name] = path
    return result


class _DirectVendorDatabase:
    """Existing-file connection with parameterized query/execute and transactions."""

    def __init__(self, database, *, install_root, readonly=False):
        self.database = Path(database).absolute()
        self.readonly = readonly
        self._db = C.c_void_p()
        self._directory = None
        self._sqlite = None
        self._native = None
        if not self.database.is_file():
            raise ApiError("library_not_found", "The database file does not exist.", status=404)
        paths = _runtime_paths(install_root)
        try:
            self._directory = os.add_dll_directory(str(Path(install_root).absolute()))
            self._sqlite = C.CDLL(str(paths["sqlite3.dll"]))
            self._native = C.WinDLL(str(paths["libBFDataCenter.dll"]))
            self._configure()
            version = self._sqlite.sqlite3_libversion().decode("ascii")
            if version != SQLITE_VERSION:
                raise _error("The loaded SQLite version does not match the frozen runtime.")
            flags = (1 if readonly else 2) | 0x40  # READONLY / READWRITE + URI, no CREATE.
            uri = self.database.as_uri() + ("?mode=ro" if readonly else "?mode=rw")
            self._check(self._sqlite.sqlite3_open_v2(uri.encode("utf-8"), C.byref(self._db), flags, None))
            self._check(self._sqlite.sqlite3_busy_timeout(self._db, 5000))
            register = C.CFUNCTYPE(C.c_int, C.c_void_p)(self._native._handle + TOKENIZER_REGISTRATION_RVA)
            self._check(register(self._db), "The Billfish tokenizer could not be registered.")
            if readonly:
                self.execute("PRAGMA query_only=ON")
        except BaseException as exc:
            self.close()
            if isinstance(exc, (ApiError, KeyboardInterrupt, SystemExit)):
                raise
            raise _error("The frozen SQLite bridge could not be initialized.", {"reason": str(exc)}) from exc

    def _configure(self):
        signatures = {
            "sqlite3_libversion": (C.c_char_p, []),
            "sqlite3_open_v2": (C.c_int, [C.c_char_p, C.POINTER(C.c_void_p), C.c_int, C.c_char_p]),
            "sqlite3_close": (C.c_int, [C.c_void_p]),
            "sqlite3_errmsg": (C.c_char_p, [C.c_void_p]),
            "sqlite3_busy_timeout": (C.c_int, [C.c_void_p, C.c_int]),
            "sqlite3_prepare_v2": (C.c_int, [C.c_void_p, C.c_char_p, C.c_int, C.POINTER(C.c_void_p), C.POINTER(C.c_char_p)]),
            "sqlite3_finalize": (C.c_int, [C.c_void_p]),
            "sqlite3_bind_parameter_count": (C.c_int, [C.c_void_p]),
            "sqlite3_bind_null": (C.c_int, [C.c_void_p, C.c_int]),
            "sqlite3_bind_int64": (C.c_int, [C.c_void_p, C.c_int, C.c_int64]),
            "sqlite3_bind_double": (C.c_int, [C.c_void_p, C.c_int, C.c_double]),
            "sqlite3_bind_text": (C.c_int, [C.c_void_p, C.c_int, C.c_char_p, C.c_int, C.c_void_p]),
            "sqlite3_bind_blob": (C.c_int, [C.c_void_p, C.c_int, C.c_void_p, C.c_int, C.c_void_p]),
            "sqlite3_step": (C.c_int, [C.c_void_p]),
            "sqlite3_column_count": (C.c_int, [C.c_void_p]),
            "sqlite3_column_name": (C.c_char_p, [C.c_void_p, C.c_int]),
            "sqlite3_column_type": (C.c_int, [C.c_void_p, C.c_int]),
            "sqlite3_column_int64": (C.c_int64, [C.c_void_p, C.c_int]),
            "sqlite3_column_double": (C.c_double, [C.c_void_p, C.c_int]),
            "sqlite3_column_text": (C.c_void_p, [C.c_void_p, C.c_int]),
            "sqlite3_column_blob": (C.c_void_p, [C.c_void_p, C.c_int]),
            "sqlite3_column_bytes": (C.c_int, [C.c_void_p, C.c_int]),
            "sqlite3_changes64": (C.c_int64, [C.c_void_p]),
            "sqlite3_get_autocommit": (C.c_int, [C.c_void_p]),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self._sqlite, name)
            function.restype, function.argtypes = result, arguments

    def _check(self, code, message="The database operation failed."):
        if code:
            reason = self._sqlite.sqlite3_errmsg(self._db).decode("utf-8", "replace") if self._db.value else "No database handle"
            raise _error(message, {"sqlite_code": code, "reason": reason})

    def _ensure_open(self):
        if not self._db.value:
            raise _error("The database connection is closed.")

    @contextmanager
    def _statement(self, sql, parameters):
        self._ensure_open()
        if not isinstance(sql, str) or "\0" in sql:
            raise ApiError("invalid_argument", "Internal SQL must be text without NUL bytes.")
        statement = C.c_void_p()
        tail = C.c_char_p()
        encoded = sql.encode("utf-8")
        try:
            self._check(self._sqlite.sqlite3_prepare_v2(self._db, encoded, len(encoded), C.byref(statement), C.byref(tail)))
            if not statement.value or (tail.value and tail.value.strip()):
                raise ApiError("invalid_argument", "Exactly one internal SQL statement is required.")
            values = tuple(parameters)
            if self._sqlite.sqlite3_bind_parameter_count(statement) != len(values):
                raise ApiError("invalid_argument", "The SQL parameter count does not match.")
            for index, value in enumerate(values, 1):
                if value is None:
                    code = self._sqlite.sqlite3_bind_null(statement, index)
                elif isinstance(value, int) and -(2**63) <= value <= 2**63 - 1:
                    code = self._sqlite.sqlite3_bind_int64(statement, index, value)
                elif isinstance(value, float):
                    code = self._sqlite.sqlite3_bind_double(statement, index, value)
                elif isinstance(value, str):
                    data = value.encode("utf-8")
                    code = self._sqlite.sqlite3_bind_text(statement, index, data, len(data), _TRANSIENT)
                elif isinstance(value, bytes):
                    code = self._sqlite.sqlite3_bind_blob(statement, index, value, len(value), _TRANSIENT)
                else:
                    raise ApiError("invalid_argument", "Unsupported SQL parameter type or integer range.")
                self._check(code)
            yield statement
        finally:
            if statement.value:
                self._sqlite.sqlite3_finalize(statement)

    def query(self, sql, parameters=()):
        rows = []
        with self._statement(sql, parameters) as statement:
            count = self._sqlite.sqlite3_column_count(statement)
            names = [self._sqlite.sqlite3_column_name(statement, i).decode("utf-8") for i in range(count)]
            while True:
                code = self._sqlite.sqlite3_step(statement)
                if code == 101:  # SQLITE_DONE
                    return rows
                if code != 100:  # SQLITE_ROW
                    self._check(code)
                row = {}
                for index, name in enumerate(names):
                    kind = self._sqlite.sqlite3_column_type(statement, index)
                    if kind == 1:
                        value = self._sqlite.sqlite3_column_int64(statement, index)
                    elif kind == 2:
                        value = self._sqlite.sqlite3_column_double(statement, index)
                    elif kind in (3, 4):
                        pointer = (self._sqlite.sqlite3_column_text if kind == 3 else self._sqlite.sqlite3_column_blob)(statement, index)
                        size = self._sqlite.sqlite3_column_bytes(statement, index)
                        value = C.string_at(pointer, size) if size else b""
                        if kind == 3:
                            value = value.decode("utf-8")
                    else:
                        value = None
                    row[name] = value
                rows.append(row)

    def execute(self, sql, parameters=()):
        self.query(sql, parameters)
        return self._sqlite.sqlite3_changes64(self._db)

    def attach(self, database, schema="summary", *, readonly=False):
        self._ensure_open()
        path = Path(database).absolute()
        if not path.is_file():
            raise ApiError("library_not_found", "The attached database file does not exist.", status=404)
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", schema) or schema.lower() in ("main", "temp"):
            raise ApiError("invalid_argument", "Invalid internal database schema name.")
        if self.readonly and not readonly:
            raise ApiError("invalid_argument", "A read-only connection requires read-only attachments.")
        uri = path.as_uri() + ("?mode=ro" if readonly else "?mode=rw")
        self.execute(f'ATTACH DATABASE ? AS "{schema}"', (uri,))

    @contextmanager
    def transaction(self):
        self._ensure_open()
        if not self._sqlite.sqlite3_get_autocommit(self._db):
            raise _error("Nested database transactions are not supported.")
        self.execute("BEGIN" if self.readonly else "BEGIN IMMEDIATE")
        try:
            yield self
            self.execute("COMMIT")
        except BaseException:
            if not self._sqlite.sqlite3_get_autocommit(self._db):
                self.execute("ROLLBACK")
            raise

    def close(self):
        if self._db.value:
            code = self._sqlite.sqlite3_close(self._db)
            self._check(code, "The database connection could not be closed.")
            self._db = C.c_void_p()
        if self._directory is not None:
            self._directory.close()
            self._directory = None

    def __enter__(self):
        self._ensure_open()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()


def _wire_encode(value):
    if isinstance(value, bytes):
        return {"$blob": base64.b64encode(value).decode("ascii")}
    if isinstance(value, (tuple, list)):
        return [_wire_encode(item) for item in value]
    if isinstance(value, dict):
        return {key: _wire_encode(item) for key, item in value.items()}
    return value


def _wire_decode(value):
    if isinstance(value, dict):
        if set(value) == {"$blob"}:
            return base64.b64decode(value["$blob"], validate=True)
        return {key: _wire_decode(item) for key, item in value.items()}
    if isinstance(value, list):
        return [_wire_decode(item) for item in value]
    return value


class VendorDatabase:
    """Dedicated vendor SQLite process, isolated from Python's sqlite3.dll.

    Python's SQLite extension may already have loaded another sqlite3.dll.
    Windows can resolve Billfish's imports against that DLL even when ctypes
    explicitly opens the vendor DLL. A pipe-only child owns both vendor DLLs
    and every connection/statement; it never imports stdlib sqlite3.
    """

    def __init__(self, database, *, install_root, readonly=False):
        self.database = Path(database).absolute()
        self.readonly = readonly
        self._process = None
        if not self.database.is_file():
            raise ApiError("library_not_found", "The database file does not exist.", status=404)
        _runtime_paths(install_root)
        worker = Path(__file__).with_name("vendor_sqlite_worker.py")
        try:
            self._process = subprocess.Popen(
                [sys.executable, "-I", str(worker)], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, encoding="utf-8", bufsize=1,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            self._request("open", database=str(self.database), install_root=str(Path(install_root).absolute()), readonly=readonly)
        except BaseException:
            self._terminate()
            raise

    def _terminate(self):
        process, self._process = self._process, None
        if process is not None:
            if process.poll() is None:
                process.kill()
            process.communicate()

    def _request(self, operation, **arguments):
        process = self._process
        if process is None:
            raise _error("The database connection is closed.")
        try:
            process.stdin.write(json.dumps(_wire_encode({"op": operation, **arguments}), ensure_ascii=True) + "\n")
            process.stdin.flush()
            line = process.stdout.readline()
            if not line:
                process.wait()
                stderr = process.stderr.read()[-4000:]
                raise _error("The isolated database process stopped unexpectedly.",
                             {"exit_code": process.returncode, "stderr": stderr})
            reply = _wire_decode(json.loads(line))
            if "error" in reply:
                error = reply["error"]
                raise ApiError(error["code"], error["message"], error.get("details"), error.get("status", 503))
            return reply["result"]
        except (OSError, ValueError, KeyError) as exc:
            raise _error("The isolated database process communication failed.", {"reason": str(exc)}) from exc

    def query(self, sql, parameters=()):
        return self._request("query", sql=sql, parameters=tuple(parameters))

    def execute(self, sql, parameters=()):
        return self._request("execute", sql=sql, parameters=tuple(parameters))

    def attach(self, database, schema="summary", *, readonly=False):
        return self._request("attach", database=str(Path(database).absolute()), schema=schema, readonly=readonly)

    @contextmanager
    def transaction(self):
        self._request("begin")
        try:
            yield self
            self._request("commit")
        except BaseException:
            self._request("rollback")
            raise

    def close(self):
        process = self._process
        if process is None:
            return
        try:
            self._request("close")
            self._process = None
            process.stdin.close()
            process.wait(timeout=5)
            process.stdout.close()
            process.stderr.close()
        except BaseException:
            if self._process is None:
                self._process = process
            self._terminate()
            raise

    def __enter__(self):
        if self._process is None:
            raise _error("The database connection is closed.")
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()
