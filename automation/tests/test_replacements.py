"""Replacement contract against disposable schema fixtures; never the personal library."""
from contextlib import closing
import hashlib
import json
from pathlib import Path
import sqlite3
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

from billfish_api.errors import ApiError
from billfish_api.replacements import replace_preflight, replace_commit, replace_verify
from test_catalog import SCHEMA

HELPER = Path(__file__).resolve().parents[1] / 'billfish_host_helper.py'


def png(path, width, height, color):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress((b'\0' + bytes(color) * width) * height)) + chunk(b'IEND', b''))


class ReplacementTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='billfish-replace-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'library'
        (self.root / '.bf').mkdir(parents=True)
        self.source = Path(self.temp.name) / 'output.png'
        self.target = self.root / 'test.png'
        png(self.source, 32, 24, (240, 50, 10))
        png(self.target, 8, 6, (20, 30, 40))
        self.database = self.root / '.bf/billfish.db'
        with closing(sqlite3.connect(self.database)) as db, db:
            db.executescript(SCHEMA)
            db.execute('INSERT INTO bf_file(id,name,pid,is_hide,is_link,file_size,md5,tid,born,ttid) VALUES(1,?,0,0,0,?,?,10,123,0)',
                ('test.png', self.target.stat().st_size, hashlib.md5(self.target.read_bytes()).hexdigest().upper()))
            db.execute('INSERT INTO bf_material_v2(file_id,status,w,h,is_recycle,thumb_tid,image_tid) VALUES(1,1,8,6,0,0,0)')
            db.execute("INSERT INTO bf_material_userdata(file_id,note,origin,score) VALUES(1,'keep note','https://example.test',5)")
            db.execute("INSERT INTO bf_tag_v2(id,name) VALUES(3,'keep tag')")
            db.execute('INSERT INTO bf_tag_join_file(file_id,tag_id,born) VALUES(1,3,5)')
        self.original = self.target.read_bytes()
        self.db_bytes = self.database.read_bytes()

    def intent(self, **extra):
        return replace_preflight('items.replace_file', {'item_id': '1', 'source_path': str(self.source), **extra}, self.root)

    def test_dry_run_defaults_to_no_mutation_and_real_helper_decoder(self):
        request = {'mode': 'replace_preflight', 'operation': 'items.replace_file', 'library_root': str(self.root),
            'arguments': {'item_id': '1', 'source_path': str(self.source)}}
        result = subprocess.run([sys.executable, '-I', str(HELPER)], input=json.dumps(request).encode(), capture_output=True, timeout=30)
        response = json.loads(result.stdout)
        self.assertTrue(response['ok'], response)
        intent = response['data']
        self.assertTrue(intent['dry_run'])
        self.assertEqual((intent['expected']['width'], intent['expected']['height']), (32, 24))
        with self.assertRaises(ApiError):
            replace_commit('items.replace_file', {'intent': intent}, self.root)
        self.assertEqual(self.target.read_bytes(), self.original)
        self.assertEqual(self.database.read_bytes(), self.db_bytes)
        self.assertFalse((self.root / '.bf/automation-replacements').exists())

    def test_atomic_owned_replace_preserves_id_metadata_and_retains_recovery(self):
        intent = self.intent(dry_run=False)
        committed = replace_commit('items.replace_file', {'intent': intent}, self.root)
        self.assertTrue(committed['file_replaced'])
        self.assertEqual(self.target.read_bytes(), self.source.read_bytes())
        self.assertEqual(Path(committed['backup_path']).read_bytes(), self.original)
        self.assertEqual(self.database.read_bytes(), self.db_bytes)
        stale = replace_verify('items.replace_file', {'intent': committed}, self.root)
        self.assertFalse(stale['verified'])
        self.assertTrue(stale['retryable'])
        self.assertIn('file_metadata', stale['mismatches'])
        # Model only the native persistence result; the replacement code never writes SQL.
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute('UPDATE bf_file SET file_size=?,md5=? WHERE id=1', (self.target.stat().st_size, committed['expected']['md5']))
            db.execute('UPDATE bf_material_v2 SET w=32,h=24 WHERE file_id=1')
        result = replace_verify('items.replace_file', {'intent': committed}, self.root)
        self.assertTrue(result['verified'], result)
        self.assertEqual(result['completion'], 'native_replace_persisted')
        self.assertEqual(result['item']['id'], '1')
        self.assertEqual(result['item']['note'], 'keep note')
        self.assertEqual(result['item']['tags'][0]['id'], '3')
        self.assertEqual(json.loads((Path(committed['workspace']) / 'state.json').read_text())['state'], 'native_replace_persisted')
        with closing(sqlite3.connect(self.database)) as db, db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM bf_file').fetchone()[0], 1)

    def test_source_and_target_changes_prevent_replacement(self):
        intent = self.intent(dry_run=False)
        self.target.write_bytes(b'changed by another actor')
        with self.assertRaises(ApiError) as error:
            replace_commit('items.replace_file', {'intent': intent}, self.root)
        self.assertEqual(error.exception.code, 'replacement_target_changed')
        self.assertEqual(self.target.read_bytes(), b'changed by another actor')
        self.assertEqual(self.database.read_bytes(), self.db_bytes)

    def test_extensions_encoding_and_owned_item_are_enforced(self):
        wrong = self.source.with_suffix('.jpg')
        wrong.write_bytes(self.source.read_bytes())
        with self.assertRaises(ApiError) as error:
            replace_preflight('items.replace_file', {'item_id': '1', 'source_path': str(wrong)}, self.root)
        self.assertEqual(error.exception.code, 'replacement_extension_mismatch')
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute('UPDATE bf_file SET is_link=1 WHERE id=1')
        with self.assertRaises(ApiError) as error:
            self.intent()
        self.assertEqual(error.exception.code, 'replacement_not_owned')
        self.assertEqual(self.target.read_bytes(), self.original)

    def test_atomic_failure_keeps_original_and_backup_without_claiming_rollback(self):
        intent = self.intent(dry_run=False)
        real_replace = __import__('os').replace
        def fail_target(source, target):
            if Path(target) == self.target:
                raise PermissionError('fixture denied atomic replacement')
            return real_replace(source, target)
        with patch('billfish_api.replacements.os.replace', side_effect=fail_target):
            with self.assertRaises(ApiError) as error:
                replace_commit('items.replace_file', {'intent': intent}, self.root)
        self.assertFalse(error.exception.details['file_replaced'])
        self.assertFalse(error.exception.details['rollback'])
        self.assertEqual(self.target.read_bytes(), self.original)
        self.assertEqual(Path(intent['backup_path']).read_bytes(), self.original)
        self.assertEqual(self.database.read_bytes(), self.db_bytes)

    def test_inactive_thumbnail_cache_is_not_mistaken_for_active_preview(self):
        # Native thumb_tid=0 uses the source even when an old cache file remains.
        cache = self.root / '.bf/.preview/01/1.small.webp'
        cache.parent.mkdir(parents=True)
        cache.write_bytes(b'old inactive stale thumbnail')
        self.source.write_bytes(self.target.read_bytes())
        same = self.intent(dry_run=False)
        result = replace_commit('items.replace_file', {'intent': same}, self.root)
        verified = replace_verify('items.replace_file', {'intent': result}, self.root)
        self.assertTrue(verified['analysis_ready'])
        self.assertFalse(result['file_replaced'])
        self.assertIsNone(verified['item']['thumbnail_path'])
        self.assertIsNone(verified['item']['preview_path'])

    def test_owned_target_cannot_be_claimed_safe_to_delete_as_source(self):
        with self.assertRaises(ApiError) as error:
            replace_preflight('items.replace_file', {'item_id': '1', 'source_path': str(self.target), 'dry_run': False}, self.root)
        self.assertEqual(error.exception.code, 'replacement_source_is_target')
        self.assertFalse(error.exception.details['source_safe_to_delete'])
        self.assertEqual(self.target.read_bytes(), self.original)

    def test_active_thumbnail_not_yet_decodable_remains_pending(self):
        cache = self.root / '.bf/.preview/01/1.small.webp'
        cache.parent.mkdir(parents=True)
        cache.write_bytes(b'native cache not finished')
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute('UPDATE bf_material_v2 SET thumb_tid=60 WHERE file_id=1')
        self.source.write_bytes(self.target.read_bytes())
        committed = replace_commit('items.replace_file', {'intent': self.intent(dry_run=False)}, self.root)
        result = replace_verify('items.replace_file', {'intent': committed}, self.root)
        self.assertFalse(result['analysis_ready'])
        self.assertTrue(result['retryable'])
        self.assertIn('thumbnail_decode_pending', result['mismatches'])


if __name__ == '__main__':
    unittest.main()
