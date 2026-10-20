"""Shared operations and machine-readable contracts for every API transport.

The catalog is the installed Billfish database. No prototype schema is created
or migrated here. Transports do not implement business behavior.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
from typing import Any

from . import __version__
from .catalog import Catalog
from .discovery import resolve_library
from .errors import ApiError


ID = {"type": "string", "pattern": "^[1-9][0-9]*$", "maxLength": 19,
      "description": "Positive signed 64-bit Billfish ID, encoded as a decimal string."}
HTTP_URL = {"type": "string", "minLength": 1, "maxLength": 32767,
            "pattern": r"^[hH][tT][tT][pP][sS]?://[^\s]+$"}
IMPORT_ORIGIN = {"type": "string", "maxLength": 8192,
                 "pattern": r"^(?:[hH][tT][tT][pP][sS]?://[^\s]+)?$"}
IMPORT_FOLDER_ID = {"type": "string", "maxLength": 10, "default": "0",
                    "description": "Decimal folder ID from 0 through 2147483647. Defaults to the library root for imports.",
                    "pattern": (r"^(?:0|[1-9][0-9]{0,8}|1[0-9]{9}|20[0-9]{8}|21[0-3][0-9]{7}|"
                                r"214[0-6][0-9]{6}|2147[0-3][0-9]{5}|21474[0-7][0-9]{4}|"
                                r"214748[0-2][0-9]{3}|2147483[0-5][0-9]{2}|21474836[0-3][0-9]|214748364[0-7])$")}


def object_schema(properties=None, required=()):
    return {"type": "object", "properties": properties or {},
            "required": list(required), "additionalProperties": False}


OPERATIONS = {
    "library.info": {"description": "Inspect the existing library without creating or migrating it.",
                     "inputSchema": object_schema(), "effect": "read"},
    "items.search": {"description": "Search literal text in the current catalog; results are ordered by ID.",
                     "inputSchema": object_schema({
                         "query": {"type": "string", "maxLength": 4096, "default": ""},
                         "limit": {"type": "integer", "minimum": 1, "maximum": 500, "default": 100},
                         "offset": {"type": "integer", "minimum": 0, "default": 0},
                         "folder_id": {"type": "string", "pattern": "^(0|[1-9][0-9]*)$", "maxLength": 19},
                         "tag_id": ID,
                         "extensions": {"type": "array", "maxItems": 100,
                                        "items": {"type": "string", "maxLength": 32}},
                         "include_recycled": {"type": "boolean", "default": False}}), "effect": "read"},
    "items.get": {"description": "Read item metadata and its ordinary on-disk path.",
                  "inputSchema": object_schema({"item_id": ID}, ["item_id"]), "effect": "read"},
    "items.selected": {"description": "Read actual items selected in the current Billfish view. Requires an open GUI library; accepts no caller-supplied item IDs.",
                       "inputSchema": object_schema(), "effect": "read", "requires": ["running-host"]},
    "items.replace_file": {"description": "Replace an owned PNG/JPG/JPEG/WebP item's content from an absolute local file of the same extension. Preserve its ID and metadata. Wait for native_replace_persisted with completed analysis before deleting the source. Defaults to dry-run; requires the running GUI library.",
                           "inputSchema": object_schema({"item_id": ID,
                               "source_path": {"type": "string", "minLength": 1, "maxLength": 32767,
                                               "pattern": r"^(?:[A-Za-z]:[\\/]|\\\\|/).+"},
                               "dry_run": {"type": "boolean", "default": True}}, ["item_id", "source_path"]),
                           "effect": "replace-file", "defaultDryRun": True, "requires": ["running-host"]},
    "items.import_path": {"description": "Import an absolute local file through the running native business layer. Wait for native_import_persisted and real item IDs; analysis_ready reports whether analysis is complete. Defaults to the library root when no folder is supplied.",
                          "inputSchema": object_schema({
                              "source_path": {"type": "string", "minLength": 1, "maxLength": 32767,
                                              "pattern": r"^(?:[A-Za-z]:[\\/]|\\\\|/).+"},
                              "name": {"type": "string", "maxLength": 32767},
                              "origin": IMPORT_ORIGIN, "folder_id": IMPORT_FOLDER_ID}, ["source_path"]),
                          "effect": "import-file", "requires": ["running-host"]},
    "items.import_url": {"description": "Download an HTTP(S) URL and import it through the running native business layer. Wait for native_import_persisted and real item IDs; analysis_ready reports whether analysis is complete. Defaults to the library root when no folder is supplied.",
                         "inputSchema": object_schema({"url": HTTP_URL,
                             "name": {"type": "string", "maxLength": 32767},
                             "origin": IMPORT_ORIGIN, "folder_id": IMPORT_FOLDER_ID}, ["url"]),
                         "effect": "download-and-import", "requires": ["running-host"]},
    "video.capture": {"description": "Accept a browser video capture and return accepted plus job_id. Download starts only after the user chooses a resolution in the Billfish UI. Query jobs.get for completion; the real configured browser capture folder is required.",
                      "inputSchema": object_schema({"url": HTTP_URL, "page_url": HTTP_URL,
                          "title": {"type": "string", "maxLength": 32767}, "media_url": HTTP_URL}, ["url"]),
                      "effect": "show-video-capture", "requires": ["running-host"]},
    "folders.list": {"description": "List the catalog folder hierarchy.",
                     "inputSchema": object_schema(), "effect": "read"},
    "tags.list": {"description": "List native tag records and their schema source.",
                  "inputSchema": object_schema(), "effect": "read"},
    "items.export": {"description": "Copy selected ordinary files to a new destination tree. Never overwrite or move originals.",
                     "inputSchema": object_schema({
                         "item_ids": {"type": "array", "minItems": 1, "maxItems": 500, "items": ID},
                         "destination": {"type": "string", "minLength": 1, "maxLength": 32767},
                         "dry_run": {"type": "boolean", "default": True}}, ["item_ids", "destination"]),
                     "effect": "copy-files", "defaultDryRun": True},
    "items.update": {"description": "Patch note and/or origin through the running Billfish business layer, or through offline maintenance when the GUI is closed.",
                     "inputSchema": object_schema({
                         "updates": {"type": "array", "minItems": 1, "maxItems": 500,
                                     "items": object_schema({"item_id": ID,
                                         "note": {"type": "string", "maxLength": 100000},
                                         "origin": {"type": "string", "maxLength": 8192}}, ["item_id"])},
                         "dry_run": {"type": "boolean", "default": True}}, ["updates"]),
                     "effect": "write-metadata", "defaultDryRun": True, "requires": ["frozen-runtime"]},
    "tags.create": {"description": "Create a native v41 tag through the running business layer or offline maintenance.",
                    "inputSchema": object_schema({"name": {"type": "string", "minLength": 1, "maxLength": 256},
                        "pid": {"type": "string", "pattern": "^(0|[1-9][0-9]*)$", "maxLength": 19, "default": "0"},
                        "dry_run": {"type": "boolean", "default": True}}, ["name"]),
                    "effect": "write-metadata", "defaultDryRun": True, "requires": ["frozen-runtime"]},
    "tags.assign": {"description": "Add or remove native tag assignments for selected items without changing media files.",
                    "inputSchema": object_schema({
                        "item_ids": {"type": "array", "minItems": 1, "maxItems": 500, "items": ID},
                        "tag_ids": {"type": "array", "minItems": 1, "maxItems": 100, "items": ID},
                        "mode": {"type": "string", "enum": ["add", "remove"], "default": "add"},
                        "dry_run": {"type": "boolean", "default": True}}, ["item_ids", "tag_ids"]),
                    "effect": "write-metadata", "defaultDryRun": True, "requires": ["frozen-runtime"]},
    "host.info": {"description": "Inspect the running host, its current library and native bridge readiness.",
                  "inputSchema": object_schema(), "effect": "read", "requires": ["running-host"]},
    "jobs.list": {"description": "List recent online tasks and their actual completion states.",
                  "inputSchema": object_schema(), "effect": "read", "requires": ["running-host"]},
    "jobs.get": {"description": "Read an online task after a call timeout without repeating its write.",
                 "inputSchema": object_schema({"job_id": {"type": "string", "minLength": 1, "maxLength": 128}}, ["job_id"]),
                 "effect": "read", "requires": ["running-host"]},
    "plugins.list": {"description": "List installed local plugins, enabled preferences, process states and command schemas.",
                     "inputSchema": object_schema(), "effect": "read", "requires": ["running-host"]},
    "plugins.install": {"description": "Install a local .eagleplugin or .zip package, or a development directory, without executing its entry. Wait for the host's final installation result; progress remains available through jobs. New plugins are disabled.",
                        "inputSchema": object_schema({"source_path": {"type": "string", "minLength": 1, "maxLength": 32767}}, ["source_path"]),
                        "effect": "install-local-plugin", "requires": ["running-host"]},
    "plugins.enable": {"description": "Persist the local plugin switch and start or stop its isolated process.",
                       "inputSchema": object_schema({"id": {"type": "string", "minLength": 1, "maxLength": 128}, "enabled": {"type": "boolean"}}, ["id", "enabled"]),
                       "effect": "plugin-lifecycle", "requires": ["running-host"]},
    "plugins.run": {"description": "Run an enabled plugin command using its declared host SDK permissions.",
                    "inputSchema": object_schema({"plugin_id": {"type": "string", "minLength": 1, "maxLength": 128},
                        "command": {"type": "string", "minLength": 1, "maxLength": 128},
                        "arguments": {"type": "object", "additionalProperties": True}}, ["plugin_id", "command", "arguments"]),
                    "effect": "plugin-command", "requires": ["running-host"]},
    "plugins.preview": {"description": "Open an enabled format-extension plugin preview for a real item in the running library. Optional plugin_id selects an eligible plugin. An accepted/job_id receipt is not completion; query jobs.get for the verified host result.",
                        "inputSchema": object_schema({"item_id": ID,
                            "plugin_id": {"type": "string", "minLength": 1, "maxLength": 128}}, ["item_id"]),
                        "effect": "show-plugin-preview", "requires": ["running-host"]},
    "plugins.thumbnail": {"description": "Generate and apply an eligible format-extension plugin thumbnail to a real item in the running library. Preserve an existing user cover. Optional plugin_id selects an eligible plugin. Query jobs.get after accepted/job_id; success requires verified native cover completion.",
                          "inputSchema": object_schema({"item_id": ID,
                              "plugin_id": {"type": "string", "minLength": 1, "maxLength": 128}}, ["item_id"]),
                          "effect": "write-native-cover", "requires": ["running-host"]},
    "ui.settings.open": {"description": "Open the plugin page inside Billfish preferences.",
                         "inputSchema": object_schema(), "effect": "show-ui", "requires": ["running-host"]},
}

UI_TOKEN = {"type": "string", "minLength": 1, "maxLength": 128}
UI_COORD = {"type": "integer", "minimum": 0, "maximum": 100000}
UI_POINT = object_schema({"x": UI_COORD, "y": UI_COORD}, ["x", "y"])
UI_MODIFIERS = {"type": "array", "maxItems": 3,
                "items": {"type": "string", "enum": ["Ctrl", "Shift", "Alt"]}}

OPERATIONS["ui.snapshot"] = {
    "description": "Read Billfish-owned widget, item and action references. References are bound to the returned snapshot_id. Read latest_action after queued input; accepted is not business completion.",
    "inputSchema": object_schema({
        "max_nodes": {"type": "integer", "minimum": 1, "maximum": 4000, "default": 1000},
        "max_items": {"type": "integer", "minimum": 1, "maximum": 500, "default": 100},
        "include_hidden": {"type": "boolean", "default": False}}),
    "effect": "read", "requires": ["running-host"]}

for _action, _properties, _required, _description in [
    ("click", {"x": UI_COORD, "y": UI_COORD,
               "button": {"type": "string", "enum": ["left", "right", "middle"], "default": "left"},
               "count": {"type": "integer", "enum": [1, 2], "default": 1}, "modifiers": UI_MODIFIERS}, [],
     "Queue a mouse click inside the referenced Billfish widget/item. Optional x/y are local coordinates."),
    ("type", {"text": {"type": "string", "maxLength": 65536},
              "replace": {"type": "boolean", "default": True}}, ["text"],
     "Queue text into the referenced Billfish editor. replace defaults to true."),
    ("key", {"key": {"type": "string", "minLength": 1, "maxLength": 128}}, ["key"],
     "Queue a Qt key sequence such as Enter, Escape or Ctrl+A to the referenced Billfish widget."),
    ("scroll", {"delta_x": {"type": "integer", "minimum": -12000, "maximum": 12000},
                "delta_y": {"type": "integer", "minimum": -12000, "maximum": 12000},
                "x": UI_COORD, "y": UI_COORD}, ["delta_x", "delta_y"],
     "Queue wheel input inside the referenced Billfish widget using Qt angle deltas; 120 is one notch."),
    ("drag", {"from": UI_POINT, "to": UI_POINT,
              "steps": {"type": "integer", "minimum": 2, "maximum": 64, "default": 12},
              "modifiers": UI_MODIFIERS}, ["from", "to"],
     "Queue a local mouse drag within the referenced Billfish widget. Does not implement OS file drag/drop."),
    ("capture", {}, [],
     "Capture only the referenced Billfish QWidget as PNG base64 with width/height; never captures the desktop."),
    ("invoke", {}, [],
     "Queue the QAction identified by an action reference from the current Billfish snapshot."),
]:
    OPERATIONS["ui." + _action] = {
        "description": _description + " Requires snapshot_id/ref from ui.snapshot. Input receipts accepted/queued do not mean the business operation completed.",
        "inputSchema": object_schema({"snapshot_id": UI_TOKEN, "ref": UI_TOKEN, **_properties},
                                     ["snapshot_id", "ref", *_required]),
        "effect": "read" if _action == "capture" else "ui-input", "requires": ["running-host"]}


def validate(value: Any, schema: dict, path="arguments"):
    """Validate the small JSON Schema vocabulary used by our public contracts."""
    import re
    kind = schema.get("type")
    matches = {"object": lambda: isinstance(value, dict),
               "array": lambda: isinstance(value, list),
               "string": lambda: isinstance(value, str),
               "integer": lambda: isinstance(value, int) and not isinstance(value, bool),
               "boolean": lambda: isinstance(value, bool)}
    if kind in matches and not matches[kind]():
        raise ApiError("invalid_arguments", f"{path} must be {kind}.")
    if kind == "object":
        props = schema.get("properties", {})
        missing = set(schema.get("required", [])) - value.keys()
        unknown = value.keys() - props.keys()
        if missing:
            raise ApiError("invalid_arguments", f"{path} is missing: {', '.join(sorted(missing))}.")
        if unknown and schema.get("additionalProperties") is False:
            raise ApiError("invalid_arguments", f"{path} has unknown fields: {', '.join(sorted(unknown))}.")
        for key, child in value.items():
            if key in props:
                validate(child, props[key], f"{path}.{key}")
    elif kind == "array":
        if len(value) < schema.get("minItems", 0) or len(value) > schema.get("maxItems", 1000000):
            raise ApiError("invalid_arguments", f"{path} has an invalid number of items.")
        for index, child in enumerate(value):
            validate(child, schema.get("items", {}), f"{path}[{index}]")
    elif kind == "string":
        if len(value) < schema.get("minLength", 0) or len(value) > schema.get("maxLength", 1000000):
            raise ApiError("invalid_arguments", f"{path} has an invalid length.")
        if "pattern" in schema and re.fullmatch(schema["pattern"], value) is None:
            raise ApiError("invalid_arguments", f"{path} has an invalid format.")
        if "\x00" in value:
            raise ApiError("invalid_arguments", f"{path} contains a NUL character.")
        if schema.get("maxLength") == 19 and "pattern" in schema and value.isdecimal() and int(value) > 9223372036854775807:
            raise ApiError("invalid_arguments", f"{path} exceeds the signed 64-bit ID range.")
    elif kind == "integer":
        if value < schema.get("minimum", value) or value > schema.get("maximum", value):
            raise ApiError("invalid_arguments", f"{path} is outside the supported range.")
    if "enum" in schema and value not in schema["enum"]:
        raise ApiError("invalid_arguments", f"{path} has an unsupported value.")


class Service:
    def __init__(self, library=None, install_root=None):
        self.library = library
        self.install_root = Path(install_root) if install_root else Path(__file__).resolve().parents[2]

    def capabilities(self):
        return {"name": "billfish", "version": __version__, "apiVersion": "v1",
                "storage": "ordinary-files", "idEncoding": "decimal-string",
                "libraryResolution": ["explicit", "BILLFISH_LIBRARY_ROOT", "recent_lib.ini"],
                "writeMode": "native-online-or-offline", "searchEngine": "literal_like",
                "operations": [{"name": name, **operation} for name, operation in OPERATIONS.items()]}

    def openapi(self):
        response = {"description": "JSON envelope: ok + data, or ok=false + error.",
                    "content": {"application/json": {"schema": {"type": "object"}}}}
        return {"openapi": "3.1.0", "info": {"title": "Billfish local API", "version": __version__},
                "servers": [{"url": "http://127.0.0.1:27393"}],
                "security": [{"localToken": []}],
                "components": {"securitySchemes": {"localToken": {"type": "http", "scheme": "bearer"}}},
                "paths": {
                    "/health": {"get": {"operationId": "health", "responses": {"200": response}}},
                    "/v1/capabilities": {"get": {"operationId": "capabilities", "responses": {"200": response}}},
                    "/openapi.json": {"get": {"operationId": "openapi", "responses": {"200": {
                        "description": "OpenAPI 3.1 document, without an envelope.",
                        "content": {"application/json": {"schema": {"type": "object"}}}}}}},
                    "/v1/execute": {"post": {"operationId": "execute",
                        "requestBody": {"required": True, "content": {"application/json": {"schema": {
                            "oneOf": [object_schema({"operation": {"type": "string", "enum": [name]},
                                                      "arguments": operation["inputSchema"]}, ["operation", "arguments"])
                                      for name, operation in OPERATIONS.items()]}}}},
                        "responses": {"200": response, "400": response, "401": response,
                                      "403": response, "404": response, "409": response,
                                      "413": response, "500": response, "503": response}}}}}

    def call(self, operation, arguments):
        if not isinstance(operation, str) or operation not in OPERATIONS:
            raise ApiError("unknown_operation", "Unknown operation.", {"operation": operation}, status=404)
        validate(arguments, OPERATIONS[operation]["inputSchema"])
        if operation in {"ui.click", "ui.scroll"} and (("x" in arguments) != ("y" in arguments)):
            raise ApiError("invalid_arguments", "Local x/y coordinates must be supplied together.")
        if operation == "ui.scroll" and arguments["delta_x"] == arguments["delta_y"] == 0:
            raise ApiError("invalid_arguments", "At least one scroll delta must be nonzero.")
        if "running-host" in OPERATIONS[operation].get("requires", []):
            from .online import OnlineClient
            library = None
            if operation in {"plugins.run", "plugins.preview", "plugins.thumbnail", "items.selected", "items.replace_file", "items.import_path", "items.import_url", "video.capture"} and (self.library or os.environ.get("BILLFISH_LIBRARY_ROOT")):
                library = str(resolve_library(self.library))
            return OnlineClient(self.install_root).call(operation, arguments, library_root=library)
        library_root = self._library_root()
        catalog = Catalog(library_root)
        args = dict(arguments)
        for name in ("item_id", "folder_id", "tag_id"):
            if name in args:
                args[name] = int(args[name])
        if operation == "library.info":
            return catalog.info()
        if operation == "items.search":
            return catalog.search(**args)
        if operation == "items.get":
            return catalog.get_item(**args)
        if operation == "folders.list":
            return catalog.list_folders()
        if operation == "tags.list":
            return catalog.list_tags()
        if operation == "items.export":
            return self._export(catalog, **args)
        if operation in ("items.update", "tags.create", "tags.assign"):
            from .mutations import Mutations, running_billfish_processes
            if os.name == "nt" and running_billfish_processes():
                from .online import OnlineClient
                return OnlineClient(self.install_root).call(operation, arguments, library_root=str(library_root))
            mutations = Mutations(library_root, self.install_root)
            if operation == "items.update":
                return mutations.update_items(**args)
            if operation == "tags.create":
                return mutations.create_tag(**args)
            return mutations.assign_tags(**args)
        raise ApiError("unknown_operation", "Unknown operation.", status=404)

    def _library_root(self):
        if self.library or os.environ.get("BILLFISH_LIBRARY_ROOT"):
            return resolve_library(self.library)
        if os.name == "nt":
            from .online import OnlineClient
            client = OnlineClient(self.install_root)
            if client.available():
                info = client.call("host.info", {}, timeout=5)
                if info.get("library_root"):
                    return resolve_library(info["library_root"])
        return resolve_library()

    def _export(self, catalog, item_ids, destination, dry_run=True):
        root = catalog.root
        target = Path(os.path.abspath(destination))
        if target == root or root in target.parents or target in root.parents:
            raise ApiError("invalid_destination", "Export destination must be outside the source library.")
        plans = []
        for item_id in dict.fromkeys(item_ids):
            item = catalog.get_item(int(item_id))
            if item.get("is_recycled") or item.get("is_recycle") or item.get("recycled"):
                raise ApiError("recycled_item", "Recycled items cannot be exported.", {"item_id": item_id})
            path = item.get("path") or item.get("absolute_path")
            relative = item.get("relative_path")
            if not path or not relative:
                raise ApiError("unresolved_path", "Item has no verified ordinary file path.", {"item_id": item_id})
            source = Path(path)
            output = target / relative
            if not source.is_file():
                raise ApiError("file_missing", "Source file is missing.", {"item_id": item_id}, status=404)
            if output.exists():
                raise ApiError("destination_exists", "Export never overwrites files.", {"path": str(output)}, status=409)
            plans.append({"item_id": item_id, "source": str(source), "destination": str(output)})
        if not dry_run:
            copied = []
            partial = None
            try:
                for plan in plans:
                    output = Path(plan["destination"])
                    output.parent.mkdir(parents=True, exist_ok=True)
                    with open(plan["source"], "rb") as src, open(output, "xb") as dst:
                        partial = output
                        shutil.copyfileobj(src, dst)
                    copied.append(plan)
                    partial = None
            except OSError as exc:
                details = {"completed": copied, "planned": plans}
                if partial is not None:
                    try:
                        partial.unlink()
                    except OSError as cleanup:
                        details.update(partial_destination=str(partial), cleanup_error=str(cleanup))
                raise ApiError("export_failed", str(exc), details, status=409) from exc
        return {"dry_run": dry_run, "items": plans, "count": len(plans), "originals_modified": False}
