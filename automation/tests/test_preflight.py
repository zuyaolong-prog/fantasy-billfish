"""Online preflight and internal-pipe contracts against a synthetic v41 catalog."""

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
from billfish_api.preflight import preflight, verify
from test_catalog import SCHEMA


HELPER = Path(__file__).resolve().parents[1] / 'billfish_host_helper.py'


class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='billfish-preflight-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / '中文素材'
        (self.root / '.bf').mkdir(parents=True)
        self.database = self.root / '.bf/billfish.db'
        with closing(sqlite3.connect(self.database)) as db, db:
            db.executescript(SCHEMA)
            db.execute('CREATE TABLE bf_type(tid INTEGER PRIMARY KEY,name TEXT)')
            db.execute("INSERT INTO bf_type VALUES(1,'png')")
            db.execute("INSERT INTO bf_folder(id,pid,name,is_recycle) VALUES(10,0,'folder',0),(20,0,'trash',1)")
            db.executemany('INSERT INTO bf_file(id,name,pid,is_link,tid) VALUES(?,?,?,?,1)',
                           [(1,'one.png',10,0),(2,'two.png',10,0),(3,'trash.png',20,0),(4,'link.png',10,1),(5,'absent.png',10,0)])
            db.executemany('INSERT INTO bf_material_v2(file_id,w,h,is_recycle) VALUES(?,1,1,0)', [(i,) for i in range(1,6)])
            db.execute("INSERT INTO bf_material_userdata(file_id,note,origin) VALUES(1,'旧备注','keep')")
            db.execute("INSERT INTO bf_tag_v2 VALUES(100,'现有标签',0,5,0,0,0)")
            db.execute('INSERT INTO bf_tag_join_file VALUES(1,100,0)')
        (self.root / 'folder').mkdir()
        for name in ('one.png','two.png','link.png'):
            (self.root / 'folder' / name).write_bytes(b'synthetic')
        self.before = hashlib.sha256(self.database.read_bytes()).hexdigest()

    def tearDown(self):
        self.assertEqual(self.before, hashlib.sha256(self.database.read_bytes()).hexdigest())
        self.assertFalse((self.root / '.bf/summary_v2.db').exists())
        self.assertFalse((self.root / '.bf/automation-backups').exists())

    def call(self, operation, arguments):
        return preflight(operation, arguments, self.root)

    def helper(self, request):
        result = subprocess.run([sys.executable,'-I',str(HELPER)], input=json.dumps(request,ensure_ascii=False).encode('utf-8'), capture_output=True, timeout=15)
        self.assertEqual(result.returncode,0,result.stderr.decode('utf-8','replace'))
        self.assertEqual(result.stderr,b'')
        return json.loads(result.stdout.decode('utf-8'))

    def test_update_normalizes_changed_fields_and_keeps_readback_ids(self):
        result = self.call('items.update',{'updates':[{'item_id':'1','note':'旧备注','origin':'新来源'}, {'item_id':'2','note':'新备注'}],'dry_run':False})
        self.assertEqual(result['arguments'], {'updates':[{'item_id':'1','origin':'新来源'},{'item_id':'2','note':'新备注'}]})
        self.assertEqual(result['requested_ids'],['1','2'])
        self.assertEqual(result['modified_count'],2)
        self.assertEqual(result['plan'][0]['before'],{'note':'旧备注','origin':'keep'})
        result = self.call('items.update',{'updates':[{'item_id':'1','note':'旧备注'}]})
        self.assertEqual(result['arguments'],{'updates':[]})
        self.assertEqual(result['modified_count'],0)
        self.assertEqual(result['requested_ids'],['1'])

    def test_entire_batch_missing_recycled_link_missing_path_rejected(self):
        for identifier, code in [('999','item_not_found'),('3','item_not_writable'),('4','item_not_writable'),('5','item_not_writable')]:
            with self.subTest(identifier=identifier), self.assertRaises(ApiError) as error:
                self.call('items.update',{'updates':[{'item_id':'1','note':'change'},{'item_id':identifier,'note':'x'}]})
            self.assertEqual(error.exception.code,code)

    def test_invalid_batch_duplicate_ids_and_empty_patch_rejected(self):
        invalid = [{'updates':[]}, {'updates':[{'item_id':'1'}]}, {'updates':[{'item_id':'1','note':'x'}]*2},
                   {'updates':[{'item_id':'1','score':1}]}, {'updates':[{'item_id':'1','note':'\0'}]},
                   {'updates':[{'item_id':1,'note':'x'}]}, {'updates':[{'item_id':'1','note':'x'}]*501}]
        for args in invalid:
            with self.subTest(args=str(args)[:100]), self.assertRaises(ApiError):
                self.call('items.update',args)

    def test_create_tag_checks_existing_parent_and_same_parent_duplicates(self):
        result = self.call('tags.create',{'name':'新标签'})
        self.assertEqual(result['arguments'],{'name':'新标签','pid':'0'})
        self.assertEqual(result['plan']['seq'],6)
        self.assertEqual(result['modified_count'],1)
        self.assertEqual(self.call('tags.create',{'name':'现有标签','pid':'100'})['arguments']['pid'],'100')
        for args,code in [({'name':'现有标签'},'tag_exists'),({'name':'x','pid':'999'},'tag_not_found'),({'name':'   '},'invalid_arguments')]:
            with self.subTest(args=args), self.assertRaises(ApiError) as error:
                self.call('tags.create',args)
            self.assertEqual(error.exception.code,code)

    def test_assign_plans_exact_changed_pairs_and_keeps_all_readback_ids(self):
        result = self.call('tags.assign',{'item_ids':['1','2'],'tag_ids':['100']})
        self.assertEqual(result['pairs'],[{'item_id':'2','tag_id':'100','action':'add'}])
        self.assertEqual(result['modified_count'],1)
        self.assertEqual(result['requested_ids'],['1','2'])
        result = self.call('tags.assign',{'item_ids':['1','2'],'tag_ids':['100'],'mode':'remove'})
        self.assertEqual(result['pairs'],[{'item_id':'1','tag_id':'100','action':'remove'}])
        for args in [{'item_ids':['1','1'],'tag_ids':['100']}, {'item_ids':['1'],'tag_ids':['100','100']}, {'item_ids':['1'],'tag_ids':['999']}]:
            with self.assertRaises(ApiError):
                self.call('tags.assign',args)

    def test_helper_preflight_and_read_have_one_utf8_envelope(self):
        result = self.helper({'operation':'items.update','arguments':{'updates':[{'item_id':'1','note':'中文管道'}]},'library_root':str(self.root),'mode':'preflight'})
        self.assertTrue(result['ok'])
        self.assertEqual(result['data']['arguments']['updates'][0]['note'],'中文管道')
        for operation,args in [('items.get',{'item_id':'1'}),('library.info',{}),('items.search',{'query':'旧备注'}),('folders.list',{}),('tags.list',{})]:
            with self.subTest(operation=operation):
                self.assertTrue(self.helper({'operation':operation,'arguments':args,'library_root':str(self.root),'mode':'read'})['ok'])

    def test_helper_rejects_cross_mode_unknown_fields_and_sql(self):
        for request in [{'operation':'items.update','arguments':{},'library_root':str(self.root),'mode':'read'},
                        {'operation':'items.get','arguments':{'item_id':'1'},'library_root':str(self.root),'mode':'preflight'},
                        {'operation':'sql.execute','arguments':{'sql':'DROP TABLE library'},'library_root':str(self.root),'mode':'read'},
                        {'operation':'items.get','arguments':{'item_id':'1'},'library_root':str(self.root),'mode':'read','extra':True}]:
            with self.subTest(request=request):
                self.assertFalse(self.helper(request)['ok'])

    def test_verify_updates_compare_only_requested_fields_and_observe_fresh_commit(self):
        args = {'updates':[{'item_id':'1','note':'new note'}]}
        result = verify('items.update',args,self.root)
        self.assertFalse(result['verified'])
        self.assertTrue(result['retryable'])
        self.assertEqual(result['mismatches'],[{'item_id':'1','field':'note','expected':'new note','actual':'旧备注'}])
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute("UPDATE bf_material_userdata SET note='new note',origin='unrelated edit' WHERE file_id=1")
        self.before = hashlib.sha256(self.database.read_bytes()).hexdigest()
        result = self.helper({'operation':'items.update','arguments':args,'library_root':str(self.root),'mode':'verify'})
        self.assertTrue(result['ok'])
        self.assertTrue(result['data']['verified'])
        self.assertEqual(result['data']['mismatches'],[])

    def test_verify_assignment_checks_requested_pairs_in_both_directions(self):
        add = {'item_ids':['1','2'],'tag_ids':['100'],'mode':'add'}
        remove = {'item_ids':['1'],'tag_ids':['100'],'mode':'remove'}
        self.assertFalse(verify('tags.assign',add,self.root)['verified'])
        self.assertFalse(verify('tags.assign',remove,self.root)['verified'])
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute('DELETE FROM bf_tag_join_file WHERE file_id=1 AND tag_id=100')
            db.execute('INSERT INTO bf_tag_join_file VALUES(2,100,0)')
            db.execute('INSERT INTO bf_tag_join_file VALUES(2,999,0)')
        self.before = hashlib.sha256(self.database.read_bytes()).hexdigest()
        self.assertTrue(verify('tags.assign',remove,self.root)['verified'])
        self.assertTrue(verify('tags.assign',{'item_ids':['2'],'tag_ids':['100']},self.root)['verified'])
        result = verify('tags.assign',add,self.root)
        self.assertEqual(result['mismatches'],[{'item_id':'1','tag_id':'100','expected':True,'actual':False}])

    def test_verify_create_returns_unique_id_and_stops_on_duplicate_matches(self):
        args = {'name':'delayed tag','pid':'0'}
        result = verify('tags.create',args,self.root)
        self.assertFalse(result['verified'])
        self.assertTrue(result['retryable'])
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute("INSERT INTO bf_tag_v2 VALUES(101,'delayed tag',0,1,0,0,0)")
        self.before = hashlib.sha256(self.database.read_bytes()).hexdigest()
        result = verify('tags.create',args,self.root)
        self.assertTrue(result['verified'])
        self.assertEqual(result['tag']['id'],'101')
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute("INSERT INTO bf_tag_v2 VALUES(102,'delayed tag',0,2,0,0,0)")
        self.before = hashlib.sha256(self.database.read_bytes()).hexdigest()
        result = self.helper({'operation':'tags.create','arguments':args,'library_root':str(self.root),'mode':'verify'})['data']
        self.assertFalse(result['verified'])
        self.assertFalse(result['retryable'])
        self.assertEqual(result['mismatches'][0]['reason'],'duplicate_tag')
        self.assertEqual(result['mismatches'][0]['tag_ids'],['101','102'])

    def test_selection_hydrates_native_ids_in_order_and_accepts_empty_view(self):
        request = {'operation':'items.selected','arguments':{'item_ids':['2','1']},'library_root':str(self.root),'mode':'selection'}
        result = self.helper(request)
        self.assertTrue(result['ok'])
        self.assertEqual(result['data']['total'],2)
        self.assertEqual([item['id'] for item in result['data']['items']],['2','1'])
        self.assertEqual(result['data']['items'][1]['note'],'旧备注')
        self.assertEqual(result['data']['library_root'],str(self.root))
        request['arguments'] = {'item_ids':[]}
        self.assertEqual(self.helper(request)['data'],{'items':[],'total':0,'library_root':str(self.root)})

    def test_selection_rejects_invalid_ids_and_stale_selected_item(self):
        for args,code in [({'item_ids':['1','1']},'invalid_arguments'),({'item_ids':['999']},'item_not_found'),
                          ({'item_ids':[1]},'invalid_arguments'),({'item_ids':['9223372036854775808']},'invalid_arguments'),
                          ({'item_ids':['1'],'query':'untrusted'},'invalid_arguments')]:
            with self.subTest(args=args):
                result = self.helper({'operation':'items.selected','arguments':args,'library_root':str(self.root),'mode':'selection'})
                self.assertFalse(result['ok'])
                self.assertEqual(result['error']['code'],code)

    def test_selection_mode_does_not_expose_ids_through_read_or_other_operations(self):
        for operation,mode,args in [('items.selected','read',{'item_ids':['1']}),('items.get','selection',{'item_ids':['1']})]:
            with self.subTest(operation=operation,mode=mode):
                result = self.helper({'operation':operation,'arguments':args,'library_root':str(self.root),'mode':mode})
                self.assertFalse(result['ok'])
                self.assertEqual(result['error']['code'],'unknown_operation')

    def test_helper_invalid_json_has_structured_error_without_traceback(self):
        result = subprocess.run([sys.executable,'-I',str(HELPER)],input=b'{broken',capture_output=True,timeout=15)
        self.assertEqual(json.loads(result.stdout)['error']['code'],'invalid_json')
        self.assertEqual(result.stderr,b'')


if __name__ == '__main__':
    unittest.main()
