"""One-request JSONL media transfer worker. It never writes a Billfish library.

stdin: {mode, workspace, url?, referer?, choice_id?}; stdout: progress/result.
The caller owns a fresh absolute workspace and removes it after import/cancel.
Terminate the worker to cancel: its Windows Job Object owns every child process.
"""
from __future__ import annotations

import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import urlparse, unquote
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
DOWNLOADER = ROOT / 'tools/video-download/yt-dlp.exe'
DOWNLOADER_SHA256 = '66674953fe251b89f4d08c5f0e35e0728679bd67ab3d7d05c0562af101dd3e7a'
FFMPEG = ROOT / 'tools/ffmpeg/bin/ffmpeg.exe'
FFPROBE = ROOT / 'tools/ffmpeg/bin/ffprobe.exe'
NODE = ROOT / 'tools/electron/electron.exe'
_JOB_HANDLE = None


class TransferError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def emit(event, **fields):
    print(json.dumps({'event': event, **fields}, ensure_ascii=False), flush=True)


def install_process_job():
    """Fail closed if children cannot be bound to this worker's lifetime."""
    global _JOB_HANDLE
    if os.name != 'nt':
        return
    from ctypes import wintypes

    class BasicLimit(ctypes.Structure):
        _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64),
                    ('PerJobUserTimeLimit', ctypes.c_int64), ('LimitFlags', wintypes.DWORD),
                    ('MinimumWorkingSetSize', ctypes.c_size_t), ('MaximumWorkingSetSize', ctypes.c_size_t),
                    ('ActiveProcessLimit', wintypes.DWORD), ('Affinity', ctypes.c_size_t),
                    ('PriorityClass', wintypes.DWORD), ('SchedulingClass', wintypes.DWORD)]

    class IO(ctypes.Structure):
        _fields_ = [(n, ctypes.c_uint64) for n in ('ReadOperationCount', 'WriteOperationCount',
                    'OtherOperationCount', 'ReadTransferCount', 'WriteTransferCount', 'OtherTransferCount')]

    class ExtendedLimit(ctypes.Structure):
        _fields_ = [('BasicLimitInformation', BasicLimit), ('IoInfo', IO),
                    ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                    ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    kernel.CreateJobObjectW.restype = wintypes.HANDLE
    kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    handle = kernel.CreateJobObjectW(None, None)
    limits = ExtendedLimit()
    limits.BasicLimitInformation.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not handle or not kernel.SetInformationJobObject(handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
        raise TransferError('process_lifecycle', '无法创建可取消的下载进程组.')
    if not kernel.AssignProcessToJobObject(handle, kernel.GetCurrentProcess()):
        raise TransferError('process_lifecycle', '无法绑定下载进程生命周期.')
    # Do not close explicitly: closing kills this process too. OS closes at exit.
    _JOB_HANDLE = handle


def http_url(value, name='url'):
    if not isinstance(value, str) or len(value) > 16384 or any(ord(c) < 32 for c in value):
        raise TransferError('invalid_request', f'{name} 必须是 HTTP(S) 地址.')
    parsed = urlparse(value)
    if parsed.scheme not in ('http', 'https') or not parsed.hostname or parsed.username or parsed.password:
        raise TransferError('invalid_request', f'{name} 必须是无内嵌凭据的 HTTP(S) 地址.')
    return value


def workspace_path(value):
    if not isinstance(value, str) or not Path(value).is_absolute():
        raise TransferError('invalid_request', 'workspace 必须是本次任务的绝对目录.')
    path = Path(value).resolve()
    if path == Path(path.anchor) or path == ROOT:
        raise TransferError('invalid_request', 'workspace 必须是独立任务目录.')
    path.mkdir(parents=True, exist_ok=True)
    return path


def number(value):
    return value if isinstance(value, (int, float)) and math.isfinite(value) and value > 0 else None


def build_choices(info):
    """Only actual extractor formats; one best candidate per resolution/fps.

    Prefer an MP4/AAC pair where available. Never substitute another resolution,
    and never choose a different video's audio. Silent sources stay silent.
    """
    formats = info.get('formats') or [info]
    formats = [f for f in formats if isinstance(f, dict) and not f.get('has_drm')
               and f.get('url') and urlparse(f['url']).scheme in ('http', 'https')]
    audio = [f for f in formats if f.get('vcodec') == 'none' and f.get('acodec') not in (None, 'none')]
    video = [f for f in formats if f.get('vcodec') != 'none'
             and f.get('format_id') is not None and not f.get('format_note', '').lower().startswith('storyboard')]
    grouped = {}
    for f in video:
        fid = str(f['format_id'])
        if not re.fullmatch(r'[A-Za-z0-9_.-]+', fid):
            continue
        has_audio = f.get('acodec') not in (None, 'none')
        paired = None
        if not has_audio and audio:
            candidates = [a for a in audio if re.fullmatch(r'[A-Za-z0-9_.-]+', str(a.get('format_id', '')))]
            if candidates:
                paired = max(candidates, key=lambda a: (a.get('ext') == 'm4a' if f.get('ext') == 'mp4' else True,
                                                       number(a.get('abr')) or 0, number(a.get('tbr')) or 0))
        key = (number(f.get('width')), number(f.get('height')), number(f.get('fps')))
        rank = (f.get('ext') == 'mp4', has_audio or paired is not None,
                str(f.get('vcodec', '')).startswith(('avc', 'h264')), number(f.get('tbr')) or 0)
        previous = grouped.get(key)
        if previous is None or rank > previous[0]:
            grouped[key] = (rank, f, paired)
    choices = []
    for _, f, paired in sorted(grouped.values(), key=lambda entry: (number(entry[1].get('height')) or 0,
                                number(entry[1].get('fps')) or 0), reverse=True):
        fid = str(f['format_id']) + ('+' + str(paired['format_id']) if paired else '')
        acodec = paired.get('acodec') if paired else f.get('acodec')
        size = number(f.get('filesize')) or number(f.get('filesize_approx'))
        if paired:
            audio_size = number(paired.get('filesize')) or number(paired.get('filesize_approx'))
            size = size + audio_size if size and audio_size else None
        height, width, fps = number(f.get('height')), number(f.get('width')), number(f.get('fps'))
        label = f'{int(height)}p' if height else '原始视频'
        if fps:
            label += f' / {fps:g} fps'
        label += ' / ' + str(f.get('vcodec') or f.get('ext') or 'video')
        if acodec in (None, 'none'):
            label += ' / 无音轨'
        choices.append(dict(choice_id=hashlib.sha256(fid.encode()).hexdigest()[:20], label=label,
                            format_id=fid, width=width, height=height, fps=fps,
                            video_codec=f.get('vcodec'), audio_codec=acodec,
                            has_audio=acodec not in (None, 'none'), estimated_bytes=int(size) if size else None))
    return choices


def child_env():
    return {**os.environ, 'ELECTRON_RUN_AS_NODE': '1', 'PYTHONIOENCODING': 'utf-8'}


def run_json(args, timeout=180):
    try:
        proc = subprocess.run([str(a) for a in args], capture_output=True, timeout=timeout,
                              env=child_env(), creationflags=0x08000000 if os.name == 'nt' else 0)
    except subprocess.TimeoutExpired as exc:
        raise TransferError('timeout', '媒体解析超时, 请重试.') from exc
    if proc.returncode:
        message = proc.stderr.decode('utf-8', 'replace').strip()[-2500:]
        raise TransferError('download_failed', message or '媒体解析失败.')
    try:
        return json.loads(proc.stdout.decode('utf-8-sig'))
    except (ValueError, UnicodeError) as exc:
        raise TransferError('invalid_response', '下载器返回了无法读取的媒体信息.') from exc


def base_command():
    if not DOWNLOADER.is_file() or hashlib.sha256(DOWNLOADER.read_bytes()).hexdigest() != DOWNLOADER_SHA256:
        raise TransferError('dependency_invalid', 'yt-dlp 缺失或校验失败.')
    if not FFMPEG.is_file() or not FFPROBE.is_file() or not NODE.is_file():
        raise TransferError('dependency_missing', 'FFmpeg 或 JavaScript runtime 缺失.')
    return [str(DOWNLOADER), '--ignore-config', '--no-plugin-dirs', '--no-playlist', '--playlist-items', '1',
            '--no-update', '--no-cache-dir', '--socket-timeout', '30', '--retries', '2',
            '--fragment-retries', '2', '--ffmpeg-location', str(FFMPEG.parent),
            '--no-js-runtimes', '--js-runtimes', f'node:{NODE}']


def media_info(target, referer=None):
    args = [str(FFPROBE), '-v', 'error', '-rw_timeout', '30000000']
    if referer:
        args += ['-headers', f'Referer: {referer}\r\n']
    return run_json(args + ['-show_streams', '-show_format', '-of', 'json', str(target)], timeout=90)


def video_stream(info):
    streams = info.get('streams', [])
    videos = [s for s in streams if s.get('codec_type') == 'video' and not s.get('disposition', {}).get('attached_pic')]
    if not videos:
        raise TransferError('not_video', '该地址没有可下载的视频画面.')
    video = videos[0]
    fps = None
    try:
        numerator, denominator = video.get('avg_frame_rate', '0/1').split('/')
        fps = number(float(numerator) / float(denominator))
    except (ValueError, ZeroDivisionError):
        pass
    return dict(width=video.get('width'), height=video.get('height'), fps=fps,
                has_audio=any(s.get('codec_type') == 'audio' for s in streams))


def probe_video(request, workspace):
    url = http_url(request.get('url'))
    referer = http_url(request['referer'], 'referer') if request.get('referer') else None
    emit('progress', stage='probe', message='正在读取网站可用分辨率')
    args = base_command() + ['--skip-download', '--dump-single-json', '--no-progress', '--no-warnings']
    if referer:
        args += ['--referer', referer]
    info = run_json(args + ['--', url])
    if info.get('_type') in ('playlist', 'multi_video') or info.get('entries'):
        raise TransferError('playlist_unsupported', '请拖入单个视频的链接.')
    if info.get('is_live') or info.get('live_status') in ('is_live', 'is_upcoming'):
        raise TransferError('live_unsupported', '暂不支持正在直播或尚未开始的视频.')
    choices = build_choices(info)
    if not choices:
        raise TransferError('no_formats', '没有可用的视频格式, 网站可能要求登录或限制下载.')
    # Generic direct media often has no dimensions in extractor metadata.
    for choice in choices:
        if not choice['height']:
            metadata = video_stream(media_info(info.get('url') or url, referer))
            choice.update(metadata)
            choice['label'] = f"{metadata['height']}p" + (f" / {metadata['fps']:g} fps" if metadata['fps'] else '')
    (workspace / 'info.json').write_text(json.dumps(info, ensure_ascii=False), encoding='utf-8')
    state = dict(version=1, title=info.get('title') or 'video', origin=url, referer=referer, choices=choices)
    (workspace / 'probe.json').write_text(json.dumps(state, ensure_ascii=False), encoding='utf-8')
    return dict(workspace=str(workspace), title=state['title'], origin=url, choices=choices)


def validate_video(actual, choice):
    for dimension in ('width', 'height'):
        expected = choice.get(dimension)
        if expected and actual.get(dimension) != expected:
            raise TransferError('format_mismatch', f"下载视频的 {dimension} 与所选分辨率不符, 未导入资料库.")
    if choice.get('fps') and actual.get('fps') and abs(choice['fps'] - actual['fps']) > 1.1:
        raise TransferError('format_mismatch', '下载视频的帧率与所选格式不符, 未导入资料库.')
    if choice.get('has_audio') and not actual.get('has_audio'):
        raise TransferError('audio_missing', '所选格式的音轨未成功合并, 未导入资料库.')


def download_video(request, workspace):
    try:
        state = json.loads((workspace / 'probe.json').read_text(encoding='utf-8'))
        info_path = workspace / 'info.json'
        if not info_path.is_file():
            raise ValueError()
        choice = next(c for c in state['choices'] if c['choice_id'] == request.get('choice_id'))
    except (OSError, ValueError, KeyError, StopIteration) as exc:
        raise TransferError('invalid_choice', '下载选项已失效, 请重新解析视频.') from exc
    http_url(state['origin'])
    args = base_command() + ['--load-info-json', str(info_path), '--format', choice['format_id'],
                            '--paths', str(workspace), '--output', 'video.%(ext)s', '--no-overwrites',
                            '--abort-on-unavailable-fragments',
                            '--merge-output-format', 'mp4/mkv', '--newline', '--progress', '--progress-delta', '0.5',
                            '--progress-template', 'download:BF_PROGRESS:%(progress._percent_str)s',
                            '--print', 'after_move:BF_OUTPUT:%(filepath)j']
    if state.get('referer'):
        args += ['--referer', http_url(state['referer'], 'referer')]
    emit('progress', stage='download', message='正在下载所选视频')
    # stderr is folded into the parser, so no unbounded stderr pipe can block it.
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            env=child_env(), creationflags=0x08000000 if os.name == 'nt' else 0)
    errors, output = [], None
    for raw in proc.stdout:
        line = raw.decode('utf-8', 'replace').strip()
        if line.startswith('BF_PROGRESS:'):
            match = re.search(r'(\d+(?:\.\d+)?)%', line)
            if match:
                emit('progress', stage='download', percent=float(match.group(1)))
        elif line.startswith('BF_OUTPUT:'):
            try:
                output = Path(json.loads(line[len('BF_OUTPUT:'):])).resolve()
            except ValueError:
                pass
        elif line:
            errors.append(line)
            errors = errors[-20:]
    proc.wait()
    if proc.returncode:
        raise TransferError('download_failed', '\n'.join(errors)[-2500:] or '视频下载失败.')
    if not output or output.parent != workspace or not output.is_file() or output.stat().st_size == 0:
        raise TransferError('missing_output', '下载器没有生成有效视频文件.')
    emit('progress', stage='verify', message='正在校验视频分辨率和音轨')
    actual = video_stream(media_info(output))
    validate_video(actual, choice)
    return dict(path=str(output), title=state['title'], origin=state['origin'], **actual)


def image_extension(header):
    if header.startswith(b'\x89PNG\r\n\x1a\n'):
        return '.png'
    if header.startswith(b'\xff\xd8\xff'):
        return '.jpg'
    if header.startswith((b'GIF87a', b'GIF89a')):
        return '.gif'
    if header.startswith(b'RIFF') and header[8:12] == b'WEBP':
        return '.webp'
    if header.startswith(b'BM'):
        return '.bmp'
    if header.startswith((b'II\x2a\x00', b'MM\x00\x2a')):
        return '.tiff'
    raise TransferError('not_image', '下载结果不是支持的图片格式.')


def download_image(request, workspace):
    url = http_url(request.get('url'))
    headers = {'User-Agent': 'Mozilla/5.0 BillfishMediaTransfer/1.0', 'Accept': 'image/*'}
    if request.get('referer'):
        headers['Referer'] = http_url(request['referer'], 'referer')
    emit('progress', stage='download', message='正在下载图片')
    temporary = workspace / 'image.part'
    try:
        with urlopen(Request(url, headers=headers), timeout=30) as response, temporary.open('wb') as target:
            http_url(response.geturl())
            header = response.read(32)
            extension = image_extension(header)
            target.write(header)
            total = len(header)
            while chunk := response.read(256 * 1024):
                total += len(chunk)
                if total > 100 * 1024 * 1024:
                    raise TransferError('image_too_large', '图片超过 100 MiB 限制.')
                target.write(chunk)
        output = workspace / ('image' + extension)
        temporary.replace(output)
    except TransferError:
        raise
    except (OSError, ValueError) as exc:
        raise TransferError('download_failed', f'图片下载失败: {exc}') from exc
    return dict(path=str(output), title=unquote(Path(urlparse(url).path).stem) or 'image', origin=url, extension=extension)


def main():
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
    try:
        install_process_job()
        raw = sys.stdin.buffer.readline(1024 * 1024 + 1)
        if len(raw) > 1024 * 1024:
            raise TransferError('invalid_request', '请求过大.')
        request = json.loads(raw.decode('utf-8'))
        if not isinstance(request, dict):
            raise TransferError('invalid_request', '请求必须是 JSON 对象.')
        mode = request.get('mode')
        operation = {'probe_video': probe_video, 'download_video': download_video, 'download_image': download_image}.get(mode)
        if not operation:
            raise TransferError('invalid_request', '未知的媒体操作.')
        result = operation(request, workspace_path(request.get('workspace')))
        emit('result', ok=True, result=result)
    except TransferError as exc:
        emit('result', ok=False, error=dict(code=exc.code, message=str(exc)))
    except Exception as exc:
        print(f'{type(exc).__name__}: {exc}', file=sys.stderr, flush=True)
        emit('result', ok=False, error=dict(code='transfer_failed', message=str(exc)))


if __name__ == '__main__':
    main()
