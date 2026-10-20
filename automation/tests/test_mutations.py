"""Mutation contracts exercise real frozen SQLite on temporary synthetic files."""

from contextlib import closing
import hashlib
import json
import os
from pathlib import Path
import sqlite3  # Deliberately loaded before the isolated vendor DLL worker.
import tempfile
import unittest
from unittest.mock import patch

from billfish_api.errors import ApiError
from billfish_api.mutations import Mutations
from billfish_api.vendor_sqlite import VendorDatabase


INSTALL_ROOT = Path(__file__).resolve().parents[2]
SCHEMA = """
CREATE TABLE library(version INTEGER);
INSERT INTO library VALUES(41);
CREATE TABLE bf_file(id INTEGER PRIMARY KEY,name TEXT,pid INTEGER,is_link INTEGER,tid INTEGER);
CREATE TABLE bf_folder(id INTEGER PRIMARY KEY,pid INTEGER,name TEXT,is_recycle INTEGER);
CREATE TABLE bf_type(tid INTEGER PRIMARY KEY,name TEXT);
CREATE TABLE bf_material_v2(file_id INTEGER PRIMARY KEY,is_recycle INTEGER);
CREATE TABLE bf_material_userdata(id INTEGER PRIMARY KEY,file_id INTEGER UNIQUE,
 comments_summary TEXT,comments_count INTEGER,comments_detail TEXT,note TEXT,origin TEXT,
 score INTEGER,rotation INTEGER,hflip INTEGER,vflip INTEGER,cover_tid TEXT);
CREATE TABLE bf_tag_v2(id INTEGER PRIMARY KEY,name TEXT,pid INTEGER,seq REAL,icon INTEGER,color INTEGER,born INTEGER);
CREATE TABLE bf_tag_join_file(file_id INTEGER,tag_id INTEGER,born INTEGER);
INSERT INTO bf_type VALUES(7,'png');
INSERT INTO bf_folder VALUES(10,0,'active',0),(20,0,'recycled',1);
INSERT INTO bf_file VALUES(1,'one.png',10,0,7),(2,'two.png',10,0,7),
 (3,'recycled.png',20,0,7),(4,'link.png',10,1,7),(5,'missing.png',10,0,7);
INSERT INTO bf_material_v2 VALUES(1,0),(2,0),(3,0),(4,0),(5,0);
INSERT INTO bf_material_userdata VALUES(8,1,'preserve comment',3,'details','旧词山川','source',5,90,1,1,'77');
INSERT INTO bf_tag_v2 VALUES(10,'existing',0,4,2,3,0);
INSERT INTO bf_tag_join_file VALUES(1,10,0);
"""


@unittest.skipUnless(os.name == 'nt' and (INSTALL_ROOT / 'libBFDataCenter.dll').is_file(), 'Requires frozen Windows runtime')
class MutationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='billfish-mutations-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / '素材'
        (self.root / '.bf').mkdir(parents=True)
        (self.root / 'active').mkdir()
        (self.root / 'recycled').mkdir()
        for name in ('one.png', 'two.png', 'link.png'):
            (self.root / 'active' / name).write_bytes(b'synthetic image')
        (self.root / 'recycled/recycled.png').write_bytes(b'synthetic image')
        self.db = self.root / '.bf/billfish.db'
        self.summary = self.root / '.bf/summary_v2.db'
        with closing(sqlite3.connect(self.db)) as db, db:
            db.executescript(SCHEMA)
        with closing(sqlite3.connect(self.summary)) as db, db:
            db.execute('PRAGMA user_version=1')
        with VendorDatabase(self.summary, install_root=INSTALL_ROOT) as db:
            db.execute("CREATE VIRTUAL TABLE bf_summary USING fts5(iid UNINDEXED,name,type,origin,comment,note,tokenize='bf_tokenizer')")
            db.execute("INSERT INTO bf_summary VALUES(1,'one.png','png','source','preserve comment','旧词山川')")
        self.mutations = Mutations(self.root, INSTALL_ROOT)
        self.guard = patch('billfish_api.mutations.running_billfish_processes', return_value=[])
        self.guard.start()
        self.addCleanup(self.guard.stop)

    def hashes(self):
        return [hashlib.sha256(path.read_bytes()).hexdigest() for path in (self.db, self.summary)]

    def main_rows(self, sql, args=()):
        with closing(sqlite3.connect(self.db)) as db, db:
            db.row_factory = sqlite3.Row
            return [dict(row) for row in db.execute(sql, args)]

    def test_dry_run_validates_whole_batch_without_writing(self):
        before = self.hashes()
        result = self.mutations.update_items([{'item_id':'1','note':'AI 标签'}])
        self.assertTrue(result['dry_run'])
        self.assertEqual(result['plan'][0]['changes'], {'note':'AI 标签'})
        self.assertEqual(before, self.hashes())
        self.assertFalse((self.root / '.bf/automation-backups').exists())
        with self.assertRaises(ApiError) as error:
            self.mutations.update_items([{'item_id':1,'note':'new'},{'item_id':999,'origin':'x'}], dry_run=False)
        self.assertEqual(error.exception.code, 'item_not_found')
        self.assertEqual(before, self.hashes())

    def test_update_preserves_non_target_fields_refreshes_native_search_and_backs_up(self):
        before = self.main_rows('SELECT * FROM bf_material_userdata WHERE file_id=1')[0]
        original_hashes = self.hashes()
        result = self.mutations.update_items([{'item_id':1,'note':'新的角色动画'}], dry_run=False)
        after = self.main_rows('SELECT * FROM bf_material_userdata WHERE file_id=1')[0]
        self.assertEqual(after, {**before, 'note':'新的角色动画'})
        self.assertTrue(result['offline'])
        self.assertTrue(result['requires_gui_restart'])
        self.assertEqual(result['modified_count'], 1)
        with VendorDatabase(self.summary, install_root=INSTALL_ROOT, readonly=True) as db:
            self.assertEqual(db.query("SELECT iid FROM bf_summary WHERE bf_summary MATCH ?", ('山川',)), [])
            self.assertEqual(db.query("SELECT iid FROM bf_summary WHERE bf_summary MATCH ?", ('角色',))[0]['iid'], 1)
            self.assertEqual(db.query('SELECT COUNT(*) AS n FROM bf_summary WHERE iid=1')[0]['n'], 1)
        backup = Path(result['backup_directory'])
        manifest = json.loads((backup / 'manifest.json').read_text(encoding='utf-8'))
        self.assertEqual([entry['sha256'] for entry in manifest['files']], original_hashes)
        self.assertEqual([hashlib.sha256((backup / name).read_bytes()).hexdigest() for name in ('billfish.db','summary_v2.db')], original_hashes)
        self.assertEqual((self.root / 'active/one.png').read_bytes(), b'synthetic image')

    def test_noop_does_not_back_up_or_require_restart(self):
        before = self.hashes()
        result = self.mutations.update_items([{'item_id':1,'note':'旧词山川'}],dry_run=False)
        self.assertEqual(result['modified_count'],0)
        self.assertIsNone(result['backup_directory'])
        self.assertFalse(result['requires_gui_restart'])
        result = self.mutations.assign_tags([1],[10],dry_run=False)
        self.assertEqual(result['modified_count'],0)
        self.assertIsNone(result['backup_directory'])
        self.assertFalse(result['requires_gui_restart'])
        self.assertFalse((self.root / '.bf/automation-backups').exists())
        self.assertEqual(self.hashes(),before)

    def test_missing_userdata_uses_observed_frozen_defaults(self):
        self.mutations.update_items([{'item_id':2,'origin':'local://example'}], dry_run=False)
        row = self.main_rows('SELECT * FROM bf_material_userdata WHERE file_id=2')[0]
        row.pop('id')
        self.assertEqual(row, {'file_id':2,'comments_summary':'','comments_count':0,'comments_detail':'',
                              'note':'','origin':'local://example','score':0,'rotation':0,'hflip':0,'vflip':0,'cover_tid':'0'})

    def test_rollback_covers_main_and_summary_and_retains_backup(self):
        with VendorDatabase(self.summary, install_root=INSTALL_ROOT) as db:
            db.execute("CREATE TRIGGER fail_summary BEFORE INSERT ON bf_summary_data BEGIN SELECT RAISE(ABORT,'synthetic failure'); END")
        before = self.hashes()
        with self.assertRaises(ApiError):
            self.mutations.update_items([{'item_id':1,'note':'must rollback'}], dry_run=False)
        self.assertEqual(self.hashes(), before)
        backups = list((self.root / '.bf/automation-backups').iterdir())
        self.assertEqual(len(backups), 1)
        self.assertTrue((backups[0] / 'manifest.json').is_file())

    def test_gui_running_rejects_apply_but_allows_dry_run(self):
        with patch('billfish_api.mutations.running_billfish_processes', return_value=[{'pid':123,'name':'Billfish.core.exe'}]):
            for operation in (lambda: self.mutations.update_items([{'item_id':1,'note':'x'}],False),
                              lambda: self.mutations.create_tag('new',dry_run=False),
                              lambda: self.mutations.assign_tags([2],[10],dry_run=False)):
                with self.assertRaises(ApiError) as error:
                    operation()
                self.assertEqual(error.exception.code, 'gui_running')
            self.mutations.create_tag('new')

    def test_invalid_items_and_inputs_rejected_before_write(self):
        before = self.hashes()
        for identifier in (3,4,5):
            with self.subTest(identifier=identifier), self.assertRaises(ApiError) as error:
                self.mutations.update_items([{'item_id':identifier,'note':'x'}],False)
            self.assertEqual(error.exception.code,'item_not_writable')
        for updates in ([], [{'item_id':1,'score':9}], [{'item_id':1}],
                        [{'item_id':1,'note':'\0'}], [{'item_id':1,'note':None}],
                        [{'item_id':1,'note':'a'}]*501, [{'item_id':1,'note':'a'}]*2):
            with self.subTest(updates=str(updates)[:80]), self.assertRaises(ApiError):
                self.mutations.update_items(updates,False)
        self.assertEqual(self.hashes(), before)

    def test_tag_creation_and_idempotent_assignment_removal(self):
        planned = self.mutations.create_tag('AI中文')
        self.assertEqual(planned['plan']['seq'],5)
        result = self.mutations.create_tag('AI中文',dry_run=False)
        tag = result['tag']
        self.assertEqual(tag['name'],'AI中文')
        self.assertEqual(tag['born'],0)
        self.assertEqual(tag['pid'],'0')
        with self.assertRaises(ApiError) as error:
            self.mutations.create_tag('AI中文')
        self.assertEqual(error.exception.code,'tag_exists')
        self.assertEqual(self.mutations.assign_tags([1,2],[tag['id']],dry_run=False)['modified_count'],2)
        self.assertEqual(self.mutations.assign_tags([1,2],[tag['id']],dry_run=False)['modified_count'],0)
        self.assertEqual(self.mutations.assign_tags([1],[tag['id']],mode='remove',dry_run=False)['modified_count'],1)
        self.assertEqual(self.mutations.assign_tags([1],[tag['id']],mode='remove')['modified_count'],0)
        self.assertEqual(self.main_rows('SELECT COUNT(*) AS n FROM bf_tag_join_file WHERE tag_id=?',(int(tag['id']),))[0]['n'],1)
        with self.assertRaises(ApiError):
            self.mutations.create_tag('child',pid=999,dry_run=False)

    def test_schema_journal_and_summary_checks_do_not_migrate(self):
        with closing(sqlite3.connect(self.db)) as db, db:
            db.execute('UPDATE library SET version=42')
        with self.assertRaises(ApiError) as error:
            self.mutations.create_tag('new',dry_run=False)
        self.assertEqual(error.exception.code,'unsupported_schema')
        with closing(sqlite3.connect(self.db)) as db, db:
            db.execute('UPDATE library SET version=41')
            db.commit()
            db.execute('PRAGMA journal_mode=WAL')
        with self.assertRaises(ApiError) as error:
            self.mutations.create_tag('new',dry_run=False)
        self.assertEqual(error.exception.code,'unsupported_journal_mode')
        self.assertFalse((self.root / '.bf/automation-backups').exists())

    def test_missing_summary_is_never_created(self):
        absent = self.root / '.bf/absent.db'
        self.mutations.summary = absent
        with self.assertRaises(ApiError):
            self.mutations.create_tag('new',dry_run=False)
        self.assertFalse(absent.exists())


if __name__ == '__main__':
    unittest.main()
