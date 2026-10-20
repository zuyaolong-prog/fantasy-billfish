"""Read-only validation around imports owned by the live native Billfish host."""

import json
import ntpath
from pathlib import Path
import re
from urllib.parse import urlsplit

from .catalog import Catalog, _id
from .errors import ApiError
from .service import ID, object_schema, validate


MAX_FOLDER_ID = 2**31 - 1
DEFAULT_BROWSER_FOLDER = "浏览器采集"
TEXT = {"type": "string", "maxLength": 8192}
NAME = {"type": "string", "minLength": 1, "maxLength": 255}
FOLDER_ID = {"type": "string", "pattern": "^(0|[1-9][0-9]*)$", "maxLength": 19}
COMMON = {"name": NAME, "origin": TEXT, "folder_id": FOLDER_ID}


def _name(value):
    if (not isinstance(value, str) or not value or value in (".", "..") or ntpath.basename(value) != value
            or any(character in '<>:"/\\|?*' or ord(character) < 32 for character in value)
            or value.endswith((" ", ".")) or len(value.encode("utf-16-le")) > 510
            or re.fullmatch(r"(?i)(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", value)):
        raise ApiError("invalid_arguments", "name must be a valid ordinary Windows file basename, including its extension.")
    return value


def _folder_id(value):
    identifier = _id(value, "folder_id", allow_zero=True)
    if identifier > MAX_FOLDER_ID:
        raise ApiError("invalid_arguments", "folder_id exceeds the native importer's signed 32-bit range.")
    return identifier


def _active_folder(catalog, connection, identifier):
    if identifier == 0:
        return {"id": 0, "name": "", "path": str(catalog.root)}
    folders = catalog._folders(connection)
    row = folders.get(identifier)
    if row is None:
        raise ApiError("folder_not_found", "The import folder does not exist.", {"folder_id": str(identifier)}, 404)
    path, recycled = catalog._path(row["name"], row["pid"], folders)
    if row["is_recycle"] or recycled or path["path_error"] or not path["path"] or not Path(path["path"]).is_dir():
        raise ApiError("folder_not_writable", "The import folder must be active and present on disk.", {"folder_id": str(identifier)}, 409)
    return {"id": identifier, "name": row["name"], "path": path["path"]}


def _url(value):
    try:
        parts = urlsplit(value)
        valid = (parts.scheme.lower() in ("http", "https") and parts.hostname
                 and parts.username is None and parts.password is None
                 and not any(ord(character) <= 32 for character in value))
        parts.port
    except (ValueError, TypeError):
        valid = False
    if not valid:
        raise ApiError("invalid_arguments", "url must be an absolute HTTP or HTTPS URL without embedded credentials.")
    return value


def import_validate(operation, arguments, library_root):
    if operation != "items.import_url":
        raise ApiError("unknown_operation", "Unknown import URL validation operation.", status=404)
    validate(arguments, object_schema({"url": {**TEXT, "minLength": 1}, **COMMON}, ["url"]))
    catalog = Catalog(library_root)
    url = _url(arguments["url"])
    folder = _folder_id(arguments.get("folder_id", "0"))
    result = {"library_root": str(catalog.root), "url": url, "target_folder_id": folder,
              "origin": arguments.get("origin", url)}
    if "name" in arguments:
        result["name"] = _name(arguments["name"])
    with catalog._connection() as connection:
        _active_folder(catalog, connection, folder)
    return result


def import_preflight(operation, arguments, library_root):
    if operation != "items.import_path":
        raise ApiError("unknown_operation", "Unknown import preflight operation.", status=404)
    validate(arguments, object_schema({"source_path": {"type": "string", "minLength": 1, "maxLength": 32767}, **COMMON}, ["source_path"]))
    catalog = Catalog(library_root)
    source = Path(arguments["source_path"]).absolute()
    if not source.is_file():
        raise ApiError("source_not_file", "source_path must identify an existing ordinary file.", {"path": str(source)}, 404)
    name = _name(arguments.get("name", source.name))
    source_suffix = source.suffix
    if not source_suffix:
        raise ApiError("invalid_arguments", "The native importer requires an actual source filename extension.")
    supplied_suffix = Path(name).suffix
    if supplied_suffix:
        if supplied_suffix.casefold() != source_suffix.casefold():
            raise ApiError("invalid_arguments", "name extension must match the actual source file extension.")
        native_name = name[:-len(supplied_suffix)]
    else:
        native_name = name
    _name(native_name)
    expected_name = _name(native_name + source_suffix)
    folder = _folder_id(arguments.get("folder_id", "0"))
    with catalog._connection() as connection:
        _active_folder(catalog, connection, folder)
    return {"library_root": str(catalog.root), "path": str(source), "target_folder_id": folder,
            "name": native_name, "expected_name": expected_name, "origin": arguments.get("origin", ""), "extension": source_suffix.lower(),
            "source_bytes": source.stat().st_size}


def import_verify(operation, arguments, library_root):
    if operation != "items.import_path":
        raise ApiError("unknown_operation", "Unknown import verification operation.", status=404)
    validate(arguments, object_schema({"item_ids": {"type": "array", "minItems": 1, "maxItems": 500, "items": ID},
             "path": {"type": "string", "maxLength": 32767}, "extension": {"type": "string", "maxLength": 255},
             "expected_name": NAME, "origin": TEXT, "folder_id": FOLDER_ID}, ["item_ids"]))
    identifiers = list(dict.fromkeys(arguments["item_ids"]))
    expected_name = _name(arguments["expected_name"]) if "expected_name" in arguments else None
    folder = _folder_id(arguments["folder_id"]) if "folder_id" in arguments else None
    extension = arguments.get("extension")
    if extension is not None:
        if extension and (not extension.startswith(".") or any(character in '/\\\0' for character in extension)):
            raise ApiError("invalid_arguments", "extension must be empty or a suffix beginning with a dot.")
        extension = extension.lower()
    catalog = Catalog(library_root)
    mismatches, items = [], []
    retryable = True
    with catalog._connection() as connection:
        folders = catalog._folders(connection)
        for identifier in identifiers:
            rows = connection.execute(catalog._SELECT + " WHERE f.id=?", (int(identifier),)).fetchall()
            if not rows:
                mismatches.append({"item_id": identifier, "field": "item", "expected": "native_result_persisted", "actual": None})
                continue
            if len(rows) > 1:
                mismatches.append({"item_id": identifier, "field": "item", "expected": "unique_metadata", "actual": len(rows)})
                retryable = False
                continue
            item = catalog._items(connection, rows, folders)[0]
            items.append(item)
            owned = (bool(item.get("path")) and Path(item["path"]).is_relative_to(catalog.root)
                     and item["exists"] and not item["is_link"] and not item["is_recycle"] and not item["path_error"])
            if not owned:
                mismatches.append({"item_id": identifier, "field": "owned_path", "expected": "existing_library_file", "actual": item.get("path"), "path_error": item["path_error"]})
            checks = {}
            if expected_name is not None:
                checks["name"] = (expected_name, item["name"])
            if extension is not None:
                checks["extension"] = (extension, Path(item["name"]).suffix.lower())
            if "origin" in arguments:
                actual_origin = item.get("origin")
                # Native imports without a source URL persist SQL NULL. That
                # is the same absent-origin value as the public empty string.
                checks["origin"] = (arguments["origin"], "" if actual_origin is None else actual_origin)
            if folder is not None:
                checks["folder_id"] = (str(folder), item["folder_id"])
            for field, (expected, actual) in checks.items():
                if expected != actual:
                    mismatches.append({"item_id": identifier, "field": field, "expected": expected, "actual": actual})
            metadata = ((isinstance(item.get("width"), (int, float)) and item["width"] > 0
                         and isinstance(item.get("height"), (int, float)) and item["height"] > 0)
                        or (isinstance(item.get("duration_ms"), (int, float)) and item["duration_ms"] > 0))
            thumbnail = bool(item.get("thumbnail_path"))
            item.update(metadata_ready=bool(metadata), thumbnail_ready=thumbnail,
                        analysis_ready=bool(owned and metadata and thumbnail))
    result = {"verified": not mismatches, "retryable": retryable, "mismatches": mismatches,
              "item_ids": identifiers, "items": items, "library_root": str(catalog.root),
              "analysis_ready": bool(items) and len(items) == len(identifiers) and all(item["analysis_ready"] for item in items)}
    if len(identifiers) == 1:
        result["item_id"] = identifiers[0]
        if items:
            result["item"] = items[0]
    return result


def browser_destination(operation, arguments, library_root):
    if operation != "browser.destination":
        raise ApiError("unknown_operation", "Unknown browser destination operation.", status=404)
    validate(arguments, object_schema())
    catalog = Catalog(library_root)
    config_path = catalog.root / ".bf/.ui_config/lib_info.json"
    configured, config_status = {}, "missing"
    try:
        configured = json.loads(config_path.read_text(encoding="utf-8-sig"))
        if not isinstance(configured, dict):
            configured = {}
            config_status = "invalid"
        else:
            config_status = "read"
    except FileNotFoundError:
        pass
    except (OSError, UnicodeError, ValueError):
        config_status = "invalid"
    result = {"library_root": str(catalog.root), "config_path": str(config_path), "config_status": config_status,
              "target_folder_id": None, "name": DEFAULT_BROWSER_FOLDER, "candidate_ids": [], "needs_binding": True}
    with catalog._connection() as connection:
        try:
            configured_id = _folder_id(configured.get("downloadFolderId", "0"))
            if configured_id:
                active = _active_folder(catalog, connection, configured_id)
                result.update(status="configured", target_folder_id=str(configured_id), name=active["name"],
                              needs_binding=configured.get("downloadFolderName") != active["name"])
                return result
        except ApiError:
            pass
        candidates = []
        for row in connection.execute("SELECT id FROM bf_folder WHERE pid=0 AND name=? ORDER BY id", (DEFAULT_BROWSER_FOLDER,)):
            try:
                identifier = _folder_id(row["id"])
                _active_folder(catalog, connection, identifier)
                candidates.append(identifier)
            except ApiError:
                continue
        result["candidate_ids"] = [str(identifier) for identifier in candidates]
        if len(candidates) == 1:
            result.update(status="candidate", target_folder_id=str(candidates[0]))
        elif candidates:
            result.update(status="ambiguous")
        else:
            result.update(status="needs_create")
    return result
