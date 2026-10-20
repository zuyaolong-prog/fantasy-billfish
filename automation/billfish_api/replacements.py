"""Atomic owned-image replacement; native file monitoring owns all catalog writes."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

from .catalog import Catalog, _id
from .errors import ApiError
from .service import ID, object_schema, validate

ROOT = Path(__file__).resolve().parents[2]
FFPROBE = ROOT / 'tools/ffmpeg/bin/ffprobe.exe'
FFMPEG = ROOT / 'tools/ffmpeg/bin/ffmpeg.exe'
FORMATS = {'.png': 'png', '.jpg': 'mjpeg', '.jpeg': 'mjpeg', '.webp': 'webp'}
PRESERVED_FILE = ('id', 'name', 'pid', 'is_hide', 'is_link', 'tid', 'born', 'ttid')


def _digest(path):
    sha, md5 = hashlib.sha256(), hashlib.md5()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            sha.update(block)
            md5.update(block)
    return {'sha256': sha.hexdigest(), 'md5': md5.hexdigest().upper(), 'bytes': path.stat().st_size}


def _image(path, *, decode=False):
    """Use shipped decoders without adding pip dependencies to portable Python."""
    options = {'capture_output': True, 'timeout': 30}
    if os.name == 'nt':
        options['creationflags'] = subprocess.CREATE_NO_WINDOW
    try:
        probe = subprocess.run([str(FFPROBE), '-v', 'error', '-select_streams', 'v:0',
            '-show_entries', 'stream=codec_name,width,height,nb_frames', '-of', 'json', str(path)], **options)
        data = json.loads(probe.stdout)
        stream = data['streams'][0]
        width, height = stream['width'], stream['height']
        if (probe.returncode or not isinstance(width, int) or not isinstance(height, int)
                or width <= 0 or height <= 0 or stream.get('nb_frames', '1') not in ('1', 'N/A')):
            raise ValueError()
        if decode:
            decoded = subprocess.run([str(FFMPEG), '-v', 'error', '-nostdin', '-i', str(path),
                '-map', '0:v:0', '-frames:v', '1', '-f', 'null', '-'], **options)
            if decoded.returncode or decoded.stderr:
                raise ValueError()
        return {'width': width, 'height': height, 'codec': stream['codec_name']}
    except (OSError, subprocess.TimeoutExpired, ValueError, KeyError, IndexError, TypeError):
        raise ApiError('invalid_image', 'The image could not be decoded by the bundled image decoder.', {'path': str(path)}) from None


def _snapshot(catalog, item_id):
    with catalog._connection() as connection:
        rows = connection.execute(catalog._SELECT + ' WHERE f.id=?', (int(item_id),)).fetchall()
        if not rows:
            raise ApiError('item_not_found', 'The replacement item no longer exists.', {'item_id': item_id}, 404)
        if len(rows) != 1:
            raise ApiError('unsupported_schema', 'The item must have unique material metadata.', status=409)
        item = catalog._items(connection, rows, catalog._folders(connection))[0]
        material = connection.execute('SELECT * FROM bf_material_v2 WHERE file_id=?', (int(item_id),)).fetchone()
        file = connection.execute('SELECT * FROM bf_file WHERE id=?', (int(item_id),)).fetchone()
        preserved = {'file': {key: file[key] for key in PRESERVED_FILE},
            'userdata': [dict(row) for row in connection.execute('SELECT * FROM bf_material_userdata WHERE file_id=? ORDER BY id', (int(item_id),))],
            'tags': [dict(row) for row in connection.execute('SELECT * FROM bf_tag_join_file WHERE file_id=? ORDER BY tag_id,born', (int(item_id),))]}
        return item, dict(material) if material else {}, preserved


def _owned(catalog, item):
    if (item['is_link'] or item['is_recycle'] or item['path_error'] or not item.get('path')
            or not item['exists'] or not Path(item['path']).is_relative_to(catalog.root)):
        raise ApiError('replacement_not_owned', 'Only an existing active library-owned image can be replaced.', {'item_id': item['id']}, 409)
    target = Path(item['path'])
    if target.suffix.lower() not in FORMATS:
        raise ApiError('unsupported_replacement_format', 'Replacement supports owned PNG, JPG, JPEG and WebP images only.')
    return target


def _thumbnail_state(item, material):
    # Catalog paths can name stale files even after native thumb_tid returns to 0.
    if not material.get('thumb_tid'):
        return None
    path = Path(item['thumbnail_path']) if item.get('thumbnail_path') else None
    if not path or not path.is_file():
        return None
    try:
        stat = path.stat()
    except OSError:
        return None
    return {'path': str(path), 'mtime_ns': stat.st_mtime_ns, 'bytes': stat.st_size}


def _operation(operation):
    if operation != 'items.replace_file':
        raise ApiError('unknown_operation', 'Unknown replacement operation.', status=404)


def replace_preflight(operation, arguments, library_root):
    _operation(operation)
    validate(arguments, object_schema({'item_id': ID, 'source_path': {'type': 'string', 'minLength': 1, 'maxLength': 32767},
        'dry_run': {'type': 'boolean', 'default': True}}, ['item_id', 'source_path']))
    catalog = Catalog(library_root)
    identifier = str(_id(arguments['item_id'], 'item_id'))
    item, material, preserved = _snapshot(catalog, identifier)
    target = _owned(catalog, item)
    source = Path(arguments['source_path'])
    if '\0' in str(source) or not source.is_absolute() or not source.is_file():
        raise ApiError('source_not_file', 'source_path must be an absolute existing image file.', status=404)
    source = source.absolute()
    if source.samefile(target):
        raise ApiError('replacement_source_is_target', 'source_path must be a separate output file; the owned target cannot be returned as safe to delete.',
            {'source_safe_to_delete': False, 'file_replaced': False})
    if source.suffix.lower() != target.suffix.lower():
        raise ApiError('replacement_extension_mismatch', 'Source and target image extensions must match; cross-extension replacement is unsupported.')
    image = _image(source, decode=True)
    if image['codec'] != FORMATS[target.suffix.lower()]:
        raise ApiError('replacement_format_mismatch', 'The source encoding does not match its filename extension.')
    source_digest, original = _digest(source), _digest(target)
    key = uuid.uuid4().hex
    workspace = catalog.root / '.bf/automation-replacements' / key
    return {'operation': operation, 'library_root': str(catalog.root), 'item_id': identifier, 'source_path': str(source),
        'target_path': str(target), 'expected': dict(source_digest, **image), 'original': original,
        'preserved': preserved, 'thumbnail_before': _thumbnail_state(item, material),
        'workspace': str(workspace), 'backup_path': str(workspace / ('original' + target.suffix)),
        'stage_path': str(catalog.root / '.bf/.temp' / ('automation-replace-' + key + target.suffix)),
        'modified': source_digest['sha256'] != original['sha256'], 'dry_run': arguments.get('dry_run', True), 'item': item}


def _intent(arguments, library_root):
    if not isinstance(arguments, dict) or set(arguments) != {'intent'} or not isinstance(arguments['intent'], dict):
        raise ApiError('invalid_arguments', 'Expected an internal replacement intent.')
    intent = arguments['intent']
    required = ('item_id', 'target_path', 'source_path', 'expected', 'original', 'preserved', 'workspace', 'backup_path', 'stage_path')
    if any(key not in intent for key in required):
        raise ApiError('invalid_arguments', 'The replacement intent is incomplete.')
    catalog = Catalog(library_root)
    workspace = Path(intent['workspace'])
    if (workspace.parent != catalog.root / '.bf/automation-replacements' or len(workspace.name) != 32
            or any(c not in '0123456789abcdef' for c in workspace.name)
            or Path(intent['backup_path']).parent != workspace
            or Path(intent['stage_path']).parent != catalog.root / '.bf/.temp'
            or Path(intent['stage_path']).name != 'automation-replace-' + workspace.name + Path(intent['target_path']).suffix):
        raise ApiError('invalid_arguments', 'Invalid replacement workspace.')
    return catalog, intent


def _journal(intent, state):
    path = Path(intent['workspace']) / 'state.json'
    temporary = path.with_suffix('.tmp')
    with temporary.open('w', encoding='utf-8') as stream:
        json.dump(dict(intent, **state), stream, ensure_ascii=False, indent=2)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def replace_commit(operation, arguments, library_root):
    _operation(operation)
    catalog, intent = _intent(arguments, library_root)
    if intent.get('dry_run', True):
        raise ApiError('invalid_arguments', 'A dry-run intent cannot replace a file.')
    item, material, preserved = _snapshot(catalog, intent['item_id'])
    target = _owned(catalog, item)
    source = Path(intent['source_path'])
    if str(target) != intent['target_path'] or preserved != intent['preserved'] or _digest(target) != intent['original']:
        raise ApiError('replacement_target_changed', 'The target changed after replacement preflight; nothing was replaced.', {'file_replaced': False}, 409)
    if _digest(source) != {key: intent['expected'][key] for key in ('sha256', 'md5', 'bytes')}:
        raise ApiError('replacement_source_changed', 'The source changed after replacement preflight; nothing was replaced.', {'file_replaced': False}, 409)
    if not intent.get('modified'):
        return dict(intent, file_replaced=False, unchanged=True, source_safe_to_delete=True)
    workspace, stage, backup = Path(intent['workspace']), Path(intent['stage_path']), Path(intent['backup_path'])
    replaced = False
    try:
        workspace.mkdir(parents=True, exist_ok=False)
        stage.parent.mkdir(parents=True, exist_ok=True)
        # open('xb') avoids overwriting an unrelated stale stage or backup.
        with target.open('rb') as src, backup.open('xb') as dst:
            shutil.copyfileobj(src, dst, 1024 * 1024)
            dst.flush(); os.fsync(dst.fileno())
        with source.open('rb') as src, stage.open('xb') as dst:
            shutil.copyfileobj(src, dst, 1024 * 1024)
            dst.flush(); os.fsync(dst.fileno())
        if _digest(backup) != intent['original'] or _digest(stage) != {key: intent['expected'][key] for key in ('sha256', 'md5', 'bytes')}:
            raise ApiError('replacement_copy_changed', 'The source or target changed while staging; nothing was replaced.')
        # Re-read item ownership and bytes immediately before the only mutation.
        fresh_item, _, fresh_preserved = _snapshot(catalog, intent['item_id'])
        if (_owned(catalog, fresh_item) != target or fresh_preserved != intent['preserved']
                or _digest(target) != intent['original']):
            raise ApiError('replacement_target_changed', 'The target changed while staging; nothing was replaced.', status=409)
        # A crash at the atomic boundary leaves a truthful unknown, with both
        # expected digests available for recovery. Never claim a rollback.
        _journal(intent, {'file_replaced': None, 'state': 'commit_pending'})
        os.replace(stage, target)
        replaced = True
        _journal(intent, {'file_replaced': True, 'state': 'awaiting_native_analysis'})
        return dict(intent, file_replaced=True, source_safe_to_delete=True, analysis_ready=False)
    except (OSError, ApiError) as error:
        details = {'item_id': intent['item_id'], 'file_replaced': replaced, 'backup_path': str(backup),
            'recovery_manifest': str(workspace / 'state.json'), 'source_safe_to_delete': True, 'rollback': False}
        if isinstance(error, ApiError):
            raise ApiError(error.code, error.message, dict(error.details or {}, **details), error.status) from error
        raise ApiError('replacement_io_failed', 'The image replacement did not complete. Inspect the retained backup and actual file before retrying.', details, 409) from error


def replace_verify(operation, arguments, library_root):
    _operation(operation)
    catalog, intent = _intent(arguments, library_root)
    item, material, preserved = _snapshot(catalog, intent['item_id'])
    target = _owned(catalog, item)
    expected = intent['expected']
    mismatches = []
    if str(target) != intent['target_path'] or preserved != intent['preserved']:
        mismatches.append('identity_or_userdata')
    actual = _digest(target)
    if actual != {key: expected[key] for key in ('sha256', 'md5', 'bytes')}:
        mismatches.append('file_content')
    if item.get('file_size') != expected['bytes'] or (item.get('md5') or '').upper() != expected['md5']:
        mismatches.append('file_metadata')
    if item.get('width') != expected['width'] or item.get('height') != expected['height'] or material.get('status') != 1:
        mismatches.append('native_analysis')
    thumbnail = _thumbnail_state(item, material)
    if material.get('thumb_tid'):
        if not thumbnail:
            mismatches.append('thumbnail_missing')
        else:
            before = intent.get('thumbnail_before')
            if intent.get('modified') and before and thumbnail['mtime_ns'] <= before['mtime_ns']:
                mismatches.append('thumbnail_not_refreshed')
            elif not mismatches:
                try:
                    if _image(Path(thumbnail['path']), decode=True)['codec'] != 'webp':
                        mismatches.append('thumbnail_decode_pending')
                except ApiError:
                    mismatches.append('thumbnail_decode_pending')
    # thumb_tid=0 intentionally uses the source image, even if an old cache file remains.
    ready = not mismatches
    result = {'verified': ready, 'retryable': all(x not in ('identity_or_userdata', 'file_content') for x in mismatches),
        'mismatches': mismatches, 'item_id': intent['item_id'], 'item': item, 'analysis_ready': ready,
        'file_replaced': intent.get('file_replaced', False), 'source_safe_to_delete': True,
        'backup_path': intent['backup_path'] if intent.get('modified') else None, 'library_root': str(catalog.root)}
    if ready:
        result['completion'] = 'native_replace_persisted'
        if intent.get('modified'):
            _journal(intent, {'file_replaced': True, 'state': 'native_replace_persisted'})
    return result
