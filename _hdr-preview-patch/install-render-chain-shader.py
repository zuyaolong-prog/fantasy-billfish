import argparse
import hashlib
import json
import shutil
import struct
import sys
import time
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DLL_PATH = ROOT / r"plugins\media_runtime\libMediaSDK64.dll"
BACKUP_DIR = ROOT / r"_hdr-preview-patch\backups"
STATE_PATH = ROOT / r"_hdr-preview-patch\state\render-chain-shader-patch.json"
TARGET_NAME = "RenderGLSLCommon/gles_color_to_rgba.frag"
MARKER = "bf_hdr_shader_v1"


PATCH_SHADER = r"""#version 100
precision highp float;
uniform sampler2D u_texture,u_texture_u,u_texture_v,u_texture_uv;uniform vec3 v_offset;uniform mat3 v_color_conversion;varying vec2 v_texCoord;
vec3 s(vec3 x){x=pow(clamp(x,0.,1.),vec3(.0126833));x=pow(max(x-.8359375,0.)/(18.8516-18.6875*x),vec3(6.2774));x=vec3(1.6605*x.r-.5876*x.g-.0728*x.b,-.1246*x.r+1.1329*x.g-.0083*x.b,-.0182*x.r-.1006*x.g+1.1187*x.b);x=max(x,0.)*80.;x=x/(1.+x);return pow(clamp(x,0.,1.),vec3(.454545));}
void main(){
#if defined(TextureBGRA)
vec4 c=texture2D(u_texture,v_texCoord);gl_FragColor=vec4(c.z,c.y,c.x,c.w);
#elif defined(TextureBGR)
vec4 c=texture2D(u_texture,v_texCoord);gl_FragColor=vec4(c.z,c.y,c.x,1.);
#elif defined(TextureRGB)
vec4 c=texture2D(u_texture,v_texCoord);gl_FragColor=vec4(c.xyz,1.);
#elif defined(TextureRGBA)
gl_FragColor=texture2D(u_texture,v_texCoord);
#elif defined(TextureYUV)
vec3 y;vec3 r;
#ifdef TextureYUV10Bit
vec4 a=texture2D(u_texture,v_texCoord),b=texture2D(u_texture_u,v_texCoord),c=texture2D(u_texture_v,v_texCoord);y=(vec3(a.r,b.r,c.r)*255.+vec3(a.a,b.a,c.a)*65280.)/1023.;
#else
y=vec3(texture2D(u_texture,v_texCoord).r,texture2D(u_texture_u,v_texCoord).r,texture2D(u_texture_v,v_texCoord).r);
#endif
y+=v_offset;r=y*v_color_conversion;
#ifdef TextureYUV10Bit
r=s(r);
#endif
gl_FragColor=vec4(r,1.);
#elif defined(TextureNV12)
vec3 y=vec3(texture2D(u_texture,v_texCoord).r,texture2D(u_texture_uv,v_texCoord).rg);y+=v_offset;gl_FragColor=vec4(y*v_color_conversion,1.);
#endif
}
"""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def raw_deflate(data: bytes, level: int = 9, strategy: int = zlib.Z_DEFAULT_STRATEGY) -> bytes:
    compressor = zlib.compressobj(level, zlib.DEFLATED, -15, 9, strategy)
    return compressor.compress(data) + compressor.flush()


def pad_shader_to_size(shader: bytes, size: int) -> bytes:
    if len(shader) > size:
        raise RuntimeError(f"patched shader is {len(shader)} bytes, larger than {size}")
    prefix = f"\n/*{MARKER}".encode("ascii")
    suffix = b"*/"
    if len(shader) == size:
        return shader
    pad_len = size - len(shader) - len(prefix) - len(suffix)
    if pad_len < 0:
        raise RuntimeError("not enough space for marker padding")
    return shader + prefix + (b" " * pad_len) + suffix


def decode_local_name(data: bytes, offset: int) -> str | None:
    if offset + 30 > len(data) or data[offset : offset + 4] != b"PK\x03\x04":
        return None
    name_len, extra_len = struct.unpack_from("<HH", data, offset + 26)
    name_start = offset + 30
    name_end = name_start + name_len
    if name_end > len(data):
        return None
    raw_name = data[name_start:name_end]
    try:
        return raw_name.decode("utf-8")
    except UnicodeDecodeError:
        return raw_name.decode("gbk", errors="replace")


def iter_local_headers(data: bytes) -> list[dict]:
    headers: list[dict] = []
    pos = 0
    while True:
        offset = data.find(b"PK\x03\x04", pos)
        if offset == -1:
            break
        if offset + 30 <= len(data):
            flags, method, _mod_time, _mod_date, crc, comp, uncomp, name_len, extra_len = struct.unpack_from(
                "<HHHHIIIHH", data, offset + 6
            )
            name = decode_local_name(data, offset)
            data_start = offset + 30 + name_len + extra_len
            if name:
                decompressor = zlib.decompressobj(-15)
                decoded = decompressor.decompress(data[data_start:])
                consumed = len(data[data_start:]) - len(decompressor.unused_data)
                data_end = data_start + consumed
                headers.append(
                    {
                        "offset": offset,
                        "name": name,
                        "flags": flags,
                        "method": method,
                        "header_crc": crc,
                        "header_compressed_size": comp,
                        "header_uncompressed_size": uncomp,
                        "data_start": data_start,
                        "data_end": data_end,
                        "consumed_size": consumed,
                        "decoded": decoded,
                    }
                )
        pos = offset + 1
    return headers


def central_directory_entries(data: bytes) -> list[dict]:
    entries: list[dict] = []
    pos = 0
    while True:
        offset = data.find(b"PK\x01\x02", pos)
        if offset == -1:
            break
        if offset + 46 <= len(data):
            (
                crc,
                comp,
                uncomp,
                name_len,
                extra_len,
                comment_len,
                _disk_start,
                _internal_attrs,
                _external_attrs,
                local_offset,
            ) = struct.unpack_from(
                "<IIIHHHHHII", data, offset + 16
            )
            name_start = offset + 46
            name_end = name_start + name_len
            if name_end <= len(data):
                raw_name = data[name_start:name_end]
                try:
                    name = raw_name.decode("utf-8")
                except UnicodeDecodeError:
                    name = raw_name.decode("gbk", errors="replace")
                entries.append(
                    {
                        "offset": offset,
                        "name": name,
                        "crc": crc,
                        "compressed_size": comp,
                        "uncompressed_size": uncomp,
                        "local_offset": local_offset,
                        "extra_len": extra_len,
                        "comment_len": comment_len,
                    }
                )
        pos = offset + 1
    return entries


def choose_compressed_payload(shader: bytes, max_size: int) -> bytes:
    best: bytes | None = None
    for level in range(1, 10):
        for strategy in [
            zlib.Z_DEFAULT_STRATEGY,
            zlib.Z_FILTERED,
            zlib.Z_HUFFMAN_ONLY,
            zlib.Z_RLE,
            zlib.Z_FIXED,
        ]:
            packed = raw_deflate(shader, level, strategy)
            if len(packed) <= max_size and (best is None or len(packed) < len(best)):
                best = packed
    if best is None:
        raise RuntimeError(f"patched shader does not fit the original {max_size} byte slot")
    return best + (b"\0" * (max_size - len(best)))


def build_patch(data: bytes) -> tuple[bytes, dict]:
    target = None
    for header in iter_local_headers(data):
        if header["name"] == TARGET_NAME:
            target = header
            break
    if target is None:
        raise RuntimeError(f"{TARGET_NAME} was not found in local resource headers")
    if target["method"] != 8:
        raise RuntimeError(f"{TARGET_NAME} uses unsupported compression method {target['method']}")

    original_shader = target["decoded"]
    if MARKER.encode("ascii") in original_shader:
        return data, {"already_installed": True, "target": summarize_target(target)}
    if b"TextureYUV10Bit" not in original_shader or b"v_color_conversion" not in original_shader:
        raise RuntimeError("target shader does not match the expected Billfish YUV shader")

    patched_shader = pad_shader_to_size(PATCH_SHADER.encode("utf-8"), len(original_shader))
    packed = choose_compressed_payload(patched_shader, target["consumed_size"])
    crc = zlib.crc32(patched_shader) & 0xFFFFFFFF

    patched = bytearray(data)
    patched[target["data_start"] : target["data_start"] + target["consumed_size"]] = packed

    descriptor_offset = target["data_end"]
    descriptor = data[descriptor_offset : descriptor_offset + 16]
    if descriptor[:4] != b"PK\x07\x08":
        raise RuntimeError(f"data descriptor signature missing at {descriptor_offset:#x}")
    struct.pack_into("<III", patched, descriptor_offset + 4, crc, target["consumed_size"], len(patched_shader))

    central_hits = []
    for entry in central_directory_entries(data):
        if entry["name"] == TARGET_NAME:
            struct.pack_into("<III", patched, entry["offset"] + 16, crc, target["consumed_size"], len(patched_shader))
            central_hits.append(entry)
    if not central_hits:
        raise RuntimeError(f"{TARGET_NAME} was not found in central directory")

    metadata = {
        "already_installed": False,
        "target": summarize_target(target),
        "patched_crc": f"{crc:08x}",
        "patched_compressed_payload_size": len(packed),
        "patched_uncompressed_size": len(patched_shader),
        "central_directory_hits": [
            {
                "offset": entry["offset"],
                "offset_hex": hex(entry["offset"]),
                "local_offset": entry["local_offset"],
            }
            for entry in central_hits
        ],
    }
    return bytes(patched), metadata


def summarize_target(target: dict) -> dict:
    return {
        "name": target["name"],
        "data_start": target["data_start"],
        "data_start_hex": hex(target["data_start"]),
        "data_end": target["data_end"],
        "data_end_hex": hex(target["data_end"]),
        "compressed_size": target["consumed_size"],
        "uncompressed_size": len(target["decoded"]),
    }


def install(dry_run: bool) -> int:
    if not DLL_PATH.exists():
        raise RuntimeError(f"missing DLL: {DLL_PATH}")
    original_hash = sha256(DLL_PATH)
    data = DLL_PATH.read_bytes()
    patched, metadata = build_patch(data)
    metadata["dll"] = str(DLL_PATH)
    metadata["original_sha256"] = original_hash

    if metadata["already_installed"]:
        print("status=already-installed")
        print(json.dumps(metadata, indent=2, ensure_ascii=False))
        return 0
    if dry_run:
        print("status=dry-run-ok")
        print(json.dumps(metadata, indent=2, ensure_ascii=False))
        return 0

    BACKUP_DIR.mkdir(parents=True, exist_ok=True)
    STATE_PATH.parent.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    backup_path = BACKUP_DIR / f"libMediaSDK64-render-chain-{original_hash[:12]}-{timestamp}.dll"
    shutil.copy2(DLL_PATH, backup_path)
    DLL_PATH.write_bytes(patched)
    patched_hash = sha256(DLL_PATH)

    state = {
        **metadata,
        "installed_at": timestamp,
        "backup_path": str(backup_path),
        "patched_sha256": patched_hash,
        "restore_command": f"python {Path(__file__)} --restore",
    }
    STATE_PATH.write_text(json.dumps(state, indent=2, ensure_ascii=False), encoding="utf-8")
    print("status=installed")
    print(json.dumps(state, indent=2, ensure_ascii=False))
    return 0


def restore() -> int:
    if not STATE_PATH.exists():
        print("status=no-state")
        return 1
    state = json.loads(STATE_PATH.read_text(encoding="utf-8"))
    backup_path = Path(state["backup_path"])
    if not backup_path.exists():
        raise RuntimeError(f"backup is missing: {backup_path}")
    current_hash = sha256(DLL_PATH)
    expected = state.get("patched_sha256")
    if expected and current_hash != expected:
        raise RuntimeError(
            f"current DLL hash {current_hash} does not match installed patch hash {expected}; refusing restore"
        )
    shutil.copy2(backup_path, DLL_PATH)
    print("status=restored")
    print(f"restored_from={backup_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--restore", action="store_true")
    args = parser.parse_args()
    if args.restore:
        return restore()
    return install(dry_run=args.dry_run)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - installer CLI
        print(f"error={exc}", file=sys.stderr)
        raise SystemExit(1)
