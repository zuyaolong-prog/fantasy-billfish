"""Read-only format preview plans and native custom-cover verification."""

import hashlib
import os
import re
from pathlib import Path

from .catalog import Catalog, _id
from .errors import ApiError
from .replacements import _image


# Frozen Core preview formats. Embedded clips from these formats may bind only
# to the verified native base item; HDR/EXR are viewer-only, not clip sources.
_CORE_EMBEDDED_EXTENSIONS = frozenset((
    '.gltf', '.vtk', '.obj', '.stl', '.dae', '.ply', '.spline', '.splinecode',
    '.usdz', '.usdc', '.3dm', '.3mf', '.amf', '.lwo', '.3ds', '.pcd', '.vox',
    '.xyz', '.bvh', '.ifc', '.kmz',
))


def _identifier(value):
    if not isinstance(value, str):
        raise ApiError('invalid_arguments', 'item_id must be a positive decimal string.')
    return str(_id(value, 'item_id'))


def _digest(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def _animation_source(binding, *, bound_source=None):
    if not isinstance(binding, dict):
        raise ApiError('invalid_animation_source', 'Animation source binding must be an object.')
    path_value = binding.get('path')
    if not isinstance(path_value, str) or not path_value or '\0' in path_value:
        raise ApiError('invalid_animation_source', 'Animation source path must be an absolute source file path.')
    path = Path(path_value)
    fingerprint = binding.get('fingerprint')
    if (not path.is_absolute() or not path.is_file() or not isinstance(fingerprint, dict)
            or not isinstance(binding.get('rowId'), str) or not binding['rowId']
            or type(binding.get('clip_index')) is not int or binding['clip_index'] < 0):
        raise ApiError('invalid_animation_source', 'Animation source requires a bound original file and clip identity.')
    if path.suffix.lower() not in ('.fbx', '.glb'):
        normalize = lambda value: os.path.normcase(os.path.abspath(value)).casefold()
        if (path.suffix.lower() not in _CORE_EMBEDDED_EXTENSIONS
                or not isinstance(bound_source, dict)
                or not isinstance(bound_source.get('path'), str)
                or not Path(bound_source['path']).is_absolute()
                or normalize(path) != normalize(bound_source['path'])
                or fingerprint != {'sha256': bound_source.get('sha256'),
                                   'bytes': str(bound_source.get('bytes')),
                                   'mtime_ns': bound_source.get('mtime_ns')}):
            raise ApiError('invalid_animation_source', 'A Core embedded animation must bind to this verified original model and fingerprint.')
    stat = path.stat()
    actual = {'sha256': _digest(path), 'bytes': str(stat.st_size), 'mtime_ns': str(stat.st_mtime_ns)}
    after = path.stat()
    identity_fields = ('st_size', 'st_mtime_ns', 'st_ctime_ns', 'st_dev', 'st_ino')
    # Reading the original to hash it may update last-access time. Only content
    # and file-identity fields participate in the before/after stability check.
    if actual != fingerprint or any(getattr(after, field) != getattr(stat, field) for field in identity_fields):
        raise ApiError('animation_source_changed', 'The original animation file changed after this clip loaded.')
    return binding


def _protected_metadata(metadata):
    """Exclude only evidenced cover-derived palette and empty storage rows."""
    result = {key: value for key, value in metadata.items()}
    result['material'] = [{key: value for key, value in row.items() if key != 'colors'}
                          for row in metadata.get('material', [])]
    result['userdata'] = []
    for row in metadata.get('userdata', []):
        fields = {key: value for key, value in row.items()
                  if key not in ('id', 'file_id', 'cover_tid') and value is not None and value != ''}
        if fields:
            result['userdata'].append(fields)
    return result


def _snapshot(catalog, identifier, *, digest=False):
    with catalog._connection() as db:
        rows = db.execute(catalog._SELECT + ' WHERE f.id=?', (int(identifier),)).fetchall()
        if len(rows) != 1:
            raise ApiError('item_not_found', 'The preview item no longer exists uniquely.', status=404)
        item = catalog._items(db, rows, catalog._folders(db))[0]
        userdata = [dict(row) for row in db.execute('SELECT * FROM bf_material_userdata WHERE file_id=?', (int(identifier),))]
        cover = userdata[0].get('cover_tid') if userdata else None
        item['cover_tid'] = str(cover or '0')
        item['has_cover'] = item['cover_tid'] != '0'
        preserved = {
            'file': [dict(row) for row in db.execute('SELECT * FROM bf_file WHERE id=?', (int(identifier),))],
            'material': [dict(row) for row in db.execute('SELECT * FROM bf_material_v2 WHERE file_id=?', (int(identifier),))],
            'userdata': [{key: value for key, value in row.items() if key != 'cover_tid'} for row in userdata],
            'tags': [dict(row) for row in db.execute('SELECT * FROM bf_tag_join_file WHERE file_id=? ORDER BY tag_id,born', (int(identifier),))],
        }
    if not item.get('path') or not item['exists'] or item['path_error'] or item['is_recycle'] or item['is_link']:
        raise ApiError('preview_source_unavailable', 'Preview requires an existing active library-owned source file.')
    path = Path(item['path'])
    stat = path.stat()
    # Qt5 JSON numbers pass through IEEE-754 doubles. Nanosecond timestamps
    # must remain exact across Python -> Qt -> Python request boundaries.
    source = {'path': str(path), 'bytes': stat.st_size, 'mtime_ns': str(stat.st_mtime_ns)}
    if digest:
        source['sha256'] = _digest(path)
    return item, source, _protected_metadata(preserved)


def _intent(args, root):
    intent = args.get('intent')
    if not isinstance(intent, dict) or str(Path(intent.get('library_root', '')).absolute()).casefold() != str(Path(root).absolute()).casefold():
        raise ApiError('library_mismatch', 'Preview intent belongs to another library.')
    identifier = _identifier(intent.get('item_id'))
    item, source, preserved = _snapshot(Catalog(root), identifier, digest=True)
    if source != intent.get('source') or preserved != intent.get('preserved'):
        before_source = intent.get('source', {})
        before_metadata = intent.get('preserved', {})
        raise ApiError('preview_source_changed', 'The source file or metadata changed during preview generation.', {
            'changed_source_fields': [key for key in source if source[key] != before_source.get(key)],
            'changed_metadata_sections': [key for key in preserved if preserved[key] != before_metadata.get(key)],
        })
    if 'animation_source' in intent:
        _animation_source(intent['animation_source'], bound_source=source)
    return intent, item


def preview_items(operation, args, root):
    identifiers = args.get('item_ids')
    if not isinstance(identifiers, list) or len(identifiers) > 512:
        raise ApiError('invalid_arguments', 'Preview hydration requires at most 512 native item IDs.')
    catalog = Catalog(root)
    identifiers = [_identifier(identifier) for identifier in identifiers]
    if len(identifiers) != len(set(identifiers)):
        raise ApiError('invalid_arguments', 'Preview item IDs must be unique.')
    items = []
    with catalog._connection() as db:
        rows = []
        covers = {}
        for identifier in identifiers:
            rows.extend(db.execute(catalog._SELECT + ' WHERE f.id=?', (int(identifier),)).fetchall())
            cover = db.execute('SELECT cover_tid FROM bf_material_userdata WHERE file_id=?', (int(identifier),)).fetchone()
            covers[identifier] = str(cover['cover_tid'] or '0') if cover else '0'
        hydrated = catalog._items(db, rows, catalog._folders(db))
    for item in hydrated:
        if not item.get('path') or not item['exists'] or item['path_error'] or item['is_recycle'] or item['is_link']:
            continue
        try:
            stat = Path(item['path']).stat()
        except OSError:
            continue
        item['cover_tid'] = covers[item['id']]
        item['has_cover'] = item['cover_tid'] != '0'
        item['source_stamp'] = f'{stat.st_size}:{stat.st_mtime_ns}'
        items.append(item)
    return {'items': items, 'library_root': str(catalog.root)}


def animation_visibility(operation, args, root):
    """Map trusted backend paths to unique active owned native items, read-only."""
    paths = args.get('hidden_paths')
    if (not isinstance(paths, list) or len(paths) > 100000 or
            any(not isinstance(path, str) or not path or '\0' in path or not Path(path).is_absolute() for path in paths)):
        raise ApiError('invalid_arguments', 'hidden_paths must contain absolute source paths.')
    catalog = Catalog(root)
    normalized = lambda path: os.path.normcase(os.path.abspath(path)).casefold()
    requested = {normalized(path): path for path in paths}
    names = {Path(path).name.lower() for path in paths if Path(path).suffix.lower() in ('.fbx', '.glb')}
    matches = {key: [] for key in requested}
    with catalog._connection() as db:
        folders = catalog._folders(db)
        for name in names:
            rows = db.execute(catalog._SELECT + ' WHERE lower(f.name)=?', (name,)).fetchall()
            # Avoid tag hydration and parameter limits: only proven path ownership
            # is relevant; IDs, paths and recycle state all come from this snapshot.
            for row in rows:
                path, recycled = catalog._path(row['name'], row['pid'], folders)
                if (not isinstance(row['id'], int) or row['id'] <= 0 or row['is_link'] or row['is_recycle']
                        or recycled or path['path_error'] or not path['exists']):
                    continue
                key = normalized(path['path'])
                if key in matches:
                    matches[key].append(str(row['id']))
    identifiers = sorted({ids[0] for ids in matches.values() if len(ids) == 1}, key=int)
    resolved = [{'path': requested[key], 'item_id': ids[0]} for key, ids in matches.items() if len(ids) == 1]
    unresolved = [requested[key] for key, ids in matches.items() if len(ids) != 1]
    return {'hidden_item_ids': identifiers, 'resolved_paths': resolved, 'unresolved_paths': unresolved, 'library_root': str(catalog.root)}


def preview_prepare(operation, args, root):
    identifier = _identifier(args.get('item_id'))
    thumbnail = operation == 'plugins.thumbnail'
    item, source, preserved = _snapshot(Catalog(root), identifier, digest=operation in ('plugins.thumbnail', 'plugins.set_cover', 'plugins.regenerate_thumbnail'))
    if operation in ('plugins.set_cover', 'plugins.regenerate_thumbnail'):
        fingerprint = {'sha256': source['sha256'], 'bytes': str(source['bytes']), 'mtime_ns': source['mtime_ns']}
        expected = args.get('expected_source')
        if not isinstance(expected, dict) or fingerprint != expected:
            raise ApiError('viewer_source_changed', 'The original model changed after this viewer loaded. Reopen it before setting a frame cover.', {
                'item_id': identifier,
                'changed_source_fields': [key for key in fingerprint if not isinstance(expected, dict) or fingerprint[key] != expected.get(key)],
            })
    result = {'item_id': identifier, 'library_root': str(Path(root).absolute()), 'item': item,
              'source': source, 'preserved': preserved, 'skipped': thumbnail and item['has_cover']}
    if 'animation_source' in args:
        result['animation_source'] = _animation_source(args['animation_source'], bound_source=source)
    return result


def preview_publish(operation, args, root):
    intent, item = _intent(args, root)
    if item['has_cover'] and operation not in ('plugins.set_cover', 'plugins.regenerate_thumbnail'):
        return {'skipped': True, 'reason': 'existing_cover', 'item_id': item['id'], 'cover_tid': item['cover_tid']}
    path = Path(args.get('dest', ''))
    staging = Path(args.get('staging_root', ''))
    if not path.is_absolute() or not staging.is_absolute():
        raise ApiError('invalid_preview_output', 'Thumbnail must be an existing PNG in the plugin-owned preview staging directory.')
    path, staging = Path(os.path.abspath(path)), Path(os.path.abspath(staging))
    if not path.is_relative_to(staging) or path.suffix.lower() != '.png' or not path.is_file():
        raise ApiError('invalid_preview_output', 'Thumbnail must be an existing PNG in the plugin-owned preview staging directory.')
    image = _image(path, decode=True)
    if image['codec'] != 'png':
        raise ApiError('invalid_preview_output', 'Thumbnail output must decode as PNG.')
    return {'intent': intent, 'cover_path': str(path), 'image': image, 'sha256': _digest(path), 'bytes': path.stat().st_size}


def preview_verify(operation, args, root):
    published = args.get('published', {})
    intent, item = _intent({'intent': published.get('intent')}, root)
    # A native custom cover is selected by cover_tid. The exact cache path is
    # resolved below from the frozen type table, never by writing thumb_tid.
    cover = _cover_path(Catalog(root), item['id'], item['cover_tid'])
    verified = item['has_cover'] and cover is not None and cover.is_file() and _digest(cover) == published.get('sha256')
    return {'verified': verified, 'retryable': not verified, 'item_id': item['id'],
            'cover_tid': item['cover_tid'], 'cover_path': str(cover) if cover else None,
            'source_unchanged': True, 'metadata_unchanged': True, 'library_root': intent['library_root']}


def _cover_path(catalog, identifier, cover_tid):
    if cover_tid == '0':
        return None
    with catalog._connection() as db:
        rows = db.execute('SELECT name FROM bf_type WHERE tid=?', (_id(cover_tid, 'cover_tid'),)).fetchall()
    if len(rows) != 1:
        return None
    extension = rows[0]['name']
    if not isinstance(extension, str) or not extension.isascii() or not extension.isalnum():
        raise ApiError('unsupported_schema', 'The native cover extension is not a safe type name.')
    bucket = catalog.root / '.bf/.preview' / f'{int(identifier) % 256:02x}'
    return bucket / f'{identifier}.cover.{extension}'


def preview_types(operation, args, root):
    extensions = args.get('extensions')
    if (not isinstance(extensions, list) or not 1 <= len(extensions) <= 128
            or len(extensions) != len(set(extensions))
            or any(not isinstance(name, str) or not re.fullmatch(r'[a-z0-9][a-z0-9_-]{0,31}', name) for name in extensions)):
        raise ApiError('invalid_arguments', 'Expected unique lowercase extension names without leading dots.')
    existing, missing = [], []
    with Catalog(root)._connection() as db:
        for name in extensions:
            rows = db.execute('SELECT tid,name,gid,can_analyze FROM bf_type WHERE lower(name)=?', (name,)).fetchall()
            if not rows:
                missing.append(name)
            elif len(rows) != 1 or not isinstance(rows[0]['tid'], int) or rows[0]['tid'] <= 0:
                raise ApiError('unsupported_schema', 'A native type has an ambiguous or invalid ID.', {'extension': name})
            else:
                existing.append(dict(rows[0]))
    return {'extensions': extensions, 'missing': missing, 'types': existing, 'verified': not missing,
            'library_root': str(Path(root).absolute())}
