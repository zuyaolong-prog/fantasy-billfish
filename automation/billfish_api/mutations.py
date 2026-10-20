"""Offline, validated mutations of the frozen Billfish v41 catalog.

No media paths are moved or removed. Both native SQLite databases participate in
one DELETE-journal transaction, with a cold backup made before its first write.
"""

from contextlib import contextmanager
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import time
import uuid

from .catalog import Catalog, _id, _serial_row
from .errors import ApiError
from .vendor_sqlite import VendorDatabase


MAX_BATCH = 500


def running_billfish_processes():
    """Enumerate Windows process names without third-party dependencies."""
    if os.name != "nt":
        raise ApiError("process_check_unavailable", "Offline writes require Windows process enumeration.", status=503)

    class ProcessEntry(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                    ("th32ProcessID", wintypes.DWORD), ("th32DefaultHeapID", ctypes.c_size_t),
                    ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                    ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", wintypes.LONG),
                    ("dwFlags", wintypes.DWORD), ("szExeFile", wintypes.WCHAR * 260)]

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.Process32FirstW.restype = wintypes.BOOL
    kernel.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.Process32NextW.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
    if snapshot == ctypes.c_void_p(-1).value:
        raise ApiError("process_check_unavailable", "Windows process enumeration failed.", status=503)
    found = []
    try:
        entry = ProcessEntry()
        entry.dwSize = ctypes.sizeof(entry)
        available = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
        while available:
            if entry.szExeFile.casefold() in ("billfish.exe", "billfish.core.exe"):
                found.append({"pid": entry.th32ProcessID, "name": entry.szExeFile})
            available = kernel.Process32NextW(snapshot, ctypes.byref(entry))
        if ctypes.get_last_error() != 18:  # ERROR_NO_MORE_FILES
            raise ApiError("process_check_unavailable", "Windows process enumeration was incomplete.", status=503)
    finally:
        kernel.CloseHandle(snapshot)
    return found


def _batch(values, name):
    if not isinstance(values, list) or not 1 <= len(values) <= MAX_BATCH:
        raise ApiError("invalid_argument", f"{name} must contain between 1 and {MAX_BATCH} entries.")
    return values


def _ids(values, name):
    result = [_id(value, name) for value in _batch(values, name)]
    if len(set(result)) != len(result):
        raise ApiError("invalid_argument", f"{name} contains duplicate IDs.")
    return result


class Mutations:
    def __init__(self, root: Path, install_root: Path):
        self.root = Path(root).absolute()
        self.install_root = Path(install_root).absolute()
        self.database = self.root / ".bf/billfish.db"
        self.summary = self.root / ".bf/summary_v2.db"

    def _gui_guard(self):
        processes = running_billfish_processes()
        if processes:
            raise ApiError("gui_running", "Close Billfish before applying offline changes. The API never closes or restarts it.",
                           {"processes": processes}, 409)

    @contextmanager
    def _connection(self, dry_run):
        if not isinstance(dry_run, bool):
            raise ApiError("invalid_argument", "dry_run must be a boolean.")
        if not dry_run:
            self._gui_guard()
        with VendorDatabase(self.database, install_root=self.install_root, readonly=dry_run) as db:
            db.attach(self.summary, readonly=dry_run)
            with db.transaction():
                self._validate_schema(db)
                yield db

    def _validate_schema(self, db):
        required = {
            "library": {"version"},
            "bf_file": {"id", "name", "pid", "is_link", "tid"},
            "bf_folder": {"id", "pid", "name", "is_recycle"},
            "bf_material_v2": {"file_id", "is_recycle"},
            "bf_material_userdata": {"file_id", "note", "origin", "comments_summary", "comments_count", "comments_detail", "score", "rotation", "hflip", "vflip", "cover_tid"},
            "bf_type": {"tid", "name"},
            "bf_tag_v2": {"id", "name", "pid", "seq", "icon", "color", "born"},
            "bf_tag_join_file": {"file_id", "tag_id", "born"},
        }
        for table, fields in required.items():
            actual = {row["name"] for row in db.query(f'PRAGMA main.table_info("{table}")')}
            if not fields <= actual:
                raise ApiError("unsupported_schema", "Required mutation fields are missing.",
                               {"table": table, "missing_columns": sorted(fields - actual)}, 409)
        version = db.query("SELECT version FROM library LIMIT 1")
        if not version or version[0]["version"] != 41:
            raise ApiError("unsupported_schema", "Offline mutations support only frozen schema version 41.", status=409)
        columns = {row["name"] for row in db.query('PRAGMA summary.table_info("bf_summary")')}
        ddl = db.query("SELECT sql FROM summary.sqlite_master WHERE name='bf_summary'")
        if (not {"iid", "name", "type", "origin", "comment", "note"} <= columns or not ddl
                or not re.search(r"\busing\s+fts5\s*\(", ddl[0]["sql"], re.I)
                or not re.search(r"\btokenize\s*=\s*['\"]?bf_tokenizer\b", ddl[0]["sql"], re.I)):
            raise ApiError("unsupported_schema", "The native bf_tokenizer summary index is required.", status=409)
        modes = {schema: next(iter(db.query(f"PRAGMA {schema}.journal_mode")[0].values())).lower()
                 for schema in ("main", "summary")}
        if any(mode != "delete" for mode in modes.values()):
            raise ApiError("unsupported_journal_mode", "Atomic two-database writes require DELETE journal mode. No PRAGMA settings were changed.", modes, 409)
        sync = {schema: next(iter(db.query(f"PRAGMA {schema}.synchronous")[0].values()))
                for schema in ("main", "summary")}
        if any(value == 0 for value in sync.values()):
            raise ApiError("unsupported_journal_mode", "Offline writes require SQLite synchronous to be enabled.", sync, 409)

    def _items(self, db, identifiers):
        folders = {row["id"]: row for row in db.query("SELECT * FROM bf_folder")}
        result = []
        for identifier in identifiers:
            rows = db.query("SELECT f.*,m.is_recycle FROM bf_file f LEFT JOIN bf_material_v2 m ON m.file_id=f.id WHERE f.id=?", (identifier,))
            if len(rows) != 1:
                raise ApiError("item_not_found", "The item does not exist.", {"item_id": str(identifier)}, 404)
            row = rows[0]
            path, parent_recycled = Catalog(self.root)._path(row["name"], row["pid"], folders)
            if row["is_link"] or row["is_recycle"] or parent_recycled or path["path_error"] or not path["exists"]:
                raise ApiError("item_not_writable", "Only existing ordinary, active on-disk items can be changed.",
                               {"item_id": str(identifier), "path_error": path["path_error"]}, 409)
            result.append(row)
        return result

    def _tags(self, db, identifiers):
        for identifier in identifiers:
            if not db.query("SELECT id FROM bf_tag_v2 WHERE id=?", (identifier,)):
                raise ApiError("tag_not_found", "The active tag does not exist.", {"tag_id": str(identifier)}, 404)

    def _backup(self):
        backup = self.root / ".bf/automation-backups" / uuid.uuid4().hex
        backup.mkdir(parents=True, exist_ok=False)
        manifest = {"format": 1, "created_at_unix_ms": int(time.time() * 1000),
                    "library_root": str(self.root), "operation_state": "backed_up_before_write", "files": [],
                    "restore_instructions": "Close Billfish and all API processes. Preserve the current .bf directory. Restore BOTH listed database files together to their original paths. Do not restore over a running process. This API has no restore endpoint."}
        for source in (self.database, self.summary):
            destination = backup / source.name
            shutil.copy2(source, destination)
            manifest["files"].append({"name": source.name, "original_path": str(source),
                                      "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
                                      "bytes": destination.stat().st_size})
        (backup / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        return str(backup)

    @staticmethod
    def _result(dry_run, plan, **values):
        return {"dry_run": dry_run, "offline": True, "requires_gui_restart": not dry_run,
                "plan": plan, **values}

    def update_items(self, updates: list[dict], dry_run=True):
        normalized = []
        for update in _batch(updates, "updates"):
            if not isinstance(update, dict) or set(update) - {"item_id", "note", "origin"} or "item_id" not in update:
                raise ApiError("invalid_argument", "Each update requires item_id and only note/origin fields.")
            fields = {key: value for key, value in update.items() if key != "item_id"}
            if not fields or any(not isinstance(value, str) or "\0" in value or len(value.encode("utf-8")) > 1048576 for value in fields.values()):
                raise ApiError("invalid_argument", "Provide note/origin strings without NUL, up to 1 MiB each.")
            normalized.append((_id(update["item_id"], "item_id"), fields))
        identifiers = [identifier for identifier, _ in normalized]
        if len(set(identifiers)) != len(identifiers):
            raise ApiError("invalid_argument", "updates contains duplicate item IDs.")
        with self._connection(dry_run) as db:
            self._items(db, identifiers)
            plan = []
            for identifier, fields in normalized:
                rows = db.query("SELECT * FROM bf_material_userdata WHERE file_id=?", (identifier,))
                if len(rows) > 1:
                    raise ApiError("unsupported_schema", "Duplicate userdata rows cannot be mutated safely.", status=409)
                before = rows[0] if rows else {}
                changes = {key: value for key, value in fields.items() if key not in before or before[key] != value}
                plan.append({"item_id": str(identifier), "before": {key: before.get(key) for key in fields}, "changes": changes})
            if dry_run:
                return self._result(True, plan, modified_count=sum(bool(row["changes"]) for row in plan))
            if not any(entry["changes"] for entry in plan):
                return self._result(False, plan, modified_count=0, items=[], backup_directory=None, requires_gui_restart=False)
            backup = self._backup()
            for entry in plan:
                fields = entry["changes"]
                if not fields:
                    continue
                identifier = int(entry["item_id"])
                exists = db.query("SELECT file_id FROM bf_material_userdata WHERE file_id=?", (identifier,))
                if exists:
                    db.execute("UPDATE bf_material_userdata SET " + ",".join(key + "=?" for key in fields) + " WHERE file_id=?", (*fields.values(), identifier))
                else:
                    defaults = {"comments_summary": "", "comments_count": 0, "comments_detail": "",
                                "note": "", "origin": "", "score": 0, "rotation": 0,
                                "hflip": 0, "vflip": 0, "cover_tid": "0", **fields}
                    db.execute("INSERT INTO bf_material_userdata(file_id," + ",".join(defaults) + ") VALUES(" + ",".join("?" for _ in range(len(defaults) + 1)) + ")", (identifier, *defaults.values()))
                db.execute("DELETE FROM summary.bf_summary WHERE iid=?", (identifier,))
                db.execute("""INSERT INTO summary.bf_summary(iid,name,type,origin,comment,note)
                    SELECT f.id,f.name,t.name,u.origin,u.comments_summary,u.note FROM bf_file f
                    LEFT JOIN bf_type t ON f.tid=t.tid LEFT JOIN bf_material_userdata u ON f.id=u.file_id WHERE f.id=?""", (identifier,))
            items = [_serial_row(row) for identifier in identifiers for row in db.query("SELECT * FROM bf_material_userdata WHERE file_id=?", (identifier,))]
            return self._result(False, plan, modified_count=sum(bool(row["changes"]) for row in plan), items=items, backup_directory=backup)

    def create_tag(self, name, pid="0", dry_run=True):
        if not isinstance(name, str) or not name.strip() or "\0" in name or len(name.encode("utf-8")) > 4096:
            raise ApiError("invalid_argument", "Tag name must be nonempty text without NUL, up to 4096 UTF-8 bytes.")
        parent = _id(pid, "pid", allow_zero=True)
        with self._connection(dry_run) as db:
            if parent:
                self._tags(db, [parent])
            if db.query("SELECT id FROM bf_tag_v2 WHERE name=? AND pid=?", (name, parent)):
                raise ApiError("tag_exists", "A tag with this name already exists under this parent.", status=409)
            sequence = db.query("SELECT COALESCE(MAX(seq),0)+1 AS next FROM bf_tag_v2 WHERE pid=?", (parent,))[0]["next"]
            plan = {"name": name, "pid": str(parent), "seq": sequence, "icon": 0, "color": 0, "born": 0}
            if dry_run:
                return self._result(True, plan, modified_count=1)
            backup = self._backup()
            db.execute("INSERT INTO bf_tag_v2(name,pid,seq,icon,color,born) VALUES(?,?,?,?,?,?)", (name, parent, sequence, 0, 0, 0))
            tag = db.query("SELECT * FROM bf_tag_v2 WHERE id=last_insert_rowid()")[0]
            return self._result(False, plan, modified_count=1, tag=_serial_row(tag), backup_directory=backup)

    def assign_tags(self, item_ids: list, tag_ids: list, mode="add", dry_run=True):
        identifiers = _ids(item_ids, "item_ids")
        tags = _ids(tag_ids, "tag_ids")
        if mode not in ("add", "remove"):
            raise ApiError("invalid_argument", "mode must be add or remove.")
        if len(identifiers) * len(tags) > MAX_BATCH:
            raise ApiError("invalid_argument", "A tag batch may contain at most 500 item/tag pairs.")
        with self._connection(dry_run) as db:
            self._items(db, identifiers)
            self._tags(db, tags)
            plan = []
            for identifier in identifiers:
                for tag in tags:
                    present = bool(db.query("SELECT file_id FROM bf_tag_join_file WHERE file_id=? AND tag_id=?", (identifier, tag)))
                    if present != (mode == "add"):
                        plan.append({"item_id": str(identifier), "tag_id": str(tag), "action": mode})
            if dry_run:
                return self._result(True, plan, modified_count=len(plan))
            if not plan:
                return self._result(False, plan, modified_count=0, items=[], backup_directory=None, requires_gui_restart=False)
            backup = self._backup()
            for entry in plan:
                pair = (int(entry["item_id"]), int(entry["tag_id"]))
                if mode == "add":
                    db.execute("INSERT INTO bf_tag_join_file(file_id,tag_id,born) VALUES(?,?,?)", (*pair, 0))
                else:
                    db.execute("DELETE FROM bf_tag_join_file WHERE file_id=? AND tag_id=?", pair)
            readback = [{"item_id": str(identifier), "tag_ids": [str(row["tag_id"]) for row in db.query("SELECT DISTINCT tag_id FROM bf_tag_join_file WHERE file_id=? ORDER BY tag_id", (identifier,))]} for identifier in identifiers]
            return self._result(False, plan, modified_count=len(plan), items=readback, backup_directory=backup)
