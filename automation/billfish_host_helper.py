"""Internal one-request JSON pipe for the in-process Billfish native adapter."""

import json
import sys
from pathlib import Path

# -I excludes the script directory. Add only this installed helper directory.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from billfish_api.catalog import Catalog
from billfish_api.errors import ApiError
from billfish_api.preflight import preflight, verify
from billfish_api.service import ID, OPERATIONS, object_schema, validate


READ_OPERATIONS = frozenset(("library.info", "items.search", "items.get", "folders.list", "tags.list"))
MAX_REQUEST_BYTES = 8 * 1024 * 1024


def hydrate_selection(operation, arguments, library_root):
    """Hydrate IDs supplied by the native view, never infer the selection from SQL."""
    if operation != "items.selected":
        raise ApiError("unknown_operation", "Unknown selection hydration operation.", status=404)
    validate(arguments, object_schema({"item_ids": {"type": "array", "items": ID}}, ["item_ids"]))
    identifiers = arguments["item_ids"]
    if len(set(identifiers)) != len(identifiers):
        raise ApiError("invalid_arguments", "item_ids contains duplicate IDs.")
    catalog = Catalog(library_root)
    items = []
    with catalog._connection() as connection:
        folders = catalog._folders(connection)
        # Keep one snapshot while bounding the Catalog tag lookup parameter count.
        for start in range(0, len(identifiers), 500):
            rows = []
            for identifier in identifiers[start:start + 500]:
                matches = connection.execute(catalog._SELECT + " WHERE f.id=?", (int(identifier),)).fetchall()
                if not matches:
                    raise ApiError("item_not_found", "A selected item no longer exists in the bound library.", {"item_id": identifier}, 404)
                if len(matches) != 1:
                    raise ApiError("unsupported_schema", "A selected item has duplicate metadata rows.", {"item_id": identifier}, 409)
                rows.append(matches[0])
            items.extend(catalog._items(connection, rows, folders))
    return {"items": items, "total": len(items), "library_root": str(catalog.root)}


def handle_request(request):
    if (not isinstance(request, dict) or set(request) != {"operation", "arguments", "library_root", "mode"}
            or not isinstance(request["library_root"], str) or not request["library_root"] or "\0" in request["library_root"]):
        raise ApiError("invalid_arguments", "Expected operation, arguments, library_root and mode.")
    operation = request["operation"]
    args = request["arguments"]
    if request["mode"] == "animation_catalog":
        if operation != "animation.catalog_snapshot":
            raise ApiError("unknown_operation", "Unknown animation catalog operation.", status=404)
        validate(args, object_schema({"scope_root": {"type": "string", "minLength": 1}}, ["scope_root"]))
        if not Path(request["library_root"]).is_absolute():
            raise ApiError("invalid_arguments", "Animation catalog requires an absolute bound library root.")
        return Catalog(request["library_root"]).animation_snapshot(args["scope_root"])
    if request["mode"] in ("preview_items", "preview_prepare", "preview_publish", "preview_verify", "preview_types", "animation_visibility"):
        from billfish_api import previews
        return getattr(previews, request["mode"])(operation, args, request["library_root"])
    if request["mode"] in ("import_validate", "import_preflight", "import_verify", "browser_destination"):
        from billfish_api import imports
        return getattr(imports, request["mode"])(operation, args, request["library_root"])
    if request["mode"] in ("replace_preflight", "replace_commit", "replace_verify"):
        from billfish_api import replacements
        return getattr(replacements, request["mode"])(operation, args, request["library_root"])
    if request["mode"] == "selection":
        return hydrate_selection(operation, args, request["library_root"])
    if request["mode"] == "preflight":
        return preflight(operation, args, request["library_root"])
    if request["mode"] == "verify":
        return verify(operation, args, request["library_root"])
    if request["mode"] != "read" or not isinstance(operation, str) or operation not in READ_OPERATIONS:
        raise ApiError("unknown_operation", "Unknown helper mode or operation.", status=404)
    validate(args, OPERATIONS[operation]["inputSchema"])
    catalog = Catalog(request["library_root"])
    if operation == "library.info":
        return catalog.info()
    if operation == "items.search":
        return catalog.search(**args)
    if operation == "items.get":
        return catalog.get_item(**args)
    if operation == "folders.list":
        return catalog.list_folders()
    return catalog.list_tags()


def main():
    try:
        raw = sys.stdin.buffer.read(MAX_REQUEST_BYTES + 1)
        if len(raw) > MAX_REQUEST_BYTES:
            raise ApiError("request_too_large", "The helper request exceeds 8 MiB.", status=413)
        try:
            request = json.loads(raw.decode("utf-8"))
        except (UnicodeError, ValueError):
            raise ApiError("invalid_json", "The helper requires a single UTF-8 JSON request.") from None
        response = {"ok": True, "data": handle_request(request)}
    except ApiError as exc:
        response = {"ok": False, "error": exc.to_dict()}
    except Exception:
        response = {"ok": False, "error": {"code": "helper_failed", "message": "The internal helper could not complete the request."}}
    sys.stdout.buffer.write((json.dumps(response, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8"))
    sys.stdout.buffer.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
