"""Read-only, whole-request validation before the live native command boundary."""

from pathlib import Path

from .catalog import Catalog, _id
from .errors import ApiError
from .service import OPERATIONS, validate


WRITE_OPERATIONS = frozenset(("items.update", "tags.create", "tags.assign"))


def _unique_ids(values, name):
    identifiers = [str(_id(value, name)) for value in values]
    if len(set(identifiers)) != len(identifiers):
        raise ApiError("invalid_arguments", f"{name} contains duplicate IDs.")
    return identifiers


def _write_schema(connection):
    required = {
        "bf_file": {"id", "name", "pid", "is_link", "tid"},
        "bf_material_userdata": {"file_id", "note", "origin", "comments_summary", "comments_count",
                                 "comments_detail", "score", "rotation", "hflip", "vflip", "cover_tid"},
        "bf_type": {"tid", "name"},
        "bf_tag_v2": {"id", "name", "pid", "seq", "icon", "color", "born"},
        "bf_tag_join_file": {"file_id", "tag_id", "born"},
    }
    for table, fields in required.items():
        columns = {row[1] for row in connection.execute(f'PRAGMA table_info("{table}")')}
        if not fields <= columns:
            raise ApiError("unsupported_schema", "Required mutation fields are missing.",
                           {"table": table, "missing_columns": sorted(fields - columns)}, 409)


def _writable_items(catalog, connection, identifiers):
    folders = catalog._folders(connection)
    items = {}
    for identifier in identifiers:
        rows = connection.execute(catalog._SELECT + " WHERE f.id=?", (int(identifier),)).fetchall()
        if len(rows) != 1:
            if not rows:
                raise ApiError("item_not_found", "The item does not exist.", {"item_id": identifier}, 404)
            raise ApiError("unsupported_schema", "Duplicate metadata rows cannot be changed safely.", {"item_id": identifier}, 409)
        item = catalog._items(connection, rows, folders)[0]
        if item["is_link"] or item["is_recycle"] or item["path_error"] or not item["exists"]:
            raise ApiError("item_not_writable", "Only existing ordinary, active on-disk items can be changed.",
                           {"item_id": identifier, "path_error": item["path_error"]}, 409)
        items[identifier] = item
    return items


def _existing_tags(connection, identifiers):
    for identifier in identifiers:
        if connection.execute("SELECT id FROM bf_tag_v2 WHERE id=?", (int(identifier),)).fetchone() is None:
            raise ApiError("tag_not_found", "The active tag does not exist.", {"tag_id": identifier}, 404)


def preflight(operation, arguments, library_root):
    """Return a validated immutable intent; never invoke a mutation or load DLLs.

    The native adapter must recheck the active library immediately before dispatch.
    requested_ids are retained for readback even when arguments omit no-op updates.
    Tag pairs list only relationships whose state will actually change.
    """
    if not isinstance(operation, str) or operation not in WRITE_OPERATIONS:
        raise ApiError("unknown_operation", "Unknown preflight operation.", status=404)
    validate(arguments, OPERATIONS[operation]["inputSchema"])
    catalog = Catalog(Path(library_root))
    args = {key: value for key, value in arguments.items() if key != "dry_run"}
    result = {"operation": operation, "arguments": args, "requested_ids": [],
              "plan": [], "modified_count": 0, "library_root": str(catalog.root)}
    with catalog._connection() as connection:
        _write_schema(connection)
        if operation == "items.update":
            identifiers = _unique_ids([entry["item_id"] for entry in args["updates"]], "updates.item_id")
            items = _writable_items(catalog, connection, identifiers)
            changes = []
            for identifier, update in zip(identifiers, args["updates"]):
                fields = {key: value for key, value in update.items() if key != "item_id"}
                if not fields:
                    raise ApiError("invalid_arguments", "Each item update requires note and/or origin.")
                before = {key: items[identifier][key] for key in fields}
                changed = {key: value for key, value in fields.items() if before[key] != value}
                result["plan"].append({"item_id": identifier, "before": before, "changes": changed})
                if changed:
                    changes.append({"item_id": identifier, **changed})
            result.update(arguments={"updates": changes}, requested_ids=identifiers, modified_count=len(changes))
        elif operation == "tags.create":
            name = args["name"]
            if not name.strip():
                raise ApiError("invalid_arguments", "Tag name must contain non-whitespace text.")
            parent = str(_id(args.get("pid", "0"), "pid", allow_zero=True))
            if parent != "0":
                _existing_tags(connection, [parent])
            if connection.execute("SELECT id FROM bf_tag_v2 WHERE name=? AND pid=?", (name, int(parent))).fetchone():
                raise ApiError("tag_exists", "A tag with this name already exists under this parent.", status=409)
            sequence = connection.execute("SELECT COALESCE(MAX(seq),0)+1 FROM bf_tag_v2 WHERE pid=?", (int(parent),)).fetchone()[0]
            result.update(arguments={"name": name, "pid": parent},
                          plan={"name": name, "pid": parent, "seq": sequence, "icon": 0, "color": 0, "born": 0},
                          modified_count=1)
        else:
            identifiers = _unique_ids(args["item_ids"], "item_ids")
            tags = _unique_ids(args["tag_ids"], "tag_ids")
            if len(identifiers) * len(tags) > 500:
                raise ApiError("invalid_arguments", "A tag batch may contain at most 500 item/tag pairs.")
            _writable_items(catalog, connection, identifiers)
            _existing_tags(connection, tags)
            mode = args.get("mode", "add")
            pairs = []
            for identifier in identifiers:
                for tag in tags:
                    present = connection.execute("SELECT file_id FROM bf_tag_join_file WHERE file_id=? AND tag_id=?", (int(identifier), int(tag))).fetchone() is not None
                    if present != (mode == "add"):
                        pairs.append({"item_id": identifier, "tag_id": tag, "action": mode})
            result.update(arguments={"item_ids": identifiers, "tag_ids": tags, "mode": mode},
                          requested_ids=identifiers, pairs=pairs, plan=pairs, modified_count=len(pairs))
    return result


def verify(operation, arguments, library_root):
    """Check only requested postconditions using a new read-only snapshot.

    Native success may be observed before a fresh read confirms the requested state.
    Callers may repeat this read while retryable is true, but must never resubmit a write.
    """
    if not isinstance(operation, str) or operation not in WRITE_OPERATIONS:
        raise ApiError("unknown_operation", "Unknown verification operation.", status=404)
    validate(arguments, OPERATIONS[operation]["inputSchema"])
    catalog = Catalog(Path(library_root))
    mismatches = []
    result = {"verified": False, "mismatches": mismatches, "retryable": True}
    with catalog._connection() as connection:
        _write_schema(connection)
        if operation == "items.update":
            _unique_ids([update["item_id"] for update in arguments["updates"]], "updates.item_id")
            for update in arguments["updates"]:
                identifier = update["item_id"]
                fields = {key: value for key, value in update.items() if key != "item_id"}
                if not fields:
                    raise ApiError("invalid_arguments", "Each item update requires note and/or origin.")
                rows = connection.execute("""SELECT f.id,u.note,u.origin FROM bf_file f
                    LEFT JOIN bf_material_userdata u ON u.file_id=f.id WHERE f.id=?""", (int(identifier),)).fetchall()
                if len(rows) != 1:
                    mismatches.append({"item_id": identifier, "expected": "one_existing_item", "actual": len(rows),
                                       "reason": "missing_item" if not rows else "duplicate_metadata"})
                    result["retryable"] = False
                    continue
                for field, expected in fields.items():
                    actual = rows[0][field]
                    if expected != actual:
                        mismatches.append({"item_id": identifier, "field": field, "expected": expected, "actual": actual})
        elif operation == "tags.assign":
            identifiers = _unique_ids(arguments["item_ids"], "item_ids")
            tags = _unique_ids(arguments["tag_ids"], "tag_ids")
            if len(identifiers) * len(tags) > 500:
                raise ApiError("invalid_arguments", "A tag batch may contain at most 500 item/tag pairs.")
            expected = arguments.get("mode", "add") == "add"
            for identifier in identifiers:
                for tag in tags:
                    actual = connection.execute("SELECT 1 FROM bf_tag_join_file WHERE file_id=? AND tag_id=? LIMIT 1", (int(identifier), int(tag))).fetchone() is not None
                    if expected != actual:
                        mismatches.append({"item_id": identifier, "tag_id": tag, "expected": expected, "actual": actual})
        else:
            name = arguments["name"]
            if not name.strip():
                raise ApiError("invalid_arguments", "Tag name must contain non-whitespace text.")
            parent = str(_id(arguments.get("pid", "0"), "pid", allow_zero=True))
            rows = connection.execute("SELECT * FROM bf_tag_v2 WHERE name=? AND pid=? ORDER BY id", (name, int(parent))).fetchall()
            if len(rows) != 1:
                mismatches.append({"name": name, "pid": parent, "expected": "one_matching_tag", "actual": len(rows),
                                   "reason": "tag_not_committed" if not rows else "duplicate_tag",
                                   "tag_ids": [str(row["id"]) for row in rows]})
                if len(rows) > 1:
                    result["retryable"] = False
            else:
                from .catalog import _serial_row
                result["tag"] = _serial_row(rows[0])
    result["verified"] = not mismatches
    return result
