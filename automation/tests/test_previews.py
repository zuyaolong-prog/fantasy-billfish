"""Native format plans and cover verification use disposable libraries only."""
from contextlib import closing
import hashlib
import ctypes
import json
import os
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest import mock

from billfish_api.errors import ApiError
from billfish_api.previews import animation_visibility, preview_items, preview_prepare, preview_publish, preview_verify, preview_types
from test_catalog import SCHEMA
from test_replacements import png


class PreviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='billfish-preview-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'library'
        (self.root / '.bf').mkdir(parents=True)
        self.model = self.root / 'cube.glb'
        self.model.write_bytes(b'fixture-model-original-bytes')
        self.staging = Path(self.temp.name) / 'plugin/preview-staging'
        self.staging.mkdir(parents=True)
        self.output = self.staging / 'cube.png'
        png(self.output, 48, 32, (40, 100, 180))
        self.database = self.root / '.bf/billfish.db'
        with closing(sqlite3.connect(self.database)) as db, db:
            db.executescript(SCHEMA)
            db.execute('CREATE TABLE bf_type(tid INTEGER PRIMARY KEY,name TEXT)')
            db.execute("INSERT INTO bf_type VALUES(10,'png')")
            db.execute("INSERT INTO bf_type VALUES(60,'webp')")
            db.execute("INSERT INTO bf_file(id,name,pid,is_link,file_size) VALUES(1,'cube.glb',0,0,?)", (self.model.stat().st_size,))
            db.execute('INSERT INTO bf_material_v2(file_id,w,h,is_recycle,thumb_tid,image_tid) VALUES(1,0,0,0,0,0)')
            db.execute("INSERT INTO bf_material_userdata(file_id,note,score,origin,cover_tid) VALUES(1,'keep note',3,'keep source','0')")
            db.execute("INSERT INTO bf_tag_v2(id,name) VALUES(3,'keep tag')")
            db.execute('INSERT INTO bf_tag_join_file(file_id,tag_id,born) VALUES(1,3,123)')
        self.original = self.model.read_bytes()
        self.database_bytes = self.database.read_bytes()

    def prepare(self):
        return preview_prepare('plugins.thumbnail', {'item_id': '1'}, self.root)

    def publish(self, intent=None, dest=None):
        return preview_publish('plugins.thumbnail', {'intent': intent or self.prepare(),
            'dest': str(dest or self.output), 'staging_root': str(self.staging)}, self.root)

    def frame_intent(self, expected=None):
        source = self.prepare()['source']
        fingerprint = {'sha256':source['sha256'], 'bytes':str(source['bytes']), 'mtime_ns':source['mtime_ns']}
        return preview_prepare('plugins.set_cover', {'item_id':'1', 'expected_source':expected or fingerprint}, self.root)

    def core_model(self, extension='gltf'):
        target = self.root / ('cube.' + extension)
        self.model.rename(target)
        self.model = target
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute('UPDATE bf_file SET name=? WHERE id=1', (target.name,))
        self.database_bytes = self.database.read_bytes()
        source = self.prepare()['source']
        fingerprint = {'sha256': source['sha256'], 'bytes': str(source['bytes']), 'mtime_ns': source['mtime_ns']}
        binding = {'path': str(target), 'rowId': 'core-embedded-0', 'clip_index': 0, 'fingerprint': fingerprint}
        return fingerprint, binding

    def test_plan_and_decode_leave_source_and_database_unchanged(self):
        intent = self.prepare()
        self.assertFalse(intent['skipped'])
        self.assertEqual(intent['item']['path'], str(self.model))
        published = self.publish(intent)
        self.assertEqual(published['image'], {'width': 48, 'height': 32, 'codec': 'png'})
        self.assertEqual(published['sha256'], hashlib.sha256(self.output.read_bytes()).hexdigest())
        result = preview_verify('plugins.thumbnail', {'published': published}, self.root)
        self.assertFalse(result['verified'])
        self.assertEqual(self.model.read_bytes(), self.original)
        self.assertEqual(self.database.read_bytes(), self.database_bytes)

    def test_external_animation_changes_reject_publication_and_verification(self):
        animation = Path(self.temp.name) / 'walk.fbx'
        animation.write_bytes(b'original-animation')
        stat = animation.stat()
        binding = {'path': str(animation), 'rowId': 'walk-row', 'clip_index': 0,
                   'fingerprint': {'sha256': hashlib.sha256(animation.read_bytes()).hexdigest(),
                                   'bytes': str(stat.st_size), 'mtime_ns': str(stat.st_mtime_ns)}}
        source = self.prepare()['source']
        intent = preview_prepare('plugins.set_cover', {'item_id': '1', 'animation_source': binding,
            'expected_source': {'sha256': source['sha256'], 'bytes': str(source['bytes']), 'mtime_ns': source['mtime_ns']}}, self.root)
        published = preview_publish('plugins.set_cover', {'intent': intent, 'dest': str(self.output),
                                    'staging_root': str(self.staging)}, self.root)
        animation.write_bytes(b'changed-animation')
        for operation in (lambda: preview_publish('plugins.set_cover', {'intent': intent, 'dest': str(self.output),
                                'staging_root': str(self.staging)}, self.root),
                          lambda: preview_verify('plugins.set_cover', {'published': published}, self.root)):
            with self.assertRaises(ApiError) as error:
                operation()
            self.assertEqual(error.exception.code, 'animation_source_changed')
        self.assertEqual(self.database.read_bytes(), self.database_bytes)

    def test_core_embedded_gltf_and_bvh_cover_bind_to_the_verified_base_file(self):
        for extension in ('gltf', 'bvh'):
            with self.subTest(extension=extension):
                expected, binding = self.core_model(extension)
                intent = preview_prepare('plugins.set_cover', {'item_id': '1', 'expected_source': expected,
                    'animation_source': binding}, self.root)
                self.assertEqual(intent['animation_source'], binding)
                published = preview_publish('plugins.set_cover', {'intent': intent, 'dest': str(self.output),
                    'staging_root': str(self.staging)}, self.root)
                verified = preview_verify('plugins.set_cover', {'published': published}, self.root)
                self.assertFalse(verified['verified'])  # No native cover was written by this read-only test.
                self.assertEqual(self.model.read_bytes(), self.original)
                self.assertEqual(self.database.read_bytes(), self.database_bytes)

    def test_core_embedded_cover_rejects_an_external_file_even_with_identical_bytes(self):
        expected, binding = self.core_model()
        external = Path(self.temp.name) / 'external.gltf'
        external.write_bytes(self.original)
        stat = external.stat()
        binding = {**binding, 'path': str(external), 'fingerprint': {
            'sha256': hashlib.sha256(external.read_bytes()).hexdigest(),
            'bytes': str(stat.st_size), 'mtime_ns': str(stat.st_mtime_ns)}}
        with self.assertRaises(ApiError) as error:
            preview_prepare('plugins.set_cover', {'item_id': '1', 'expected_source': expected,
                'animation_source': binding}, self.root)
        self.assertEqual(error.exception.code, 'invalid_animation_source')
        self.assertEqual(self.database.read_bytes(), self.database_bytes)

    def test_external_fbx_and_glb_animation_bindings_remain_supported(self):
        expected, _ = self.core_model()
        for extension in ('fbx', 'glb'):
            with self.subTest(extension=extension):
                animation = Path(self.temp.name) / ('external.' + extension)
                animation.write_bytes(b'existing external animation behavior')
                stat = animation.stat()
                binding = {'path': str(animation), 'rowId': 'external-row', 'clip_index': 1,
                    'fingerprint': {'sha256': hashlib.sha256(animation.read_bytes()).hexdigest(),
                        'bytes': str(stat.st_size), 'mtime_ns': str(stat.st_mtime_ns)}}
                intent = preview_prepare('plugins.set_cover', {'item_id': '1', 'expected_source': expected,
                    'animation_source': binding}, self.root)
                self.assertEqual(intent['animation_source'], binding)
                published = preview_publish('plugins.set_cover', {'intent': intent, 'dest': str(self.output),
                    'staging_root': str(self.staging)}, self.root)
                self.assertFalse(preview_verify('plugins.set_cover', {'published': published}, self.root)['verified'])

    def test_core_embedded_cover_rechecks_base_changes_and_binding_fingerprint(self):
        expected, binding = self.core_model()
        with self.assertRaises(ApiError) as error:
            preview_prepare('plugins.set_cover', {'item_id': '1', 'expected_source': expected,
                'animation_source': {**binding, 'fingerprint': {**expected, 'sha256': '0' * 64}}}, self.root)
        self.assertEqual(error.exception.code, 'invalid_animation_source')
        intent = preview_prepare('plugins.set_cover', {'item_id': '1', 'expected_source': expected,
            'animation_source': binding}, self.root)
        published = preview_publish('plugins.set_cover', {'intent': intent, 'dest': str(self.output),
            'staging_root': str(self.staging)}, self.root)
        self.model.write_bytes(b'changed Core source')
        for operation in (
            lambda: preview_prepare('plugins.set_cover', {'item_id': '1', 'expected_source': expected, 'animation_source': binding}, self.root),
            lambda: preview_publish('plugins.set_cover', {'intent': intent, 'dest': str(self.output), 'staging_root': str(self.staging)}, self.root),
            lambda: preview_verify('plugins.set_cover', {'published': published}, self.root),
        ):
            with self.assertRaises(ApiError):
                operation()

    def test_core_embedded_cover_does_not_open_arbitrary_extensions(self):
        expected, binding = self.core_model('txt')
        with self.assertRaises(ApiError) as error:
            preview_prepare('plugins.set_cover', {'item_id': '1', 'expected_source': expected,
                'animation_source': binding}, self.root)
        self.assertEqual(error.exception.code, 'invalid_animation_source')

    def test_core_embedded_cover_requires_valid_clip_identity(self):
        expected, binding = self.core_model()
        for invalid in ({**binding, 'rowId': ''}, {**binding, 'clip_index': -1},
                        {**binding, 'clip_index': True}, {**binding, 'path': self.model.name}):
            with self.subTest(binding=invalid), self.assertRaises(ApiError):
                preview_prepare('plugins.set_cover', {'item_id': '1', 'expected_source': expected,
                    'animation_source': invalid}, self.root)

    def test_external_animation_hash_read_may_update_access_time_without_invalidating_cover(self):
        animation = Path(self.temp.name) / 'walk.fbx'
        animation.write_bytes(b'unchanged-animation')
        stat = animation.stat()
        binding = {'path': str(animation), 'rowId': 'walk-row', 'clip_index': 0,
                   'fingerprint': {'sha256': hashlib.sha256(animation.read_bytes()).hexdigest(),
                                   'bytes': str(stat.st_size), 'mtime_ns': str(stat.st_mtime_ns)}}
        source = self.prepare()['source']
        from billfish_api.previews import _digest

        def hash_with_access_update(path):
            digest = _digest(path)
            if path == animation:
                current = path.stat()
                os.utime(path, ns=(current.st_atime_ns + 10_000_000, current.st_mtime_ns))
            return digest

        with mock.patch('billfish_api.previews._digest', side_effect=hash_with_access_update):
            intent = preview_prepare('plugins.set_cover', {'item_id': '1', 'animation_source': binding,
                'expected_source': {'sha256': source['sha256'], 'bytes': str(source['bytes']), 'mtime_ns': source['mtime_ns']}}, self.root)
            published = preview_publish('plugins.set_cover', {'intent': intent, 'dest': str(self.output),
                                        'staging_root': str(self.staging)}, self.root)
            self.assertTrue(preview_verify('plugins.set_cover', {'published': published}, self.root)['source_unchanged'])
        self.assertNotEqual(animation.stat().st_atime_ns, stat.st_atime_ns)
        self.assertEqual(animation.stat().st_mtime_ns, stat.st_mtime_ns)
        self.assertEqual(animation.read_bytes(), b'unchanged-animation')
        self.assertEqual(self.database.read_bytes(), self.database_bytes)

    def test_explicit_regeneration_replaces_cover_while_automatic_preserves_it(self):
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute("UPDATE bf_material_userdata SET cover_tid='10' WHERE file_id=1")
        source = self.prepare()['source']
        self.assertTrue(self.prepare()['skipped'])
        intent = preview_prepare('plugins.regenerate_thumbnail', {'item_id': '1', 'expected_source': {
            'sha256': source['sha256'], 'bytes': str(source['bytes']), 'mtime_ns': source['mtime_ns']}}, self.root)
        self.assertFalse(intent['skipped'])
        published = preview_publish('plugins.regenerate_thumbnail', {'intent': intent, 'dest': str(self.output),
                                    'staging_root': str(self.staging)}, self.root)
        self.assertIn('cover_path', published)

    def test_visibility_requires_unique_active_owned_full_path_and_never_writes_catalog(self):
        external = Path(self.temp.name) / 'cube.glb'
        external.write_bytes(b'external same basename')
        result = animation_visibility('animation.visibility', {'hidden_paths': [str(self.model), str(external)]}, self.root)
        self.assertEqual(result['hidden_item_ids'], ['1'])
        self.assertEqual(result['resolved_paths'], [{'path': str(self.model), 'item_id': '1'}])
        self.assertEqual(result['unresolved_paths'], [str(external)])
        self.assertEqual(self.database.read_bytes(), self.database_bytes)
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute('UPDATE bf_material_v2 SET is_recycle=1 WHERE file_id=1')
        self.assertEqual(animation_visibility('animation.visibility', {'hidden_paths': [str(self.model)]}, self.root)['hidden_item_ids'], [])

    def test_fresh_native_cover_tid_type_and_bytes_are_all_required(self):
        published = self.publish()
        cover = self.root / '.bf/.preview/01/1.cover.png'
        cover.parent.mkdir(parents=True)
        cover.write_bytes(self.output.read_bytes())
        # Retained cache bytes with inactive cover_tid do not prove completion.
        self.assertFalse(preview_verify('plugins.thumbnail', {'published': published}, self.root)['verified'])
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute("UPDATE bf_material_userdata SET cover_tid='10' WHERE file_id=1")
        result = preview_verify('plugins.thumbnail', {'published': published}, self.root)
        self.assertTrue(result['verified'], result)
        self.assertEqual(result['cover_path'], str(cover))
        self.assertTrue(result['metadata_unchanged'])
        self.assertEqual(self.model.read_bytes(), self.original)
        cover.write_bytes(b'wrong PNG')
        self.assertFalse(preview_verify('plugins.thumbnail', {'published': published}, self.root)['verified'])

    def test_existing_cover_is_preserved_even_when_cache_is_missing(self):
        intent = self.prepare()
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute("UPDATE bf_material_userdata SET cover_tid='10' WHERE file_id=1")
        self.assertTrue(self.prepare()['skipped'])
        result = self.publish(intent)
        self.assertTrue(result['skipped'])
        self.assertEqual(result['reason'], 'existing_cover')

    def test_native_cover_palette_and_empty_userdata_storage_are_derived_only(self):
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute('DELETE FROM bf_material_userdata WHERE file_id=1')
            db.execute("UPDATE bf_material_v2 SET colors='' WHERE file_id=1")
        published = self.publish()
        self.assertEqual(published['intent']['preserved']['userdata'], [])
        cover = self.root / '.bf/.preview/01/1.cover.png'
        cover.parent.mkdir(parents=True)
        cover.write_bytes(self.output.read_bytes())
        # Model only the observed native action result: it creates an otherwise
        # empty userdata row and derives a color palette from the new cover.
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute("INSERT INTO bf_material_userdata(file_id,cover_tid) VALUES(1,'10')")
            db.execute("UPDATE bf_material_v2 SET colors='56.02,12433837|0.00,0' WHERE file_id=1")
        result = preview_verify('plugins.thumbnail', {'published': published}, self.root)
        self.assertTrue(result['verified'], result)
        self.assertTrue(result['metadata_unchanged'])
        self.assertEqual(self.model.read_bytes(), self.original)
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute("UPDATE bf_material_userdata SET note='user note must still be protected' WHERE file_id=1")
        with self.assertRaises(ApiError) as raised:
            preview_verify('plugins.thumbnail', {'published': published}, self.root)
        self.assertEqual(raised.exception.code, 'preview_source_changed')

    def test_source_and_metadata_changes_reject_publication(self):
        intent = self.prepare()
        self.model.write_bytes(b'changed model')
        with self.assertRaises(ApiError) as raised:
            self.publish(intent)
        self.assertEqual(raised.exception.code, 'preview_source_changed')
        self.model.write_bytes(self.original)
        intent = self.prepare()
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute("UPDATE bf_material_userdata SET note='user changed it' WHERE file_id=1")
        with self.assertRaises(ApiError):
            self.publish(intent)

    def test_staging_boundary_real_decoder_and_string_id(self):
        outside = Path(self.temp.name) / 'outside.png'
        outside.write_bytes(self.output.read_bytes())
        with self.assertRaises(ApiError):
            self.publish(dest=outside)
        self.output.write_bytes(b'not an image')
        with self.assertRaises(ApiError):
            self.publish()
        with self.assertRaises(ApiError):
            preview_prepare('plugins.preview', {'item_id': 1}, self.root)

    def test_visible_items_have_stamp_and_missing_source_is_skipped(self):
        items = preview_items('plugins.thumbnail', {'item_ids': ['1']}, self.root)['items']
        self.assertEqual(len(items), 1)
        self.assertTrue(items[0]['source_stamp'])
        self.model.unlink()
        self.assertEqual(preview_items('plugins.thumbnail', {'item_ids': ['1']}, self.root)['items'], [])

    def test_native_type_prepare_and_fresh_registration_readback_are_read_only(self):
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute('ALTER TABLE bf_type ADD COLUMN gid INTEGER DEFAULT 0')
            db.execute('ALTER TABLE bf_type ADD COLUMN can_analyze INTEGER DEFAULT 0')
        before = self.database.read_bytes()
        args = {'extensions':['png', 'glb', 'fbx']}
        plan = preview_types('types.add', args, self.root)
        self.assertEqual(plan['missing'], ['glb', 'fbx'])
        self.assertFalse(plan['verified'])
        self.assertEqual(self.database.read_bytes(), before)
        # Only this disposable fixture models the real native AddTypes result.
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute("INSERT INTO bf_type(tid,name) VALUES(4001,'glb'),(4002,'fbx')")
        native_result = self.database.read_bytes()
        actual = preview_types('types.add', args, self.root)
        self.assertTrue(actual['verified'])
        self.assertEqual(actual['missing'], [])
        self.assertEqual(self.database.read_bytes(), native_result)
        for invalid in (['.glb'], ['glb', 'glb'], ['../glb']):
            with self.subTest(invalid=invalid), self.assertRaises(ApiError):
                preview_types('types.add', {'extensions':invalid}, self.root)

    @unittest.skipUnless(os.name == 'nt', 'Frozen Qt5Core roundtrip is Windows x64 only.')
    def test_nanosecond_stamp_survives_real_frozen_qt_json_roundtrip(self):
        qt = ctypes.WinDLL(str(Path(__file__).resolve().parents[2] / 'Qt5Core.dll'))
        pointer = ctypes.c_void_p
        def function(name, args, result=pointer):
            call = getattr(qt, name)
            call.argtypes, call.restype = args, result
            return call
        construct = function('??0QByteArray@@QEAA@PEBDH@Z', [pointer, ctypes.c_char_p, ctypes.c_int])
        parse = function('?fromJson@QJsonDocument@@SA?AV1@AEBVQByteArray@@PEAUQJsonParseError@@@Z', [pointer, pointer, pointer])
        serialize = function('?toJson@QJsonDocument@@QEBA?AVQByteArray@@W4JsonFormat@1@@Z', [pointer, pointer, ctypes.c_int])
        data = function('?constData@QByteArray@@QEBAPEBDXZ', [pointer], ctypes.c_char_p)
        free_array = function('??1QByteArray@@QEAA@XZ', [pointer], None)
        free_document = function('??1QJsonDocument@@QEAA@XZ', [pointer], None)
        def roundtrip(value):
            raw = json.dumps(value).encode('utf-8')
            source, document, output, error = (ctypes.create_string_buffer(64) for _ in range(4))
            construct(source, raw, len(raw))
            parse(document, source, error)
            serialize(document, output, 1)
            try:
                return json.loads(data(output))
            finally:
                free_array(output)
                free_document(document)
                free_array(source)
        # This exact timestamp failed on the real FBX ID 52. Qt emitted its
        # numeric value with the final two decimal digits changed by rounding.
        stamp = 1792329829932545900
        self.assertNotEqual(roundtrip({'mtime_ns': stamp})['mtime_ns'], stamp)
        os.utime(self.model, ns=(stamp, stamp))
        intent = self.prepare()
        self.assertEqual(intent['source']['mtime_ns'], str(self.model.stat().st_mtime_ns))
        transported = roundtrip(intent)
        self.assertEqual(transported, intent)
        self.assertEqual(self.publish(transported)['intent'], intent)

    def test_viewer_frame_can_replace_existing_cover_after_native_readback_only(self):
        cover = self.root / '.bf/.preview/01/1.cover.png'
        cover.parent.mkdir(parents=True)
        png(cover, 8, 6, (200, 20, 40))
        old_cover = cover.read_bytes()
        with closing(sqlite3.connect(self.database)) as db, db:
            db.execute("UPDATE bf_material_userdata SET cover_tid='10' WHERE file_id=1")
        intent = self.frame_intent()
        self.assertFalse(intent['skipped'])
        published = preview_publish('plugins.set_cover', {'intent':intent, 'dest':str(self.output), 'staging_root':str(self.staging)}, self.root)
        self.assertFalse(published.get('skipped', False))
        self.assertEqual(cover.read_bytes(), old_cover)
        # A successful renderer capture does not establish native completion.
        self.assertFalse(preview_verify('plugins.thumbnail', {'published':published}, self.root)['verified'])
        self.assertTrue(preview_publish('plugins.thumbnail', {'intent':intent, 'dest':str(self.output), 'staging_root':str(self.staging)}, self.root)['skipped'])
        # Model only the completed native copy action in this disposable fixture.
        cover.write_bytes(self.output.read_bytes())
        result = preview_verify('plugins.thumbnail', {'published':published}, self.root)
        self.assertTrue(result['verified'], result)
        self.assertEqual(self.model.read_bytes(), self.original)
        self.assertTrue(result['metadata_unchanged'])

    def test_viewer_cover_rejects_model_replaced_at_same_path_after_load(self):
        loaded = self.prepare()['source']
        expected = {'sha256':loaded['sha256'], 'bytes':str(loaded['bytes']), 'mtime_ns':loaded['mtime_ns']}
        self.model.write_bytes(b'a different model in the same location')
        with self.assertRaises(ApiError) as raised:
            self.frame_intent(expected)
        self.assertEqual(raised.exception.code, 'viewer_source_changed')
        self.assertIn('sha256', raised.exception.details['changed_source_fields'])

    def test_viewer_cover_rejects_staging_parent_traversal_and_changed_source(self):
        intent = self.frame_intent()
        outside = self.staging.parent / 'outside.png'
        png(outside, 8, 6, (1, 2, 3))
        with self.assertRaises(ApiError) as raised:
            preview_publish('plugins.set_cover', {'intent':intent, 'dest':str(self.staging/'..'/'outside.png'),
                'staging_root':str(self.staging)}, self.root)
        self.assertEqual(raised.exception.code, 'invalid_preview_output')
        self.model.write_bytes(b'changed after the frame preflight')
        with self.assertRaises(ApiError) as raised:
            preview_publish('plugins.set_cover', {'intent':intent, 'dest':str(self.output), 'staging_root':str(self.staging)}, self.root)
        self.assertEqual(raised.exception.code, 'preview_source_changed')


if __name__ == '__main__':
    unittest.main()
