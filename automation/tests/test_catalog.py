"""Contract checks against a synthetic installed-schema catalog, never a personal library."""

import hashlib
import os
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch

from billfish_api.catalog import Catalog
from billfish_api.discovery import resolve_library
from billfish_api.errors import ApiError


SCHEMA = """
CREATE TABLE library(id INTEGER PRIMARY KEY,version INTEGER,platform TEXT);
INSERT INTO library VALUES(1,41,'win');
CREATE TABLE bf_file(id INTEGER PRIMARY KEY,name TEXT,pid INTEGER,is_hide INTEGER,is_link INTEGER,file_size INTEGER,ctime INTEGER,mtime INTEGER,md5 TEXT,tid INTEGER,born INTEGER,ttid INTEGER);
CREATE TABLE bf_folder(id INTEGER PRIMARY KEY,born INTEGER,pid INTEGER,name TEXT,desc TEXT,cover_tid INTEGER,hide INTEGER,seq REAL,color INTEGER,is_recycle INTEGER);
CREATE TABLE bf_material_v2(file_id INTEGER PRIMARY KEY,status INTEGER,thumb_tid INTEGER,image_tid INTEGER,w INTEGER,h INTEGER,is_recycle INTEGER,colors TEXT);
CREATE TABLE bf_material_userdata(id INTEGER PRIMARY KEY,file_id INTEGER UNIQUE,comments_summary TEXT,comments_count INTEGER,comments_detail TEXT,note TEXT,origin TEXT,score INTEGER,rotation INTEGER,hflip INTEGER,vflip INTEGER,cover_tid TEXT);
CREATE TABLE bf_tag(id INTEGER PRIMARY KEY,name TEXT,color INTEGER,born INTEGER);
CREATE TABLE bf_tag_v2(id INTEGER PRIMARY KEY,name TEXT,pid INTEGER,seq REAL,icon INTEGER,color INTEGER,born INTEGER);
CREATE TABLE bf_tag_join_file(file_id INTEGER,tag_id INTEGER,born INTEGER);
CREATE TABLE bf_material_video(id INTEGER PRIMARY KEY,file_id INTEGER,duration INTEGER);
CREATE TABLE bf_material_audio(id INTEGER PRIMARY KEY,file_id INTEGER,duration INTEGER);
"""


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="billfish-api-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "测试素材"
        (self.root / ".bf").mkdir(parents=True)
        self.db = self.root / ".bf/billfish.db"
        with sqlite3.connect(self.db) as connection:
            connection.executescript(SCHEMA)
            connection.executemany("INSERT INTO bf_folder(id,pid,name,is_recycle) VALUES(?,?,?,?)", [
                (10,0,"图片",0),(20,0,"回收",1),(21,20,"子目录",0),
                (30,31,"环一",0),(31,30,"环二",0)])
            connection.executemany("INSERT INTO bf_file(id,name,pid,file_size,tid) VALUES(?,?,?,123,9007199254740995)", [
                (1,"纹理.png",10),(2,"recycled.png",21),(3,"missing.mp4",0),
                (4,"../escape.png",0),(5,"cycle.png",30),(6,"orphan.png",999),
                (7,"root.jpg",0),(8,"100%_complete.png",0),(9,"你好.PNG",0),
                (9007199254740993,"large-id.png",0)])
            connection.execute("INSERT INTO bf_material_v2(file_id,w,h,is_recycle,thumb_tid,image_tid) VALUES(1,1920,1080,0,60,0)")
            connection.execute("INSERT INTO bf_material_userdata(file_id,note,score,origin) VALUES(1,'中文备注',5,'https://example.test/')")
            connection.execute("INSERT INTO bf_tag VALUES(100,'legacy tag',0,0)")
            connection.execute("INSERT INTO bf_tag_v2 VALUES(100,'modern tag',0,0,0,0,0)")
            connection.execute("INSERT INTO bf_tag_join_file VALUES(1,100,0)")
            connection.execute("INSERT INTO bf_material_video VALUES(1,3,12345)")
            connection.execute("INSERT INTO bf_material_audio VALUES(1,7,54321)")
        connection.close()
        (self.root / "图片").mkdir()
        (self.root / "图片/纹理.png").write_bytes(b"synthetic")
        previews = self.root / ".bf/.preview/01"
        previews.mkdir(parents=True)
        (previews / "1.small.webp").write_bytes(b"thumbnail")
        self.catalog = Catalog(self.root)

    def test_unicode_paths_metadata_and_string_ids(self):
        item = self.catalog.get_item(1)
        self.assertEqual(item["relative_path"], "图片/纹理.png")
        self.assertTrue(item["exists"])
        self.assertEqual(item["note"], "中文备注")
        self.assertEqual(item["score"], 5)
        self.assertEqual(item["tid"], "9007199254740995")
        self.assertEqual(item["folder_id"], "10")
        self.assertTrue(item["thumbnail_path"].endswith("1.small.webp"))
        self.assertIsNone(item["preview_path"])
        self.assertEqual(self.catalog.get_item("9007199254740993")["id"], "9007199254740993")

    def test_native_inactive_cache_files_stay_hidden_across_item_reads(self):
        from billfish_host_helper import hydrate_selection
        preview = self.root / '.bf/.preview/01/1.hd.webp'
        preview.write_bytes(b'old preview')
        connection = sqlite3.connect(self.db)
        try:
            with connection:
                connection.execute('UPDATE bf_material_v2 SET thumb_tid=60,image_tid=60 WHERE file_id=1')
            active = self.catalog.get_item('1')
            self.assertTrue(active['thumbnail_path'].endswith('1.small.webp'))
            self.assertEqual(active['preview_path'], str(preview))
            with connection:
                connection.execute('UPDATE bf_material_v2 SET thumb_tid=0,image_tid=0 WHERE file_id=1')
        finally:
            connection.close()
        results = [self.catalog.get_item('1'), self.catalog.search('纹理')['items'][0],
            hydrate_selection('items.selected', {'item_ids': ['1']}, str(self.root))['items'][0]]
        for item in results:
            self.assertIsNone(item['thumbnail_path'])
            self.assertIsNone(item['preview_path'])
            self.assertNotIn('_native_thumb_tid', item)
            self.assertNotIn('_native_image_tid', item)
        self.assertTrue(preview.is_file())
        self.assertTrue((preview.parent / '1.small.webp').is_file())

    def test_literal_search_note_tags_extensions_and_stable_paging(self):
        self.assertEqual(self.catalog.search("%_")["items"][0]["id"], "8")
        self.assertEqual(self.catalog.search("中文备注")["total"], 1)
        self.assertEqual(self.catalog.search("modern tag")["total"], 1)
        self.assertEqual(self.catalog.search("legacy tag")["total"], 0)
        self.assertEqual(self.catalog.search("example.test")["total"], 1)
        self.assertEqual(self.catalog.search("' OR 1=1 --")["total"], 0)
        self.assertEqual(self.catalog.search(folder_id="10", tag_id="100")["total"], 1)
        self.assertEqual(self.catalog.search(extensions=[".mp4"])["items"][0]["id"], "3")
        first = self.catalog.search(limit=2)
        second = self.catalog.search(limit=2, offset=2)
        self.assertEqual([item["id"] for item in first["items"] + second["items"]], ["1","3","4","5"])
        self.assertEqual(first["total"], second["total"])

    def test_recycled_ancestors_and_active_tag_source(self):
        self.assertNotIn("2", [item["id"] for item in self.catalog.search()["items"]])
        self.assertIn("2", [item["id"] for item in self.catalog.search(include_recycled=True)["items"]])
        self.assertTrue(self.catalog.get_item(2)["is_recycle"])
        tags = self.catalog.list_tags()
        self.assertEqual(tags["active_source"], "bf_tag_v2")
        self.assertEqual({tag["source"] for tag in tags["items"]}, {"bf_tag_v2"})
        self.assertEqual(len(self.catalog.get_item(1)["tags"]), 1)
        self.assertEqual(self.catalog.get_item(1)["tags"][0]["name"], "modern tag")
        item = self.catalog.get_item(2)
        self.assertIsNone(item["path"])
        self.assertFalse(item["exists"])
        folder = next(f for f in self.catalog.list_folders()["items"] if f["id"] == "21")
        self.assertIsNone(folder["path"])

    def test_link_paths_duration_and_schema_version(self):
        connection = sqlite3.connect(self.db)
        connection.execute("UPDATE bf_file SET is_link=1 WHERE id=1")
        connection.commit()
        connection.close()
        item = self.catalog.get_item(1)
        self.assertIsNone(item["path"])
        self.assertEqual(item["path_error"]["code"], "linked_path_unresolved")
        self.assertEqual(self.catalog.get_item(3)["duration_ms"], 12345)
        self.assertEqual(self.catalog.get_item(7)["duration_ms"], 54321)
        self.assertEqual(self.catalog.info()["schema_version"], 41)
        self.assertEqual(self.catalog.info()["platform"], "win")
        connection = sqlite3.connect(self.db)
        connection.execute("UPDATE library SET version=42")
        connection.commit()
        connection.close()
        with self.assertRaises(ApiError) as unsupported:
            self.catalog.info()
        self.assertEqual(unsupported.exception.code, "unsupported_schema")

    def test_missing_files_and_invalid_hierarchies_are_per_item_errors(self):
        self.assertFalse(self.catalog.get_item(3)["exists"])
        self.assertIsNone(self.catalog.get_item(3)["path_error"])
        for identifier, code in [(4,"unsafe_path"),(5,"folder_cycle"),(6,"folder_not_found")]:
            with self.subTest(identifier=identifier):
                item = self.catalog.get_item(identifier)
                self.assertIsNone(item["path"])
                self.assertEqual(item["path_error"]["code"], code)
        folder = next(item for item in self.catalog.list_folders()["items"] if item["id"] == "10")
        self.assertTrue(folder["exists"])

    def test_database_unchanged_and_connections_closed(self):
        before = hashlib.sha256(self.db.read_bytes()).digest()
        for operation in (self.catalog.info, self.catalog.search, self.catalog.list_folders, self.catalog.list_tags):
            operation()
        self.assertEqual(hashlib.sha256(self.db.read_bytes()).digest(), before)
        self.assertFalse(Path(str(self.db) + "-wal").exists())
        # Windows denies this rename while a SQLite connection remains open.
        renamed = self.db.with_name("closed.db")
        self.db.rename(renamed)
        renamed.rename(self.db)
        with self.catalog._connection() as connection:
            with self.assertRaises(sqlite3.OperationalError):
                connection.execute("DELETE FROM bf_file")

    def test_validation_and_errors(self):
        for identifier in (0,-1,True,1.5,"01",2**63):
            with self.subTest(identifier=identifier), self.assertRaises(ApiError):
                self.catalog.get_item(identifier)
        for kwargs in ({"limit":501},{"offset":-1},{"limit":True},{"extensions":["x%"]}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ApiError):
                self.catalog.search(**kwargs)
        with self.assertRaises(ApiError) as missing:
            self.catalog.get_item(1000)
        self.assertEqual(missing.exception.status,404)
        self.db.unlink()
        with self.assertRaises(ApiError):
            self.catalog.info()
        self.assertFalse(self.db.exists())

    def test_discovery_priority_and_qsettings_escaping(self):
        config_root = Path(self.temporary.name) / "roaming"
        config = config_root / "Billfish/config/recent_lib.ini"
        config.parent.mkdir(parents=True)
        escaped = str(self.root).replace("\\", "\\\\")
        config.write_text("[LoadDefaultLibType]\nLastLoadLibPath=" + escaped, encoding="utf-8")
        with patch.dict(os.environ, {"APPDATA":str(config_root),"BILLFISH_LIBRARY_ROOT":""}):
            self.assertEqual(resolve_library(), self.root)
            with patch.dict(os.environ, {"BILLFISH_LIBRARY_ROOT":"missing"}):
                self.assertEqual(resolve_library(self.root), self.root)
                with self.assertRaises(ApiError):
                    resolve_library()


if __name__ == "__main__":
    unittest.main()
