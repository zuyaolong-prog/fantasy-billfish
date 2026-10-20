"""Read-only access to the installed Billfish v41 catalog and disk layout."""

from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import sqlite3

from .errors import ApiError


def _id(value, name="id", allow_zero=False):
    try:
        if isinstance(value, bool) or not isinstance(value, (str, int)):
            raise ValueError
        number = int(value)
        if str(number) != str(value) or number < (0 if allow_zero else 1) or number > 2**63 - 1:
            raise ValueError
        return number
    except (ValueError, TypeError):
        raise ApiError("invalid_argument", f"{name} must be a {'nonnegative' if allow_zero else 'positive'} signed 64-bit integer.") from None


def _safe_component(name):
    return (isinstance(name, str) and bool(name) and name not in (".", "..")
            and not any(c in name for c in "/\\:\0")
            and not any(ord(c) < 32 for c in name))


def _serial_row(row):
    return {key: str(value) if value is not None and (key == "id" or key == "pid" or key.endswith("_id") or key.endswith("_tid") or key in ("tid", "ttid")) else value
            for key, value in dict(row).items()}


class Catalog:
    def __init__(self, root: Path):
        self.root = Path(root).absolute()
        self.database = self.root / ".bf/billfish.db"

    @contextmanager
    def _connection(self):
        if not self.database.is_file():
            raise ApiError("library_not_found", "The library database does not exist.", status=404)
        connection = None
        try:
            connection = sqlite3.connect(self.database.as_uri() + "?mode=ro", uri=True, timeout=5)
            connection.row_factory = sqlite3.Row
            connection.execute("PRAGMA query_only=ON")
            connection.execute("BEGIN")
            required = {
                "library": {"version", "platform"},
                "bf_file": {"id", "name", "pid", "is_link"},
                "bf_folder": {"id", "name", "pid", "is_recycle"},
                "bf_material_v2": {"file_id", "is_recycle", "w", "h", "thumb_tid", "image_tid"},
                "bf_material_userdata": {"file_id", "note", "score", "origin", "comments_summary", "comments_count", "comments_detail", "rotation", "hflip", "vflip"},
                "bf_tag_v2": {"id", "name"},
                "bf_tag_join_file": {"file_id", "tag_id"},
            }
            for table, columns in required.items():
                actual = {row[1] for row in connection.execute(f'PRAGMA table_info("{table}")')}
                if not columns <= actual:
                    raise ApiError("unsupported_schema", "The catalog is missing required schema fields.",
                                   {"table": table, "missing_columns": sorted(columns - actual)}, 409)
            library = connection.execute("SELECT version,platform FROM library LIMIT 1").fetchone()
            if library is None or library["version"] != 41:
                raise ApiError("unsupported_schema", "Only the frozen Billfish schema version 41 is supported.",
                               {"version": library["version"] if library else None}, 409)
            yield connection
        except sqlite3.Error as exc:
            raise ApiError("catalog_unavailable", "The catalog could not be read.", {"reason": str(exc)}, 503) from exc
        finally:
            if connection is not None:
                connection.close()

    def _folders(self, connection):
        return {row["id"]: dict(row) for row in connection.execute("SELECT * FROM bf_folder")}

    def _path(self, name, parent, folders):
        components = [] if name is None else [name]
        seen = set()
        recycled = False
        while parent != 0:
            if not isinstance(parent, int) or parent < 0:
                return {"path": None, "relative_path": None, "exists": False,
                        "path_error": {"code": "invalid_parent", "message": "Folder parent must be zero or a positive ID."}}, recycled
            if parent in seen or len(seen) >= 256:
                return {"path": None, "relative_path": None, "exists": False,
                        "path_error": {"code": "folder_cycle" if parent in seen else "folder_depth", "message": "Folder hierarchy is cyclic or exceeds 256 levels."}}, recycled
            seen.add(parent)
            folder = folders.get(parent)
            if folder is None:
                return {"path": None, "relative_path": None, "exists": False,
                        "path_error": {"code": "folder_not_found", "message": "A parent folder is missing.", "folder_id": str(parent)}}, recycled
            components.insert(0, folder["name"])
            recycled = recycled or bool(folder["is_recycle"])
            parent = folder["pid"]
        if not all(_safe_component(component) for component in components):
            return {"path": None, "relative_path": None, "exists": False,
                    "path_error": {"code": "unsafe_path", "message": "A file or folder name is not a safe path component."}}, recycled
        path = self.root.joinpath(*components)
        return {"path": str(path), "relative_path": "/".join(components), "exists": path.is_file() if name is not None else path.is_dir(), "path_error": None}, recycled

    def _items(self, connection, rows, folders):
        items = []
        for row in rows:
            item = _serial_row(row)
            thumbnail_active = bool(row["_native_thumb_tid"])
            preview_active = bool(row["_native_image_tid"])
            item.pop("_native_thumb_tid")
            item.pop("_native_image_tid")
            item_id = row["id"]
            path, parent_recycled = self._path(row["name"], row["pid"], folders)
            item.update(path)
            item["folder_id"] = str(row["pid"])
            item["is_recycle"] = bool(item.get("is_recycle")) or parent_recycled
            if item.get("is_link") or item["is_recycle"]:
                item.update(path=None, relative_path=None, exists=False, path_error={
                    "code": "linked_path_unresolved" if item.get("is_link") else "recycled_path_unresolved",
                    "message": "The actual linked or recycled disk location has not been established."})
            bucket = f"{item_id % 256:02x}"
            preview_root = self.root / ".bf/.preview" / bucket
            thumbnail, preview = preview_root / f"{item_id}.small.webp", preview_root / f"{item_id}.hd.webp"
            # Native analysis can leave old files after returning to source-image
            # fallback. Only the material's active cache type makes them usable.
            item["thumbnail_path"] = str(thumbnail) if thumbnail_active and thumbnail.is_file() else None
            item["preview_path"] = str(preview) if preview_active and preview.is_file() else None
            items.append(item)
        if items:
            placeholders = ",".join("?" for _ in items)
            identifiers = [int(item["id"]) for item in items]
            tags_by_file = {identifier: [] for identifier in identifiers}
            for source in ("bf_tag_v2",):
                for row in connection.execute(f"SELECT j.file_id,t.* FROM bf_tag_join_file j JOIN {source} t ON t.id=j.tag_id WHERE j.file_id IN ({placeholders}) ORDER BY t.id", identifiers):
                    tag = _serial_row(row)
                    tag.pop("file_id")
                    tag["source"] = source
                    tags_by_file[row["file_id"]].append(tag)
            for item in items:
                item["tags"] = tags_by_file[int(item["id"])]
                item["tag_source_status"] = "active"
                item["duration_ms"] = None
            for table in ("bf_material_video", "bf_material_audio"):
                columns = {row[1] for row in connection.execute(f'PRAGMA table_info("{table}")')}
                if {"file_id", "duration"} <= columns:
                    durations = dict(connection.execute(f"SELECT file_id,duration FROM {table} WHERE file_id IN ({placeholders})", identifiers))
                    for item in items:
                        duration = durations.get(int(item["id"]))
                        if item["duration_ms"] is None and isinstance(duration, int) and duration >= 0:
                            item["duration_ms"] = duration
        return items

    _SELECT = """SELECT f.*, m.w AS width, m.h AS height, m.is_recycle,
        m.thumb_tid AS _native_thumb_tid, m.image_tid AS _native_image_tid,
        u.note, u.score, u.origin, u.comments_summary, u.comments_count,
        u.comments_detail, u.rotation, u.hflip, u.vflip
        FROM bf_file f LEFT JOIN bf_material_v2 m ON m.file_id=f.id
        LEFT JOIN bf_material_userdata u ON u.file_id=f.id"""

    def info(self):
        with self._connection() as connection:
            library = connection.execute("SELECT version,platform FROM library LIMIT 1").fetchone()
            return {"library_root": str(self.root), "database_path": str(self.database),
                    "schema_version": library["version"], "platform": library["platform"],
                    "read_only": True, "file_count": connection.execute("SELECT COUNT(*) FROM bf_file WHERE id>0").fetchone()[0],
                    "folder_count": connection.execute("SELECT COUNT(*) FROM bf_folder WHERE id>0").fetchone()[0],
                    "tag_source_status": "active", "tag_sources": ["bf_tag_v2"],
                    "search_engine": "literal_like", "search_fields": ["name", "note", "origin", "tag"]}

    def search(self, query="", limit=100, offset=0, folder_id=None, tag_id=None, extensions=None, include_recycled=False):
        if not isinstance(query, str) or not isinstance(include_recycled, bool):
            raise ApiError("invalid_argument", "query must be a string and include_recycled must be boolean.")
        if isinstance(limit, bool) or not isinstance(limit, int) or not 1 <= limit <= 500:
            raise ApiError("invalid_argument", "limit must be between 1 and 500.")
        if isinstance(offset, bool) or not isinstance(offset, int) or not 0 <= offset <= 2**63 - 1:
            raise ApiError("invalid_argument", "offset must be a nonnegative signed 64-bit integer.")
        where, params = ["f.id>0"], []
        if not include_recycled:
            where += ["COALESCE(m.is_recycle,0)=0", "f.pid NOT IN (SELECT id FROM recycled)"]
        if folder_id is not None:
            where.append("f.pid=?")
            params.append(_id(folder_id, "folder_id", allow_zero=True))
        if tag_id is not None:
            where.append("EXISTS(SELECT 1 FROM bf_tag_join_file j WHERE j.file_id=f.id AND j.tag_id=?)")
            params.append(_id(tag_id, "tag_id"))
        if query:
            pattern = "%" + query.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_") + "%"
            where.append(r"""(f.name LIKE ? ESCAPE '\' OR u.note LIKE ? ESCAPE '\' OR u.origin LIKE ? ESCAPE '\' OR EXISTS(
                SELECT 1 FROM bf_tag_join_file j JOIN bf_tag_v2 t ON t.id=j.tag_id
                WHERE j.file_id=f.id AND t.name LIKE ? ESCAPE '\'))""")
            params += [pattern] * 4
        if extensions is not None:
            if not isinstance(extensions, (list, tuple)) or not extensions or len(extensions) > 100 or any(not isinstance(ext, str) or not ext.lstrip(".") or not ext.lstrip(".").isalnum() for ext in extensions):
                raise ApiError("invalid_argument", "extensions must contain 1 to 100 extension names, such as png or .mp4.")
            where.append("(" + " OR ".join("lower(f.name) LIKE ?" for _ in extensions) + ")")
            params += ["%." + ext.lstrip(".").lower() for ext in extensions]
        prefix = "WITH RECURSIVE recycled(id) AS (SELECT id FROM bf_folder WHERE is_recycle!=0 UNION SELECT f.id FROM bf_folder f JOIN recycled r ON f.pid=r.id) "
        sql = self._SELECT + " WHERE " + " AND ".join(where)
        with self._connection() as connection:
            total = connection.execute(prefix + "SELECT COUNT(*) FROM (" + sql + ")", params).fetchone()[0]
            rows = connection.execute(prefix + sql + " ORDER BY f.id LIMIT ? OFFSET ?", params + [limit, offset]).fetchall()
            return {"items": self._items(connection, rows, self._folders(connection)), "total": total, "limit": limit, "offset": offset}

    def get_item(self, item_id: int):
        item_id = _id(item_id, "item_id")
        with self._connection() as connection:
            row = connection.execute(self._SELECT + " WHERE f.id=?", (item_id,)).fetchone()
            if row is None:
                raise ApiError("item_not_found", "The item was not found.", {"item_id": str(item_id)}, 404)
            return self._items(connection, [row], self._folders(connection))[0]

    def animation_snapshot(self, scope_root):
        """Read indexed FBX/GLB members inside one model directory subtree."""
        if not isinstance(scope_root, str) or not scope_root or "\0" in scope_root or not Path(scope_root).is_absolute():
            raise ApiError("invalid_animation_scope", "An absolute model directory scope is required.")
        scope = Path(scope_root).absolute()
        try:
            components = scope.relative_to(self.root).parts
        except ValueError:
            raise ApiError("invalid_animation_scope", "The animation directory is outside the bound library.") from None
        if not all(_safe_component(component) for component in components):
            raise ApiError("invalid_animation_scope", "The animation directory has an unsafe path component.")
        if not scope.is_dir():
            raise ApiError("animation_scope_unavailable", "The model directory no longer exists.", status=404)
        subtree = """WITH RECURSIVE scope(id) AS (
            SELECT ?
            UNION SELECT f.id FROM bf_folder f JOIN scope s ON f.pid=s.id
                  WHERE f.id>0 AND COALESCE(f.is_recycle,0)=0
        ) """
        sql = subtree + """SELECT f.id,f.name,f.pid,f.is_link FROM bf_file f
        JOIN scope s ON f.pid=s.id
        WHERE f.id>0 AND (lower(f.name) LIKE '%.fbx' OR lower(f.name) LIKE '%.glb')
          AND NOT EXISTS (SELECT 1 FROM bf_material_v2 m
                          WHERE m.file_id=f.id AND COALESCE(m.is_recycle,0)!=0)
        ORDER BY f.id"""
        items = []
        with self._connection() as connection:
            # Resolve only the anchor's ancestors, then enumerate its subtree in SQL.
            # Do not hydrate all library files or probe sibling paths before filtering.
            anchor, folders = 0, {}
            for component in components:
                matches = connection.execute("""SELECT * FROM bf_folder
                    WHERE pid=? AND name=? COLLATE NOCASE AND id>0 AND COALESCE(is_recycle,0)=0""",
                    (anchor, component)).fetchall()
                if len(matches) != 1:
                    raise ApiError("animation_scope_unavailable", "The model directory is not one active indexed folder.",
                                   {"scope_root": str(scope)}, 404)
                folder = matches[0]
                anchor = folder["id"]
                folders[anchor] = dict(folder)
            for folder in connection.execute(subtree + "SELECT f.* FROM bf_folder f JOIN scope s ON f.id=s.id", (anchor,)):
                folders[folder["id"]] = dict(folder)
            for row in connection.execute(sql, (anchor,)):
                linked = bool(row["is_link"])
                if linked:
                    location = {"path": None, "relative_path": None, "exists": False,
                                "path_error": {"code": "linked_path_unresolved",
                                               "message": "The actual linked disk location has not been established."}}
                else:
                    location, _ = self._path(row["name"], row["pid"], folders)
                items.append({"id": str(row["id"]), "name": row["name"],
                              "folderId": str(row["pid"]) if row["pid"] is not None else None,
                              "path": location["path"], "relativePath": location["relative_path"],
                              "exists": location["exists"], "isLink": linked,
                              "pathError": location["path_error"]})
        semantic = {"schemaVersion": 2, "libraryRoot": str(self.root), "scopeRoot": str(scope),
                    "scopeMode": "model-directory-recursive", "items": items}
        encoded = json.dumps(semantic, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
        return {**semantic, "complete": True, "revision": hashlib.sha256(encoded).hexdigest()}

    def list_folders(self):
        with self._connection() as connection:
            folders = self._folders(connection)
            items = []
            for identifier, row in sorted(folders.items()):
                if identifier <= 0:
                    continue
                item = _serial_row(row)
                path, inherited_recycle = self._path(row["name"], row["pid"], folders)
                # Folder paths identify directories, not material files.
                path["exists"] = Path(path["path"]).is_dir() if path["path"] else False
                item.update(path)
                item["is_recycle"] = bool(row["is_recycle"]) or inherited_recycle
                if item["is_recycle"]:
                    item.update(path=None, relative_path=None, exists=False, path_error={
                        "code": "recycled_path_unresolved", "message": "The actual recycled disk location has not been established."})
                items.append(item)
            return {"items": items, "total": len(items)}

    def list_tags(self):
        with self._connection() as connection:
            items = []
            for source in ("bf_tag_v2",):
                for row in connection.execute(f"SELECT * FROM {source} ORDER BY id"):
                    items.append(dict(_serial_row(row), source=source))
            return {"items": items, "total": len(items), "active_source": "bf_tag_v2",
                    "source_status": "active", "sources": ["bf_tag_v2"]}
