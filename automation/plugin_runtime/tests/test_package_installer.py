import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location("installer", Path(__file__).parents[1] / "package_installer.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class PackageInstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archive = self.root / "plugin.eagleplugin"
        self.destination = self.root / "staging"

    def package(self, entries):
        with zipfile.ZipFile(self.archive, "w", zipfile.ZIP_DEFLATED) as archive:
            for name, value in entries:
                archive.writestr(name, value)

    def test_real_zip_and_development_directory_preserve_bytes(self):
        self.package([("manifest.json", b'{"id":"example"}'), ("nested/script.js", b"binary\x00\xff")])
        installer.stage(self.archive, self.destination)
        self.assertEqual((self.destination / "nested/script.js").read_bytes(), b"binary\x00\xff")
        installer.stage(self.destination, self.root / "directory-copy")
        self.assertEqual((self.root / "directory-copy/nested/script.js").read_bytes(), b"binary\x00\xff")

    def test_unsafe_zip_paths_and_conflicts_never_leave_staging(self):
        for path in ("../escape", "/absolute", "C:/absolute", "a\\..\\escape", "a:stream", "CON", "a.", "a "):
            with self.subTest(path=path):
                self.package([("manifest.json", "{}"), (path, "bad")])
                with self.assertRaises(ValueError):
                    installer.stage(self.archive, self.destination)
                self.assertFalse(self.destination.exists())
        for entries in ([('A', '1'), ('a', '2')], [('a', '1'), ('a/b', '2')]):
            self.package([('manifest.json', '{}'), *entries])
            with self.assertRaises(ValueError):
                installer.stage(self.archive, self.destination)
            self.assertFalse(self.destination.exists())

    def test_root_manifest_required_and_existing_destination_preserved(self):
        self.package([("nested/manifest.json", "{}")])
        with self.assertRaises(ValueError):
            installer.stage(self.archive, self.destination)
        self.destination.mkdir()
        marker = self.destination / "user-file"
        marker.write_text("keep")
        with self.assertRaises(ValueError):
            installer.stage(self.archive, self.destination)
        self.assertEqual(marker.read_text(), "keep")

    def test_expanded_size_and_inventory_limits(self):
        self.package([("manifest.json", "{}"), ("bomb", b"0" * (2 * 1024 * 1024))])
        with patch.object(installer, "MAX_BYTES", 1024 * 1024), self.assertRaises(ValueError):
            installer.stage(self.archive, self.destination)
        self.assertFalse(self.destination.exists())
        self.package([("manifest.json", "{}"), *[(f"file{i}", "") for i in range(installer.MAX_FILES)]])
        with self.assertRaises(ValueError):
            installer.stage(self.archive, self.destination)


if __name__ == "__main__":
    unittest.main()
