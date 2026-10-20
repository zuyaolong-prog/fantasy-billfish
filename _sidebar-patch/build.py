"""Build a fixed-layout Qt resource patch for the installed frozen tree plugin."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

BASE_SHA256 = 'b26e12f54b569da9aa9803103615bcdce99fade2377aae542346ccf601d8b956'
RESOURCES = [(288432, 316480, 'BfLeftPanel.qml'), (294848, 316612, 'BfFolderTreeView.qml')]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def build(original, sources):
    if digest(original) != BASE_SHA256:
        raise ValueError('Unexpected treemodule.dll identity')
    result = bytearray(original)
    changes = []
    for offset, timestamp_offset, name in RESOURCES:
        old_size, raw_size = struct.unpack_from('>II', original, offset - 8)
        capacity = old_size - 4
        decoder = zlib.decompressobj()
        old_raw = decoder.decompress(original[offset:offset + capacity])
        if not decoder.eof or decoder.unused_data or len(old_raw) != raw_size:
            raise ValueError(f'Unexpected Qt compressed resource boundary: {name}')
        raw = (sources / name).read_bytes()
        # Only strip complete comment/blank lines; preserve all executable QML.
        lines = raw.decode('utf-8-sig').splitlines()
        raw = ('\n'.join(line for line in lines
                         if line.strip() and not line.lstrip().startswith('//')) + '\n').encode('utf-8')
        compressed = zlib.compress(raw, 9)
        if len(compressed) > capacity:
            raise ValueError(f'Resource exceeds fixed capacity: {name}')
        struct.pack_into('>II', result, offset - 8, len(compressed) + 4, len(raw))
        result[offset:offset + capacity] = compressed + bytes(capacity - len(compressed))
        # Qt's disk cache keys qrc sources by the RCC v3 file timestamp. Keeping
        # the frozen timestamp would silently execute stale compiled QML.
        original_timestamp = struct.unpack_from('>Q', original, timestamp_offset)[0]
        timestamp = (sources / name).stat().st_mtime_ns // 1_000_000
        if timestamp <= original_timestamp:
            raise ValueError(f'Source timestamp must be newer than frozen resource: {name}')
        struct.pack_into('>Q', result, timestamp_offset, timestamp)
        if zlib.decompress(result[offset:offset + len(compressed)]) != raw:
            raise ValueError(f'Roundtrip failed: {name}')
        changes.append(dict(name=name, offset=offset - 8, length=capacity + 8,
                            original_resource_sha256=digest(old_raw),
                            source_sha256=digest((sources / name).read_bytes()),
                            installed_resource_sha256=digest(raw),
                            original_compressed_bytes=capacity,
                            compressed_bytes=len(compressed),
                            timestamp_offset=timestamp_offset,
                            original_timestamp_ms=original_timestamp,
                            source_timestamp_ms=timestamp))
    for i, (before, after) in enumerate(zip(original, result)):
        if before != after and not any(c['offset'] <= i < c['offset'] + c['length']
                                      or c['timestamp_offset'] <= i < c['timestamp_offset'] + 8
                                      for c in changes):
            raise ValueError('Unexpected change outside the resource slots')
    return bytes(result), changes


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    original = args.input.read_bytes()
    candidate, changes = build(original, Path(__file__).parent / 'src')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(candidate)
    report = dict(original_sha256=digest(original), candidate_sha256=digest(candidate),
                  original_bytes=len(original), candidate_bytes=len(candidate), resources=changes)
    args.output.with_suffix('.resources.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
