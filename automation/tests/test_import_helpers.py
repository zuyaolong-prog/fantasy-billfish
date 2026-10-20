"""Native import boundary validation on synthetic libraries and source files only."""

from contextlib import closing
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest

from billfish_api.errors import ApiError
from billfish_api.imports import browser_destination, import_preflight, import_validate, import_verify
from test_catalog import SCHEMA


HELPER = Path(__file__).resolve().parents[1] / 'billfish_host_helper.py'


class ImportHelperTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='billfish-import-helper-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / '素材'
        (self.root / '.bf').mkdir(parents=True)
        (self.root / 'active').mkdir()
        (self.root / 'trash').mkdir()
        self.source = Path(self.temp.name) / 'source.png'
        self.source.write_bytes(b'synthetic source')
        self.database = self.root / '.bf/billfish.db'
        self.config = self.root / '.bf/.ui_config/lib_info.json'
        with closing(sqlite3.connect(self.database)) as db, db:
            db.executescript(SCHEMA)
            db.execute("INSERT INTO bf_folder(id,pid,name,is_recycle) VALUES(10,0,'active',0),(20,0,'trash',1)")
            db.execute("INSERT INTO bf_file(id,name,pid,is_link,tid,file_size) VALUES(1,'source.png',10,0,7,16)")
            db.execute('INSERT INTO bf_material_v2(file_id,w,h,is_recycle) VALUES(1,128,64,0)')
            db.execute("INSERT INTO bf_material_userdata(file_id,note,origin) VALUES(1,'','https://example.test/source')")
        (self.root / 'active/source.png').write_bytes(self.source.read_bytes())
        self.mark()

    def mark(self):
        self.hashes = {path:hashlib.sha256(path.read_bytes()).hexdigest() for path in (self.database,self.config) if path.is_file()}

    def tearDown(self):
        self.assertEqual({path:hashlib.sha256(path.read_bytes()).hexdigest() for path in self.hashes},self.hashes)
        self.assertEqual(self.source.read_bytes(),b'synthetic source')
        self.assertFalse((self.root / '.bf/automation-backups').exists())

    def configure(self, value):
        self.config.parent.mkdir(parents=True,exist_ok=True)
        self.config.write_text(json.dumps(value,ensure_ascii=False),encoding='utf-8')
        self.mark()

    def add_folder(self,identifier,name='浏览器采集',pid=0,recycled=0):
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute('INSERT INTO bf_folder(id,pid,name,is_recycle) VALUES(?,?,?,?)',(identifier,pid,name,recycled))
        if pid == 0:
            (self.root / name).mkdir(exist_ok=True)
        self.mark()

    def test_path_preflight_validates_source_name_and_active_native_folder(self):
        result = import_preflight('items.import_path',{'source_path':str(self.source),'folder_id':'10','name':'new.png','origin':'local'},self.root)
        self.assertEqual(result,{'library_root':str(self.root),'path':str(self.source),'target_folder_id':10,'name':'new','expected_name':'new.png','origin':'local','extension':'.png','source_bytes':16})
        for args,code in [({'source_path':str(self.source),'folder_id':'999'},'folder_not_found'),
                          ({'source_path':str(self.source),'folder_id':'20'},'folder_not_writable'),
                          ({'source_path':str(self.source),'folder_id':str(2**31)},'invalid_arguments'),
                          ({'source_path':str(self.root)},'source_not_file'),
                          ({'source_path':str(self.source),'name':'../escape.png'},'invalid_arguments'),
                          ({'source_path':str(self.source),'name':'CON.png'},'invalid_arguments')]:
            with self.subTest(args=args),self.assertRaises(ApiError) as error:
                import_preflight('items.import_path',args,self.root)
            self.assertEqual(error.exception.code,code)

    def test_public_import_name_uses_native_stem_and_source_extension(self):
        result = import_preflight('items.import_path',{'source_path':str(self.source),'name':'AI材质'},self.root)
        self.assertEqual(result['name'],'AI材质')
        self.assertEqual(result['expected_name'],'AI材质.png')
        self.assertEqual(result['extension'],'.png')
        result = import_preflight('items.import_path',{'source_path':str(self.source)},self.root)
        self.assertEqual(result['name'],'source')
        self.assertEqual(result['expected_name'],'source.png')
        with self.assertRaises(ApiError):
            import_preflight('items.import_path',{'source_path':str(self.source),'name':'wrong.mp4'},self.root)
        result = import_preflight('items.import_path',{'source_path':str(self.source),'name':'case.PNG'},self.root)
        self.assertEqual(result['expected_name'],'case.png')
        no_suffix = Path(self.temp.name) / 'no_extension'
        no_suffix.write_bytes(b'synthetic')
        with self.assertRaises(ApiError):
            import_preflight('items.import_path',{'source_path':str(no_suffix)},self.root)


    def test_url_validation_precedes_download_and_preserves_origin(self):
        result = import_validate('items.import_url',{'url':'https://example.test/movie.mp4','folder_id':'10'},self.root)
        self.assertEqual(result['target_folder_id'],10)
        self.assertEqual(result['origin'],'https://example.test/movie.mp4')
        for args in [{'url':'file:///C:/private'}, {'url':'https://user:pass@example.test/'},
                     {'url':'https://example.test/','name':'bad?.mp4'}, {'url':'https://example.test/','folder_id':'999'}]:
            with self.subTest(args=args),self.assertRaises(ApiError):
                import_validate('items.import_url',args,self.root)

    def test_import_verify_native_id_duplicate_and_readiness_are_truthful(self):
        args = {'item_ids':['1','1'],'path':str(self.source),'extension':'.PNG','expected_name':'source.png','origin':'https://example.test/source','folder_id':'10'}
        result = import_verify('items.import_path',args,self.root)
        self.assertTrue(result['verified'])
        self.assertEqual(result['item_ids'],['1'])
        self.assertEqual(result['item_id'],'1')
        self.assertTrue(result['item']['metadata_ready'])
        self.assertFalse(result['item']['thumbnail_ready'])
        self.assertFalse(result['analysis_ready'])
        preview = self.root / '.bf/.preview/01/1.small.webp'
        preview.parent.mkdir(parents=True)
        preview.write_bytes(b'synthetic thumbnail')
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute('UPDATE bf_material_v2 SET thumb_tid=60 WHERE file_id=1')
        self.mark()
        self.assertTrue(import_verify('items.import_path',args,self.root)['analysis_ready'])
        mismatch = import_verify('items.import_path',{**args,'origin':'wrong','expected_name':'wrong.mp4'},self.root)
        self.assertFalse(mismatch['verified'])
        self.assertEqual({entry['field'] for entry in mismatch['mismatches']},{'origin','name'})

    def test_verify_rejects_unknown_native_result_and_absent_owned_file(self):
        result = import_verify('items.import_path',{'item_ids':['999']},self.root)
        self.assertFalse(result['verified'])
        self.assertEqual(result['items'],[])
        self.assertTrue(result['retryable'])
        with self.assertRaises(ApiError):
            import_verify('items.import_path',{'item_ids':[]},self.root)
        (self.root / 'active/source.png').unlink()
        result = import_verify('items.import_path',{'item_ids':['1']},self.root)
        self.assertFalse(result['verified'])
        self.assertEqual(result['mismatches'][0]['field'],'owned_path')

    def test_empty_import_origin_accepts_native_null_without_bypassing_real_origin(self):
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute('UPDATE bf_material_userdata SET origin=NULL WHERE file_id=1')
            db.execute('UPDATE bf_material_v2 SET w=0,h=0 WHERE file_id=1')
        self.mark()
        result = import_verify('items.import_path', {'item_ids':['1'], 'origin':''}, self.root)
        self.assertTrue(result['verified'], result)
        self.assertIsNone(result['item']['origin'])
        self.assertFalse(result['item']['metadata_ready'])
        self.assertFalse(result['item']['thumbnail_ready'])
        self.assertFalse(result['analysis_ready'])
        nonempty = import_verify('items.import_path', {'item_ids':['1'], 'origin':'https://example.test/source'}, self.root)
        self.assertFalse(nonempty['verified'])
        self.assertEqual(nonempty['mismatches'][0]['field'], 'origin')
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute("UPDATE bf_material_userdata SET origin='https://example.test/source' WHERE file_id=1")
        self.mark()
        unexpected = import_verify('items.import_path', {'item_ids':['1'], 'origin':''}, self.root)
        self.assertFalse(unexpected['verified'])
        self.assertEqual(unexpected['mismatches'][0]['field'], 'origin')

    def test_browser_configured_target_has_priority_over_same_named_candidate(self):
        self.add_folder(86)
        self.configure({'downloadFolderId':'10','downloadFolderName':'active'})
        result = browser_destination('browser.destination',{},self.root)
        self.assertEqual(result['status'],'configured')
        self.assertEqual(result['target_folder_id'],'10')
        self.assertIsInstance(result['target_folder_id'],str)
        self.assertFalse(result['needs_binding'])

    def test_browser_candidate_missing_and_ambiguous_do_not_guess(self):
        self.configure({'downloadFolderId':'20','downloadFolderName':'trash'})
        self.assertEqual(browser_destination('browser.destination',{},self.root)['status'],'needs_create')
        self.add_folder(86)
        result = browser_destination('browser.destination',{},self.root)
        self.assertEqual(result['status'],'candidate')
        self.assertEqual(result['target_folder_id'],'86')
        self.assertIsInstance(result['target_folder_id'],str)
        self.assertTrue(result['needs_binding'])
        self.add_folder(87)
        result = browser_destination('browser.destination',{},self.root)
        self.assertEqual(result['status'],'ambiguous')
        self.assertIsNone(result['target_folder_id'])
        self.assertEqual(result['candidate_ids'],['86','87'])

    def test_browser_actual_helper_json_preserves_decimal_string_folder_ids(self):
        self.add_folder(86)
        request = {'mode':'browser_destination','operation':'browser.destination','arguments':{},'library_root':str(self.root)}
        for configured, expected_status, expected_id in [({},'candidate','86'),
                ({'downloadFolderId':'10','downloadFolderName':'active'},'configured','10')]:
            self.configure(configured)
            with self.subTest(status=expected_status):
                result = subprocess.run([sys.executable,'-I',str(HELPER)],input=json.dumps(request).encode('utf-8'),capture_output=True,timeout=15)
                self.assertEqual(result.returncode,0)
                self.assertEqual(result.stderr,b'')
                response = json.loads(result.stdout)
                self.assertTrue(response['ok'])
                self.assertEqual(response['data']['status'],expected_status)
                self.assertEqual(response['data']['target_folder_id'],expected_id)
                self.assertIsInstance(response['data']['target_folder_id'],str)

    def test_internal_helper_fixed_import_modes_serialize_without_traceback(self):
        for mode,operation,args in [('import_preflight','items.import_path',{'source_path':str(self.source)}),
                                    ('import_validate','items.import_url',{'url':'https://example.test/x'}),
                                    ('import_verify','items.import_path',{'item_ids':['1']}),
                                    ('browser_destination','browser.destination',{})]:
            with self.subTest(mode=mode):
                request={'mode':mode,'operation':operation,'arguments':args,'library_root':str(self.root)}
                result=subprocess.run([sys.executable,'-I',str(HELPER)],input=json.dumps(request).encode('utf-8'),capture_output=True,timeout=15)
                self.assertEqual(result.returncode,0)
                self.assertEqual(result.stderr,b'')
                self.assertTrue(json.loads(result.stdout)['ok'])


if __name__ == '__main__':
    unittest.main()
