"""Model-directory subtree snapshots on temporary read-only catalogs."""
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from billfish_api.catalog import Catalog
from billfish_api.errors import ApiError
from billfish_host_helper import handle_request
from test_catalog import SCHEMA


class AnimationCatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="billfish-animation-scope-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "素材"
        self.scope = self.root / "角色"
        (self.root / ".bf").mkdir(parents=True)
        (self.scope / "子目录").mkdir(parents=True)
        (self.root / "动作").mkdir()
        self.db = self.root / ".bf/billfish.db"
        self.write(SCHEMA, script=True)
        self.write("INSERT INTO bf_folder(id,pid,name,is_recycle) VALUES(10,0,'角色',0),(11,0,'动作',0),(12,10,'子目录',0),(20,10,'trash',1),(21,20,'child',0)")
        self.write("INSERT INTO bf_file(id,name,pid,is_link,is_hide) VALUES(1,'base.FBX',10,0,0),(2,'walk.fbx',12,0,1),(3,'linked.glb',10,1,0),(4,'gone.fbx',21,0,0),(5,'recycled.glb',10,0,0),(6,'ordinary.png',10,0,0),(7,'orphan.glb',999,0,0),(8,'sibling.fbx',11,0,0)")
        self.write("INSERT INTO bf_material_v2(file_id,is_recycle) VALUES(5,1)")
        (self.scope / "base.FBX").write_bytes(b"base")
        (self.scope / "子目录/walk.fbx").write_bytes(b"animation")
        (self.root / "动作/sibling.fbx").write_bytes(b"outside")
        self.catalog = Catalog(self.root)

    def write(self, sql, params=(), *, script=False):
        connection = sqlite3.connect(self.db)
        try:
            with connection:
                if script:
                    connection.executescript(sql)
                else:
                    connection.execute(sql, params)
        finally:
            connection.close()

    def snapshot(self):
        return self.catalog.animation_snapshot(str(self.scope))

    def request(self, **changes):
        return {"mode": "animation_catalog", "operation": "animation.catalog_snapshot",
                "arguments": {"scope_root": str(self.scope)}, "library_root": str(self.root), **changes}

    def test_scope_includes_hidden_descendants_but_excludes_parent_siblings_recycle_and_unindexed(self):
        (self.scope / "unindexed.fbx").write_bytes(b"not catalog owned")
        snapshot = self.snapshot()
        self.assertEqual(snapshot["schemaVersion"], 2)
        self.assertTrue(snapshot["complete"])
        self.assertEqual(snapshot["scopeRoot"], str(self.scope))
        self.assertEqual(snapshot["scopeMode"], "model-directory-recursive")
        items = {item["id"]: item for item in snapshot["items"]}
        self.assertEqual(set(items), {"1", "2", "3"})
        self.assertEqual(items["2"]["relativePath"], "角色/子目录/walk.fbx")
        self.assertTrue(items["2"]["exists"])
        self.assertEqual(items["3"]["pathError"]["code"], "linked_path_unresolved")

    def test_snapshot_is_complete_for_large_scopes_and_preserves_large_native_ids(self):
        connection = sqlite3.connect(self.db)
        try:
            with connection:
                connection.executemany("INSERT INTO bf_file(id,name,pid,is_link) VALUES(?,?,12,0)", [(i, f"clip-{i}.glb") for i in range(100, 720)])
                connection.execute("INSERT INTO bf_file(id,name,pid,is_link) VALUES(9007199254740993,'large.fbx',10,0)")
        finally:
            connection.close()
        snapshot = self.snapshot()
        self.assertEqual(len(snapshot["items"]), 624)
        self.assertIn("9007199254740993", {item["id"] for item in snapshot["items"]})

    def test_revision_tracks_scope_membership_but_not_sibling_changes_or_unrelated_metadata(self):
        first = self.snapshot()
        self.write("UPDATE bf_file SET is_hide=0,mtime=123 WHERE id=2")
        self.write("UPDATE bf_file SET name='sibling-renamed.fbx' WHERE id=8")
        self.write("INSERT INTO bf_material_userdata(file_id,note) VALUES(1,'unrelated note')")
        (self.scope / "base.FBX").write_bytes(b"changed source; backend fingerprints content")
        self.assertEqual(first["revision"], self.snapshot()["revision"])
        self.write("UPDATE bf_file SET pid=11 WHERE id=2")
        moved = self.snapshot()
        self.assertNotEqual(first["revision"], moved["revision"])
        self.assertNotIn("2", {item["id"] for item in moved["items"]})
        self.write("UPDATE bf_file SET pid=12 WHERE id=8")
        self.assertIn("8", {item["id"] for item in self.snapshot()["items"]})

    def test_explicit_parent_scope_adds_siblings_but_unbound_or_missing_scopes_fail(self):
        parent = self.catalog.animation_snapshot(str(self.root))
        self.assertIn("8", {item["id"] for item in parent["items"]})
        for scope in ("", "relative", str(self.root.parent), str(self.scope / "missing"), str(self.scope / "trash")):
            with self.subTest(scope=scope), self.assertRaises(ApiError):
                self.catalog.animation_snapshot(scope)
        (self.scope / "unindexed-directory").mkdir()
        with self.assertRaises(ApiError):
            self.catalog.animation_snapshot(str(self.scope / "unindexed-directory"))

    def test_helper_requires_scope_and_round_trip_preserves_catalog_and_sources(self):
        paths = [self.db, self.scope / "base.FBX", self.scope / "子目录/walk.fbx"]
        before = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
        helper = Path(__file__).resolve().parents[1] / "billfish_host_helper.py"
        result = subprocess.run([sys.executable, "-I", str(helper)], input=json.dumps(self.request()).encode(), capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(json.loads(result.stdout)["data"], self.snapshot())
        self.assertEqual(before, {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths})
        for request in (self.request(arguments={}), self.request(arguments={"scope_root": str(self.scope), "root": str(self.root)}), self.request(library_root="relative"), self.request(operation="items.search")):
            with self.subTest(request=request), self.assertRaises(ApiError):
                handle_request(request)


if __name__ == "__main__":
    unittest.main()
