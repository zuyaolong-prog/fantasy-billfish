"""Exercise the source archive boundary through the public export CLI."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


EXPORTER = Path(__file__).resolve().parents[1] / "export-portable.py"
SPEC = importlib.util.spec_from_file_location("export_portable", EXPORTER)
EXPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORT)


class SourceExportTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="billfish-source-export-")
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.root = self.base / "source"
        self.destination = self.base / "export"
        self.root.mkdir()
        subprocess.run(["git", "init", "--quiet", str(self.root)], check=True, capture_output=True)
        for encoded in EXPORT.REQUIRED_SOURCE_FILES:
            self.write(encoded.decode("utf-8"), "source fixture\n")
        self.write("browser-extension/maintained/provenance.json", '{"source_hashes": {}}\n')
        self.write(".gitignore", (EXPORTER.parents[1] / ".gitignore").read_text(encoding="utf-8"))
        self.write(".gitattributes", "* -text\n")
        self.write("tools/export-portable.py", EXPORTER.read_text(encoding="utf-8"))

    def write(self, name, content):
        target = self.root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")

    def export(self):
        return subprocess.run(
            [sys.executable, str(self.root / "tools/export-portable.py"), str(self.destination), "--source-only"],
            capture_output=True, text=True, check=False,
        )

    def test_source_export_contains_parser_and_viewer_without_runtime_or_private_data(self):
        self.write("_animation-browser-parser/tests/cli_contract.py", "# parser contract\n")
        excluded = [
            "Billfish.exe", "Billfish.core.exe", "tools/animation-parser/parser.exe",
            "_animation-browser-parser/parser.dll", "_animation-browser-parser/build/CMakeCache.txt",
            "tools/electron/LICENSE", "tools/python/python314.dll", "AGENTS.md",
            "automation/AGENTS.md", "task-memory/task/private.md", "automation/logs/local.json",
            "_video-trim-patch/backups/old.cpp", "automation/local.tmp",
        ]
        for name in excluded:
            self.write(name, "runtime or private fixture\n")
        result = self.export()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["mode"], "source")
        expected = [
            "_animation-browser-parser/CMakeLists.txt", "_animation-browser-parser/src/main.cpp",
            "_animation-browser-parser/tests/cli_contract.py",
            "_animation-browser-parser/vendor/ufbx/ufbx.c", "_animation-browser-parser/vendor/ufbx/ufbx.h",
            "_animation-browser-parser/vendor/ufbx/LICENSE",
            "_animation-browser-parser/vendor/nlohmann/json.hpp",
            "_animation-browser-parser/vendor/nlohmann/LICENSE.MIT",
            "automation/animation_browser/backend/service.cjs", "automation/animation_browser/ui/app.mjs",
            "automation/plugin_runtime/animation-browser-runtime.cjs", "automation/animation_catalog_helper.py",
            "_video-trim-patch/src/automation/format_preview_bridge.cpp",
            "_video-trim-patch/src/automation/native_adapter.cpp", "_sidebar-patch/src/BfFolderTreeView.qml",
        ]
        for name in expected:
            self.assertEqual((self.destination / name).read_bytes(), (self.root / name).read_bytes(), name)
        for name in excluded:
            self.assertFalse((self.destination / name).exists(), name)
        self.assertFalse((self.destination / ".git").exists())
        self.assertIn("*.dll", (self.destination / ".gitignore").read_text(encoding="utf-8"))
        self.assertIn("* text=auto", (self.destination / ".gitattributes").read_text(encoding="utf-8"))

    def test_missing_parser_license_fails_before_export(self):
        license_path = self.root / "_animation-browser-parser/vendor/ufbx/LICENSE"
        # Move the input away to model an incomplete checkout without deleting it.
        shutil.move(str(license_path), str(self.base / "LICENSE.saved"))
        result = self.export()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("_animation-browser-parser/vendor/ufbx/LICENSE", result.stderr)
        self.assertFalse(self.destination.exists())

    def test_frozen_inputs_keep_original_bytes_in_exported_git_index(self):
        frozen = [
            "automation/animation_browser/vendor/core/lib/3dm/rhino3dm.js",
            "automation/animation_browser/vendor/core/resources.sha256.json",
            "browser-extension/maintained/globalThis.js",
            "browser-extension/maintained/edit/home.html",
            "_sidebar-patch/src/BfFolderTreeView.qml",
            "_animation-browser-parser/vendor/ufbx/ufbx.c",
            "_animation-browser-parser/vendor/nlohmann/LICENSE.MIT",
        ]
        payload = b"first line\r\nsecond line with inherited whitespace  \r\n"
        for name in frozen:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)
        result = self.export()
        self.assertEqual(result.returncode, 0, result.stderr)
        subprocess.run(["git", "init", "--quiet", str(self.destination)], check=True, capture_output=True)
        subprocess.run(["git", "-C", str(self.destination), "config", "core.autocrlf", "true"], check=True, capture_output=True)
        subprocess.run(["git", "-C", str(self.destination), "add", "."], check=True, capture_output=True)
        for name in frozen:
            indexed = subprocess.run(
                ["git", "-C", str(self.destination), "cat-file", "blob", ":" + name],
                check=True, capture_output=True,
            ).stdout
            self.assertEqual(indexed, (self.root / name).read_bytes(), name)


if __name__ == "__main__":
    unittest.main()
