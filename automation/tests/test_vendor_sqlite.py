"""Real frozen tokenizer tests, limited to newly created temporary databases."""

import os
from pathlib import Path
import sqlite3
import tempfile
import unittest

from billfish_api.errors import ApiError
from billfish_api.vendor_sqlite import VendorDatabase


INSTALL_ROOT = Path(__file__).resolve().parents[2]
RUNTIME_AVAILABLE = os.name == "nt" and all((INSTALL_ROOT / name).is_file()
                                           for name in ("sqlite3.dll", "libBFDataCenter.dll"))


@unittest.skipUnless(RUNTIME_AVAILABLE, "Matching installed Windows vendor runtime required")
class VendorSqliteTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="billfish-native-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "临时素材"
        self.root.mkdir()
        self.main = self.root / "billfish.db"
        self.summary = self.root / "summary_v2.db"
        for path in (self.main, self.summary):
            sqlite3.connect(path).close()

    def connect(self, **kwargs):
        return VendorDatabase(self.main, install_root=INSTALL_ROOT, **kwargs)

    def test_tokenizer_metadata_update_and_cross_database_rollback(self):
        with self.connect() as db:
            db.attach(self.summary)
            db.execute("CREATE TABLE bf_material_userdata(file_id INTEGER PRIMARY KEY,note TEXT,origin TEXT)")
            db.execute("CREATE VIRTUAL TABLE summary.bf_summary USING fts5(iid UNINDEXED,name,type,origin,comment,note,tokenize='bf_tokenizer')")
            with db.transaction():
                db.execute("INSERT INTO bf_material_userdata VALUES(?,?,?)", (7, "旧的中文备注", "来源"))
                db.execute("INSERT INTO summary.bf_summary VALUES(?,?,?,?,?,?)", (7, "中国素材", "png", "来源", "", "旧的中文备注"))
            self.assertEqual(db.query("SELECT iid FROM summary.bf_summary WHERE bf_summary MATCH ?", ("备注",)), [{"iid": 7}])
            with db.transaction():
                db.execute("UPDATE bf_material_userdata SET note=? WHERE file_id=?", ("更新说明", 7))
                db.execute("UPDATE summary.bf_summary SET note=? WHERE iid=?", ("更新说明", 7))
            self.assertEqual(db.query("SELECT iid FROM summary.bf_summary WHERE bf_summary MATCH ?", ("说明",)), [{"iid": 7}])
            self.assertEqual(db.query("SELECT iid FROM summary.bf_summary WHERE bf_summary MATCH ?", ("备注",)), [])
            with self.assertRaisesRegex(RuntimeError, "abort"):
                with db.transaction():
                    db.execute("UPDATE bf_material_userdata SET note=? WHERE file_id=?", ("回滚字段", 7))
                    db.execute("UPDATE summary.bf_summary SET note=? WHERE iid=?", ("回滚字段", 7))
                    raise RuntimeError("abort")
            self.assertEqual(db.query("SELECT note FROM bf_material_userdata"), [{"note": "更新说明"}])
            self.assertEqual(db.query("SELECT iid FROM summary.bf_summary WHERE bf_summary MATCH ?", ("说明",)), [{"iid": 7}])
            self.assertEqual(db.query("SELECT iid FROM summary.bf_summary WHERE bf_summary MATCH ?", ("回滚",)), [])
        with self.connect(readonly=True) as db:
            db.attach(self.summary, readonly=True)
            self.assertEqual(db.query("SELECT iid FROM summary.bf_summary WHERE bf_summary MATCH ?", ("说明",)), [{"iid": 7}])
            with self.assertRaises(ApiError):
                db.execute("UPDATE bf_material_userdata SET note='should fail'")

    def test_bound_values_and_statement_lifetimes(self):
        with self.connect() as db:
            db.execute("CREATE TABLE values_test(i INTEGER,t TEXT,b BLOB,n,d REAL)")
            values = (9007199254740993, "中文\0文本';DROP TABLE values_test;--", b"\0\xff", None, 1.25)
            self.assertEqual(db.execute("INSERT INTO values_test VALUES(?,?,?,?,?)", values), 1)
            self.assertEqual(db.query("SELECT * FROM values_test"), [dict(zip(("i", "t", "b", "n", "d"), values))])
            with self.assertRaises(ApiError):
                db.execute("INSERT INTO values_test VALUES(?,?,?,?,?)", (1,))
            with self.assertRaises(ApiError):
                db.query("SELECT 1; DROP TABLE values_test")
            with self.assertRaises(ApiError):
                db.query("SELECT * FROM nonexistent")
            self.assertEqual(len(db.query("SELECT * FROM values_test")), 1)
        db.close()
        with self.assertRaises(ApiError):
            db.query("SELECT 1")
        # Windows deletion establishes that both database handles were released.
        self.main.unlink()

    def test_missing_database_does_not_create_and_wrong_runtime_is_rejected(self):
        absent = self.root / "absent.db"
        with self.assertRaises(ApiError):
            VendorDatabase(absent, install_root=INSTALL_ROOT)
        self.assertFalse(absent.exists())
        fake_runtime = self.root / "runtime"
        fake_runtime.mkdir()
        (fake_runtime / "sqlite3.dll").write_bytes(b"invalid")
        with self.assertRaises(ApiError) as context:
            VendorDatabase(self.main, install_root=fake_runtime)
        self.assertEqual(context.exception.code, "native_database_unavailable")


if __name__ == "__main__":
    unittest.main()
