"""Public command behavior using ordinary files and the installed schema."""
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch

from billfish_api.errors import ApiError
from billfish_api.service import OPERATIONS, Service
from test_catalog import SCHEMA


class ServiceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="billfish-service-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "library"
        (self.root / ".bf").mkdir(parents=True)
        self.source = self.root / "普通素材.png"
        self.source.write_bytes(b"ordinary-file-stays-here")
        with sqlite3.connect(self.root / ".bf/billfish.db") as connection:
            connection.executescript(SCHEMA)
            connection.execute("INSERT INTO bf_file(id,name,pid,is_link) VALUES(1,?,0,0)", (self.source.name,))
            connection.execute("INSERT INTO bf_material_v2(file_id,is_recycle) VALUES(1,0)")
        connection.close()
        self.service = Service(self.root)

    def test_discovery_is_available_without_a_library(self):
        service = Service("absent")
        capabilities = service.capabilities()
        self.assertEqual({item["name"] for item in capabilities["operations"]}, set(OPERATIONS))
        spec = service.openapi()
        operations = spec["paths"]["/v1/execute"]["post"]["requestBody"]["content"]["application/json"]["schema"]["oneOf"]
        self.assertEqual(len(operations), len(OPERATIONS))
        json.dumps(spec)

    def test_scoped_ui_contract_and_invalid_input_never_dispatches(self):
        token = {"snapshot_id": "snapshot-1", "ref": "widget-3"}
        valid = {"ui.snapshot": {"max_nodes": 4000, "max_items": 500},
                 "ui.click": {**token, "x": 1, "y": 2, "count": 2},
                 "ui.type": {**token, "text": "搜索"}, "ui.key": {**token, "key": "Ctrl+A"},
                 "ui.scroll": {**token, "delta_x": 0, "delta_y": -120},
                 "ui.drag": {**token, "from": {"x": 0, "y": 0}, "to": {"x": 10, "y": 20}},
                 "ui.capture": token, "ui.invoke": token}
        with patch("billfish_api.online.OnlineClient") as client:
            client.return_value.call.return_value = {"accepted": True, "queued": True}
            for operation, arguments in valid.items():
                with self.subTest(operation=operation):
                    self.assertEqual(OPERATIONS[operation]["requires"], ["running-host"])
                    self.assertTrue(self.service.call(operation, arguments)["queued"])
                    client.return_value.call.assert_called_with(operation, arguments, library_root=None)
            client.return_value.call.reset_mock()
            for operation, arguments in [
                ("ui.snapshot", {"max_nodes": 4001}), ("ui.snapshot", {"include_hidden": 1}),
                ("ui.click", {**token, "x": 3}), ("ui.click", {**token, "count": 3}),
                ("ui.type", {**token, "text": "x" * 65537}),
                ("ui.capture", {"ref": "w"}), ("ui.capture", {**token, "desktop": True}),
                ("ui.scroll", {**token, "delta_x": 0, "delta_y": 0}),
                ("ui.drag", {**token, "from": {"x": 0, "y": 0}, "to": {"x": 1, "y": 2}, "steps": 65}),
                ("ui.key", {**token, "key": "Enter", "global": True})]:
                with self.subTest(operation=operation, arguments=arguments), self.assertRaises(ApiError) as error:
                    self.service.call(operation, arguments)
                self.assertEqual(error.exception.code, "invalid_arguments")
            client.return_value.call.assert_not_called()

    def test_catalog_command_contract_and_invalid_input(self):
        item = self.service.call("items.get", {"item_id": "1"})
        self.assertEqual(item["path"], str(self.source))
        self.assertEqual(item["id"], "1")
        self.assertEqual(self.service.call("items.search", {"query": "普通"})["total"], 1)
        for name, arguments in [("items.get", {"item_id": 1}),
                                ("items.get", {"item_id": "9223372036854775808"}),
                                ("items.get", {"item_id": "1", "sql": "DROP TABLE bf_file"}),
                                ("items.search", {"limit": True}),
                                ("items.export", {"item_ids": [], "destination": "x"}),
                                ("items.update", {"updates": [{"item_id": "1", "score": 5}]})]:
            with self.subTest(name=name, arguments=arguments), self.assertRaises(ApiError):
                self.service.call(name, arguments)

    def test_export_dry_run_apply_and_no_overwrite(self):
        destination = Path(self.temp.name) / "export"
        request = {"item_ids": ["1", "1"], "destination": str(destination)}
        source_hash = hashlib.sha256(self.source.read_bytes()).digest()
        planned = self.service.call("items.export", request)
        self.assertTrue(planned["dry_run"])
        self.assertEqual(planned["count"], 1)
        self.assertFalse(destination.exists())
        copied = self.service.call("items.export", {**request, "dry_run": False})
        self.assertEqual(copied["count"], 1)
        self.assertEqual((destination / self.source.name).read_bytes(), self.source.read_bytes())
        self.assertEqual(hashlib.sha256(self.source.read_bytes()).digest(), source_hash)
        with self.assertRaises(ApiError) as error:
            self.service.call("items.export", {**request, "dry_run": False})
        self.assertEqual(error.exception.code, "destination_exists")
        with self.assertRaises(ApiError):
            self.service.call("items.export", {**request, "destination": str(self.root / "copy")})

    def test_replace_file_online_contract_and_strict_input(self):
        schema = OPERATIONS["items.replace_file"]
        self.assertTrue(schema["defaultDryRun"])
        self.assertTrue(schema["inputSchema"]["properties"]["dry_run"]["default"])
        request = {"item_id": "9223372036854775806", "source_path": str(self.source)}
        result = {"completion": "native_replace_persisted", "item_id": request["item_id"],
                  "analysis_ready": True, "source_safe_to_delete": True}
        with patch("billfish_api.online.OnlineClient") as client:
            client.return_value.call.return_value = result
            self.assertEqual(self.service.call("items.replace_file", request), result)
            client.return_value.call.assert_called_once_with("items.replace_file", request, library_root=str(self.root))
            client.return_value.call.reset_mock()
            client.return_value.call.side_effect = ApiError("replace_analysis_timeout", "analysis failed", {"job_id": "job-1"})
            with self.assertRaises(ApiError) as error:
                self.service.call("items.replace_file", {**request, "dry_run": False})
            self.assertEqual(error.exception.code, "replace_analysis_timeout")
            self.assertEqual(error.exception.details, {"job_id": "job-1"})
            client.return_value.call.assert_called_once()
            client.return_value.call.reset_mock()
            for invalid in ({**request, "item_id": 1}, {**request, "item_id": "9223372036854775808"},
                            {**request, "source_path": "relative.png"}, {**request, "source_path": "https://example.invalid/a.png"},
                            {**request, "dry_run": 0}, {**request, "unknown": True}, {"item_id": "1"}):
                with self.subTest(invalid=invalid), self.assertRaises(ApiError) as error:
                    self.service.call("items.replace_file", invalid)
                self.assertEqual(error.exception.code, "invalid_arguments")
            client.return_value.call.assert_not_called()

        with patch.dict(os.environ, {"BILLFISH_LIBRARY_ROOT": str(self.root)}), \
             patch("billfish_api.online.OnlineClient") as client:
            Service().call("items.replace_file", request)
            client.return_value.call.assert_called_once_with("items.replace_file", request, library_root=str(self.root))

    def test_interrupted_export_removes_only_the_incomplete_copy(self):
        destination = Path(self.temp.name) / "interrupted"
        def interrupt(source, output):
            output.write(source.read(3))
            raise OSError("simulated disk failure")
        with patch("billfish_api.service.shutil.copyfileobj", side_effect=interrupt):
            with self.assertRaises(ApiError) as failure:
                self.service.call("items.export", {"item_ids": ["1"], "destination": str(destination), "dry_run": False})
        self.assertEqual(failure.exception.code, "export_failed")
        self.assertEqual(failure.exception.details["completed"], [])
        self.assertFalse((destination / self.source.name).exists())
        self.assertEqual(self.source.read_bytes(), b"ordinary-file-stays-here")

    def test_running_gui_routes_to_native_host_and_never_falls_back_to_sql(self):
        request = {"updates": [{"item_id": "1", "note": "online"}], "dry_run": False}
        before = hashlib.sha256((self.root / ".bf/billfish.db").read_bytes()).digest()
        with patch("billfish_api.mutations.running_billfish_processes", return_value=[{"pid": 1}]), \
             patch("billfish_api.online.OnlineClient") as client:
            client.return_value.call.return_value = {"online": True, "modified_count": 1}
            result = self.service.call("items.update", request)
            self.assertTrue(result["online"])
            client.return_value.call.assert_called_once_with("items.update", request, library_root=str(self.root))
            client.return_value.call.side_effect = ApiError("online_unavailable", "host unavailable")
            with self.assertRaises(ApiError) as unavailable:
                self.service.call("items.update", request)
            self.assertEqual(unavailable.exception.code, "online_unavailable")
        self.assertEqual(before, hashlib.sha256((self.root / ".bf/billfish.db").read_bytes()).digest())
        self.assertFalse((self.root / ".bf/automation-backups").exists())

    def test_plugin_run_binds_environment_library_before_host_dispatch(self):
        gui_library = Path(self.temp.name) / "gui-library-B"
        request = {"plugin_id": "test.plugin", "command": "organize", "arguments": {}}

        def host_call(operation, arguments, library_root=None):
            if library_root is not None and Path(library_root) != gui_library:
                raise ApiError("library_mismatch", "The GUI has another library open.")
            return {"library_root": str(gui_library)}

        with patch.dict(os.environ, {"BILLFISH_LIBRARY_ROOT": str(self.root)}), \
             patch("billfish_api.online.OnlineClient") as client:
            client.return_value.call.side_effect = host_call
            with self.assertRaises(ApiError) as mismatch:
                Service().call("plugins.run", request)
            self.assertEqual(mismatch.exception.code, "library_mismatch")
            client.return_value.call.assert_called_once_with("plugins.run", request, library_root=str(self.root))

        with patch.dict(os.environ, {"BILLFISH_LIBRARY_ROOT": ""}), \
             patch("billfish_api.online.OnlineClient") as client:
            client.return_value.call.side_effect = host_call
            self.assertEqual(Service().call("plugins.run", request)["library_root"], str(gui_library))
            client.return_value.call.assert_called_once_with("plugins.run", request, library_root=None)

    def test_plugin_listing_has_no_library_dependency(self):
        with patch.dict(os.environ, {"BILLFISH_LIBRARY_ROOT": "missing-library"}), \
             patch("billfish_api.online.OnlineClient") as client:
            client.return_value.call.return_value = {"plugins": []}
            self.assertEqual(Service("also-missing").call("plugins.list", {}), {"plugins": []})
            client.return_value.call.assert_called_once_with("plugins.list", {}, library_root=None)

    def test_format_extension_operations_require_real_item_and_bind_library(self):
        for operation, effect in (("plugins.preview", "show-plugin-preview"),
                                  ("plugins.thumbnail", "write-native-cover")):
            request = {"item_id": "9223372036854775806", "plugin_id": "test.3d"}
            with self.subTest(operation=operation), patch("billfish_api.online.OnlineClient") as client:
                self.assertEqual(OPERATIONS[operation]["effect"], effect)
                client.return_value.call.return_value = {"accepted": True, "job_id": "job-1"}
                for library, environment_root, expected in ((self.root, "", str(self.root)),
                                                            (None, str(self.root), str(self.root)),
                                                            (None, "", None)):
                    client.return_value.call.reset_mock()
                    with patch.dict(os.environ, {"BILLFISH_LIBRARY_ROOT": environment_root}):
                        self.assertEqual(Service(library).call(operation, request)["job_id"], "job-1")
                    client.return_value.call.assert_called_once_with(operation, request, library_root=expected)
                client.return_value.call.reset_mock()
                for arguments in ({}, {"item_id": 1}, {"item_id": "0"}, {"item_id": "9223372036854775808"},
                                  {"item_id": "1", "plugin_id": ""}, {"item_id": "1", "plugin_id": "x" * 129},
                                  {"item_id": "1", "overwrite_user_cover": True}):
                    with self.subTest(arguments=arguments), self.assertRaises(ApiError) as invalid:
                        self.service.call(operation, arguments)
                    self.assertEqual(invalid.exception.code, "invalid_arguments")
                client.return_value.call.assert_not_called()

    def test_plugin_install_accepts_package_or_directory_and_returns_final_host_result(self):
        installed = {"id": "example.plugin", "installed": True, "enabled": False}
        with patch("billfish_api.online.OnlineClient") as client:
            client.return_value.call.return_value = installed
            for source in ("C:/Plugins/example.eagleplugin", "C:/Plugins/example.zip", "C:/Plugins/development"):
                with self.subTest(source=source):
                    client.return_value.call.reset_mock()
                    arguments = {"source_path": source}
                    self.assertEqual(Service().call("plugins.install", arguments), installed)
                    client.return_value.call.assert_called_once_with("plugins.install", arguments, library_root=None)
            client.return_value.call.reset_mock()
            for arguments in ({"source_directory": "C:/Plugins/old"},
                              {"source_path": "C:/Plugins/example.zip", "source_directory": "C:/Plugins/old"},
                              {"source_path": ""}):
                with self.subTest(arguments=arguments), self.assertRaises(ApiError) as invalid:
                    Service().call("plugins.install", arguments)
                self.assertEqual(invalid.exception.code, "invalid_arguments")
            client.return_value.call.assert_not_called()

    def test_selected_items_only_reads_actual_online_selection(self):
        selection = {"items": [{"id": "1", "path": str(self.source)}], "total": 1,
                     "library_root": str(self.root), "source": "native_current_view", "item_ids": ["1"]}
        for library, environment_root, expected_root in [(self.root, "", str(self.root)),
                                                        (None, str(self.root), str(self.root)),
                                                        (None, "", None)]:
            with self.subTest(library=library, environment_root=environment_root), \
                 patch.dict(os.environ, {"BILLFISH_LIBRARY_ROOT": environment_root}), \
                 patch("billfish_api.online.OnlineClient") as client:
                client.return_value.call.return_value = selection
                self.assertEqual(Service(library).call("items.selected", {}), selection)
                client.return_value.call.assert_called_once_with("items.selected", {}, library_root=expected_root)

        with patch("billfish_api.online.OnlineClient") as client:
            with self.assertRaises(ApiError) as invalid:
                self.service.call("items.selected", {"item_ids": ["1"]})
            self.assertEqual(invalid.exception.code, "invalid_arguments")
            client.assert_not_called()

        with patch.dict(os.environ, {"BILLFISH_LIBRARY_ROOT": ""}), \
             patch("billfish_api.online.OnlineClient") as client:
            client.return_value.call.side_effect = ApiError("library_not_open", "Open a library in Billfish before reading the selection.")
            with self.assertRaises(ApiError) as unavailable:
                Service().call("items.selected", {})
            self.assertEqual(unavailable.exception.code, "library_not_open")

    def test_native_import_and_video_capture_contracts(self):
        requests = [("items.import_path", {"source_path": str(self.source), "name": "素材.png", "origin": "", "folder_id": "0"}),
                    ("items.import_url", {"url": "https://example.com/image.png", "origin": "https://example.com/page", "folder_id": "2147483647"}),
                    ("video.capture", {"url": "https://example.com/watch", "page_url": "https://example.com/page", "title": "视频", "media_url": "https://example.com/video.mp4"})]
        for operation, arguments in requests:
            result = {"accepted": True, "job_id": "capture-job"} if operation == "video.capture" else {
                "item_id": "1", "item_ids": ["1"], "item": {"id": "1"},
                "completion": "native_import_persisted", "analysis_ready": False}
            with self.subTest(operation=operation), patch("billfish_api.online.OnlineClient") as client:
                client.return_value.call.return_value = result
                self.assertEqual(self.service.call(operation, arguments), result)
                client.return_value.call.assert_called_once_with(operation, arguments, library_root=str(self.root))
                client.return_value.call.reset_mock()
                with patch.dict(os.environ, {"BILLFISH_LIBRARY_ROOT": str(self.root)}):
                    self.assertEqual(Service().call(operation, arguments), result)
                client.return_value.call.assert_called_once_with(operation, arguments, library_root=str(self.root))

        invalid = [("items.import_path", {"source_path": "relative.png"}),
                   ("items.import_path", {"source_path": True}),
                   ("items.import_url", {"url": "file:///image.png"}),
                   ("items.import_url", {"url": True}),
                   ("video.capture", {"url": "https://example.com", "media_url": "ftp://example.com/video"})]
        for folder in (True, 0, "-1", "01", "2147483648"):
            invalid.append(("items.import_url", {"url": "https://example.com/image", "folder_id": folder}))
        for operation, arguments in requests:
            invalid.extend([(operation, {**arguments, "dry_run": True}), (operation, {})])
        invalid.append(("items.import_path", {"source_path": str(self.source), "origin": "local"}))
        with patch("billfish_api.online.OnlineClient") as client:
            for operation, arguments in invalid:
                with self.subTest(operation=operation, arguments=arguments), self.assertRaises(ApiError) as failure:
                    self.service.call(operation, arguments)
                self.assertEqual(failure.exception.code, "invalid_arguments")
            client.assert_not_called()


if __name__ == "__main__":
    unittest.main()
