from __future__ import annotations

import json
import shutil
import sqlite3
import time
import uuid
import zipfile
from pathlib import Path
from typing import Any, Iterable

from .api_types import (
    BFExportRequest,
    BFImportMode,
    BFImportRequest,
    BFMoveRequest,
    BFReIndexRequest,
    BFRemoveOrRestoreRequest,
    BFScanDuplicationRequest,
    BFSearchSummaryRequest,
    BFSetCoverRequest,
)
from .db import LibraryDB
from .errors import (
    ERR_DATABASE,
    ERR_INVALID_ARGUMENT,
    ERR_IO,
    ERR_LIBRARY_ALREADY_OPEN,
    ERR_LIBRARY_NOT_OPEN,
    ERR_NOT_FOUND,
    ERR_UNSUPPORTED,
    ERR_VERSION,
    OK,
)
from .schema import MIGRATION_40_TO_41_SQL
from .utils import file_sha1, normalize_for_search, safe_ext


class BillfishDataCenter:
    def __init__(self) -> None:
        self._library_root: Path | None = None
        self._bf_dir: Path | None = None
        self._db: LibraryDB | None = None

    # region library management
    def createLibrary(self, path: str, name: str) -> dict[str, Any]:
        if not path or not name:
            return self._err(ERR_INVALID_ARGUMENT)
        library_root = Path(path).expanduser().resolve()
        bf_dir = library_root / ".bf"
        try:
            bf_dir.mkdir(parents=True, exist_ok=True)
            for child in (".preview", ".recycle", ".temp", ".ui_config"):
                (bf_dir / child).mkdir(exist_ok=True)
            LibraryDB.bootstrap(bf_dir, name)
            return self._ok(message="library created")
        except OSError as exc:
            return self._err(ERR_IO, str(exc))
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def openLibrary(self, path: str) -> dict[str, Any]:
        if self._db is not None:
            return self._err(ERR_LIBRARY_ALREADY_OPEN)
        library_root = Path(path).expanduser().resolve()
        bf_dir = library_root / ".bf"
        main_db = bf_dir / "billfish.db"
        if not main_db.exists():
            return self._err(ERR_NOT_FOUND, f"missing database: {main_db}")

        try:
            db = LibraryDB(bf_dir)
            version = self._read_library_version(db.main)
            if version not in (40, 41):
                db.close()
                return self._err(ERR_VERSION, f"unsupported library version {version}")

            schema_version = self._read_schema_version(db.main)
            if schema_version < 41:
                db.main.executescript(MIGRATION_40_TO_41_SQL)
                db.main.commit()
                schema_version = 41

            self._library_root = library_root
            self._bf_dir = bf_dir
            self._db = db
            name = self._read_library_name(db.main)
            return self._ok(
                libInfo={
                    "root": str(library_root),
                    "name": name,
                    "version": version,
                    "schema_version": schema_version,
                }
            )
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def upgradeLibrary(self, path: str, fromVersion: int, toVersion: int) -> dict[str, Any]:
        if (fromVersion, toVersion) != (40, 41):
            return self._err(ERR_UNSUPPORTED, "only 40->41 migration is implemented")

        library_root = Path(path).expanduser().resolve()
        bf_dir = library_root / ".bf"
        main_db = bf_dir / "billfish.db"
        if not main_db.exists():
            return self._err(ERR_NOT_FOUND, f"missing database: {main_db}")

        try:
            conn = sqlite3.connect(main_db)
            conn.executescript(MIGRATION_40_TO_41_SQL)
            conn.commit()
            conn.close()
            return self._ok(progress=100, message="upgraded 40->41")
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def closeLibrary(self) -> dict[str, Any]:
        if self._db is None:
            return self._ok(message="library already closed")
        self._db.close()
        self._db = None
        self._library_root = None
        self._bf_dir = None
        return self._ok(message="library closed")

    # endregion

    # region file/folder actions
    def import_(self, request: BFImportRequest) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)
        if not request.entries:
            return self._err(ERR_INVALID_ARGUMENT, "entries is empty")

        source_files = list(self._walk_input_files(request.entries, request.recursive))
        if not source_files:
            return self._err(ERR_NOT_FOUND, "no files found")

        imported: list[int] = []
        failed: list[dict[str, str]] = []

        try:
            with db.tx() as conn:
                for src in source_files:
                    try:
                        row_id = self._import_one(conn, src, request)
                        imported.append(row_id)
                    except Exception as exc:  # noqa: BLE001
                        failed.append({"path": str(src), "reason": str(exc)})

            self._refresh_summary(imported)
            return self._ok(imported=imported, failed=failed, total=len(source_files))
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def reindex(self, request: BFReIndexRequest) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)

        try:
            if request.file_ids:
                file_ids = request.file_ids
            else:
                rows = db.main.execute("SELECT id FROM bf_file").fetchall()
                file_ids = [int(r["id"]) for r in rows]
            self._refresh_summary(file_ids)
            return self._ok(reindexed=len(file_ids))
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def move(self, request: BFMoveRequest) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)
        if not request.file_ids:
            return self._err(ERR_INVALID_ARGUMENT, "file_ids is empty")

        ph = _placeholders(len(request.file_ids))
        sql = f"UPDATE bf_file SET pid=?, update_time=? WHERE id IN ({ph})"
        now = _now()
        try:
            with db.tx() as conn:
                conn.execute(sql, [request.target_folder_id, now, *request.file_ids])
            self._refresh_summary(request.file_ids)
            return self._ok(moved=len(request.file_ids))
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def removeOrRestore(self, request: BFRemoveOrRestoreRequest) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)
        if not request.file_ids:
            return self._err(ERR_INVALID_ARGUMENT, "file_ids is empty")

        action = request.action.lower().strip()
        if action not in {"remove", "restore", "purge"}:
            return self._err(ERR_INVALID_ARGUMENT, "action must be remove/restore/purge")

        ph = _placeholders(len(request.file_ids))
        now = _now()

        try:
            with db.tx() as conn:
                rows = conn.execute(
                    f"SELECT id, path FROM bf_file WHERE id IN ({ph})",
                    request.file_ids,
                ).fetchall()
                file_map = {int(r["id"]): str(r["path"]) for r in rows}

                if action == "remove":
                    conn.execute(
                        f"UPDATE bf_file SET is_recycle=1, update_time=? WHERE id IN ({ph})",
                        [now, *request.file_ids],
                    )
                    for file_id in request.file_ids:
                        original_path = file_map.get(file_id, "")
                        conn.execute(
                            """
                            INSERT INTO bf_recycle(entity_type, entity_id, original_path, deleted_at)
                            VALUES('file', ?, ?, ?)
                            ON CONFLICT(entity_type, entity_id)
                            DO UPDATE SET original_path=excluded.original_path, deleted_at=excluded.deleted_at
                            """,
                            (file_id, original_path, now),
                        )

                if action == "restore":
                    conn.execute(
                        f"UPDATE bf_file SET is_recycle=0, update_time=? WHERE id IN ({ph})",
                        [now, *request.file_ids],
                    )
                    conn.execute(
                        f"DELETE FROM bf_recycle WHERE entity_type='file' AND entity_id IN ({ph})",
                        request.file_ids,
                    )

                if action == "purge":
                    imported_root = self._imported_root()
                    for row in conn.execute(
                        f"SELECT id, path, import_mode FROM bf_file WHERE id IN ({ph})",
                        request.file_ids,
                    ).fetchall():
                        file_path = Path(str(row["path"]))
                        mode = int(row["import_mode"])
                        if mode in (int(BFImportMode.ImportCopy), int(BFImportMode.ImportCut)):
                            if imported_root in file_path.parents and file_path.exists():
                                file_path.unlink(missing_ok=True)

                    conn.execute(f"DELETE FROM bf_file WHERE id IN ({ph})", request.file_ids)
                    conn.execute(
                        f"DELETE FROM bf_recycle WHERE entity_type='file' AND entity_id IN ({ph})",
                        request.file_ids,
                    )

            self._refresh_summary(request.file_ids)
            return self._ok(action=action, count=len(request.file_ids))
        except OSError as exc:
            return self._err(ERR_IO, str(exc))
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def setCover(self, request: BFSetCoverRequest) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)

        try:
            with db.tx() as conn:
                conn.execute(
                    "UPDATE bf_folder SET cover_id=?, update_time=? WHERE id=?",
                    (request.file_id, _now(), request.folder_id),
                )
            return self._ok(folder_id=request.folder_id, cover_id=request.file_id)
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    # endregion

    # region tag APIs
    def tagNew(self, name: str, group_id: int = 0, parent_id: int = 0) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)
        if not name.strip():
            return self._err(ERR_INVALID_ARGUMENT, "tag name is empty")

        now = _now()
        try:
            with db.tx() as conn:
                cur = conn.execute(
                    """
                    INSERT INTO bf_tag(name, group_id, parent_id, sort_index, create_time, update_time)
                    VALUES(?, ?, ?, 0, ?, ?)
                    """,
                    (name.strip(), group_id, parent_id, now, now),
                )
                tag_id = int(cur.lastrowid)
            return self._ok(tag_id=tag_id)
        except sqlite3.IntegrityError:
            return self._err(ERR_INVALID_ARGUMENT, "tag already exists in group")
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def tagUpdate(self, tag_id: int, name: str) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)

        try:
            with db.tx() as conn:
                conn.execute(
                    "UPDATE bf_tag SET name=?, update_time=? WHERE id=?",
                    (name.strip(), _now(), tag_id),
                )
            return self._ok(tag_id=tag_id)
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def tagRemove(self, tag_id: int) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)

        try:
            with db.tx() as conn:
                conn.execute("DELETE FROM bf_tag WHERE id=?", (tag_id,))
            self.reindex(BFReIndexRequest())
            return self._ok(tag_id=tag_id)
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def tagMove(self, tag_id: int, group_id: int, parent_id: int = 0) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)

        try:
            with db.tx() as conn:
                conn.execute(
                    "UPDATE bf_tag SET group_id=?, parent_id=?, update_time=? WHERE id=?",
                    (group_id, parent_id, _now(), tag_id),
                )
            return self._ok(tag_id=tag_id)
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def tagAttach(self, file_ids: list[int], tag_ids: list[int]) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)

        now = _now()
        links = [(fid, tid, now) for fid in file_ids for tid in tag_ids]
        try:
            with db.tx() as conn:
                conn.executemany(
                    "INSERT OR IGNORE INTO bf_file_tag(file_id, tag_id, create_time) VALUES(?, ?, ?)",
                    links,
                )
            self._refresh_summary(file_ids)
            return self._ok(link_count=len(links))
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def tagDetach(self, file_ids: list[int], tag_ids: list[int]) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)

        try:
            with db.tx() as conn:
                for fid in file_ids:
                    ph = _placeholders(len(tag_ids))
                    conn.execute(
                        f"DELETE FROM bf_file_tag WHERE file_id=? AND tag_id IN ({ph})",
                        [fid, *tag_ids],
                    )
            self._refresh_summary(file_ids)
            return self._ok(detached=len(file_ids) * len(tag_ids))
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    # endregion

    # region search and export
    def searchSummary(self, request: BFSearchSummaryRequest) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)

        try:
            if request.query.strip():
                q = normalize_for_search(request.query)
                rows = db.summary.execute(
                    "SELECT iid FROM bf_summary WHERE bf_summary MATCH ? LIMIT ? OFFSET ?",
                    (q, request.limit, request.offset),
                ).fetchall()
                ids = [int(r["iid"]) for r in rows]
                if not ids:
                    return self._ok(items=[])
                ph = _placeholders(len(ids))
                recycle_clause = "" if request.include_recycle else "AND is_recycle=0"
                q2 = (
                    f"SELECT id, pid, name, ext_name, path, size, hash, is_recycle FROM bf_file "
                    f"WHERE id IN ({ph}) {recycle_clause}"
                )
                file_rows = db.main.execute(q2, ids).fetchall()
            else:
                recycle_clause = "" if request.include_recycle else "WHERE is_recycle=0"
                file_rows = db.main.execute(
                    f"SELECT id, pid, name, ext_name, path, size, hash, is_recycle FROM bf_file {recycle_clause} ORDER BY update_time DESC LIMIT ? OFFSET ?",
                    (request.limit, request.offset),
                ).fetchall()

            items = [
                {
                    "id": int(r["id"]),
                    "pid": int(r["pid"]),
                    "name": str(r["name"]),
                    "ext_name": str(r["ext_name"]),
                    "path": str(r["path"]),
                    "size": int(r["size"]),
                    "hash": str(r["hash"]),
                    "is_recycle": bool(r["is_recycle"]),
                }
                for r in file_rows
            ]
            return self._ok(items=items)
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def scanDuplication(self, request: BFScanDuplicationRequest) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)

        try:
            dup_keys = db.main.execute(
                """
                SELECT hash, size, COUNT(*) c
                FROM bf_file
                WHERE is_recycle=0 AND hash<>''
                GROUP BY hash, size
                HAVING c > 1
                ORDER BY c DESC
                """
            ).fetchall()
            groups: list[dict[str, Any]] = []
            for key in dup_keys:
                rows = db.main.execute(
                    "SELECT id, path FROM bf_file WHERE hash=? AND size=? AND is_recycle=0",
                    (key["hash"], key["size"]),
                ).fetchall()
                groups.append(
                    {
                        "hash": str(key["hash"]),
                        "size": int(key["size"]),
                        "items": [{"id": int(r["id"]), "path": str(r["path"])} for r in rows],
                    }
                )
            return self._ok(groups=groups)
        except sqlite3.DatabaseError as exc:
            return self._err(ERR_DATABASE, str(exc))

    def export(self, request: BFExportRequest) -> dict[str, Any]:
        db = self._require_db()
        if db is None:
            return self._err(ERR_LIBRARY_NOT_OPEN)
        if not request.file_ids:
            return self._err(ERR_INVALID_ARGUMENT, "file_ids is empty")

        destination = Path(request.destination).expanduser().resolve()
        destination.mkdir(parents=True, exist_ok=True)

        ph = _placeholders(len(request.file_ids))
        rows = db.main.execute(
            f"SELECT id, name, ext_name, path, hash, size FROM bf_file WHERE id IN ({ph})",
            request.file_ids,
        ).fetchall()

        if request.mode.lower() == "folder":
            copied = 0
            for row in rows:
                src = Path(str(row["path"]))
                if not src.exists():
                    continue
                target = _dedup_path(destination / src.name)
                shutil.copy2(src, target)
                copied += 1
            return self._ok(mode="Folder", exported=copied)

        if request.mode.lower() == "packet":
            packet_path = destination / f"BillfishPack_{int(time.time())}.zip"
            manifest: dict[str, Any] = {"version": 1, "items": []}
            with zipfile.ZipFile(packet_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
                for row in rows:
                    src = Path(str(row["path"]))
                    if not src.exists():
                        continue
                    arcname = f"files/{int(row['id'])}_{src.name}"
                    zf.write(src, arcname)
                    manifest["items"].append(
                        {
                            "id": int(row["id"]),
                            "name": str(row["name"]),
                            "ext": str(row["ext_name"]),
                            "size": int(row["size"]),
                            "hash": str(row["hash"]),
                            "arcname": arcname,
                        }
                    )
                zf.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2))
            return self._ok(mode="Packet", packet=str(packet_path), exported=len(manifest["items"]))

        return self._err(ERR_INVALID_ARGUMENT, "mode must be Folder or Packet")

    # endregion

    # region transfer placeholders
    def transferPrepare(self, *args: Any, **kwargs: Any) -> dict[str, Any]:
        return self._err(ERR_UNSUPPORTED, "cloud transfer is not in phase-1 scope")

    def transferFolder(self, *args: Any, **kwargs: Any) -> dict[str, Any]:
        return self._err(ERR_UNSUPPORTED, "cloud transfer is not in phase-1 scope")

    def transferFile(self, *args: Any, **kwargs: Any) -> dict[str, Any]:
        return self._err(ERR_UNSUPPORTED, "cloud transfer is not in phase-1 scope")

    # endregion

    # region internals
    def _require_db(self) -> LibraryDB | None:
        return self._db

    def _ok(self, message: str | None = None, **payload: Any) -> dict[str, Any]:
        out = {
            "code": OK.code,
            "message": message or OK.message,
        }
        out.update(payload)
        return out

    def _err(self, err, details: str | None = None) -> dict[str, Any]:
        message = err.message if details is None else f"{err.message}: {details}"
        return {"code": err.code, "message": message}

    def _read_library_version(self, conn: sqlite3.Connection) -> int:
        row = conn.execute("SELECT value FROM library WHERE key='version'").fetchone()
        if not row:
            return 40
        return int(row["value"])

    def _read_library_name(self, conn: sqlite3.Connection) -> str:
        row = conn.execute("SELECT value FROM library WHERE key='name'").fetchone()
        return str(row["value"]) if row else "Billfish Library"

    def _read_schema_version(self, conn: sqlite3.Connection) -> int:
        row = conn.execute("SELECT value FROM app_meta WHERE key='schema_version'").fetchone()
        if not row:
            return 40
        return int(row["value"])

    def _walk_input_files(self, entries: Iterable[str], recursive: bool) -> Iterable[Path]:
        for raw in entries:
            p = Path(raw).expanduser().resolve()
            if not p.exists():
                continue
            if p.is_file():
                yield p
            elif p.is_dir():
                if recursive:
                    for f in p.rglob("*"):
                        if f.is_file():
                            yield f
                else:
                    for f in p.iterdir():
                        if f.is_file():
                            yield f

    def _import_one(self, conn: sqlite3.Connection, src: Path, request: BFImportRequest) -> int:
        mode = request.import_mode
        final_path = src

        if mode in (BFImportMode.ImportCopy, BFImportMode.ImportCut):
            imported_root = self._imported_root()
            imported_root.mkdir(parents=True, exist_ok=True)
            token = uuid.uuid4().hex[:12]
            final_path = imported_root / f"{token}{src.suffix.lower()}"
            if mode == BFImportMode.ImportCopy:
                shutil.copy2(src, final_path)
            else:
                shutil.move(str(src), str(final_path))

        stat = final_path.stat()
        ext = safe_ext(final_path)
        digest = file_sha1(final_path)
        now = _now()

        conn.execute(
            """
            INSERT INTO bf_file(
                pid, name, ext_name, path, size, hash,
                ctime, mtime, import_mode, is_index, is_recycle,
                create_time, update_time
            ) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, 1, 0, ?, ?)
            ON CONFLICT(path) DO UPDATE SET
                size=excluded.size,
                hash=excluded.hash,
                mtime=excluded.mtime,
                update_time=excluded.update_time,
                is_recycle=0
            """,
            (
                request.target_folder_id,
                final_path.name,
                ext,
                str(final_path),
                int(stat.st_size),
                digest,
                int(stat.st_ctime),
                int(stat.st_mtime),
                int(mode),
                now,
                now,
            ),
        )

        row = conn.execute("SELECT id FROM bf_file WHERE path=?", (str(final_path),)).fetchone()
        file_id = int(row["id"])

        conn.execute(
            "INSERT OR IGNORE INTO bf_material_userdata(file_id, comment, note) VALUES(?, '', '')",
            (file_id,),
        )
        return file_id

    def _imported_root(self) -> Path:
        assert self._library_root is not None
        return self._library_root / "_imported"

    def _refresh_summary(self, file_ids: list[int]) -> None:
        db = self._require_db()
        if db is None:
            return
        if not file_ids:
            return

        ph = _placeholders(len(file_ids))
        rows = db.main.execute(
            f"""
            SELECT f.id, f.name, f.ext_name, f.pid, f.is_recycle,
                   COALESCE(u.comment, '') AS comment,
                   COALESCE(u.note, '') AS note,
                   COALESCE(fd.name, '') AS folder_name
            FROM bf_file f
            LEFT JOIN bf_material_userdata u ON u.file_id=f.id
            LEFT JOIN bf_folder fd ON fd.id=f.pid
            WHERE f.id IN ({ph})
            """,
            file_ids,
        ).fetchall()

        with db.summary_tx() as conn:
            for row in rows:
                file_id = int(row["id"])
                if int(row["is_recycle"]) == 1:
                    conn.execute("DELETE FROM bf_summary WHERE iid=?", (file_id,))
                    continue

                tag_rows = db.main.execute(
                    """
                    SELECT t.name
                    FROM bf_file_tag ft
                    JOIN bf_tag t ON t.id = ft.tag_id
                    WHERE ft.file_id=?
                    ORDER BY t.name
                    """,
                    (file_id,),
                ).fetchall()
                tags = " ".join(str(t["name"]) for t in tag_rows)

                name = normalize_for_search(str(row["name"]))
                ext_name = normalize_for_search(str(row["ext_name"]))
                folder_name = normalize_for_search(str(row["folder_name"]))
                comment = normalize_for_search(str(row["comment"]))
                note = normalize_for_search(str(row["note"]))
                tags_norm = normalize_for_search(tags)

                conn.execute("DELETE FROM bf_summary WHERE iid=?", (file_id,))
                conn.execute(
                    """
                    INSERT INTO bf_summary(iid, name, ext_name, tags, folder_name, comment, note)
                    VALUES(?, ?, ?, ?, ?, ?, ?)
                    """,
                    (file_id, name, ext_name, tags_norm, folder_name, comment, note),
                )

    # endregion


def _placeholders(n: int) -> str:
    return ",".join("?" for _ in range(n))


def _now() -> int:
    return int(time.time())


def _dedup_path(path: Path) -> Path:
    if not path.exists():
        return path

    stem = path.stem
    suffix = path.suffix
    idx = 1
    while True:
        candidate = path.with_name(f"{stem}_{idx}{suffix}")
        if not candidate.exists():
            return candidate
        idx += 1
