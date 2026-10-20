"""Persistent read-only transport observes real catalog and disk changes."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import unittest
import test_animation_catalog as fixtures


class CatalogHelperTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.AnimationCatalogTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        helper = Path(__file__).resolve().parents[1] / 'animation_catalog_helper.py'
        self.child = subprocess.Popen([sys.executable, '-I', str(helper), str(self.fixture.root)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        def close():
            if self.child.poll() is None:
                self.child.stdin.close()
            self.child.wait(timeout=10)
            self.child.stdout.close()
            self.child.stderr.close()
        self.addCleanup(close)
        self.sequence = 0

    def query(self, **changes):
        self.sequence += 1
        request = {"requestId": str(self.sequence), "scopeRoot": str(self.fixture.scope), **changes}
        self.child.stdin.write((json.dumps(request) + '\n').encode('utf-8'))
        self.child.stdin.flush()
        reply = json.loads(self.child.stdout.readline())
        self.assertEqual(reply['requestId'], request['requestId'])
        return reply

    def test_reused_process_observes_fresh_membership_and_disk_existence_without_writes(self):
        f = self.fixture
        protected = [f.db, f.scope / 'base.FBX', f.scope / '子目录/walk.fbx']
        before = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
        first = self.query()
        self.assertEqual(first['data'], f.snapshot())
        self.assertEqual(self.query()['data'], first['data'])
        self.assertEqual(before, {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in protected})
        (f.scope / '子目录/walk.fbx').unlink()
        deleted = self.query()['data']
        self.assertFalse(next(item for item in deleted['items'] if item['id'] == '2')['exists'])
        f.write('UPDATE bf_file SET pid=11 WHERE id=2')
        moved = self.query()['data']
        self.assertNotIn('2', {item['id'] for item in moved['items']})
        self.assertEqual(moved, f.snapshot())

    def test_query_error_is_typed_and_does_not_poison_following_request(self):
        self.assertEqual(self.query(scopeRoot=str(self.fixture.root.parent))['error']['code'], 'invalid_animation_scope')
        self.assertTrue(self.query()['ok'])


if __name__ == '__main__':
    unittest.main()
