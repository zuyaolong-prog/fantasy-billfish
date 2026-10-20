"""Media choices, real local HTTP transfers and Windows cancellation lifecycle."""
import base64
import functools
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import media_transfer as media


class ChoiceTests(unittest.TestCase):
    def test_pairs_audio_and_never_invents_resolutions(self):
        formats = [dict(format_id='137', url='https://example.org/1080', width=1920, height=1080,
                        fps=30, vcodec='avc1', acodec='none', ext='mp4', filesize=100),
                   dict(format_id='140', url='https://example.org/audio', vcodec='none', acodec='aac',
                        ext='m4a', abr=128, filesize=10),
                   dict(format_id='22', url='https://example.org/720', width=1280, height=720,
                        fps=30, vcodec='avc1', acodec='aac', ext='mp4', filesize=70)]
        choices = media.build_choices({'formats': formats})
        self.assertEqual([c['height'] for c in choices], [1080, 720])
        self.assertEqual([c['format_id'] for c in choices], ['137+140', '22'])
        self.assertTrue(all(c['has_audio'] for c in choices))
        self.assertEqual(choices[0]['estimated_bytes'], 110)

    def test_drm_audio_only_storyboards_and_non_http_are_excluded(self):
        invalid = [dict(format_id='drm', has_drm=True, url='https://example.org/v', vcodec='avc'),
                   dict(format_id='a', url='https://example.org/a', vcodec='none', acodec='aac'),
                   dict(format_id='sb', url='https://example.org/s', vcodec='images', format_note='storyboard'),
                   dict(format_id='f', url='file:///etc/passwd', vcodec='avc')]
        self.assertEqual(media.build_choices({'formats': invalid}), [])

    def test_dimensions_and_audio_are_verified_after_download(self):
        choice = dict(width=1920, height=1080, fps=30, has_audio=True)
        for actual in [dict(width=1280, height=720, fps=30, has_audio=True),
                       dict(width=1920, height=1080, fps=30, has_audio=False)]:
            with self.assertRaises(media.TransferError):
                media.validate_video(actual, choice)

    def test_image_signature_rejects_html(self):
        with self.assertRaises(media.TransferError):
            media.image_extension(b'<html>error</html>')


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


class LocalTransferTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='billfish-media-tests-')
        cls.root = Path(cls.temp.name)
        cls.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0),
                        functools.partial(QuietHandler, directory=str(cls.root)))
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f'http://127.0.0.1:{cls.server.server_port}'
        subprocess.run([str(media.FFMPEG), '-hide_banner', '-loglevel', 'error',
                        '-f', 'lavfi', '-i', 'color=c=blue:s=320x180:r=12',
                        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100',
                        '-t', '1', '-c:v', 'libx264', '-c:a', 'aac', '-shortest',
                        str(cls.root / 'clip.mp4')], check=True, timeout=30)
        for filename, selection in [('video-only.mp4', '-an'), ('audio-only.m4a', '-vn')]:
            subprocess.run([str(media.FFMPEG), '-hide_banner', '-loglevel', 'error',
                            '-i', str(cls.root / 'clip.mp4'), selection, '-c', 'copy',
                            str(cls.root / filename)], check=True, timeout=30)
        (cls.root / 'image.png').write_bytes(base64.b64decode(
            'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aF4sAAAAASUVORK5CYII='))
        (cls.root / 'bad.png').write_text('<html>not image</html>')

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()
        cls.temp.cleanup()

    def call(self, request):
        proc = subprocess.run([sys.executable, '-I', str(Path(media.__file__))],
                              input=json.dumps(request).encode(), capture_output=True, timeout=90)
        self.assertEqual(proc.returncode, 0, proc.stderr.decode('utf-8', 'replace'))
        lines = [json.loads(line) for line in proc.stdout.decode('utf-8').splitlines()]
        terminal = [line for line in lines if line['event'] == 'result']
        self.assertEqual(len(terminal), 1, lines)
        return terminal[0]

    def test_real_direct_video_probe_download_and_verification(self):
        workspace = self.root / 'video-job'
        result = self.call(dict(mode='probe_video', workspace=str(workspace), url=self.base + '/clip.mp4'))
        self.assertTrue(result['ok'], result)
        choices = result['result']['choices']
        self.assertEqual(len(choices), 1)
        self.assertEqual((choices[0]['width'], choices[0]['height']), (320, 180))
        result = self.call(dict(mode='download_video', workspace=str(workspace), choice_id=choices[0]['choice_id']))
        self.assertTrue(result['ok'], result)
        self.assertTrue(Path(result['result']['path']).is_file())
        self.assertTrue(result['result']['has_audio'])
        self.assertEqual((result['result']['width'], result['result']['height']), (320, 180))

    def test_image_and_invalid_response(self):
        result = self.call(dict(mode='download_image', workspace=str(self.root / 'image-job'), url=self.base + '/image.png'))
        self.assertTrue(result['ok'], result)
        self.assertEqual(Path(result['result']['path']).suffix, '.png')
        result = self.call(dict(mode='download_image', workspace=str(self.root / 'bad-job'), url=self.base + '/bad.png'))
        self.assertFalse(result['ok'])
        self.assertEqual(result['error']['code'], 'not_image')

    def test_actual_separate_streams_are_merged_with_audio(self):
        workspace = self.root / 'merge-job'
        workspace.mkdir()
        info = dict(id='split', title='split local fixture', extractor='generic',
                    webpage_url=self.base + '/clip.mp4', formats=[
            dict(format_id='v', url=self.base + '/video-only.mp4', ext='mp4',
                 vcodec='h264', acodec='none', width=320, height=180, fps=12,
                 protocol='http'),
            dict(format_id='a', url=self.base + '/audio-only.m4a', ext='m4a',
                 vcodec='none', acodec='aac', protocol='http', abr=64)])
        choices = media.build_choices(info)
        (workspace / 'info.json').write_text(json.dumps(info), encoding='utf-8')
        (workspace / 'probe.json').write_text(json.dumps(dict(version=1, title='split local fixture',
                       origin=self.base + '/clip.mp4', choices=choices)), encoding='utf-8')
        result = self.call(dict(mode='download_video', workspace=str(workspace), choice_id=choices[0]['choice_id']))
        self.assertTrue(result['ok'], result)
        self.assertTrue(result['result']['has_audio'])
        self.assertEqual((result['result']['width'], result['result']['height']), (320, 180))

    def test_invalid_choice_does_not_download(self):
        result = self.call(dict(mode='download_video', workspace=str(self.root / 'missing-job'), choice_id='unknown'))
        self.assertFalse(result['ok'])
        self.assertEqual(result['error']['code'], 'invalid_choice')

    def test_missing_last_hls_fragment_fails_without_importable_success(self):
        hls = self.root / 'missing-tail-hls'
        hls.mkdir()
        subprocess.run([str(media.FFMPEG), '-hide_banner', '-loglevel', 'error',
                        '-f', 'lavfi', '-i', 'color=c=blue:s=160x90:r=10',
                        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100',
                        '-t', '3', '-c:v', 'libx264', '-g', '10', '-sc_threshold', '0',
                        '-c:a', 'aac', '-hls_time', '1', '-hls_list_size', '0',
                        str(hls / 'clip.m3u8')], check=True, timeout=30)
        workspace = self.root / 'missing-tail-job'
        probe = self.call(dict(mode='probe_video', workspace=str(workspace),
                               url=self.base + '/missing-tail-hls/clip.m3u8'))
        self.assertTrue(probe['ok'], probe)
        fragments = sorted(hls.glob('*.ts'))
        self.assertEqual(len(fragments), 3)
        fragments[-1].unlink()
        result = self.call(dict(mode='download_video', workspace=str(workspace),
                                choice_id=probe['result']['choices'][0]['choice_id']))
        self.assertFalse(result['ok'], result)
        self.assertEqual(result['error']['code'], 'download_failed')
        self.assertNotIn('result', result)

    @unittest.skipUnless(os.name == 'nt', 'Windows Job Object contract')
    def test_terminating_worker_kills_its_child(self):
        script = "import sys,time,subprocess;sys.path.insert(0,sys.argv[1]);import media_transfer as m;m.install_process_job();p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(120)']);print(p.pid,flush=True);time.sleep(120)"
        worker = subprocess.Popen([sys.executable, '-I', '-c', script, str(Path(media.__file__).parent)],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(lambda: worker.poll() is None and worker.kill())
        child_pid = int(worker.stdout.readline())
        import ctypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.OpenProcess(0x100000, False, child_pid)
        self.assertTrue(handle)
        try:
            worker.terminate()
            worker.wait(timeout=10)
            self.assertEqual(kernel.WaitForSingleObject(handle, 10000), 0, 'download child survived cancellation')
        finally:
            kernel.CloseHandle(handle)
            worker.communicate(timeout=10)


if __name__ == '__main__':
    unittest.main()
