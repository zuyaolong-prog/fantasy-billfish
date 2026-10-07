#!/usr/bin/env python3
"""
Repair Billfish analysis records for AV1 MP4 files by using an external
AV1-capable ffmpeg/ffprobe, without modifying the source videos.

Default mode is dry-run: it probes files and prints the DB changes it would
make. Use --apply only after Billfish is closed.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import sqlite3
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
LIBRARY_ROOT: Path
DB_PATH: Path
PREVIEW_ROOT: Path
PATCH_ROOT = ROOT / "_av1-analysis-patch"
BACKUP_ROOT = PATCH_ROOT / "backups"
WORK_ROOT = PATCH_ROOT / "work"
STATE_ROOT = PATCH_ROOT / "state"
SCAN_CACHE_PATH = STATE_ROOT / "scan-cache.json"

@dataclass(frozen=True)
class AnalysisResult:
    file_id: int
    path: Path
    width: int
    height: int
    duration_ms: int
    rotation: int
    colors: str
    bf_extend: str
    small_preview: Path
    hd_preview: Path
    temp_small: Path
    temp_hd: Path


def configure_console() -> None:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def run(args: list[str], *, capture_json: bool = False) -> Any:
    proc = subprocess.run(
        args,
        check=False,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            "Command failed:\n"
            + " ".join(args)
            + f"\nexit={proc.returncode}\nstdout={proc.stdout}\nstderr={proc.stderr}"
        )
    if capture_json:
        return json.loads(proc.stdout)
    return proc.stdout


def require_tool(name: str, override: str | None = None) -> str:
    bundled = ROOT / "tools" / "ffmpeg" / "bin" / f"{name}.exe"
    candidate = override or (str(bundled) if bundled.is_file() else shutil.which(name))
    if not candidate:
        raise SystemExit(f"Missing required tool: {name}")
    return candidate


def check_processes_not_running(billfish_root: Path = ROOT) -> None:
    ps = shutil.which("powershell") or shutil.which("pwsh")
    if not ps:
        raise SystemExit("Refusing to modify the library because PowerShell process inspection is unavailable.")
    escaped_root = str(billfish_root.resolve()).replace("'", "''")
    script = (
        f"$root=[IO.Path]::GetFullPath('{escaped_root}').TrimEnd('\\');$prefix=$root+'\\';"
        "$fixed=@((Join-Path $root 'Billfish.exe'),(Join-Path $root 'Billfish.core.exe'))|%{[IO.Path]::GetFullPath($_)};"
        "$matches=@();foreach($p in @(Get-Process -Name Billfish,Billfish.core,PluginWorker,PluginWorker64 -ErrorAction SilentlyContinue)){"
        "$path=$null;try{$path=[string]$p.Path}catch{};if([string]::IsNullOrWhiteSpace($path)){"
        "$matches+=[pscustomobject]@{name=$p.ProcessName;pid=$p.Id;path=$null;reason='path-unavailable'};continue};"
        "$full=[IO.Path]::GetFullPath($path);$fixedMatch=@($fixed|?{$_.Equals($full,[StringComparison]::OrdinalIgnoreCase)}).Count-gt0;"
        "$worker=$full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)-and([IO.Path]::GetFileNameWithoutExtension($full)-in@('PluginWorker','PluginWorker64'));"
        "if($fixedMatch-or$worker){$matches+=[pscustomobject]@{name=$p.ProcessName;pid=$p.Id;path=$full;reason='billfish-root'}}};"
        "ConvertTo-Json -Compress -InputObject @($matches)"
    )
    cmd = [
        ps,
        "-NoProfile",
        "-Command",
        script,
    ]
    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        raise SystemExit("Refusing to modify the library because Billfish process inspection failed.")
    try:
        matches = json.loads(proc.stdout or "[]")
    except json.JSONDecodeError as exc:
        raise SystemExit("Refusing to modify the library because Billfish process inspection returned invalid data.") from exc
    if matches:
        joined = ", ".join(
            f"{item.get('name')} PID {item.get('pid')} ({item.get('path') or item.get('reason')})"
            for item in matches
        )
        raise SystemExit(f"Refusing to modify the library while these processes are running: {joined}")


def preview_bucket(file_id: int) -> str:
    return f"{file_id % 256:02x}"


def preview_paths(file_id: int) -> tuple[Path, Path]:
    bucket = PREVIEW_ROOT / preview_bucket(file_id)
    return bucket / f"{file_id}.small.webp", bucket / f"{file_id}.hd.webp"


def load_scan_cache() -> dict[str, Any]:
    if not SCAN_CACHE_PATH.exists():
        return {}
    try:
        return json.loads(SCAN_CACHE_PATH.read_text(encoding="utf-8"))
    except Exception:
        return {}


def save_scan_cache(cache: dict[str, Any]) -> None:
    STATE_ROOT.mkdir(parents=True, exist_ok=True)
    SCAN_CACHE_PATH.write_text(json.dumps(cache, ensure_ascii=False, indent=2), encoding="utf-8")


def build_folder_path_map(conn: sqlite3.Connection) -> dict[int, Path]:
    conn.row_factory = sqlite3.Row
    rows = conn.execute("SELECT id, pid, name FROM bf_folder").fetchall()
    folders = {int(row["id"]): (int(row["pid"]), str(row["name"])) for row in rows}
    memo: dict[int, Path] = {}

    def resolve(folder_id: int) -> Path:
        if folder_id in memo:
            return memo[folder_id]
        if folder_id == 0:
            return LIBRARY_ROOT
        if folder_id not in folders:
            raise RuntimeError(f"Folder id not found: {folder_id}")
        parent_id, name = folders[folder_id]
        if parent_id in (-1, 0):
            path = LIBRARY_ROOT / name
        else:
            path = resolve(parent_id) / name
        memo[folder_id] = path
        return path

    for folder_id in folders:
        try:
            resolve(folder_id)
        except RuntimeError:
            pass
    return memo


def quick_video_codec(ffprobe: str, path: Path) -> str:
    data = run(
        [
            ffprobe,
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=codec_name",
            "-of",
            "json",
            str(path),
        ],
        capture_json=True,
    )
    streams = data.get("streams") or []
    return str(streams[0].get("codec_name") or "") if streams else ""


TRANSIENT_SCAN_STATES = {"missing", "probe_error"}


def discover_targets(
    ffprobe: str,
    *,
    codec_filter: str | None,
    label: str,
    limit: int | None = None,
) -> list[dict[str, Any]]:
    conn = db_connect(readonly=True)
    cache = load_scan_cache()
    discovered: list[dict[str, Any]] = []
    checked = 0
    skipped_unreadable = 0
    try:
        folder_paths = build_folder_path_map(conn)
        conn.row_factory = sqlite3.Row
        rows = conn.execute(
            """
            SELECT f.id, f.name, f.pid, f.file_size, f.mtime,
                   m.status, m.w, m.h, v.file_id AS has_video_row
            FROM bf_file f
            LEFT JOIN bf_material_v2 m ON m.file_id = f.id
            LEFT JOIN bf_material_video v ON v.file_id = f.id
            WHERE f.tid = 220
              AND COALESCE(m.is_recycle, 0) = 0
              AND (
                    COALESCE(m.status, 0) <> 1
                 OR COALESCE(m.w, 0) = 0
                 OR COALESCE(m.h, 0) = 0
                 OR v.file_id IS NULL
              )
            ORDER BY f.id
            """
        ).fetchall()
        print(f"Scan candidates: {len(rows)} mp4 rows need analysis metadata.")
        for row in rows:
            file_id = int(row["id"])
            folder = folder_paths.get(int(row["pid"]))
            if folder is None:
                continue
            path = folder / str(row["name"])
            signature = f"{file_id}:{row['file_size']}:{row['mtime']}"
            cached = cache.get(signature)
            cached_codec = cached.get("codec") if cached else ""
            if cached_codec and cached_codec not in TRANSIENT_SCAN_STATES:
                if codec_filter is None or cached_codec == codec_filter:
                    discovered.append({"file_id": file_id, "path": path})
                    if limit and len(discovered) >= limit:
                        break
                continue
            if not path.exists():
                cache[signature] = {"codec": "missing", "path": str(path), "checked_at": int(time.time())}
                skipped_unreadable += 1
                continue
            try:
                codec = quick_video_codec(ffprobe, path)
            except Exception as exc:
                cache[signature] = {
                    "codec": "probe_error",
                    "path": str(path),
                    "error": str(exc)[:500],
                    "checked_at": int(time.time()),
                }
                skipped_unreadable += 1
                continue
            cache[signature] = {"codec": codec, "path": str(path), "checked_at": int(time.time())}
            checked += 1
            if codec_filter is None or codec == codec_filter:
                discovered.append({"file_id": file_id, "path": path})
                if limit and len(discovered) >= limit:
                    break
    finally:
        conn.close()
        save_scan_cache(cache)
    print(f"Newly probed: {checked}; skipped unreadable/missing: {skipped_unreadable}; {label} repairs queued: {len(discovered)}")
    return discovered


def discover_av1_targets(ffprobe: str, *, limit: int | None = None) -> list[dict[str, Any]]:
    return discover_targets(ffprobe, codec_filter="av1", label="AV1", limit=limit)


def discover_mp4_targets(ffprobe: str, *, limit: int | None = None) -> list[dict[str, Any]]:
    return discover_targets(ffprobe, codec_filter=None, label="MP4", limit=limit)


def rational_to_float(value: str | None) -> float:
    if not value or value == "0/0":
        return 0.0
    if "/" in value:
        num, den = value.split("/", 1)
        den_f = float(den)
        return float(num) / den_f if den_f else 0.0
    return float(value)


def seconds_text(value: float) -> str:
    return f"{value:.3f}".rstrip("0").rstrip(".") if value else "0"


def utc_file_time(path: Path, attr: str) -> str:
    timestamp = getattr(path.stat(), attr)
    return datetime.fromtimestamp(timestamp, timezone.utc).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3] + " UTC"


def local_file_time(path: Path, attr: str) -> str:
    timestamp = getattr(path.stat(), attr)
    return datetime.fromtimestamp(timestamp).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def stream_duration(stream: dict[str, Any], fallback: float) -> float:
    try:
        return float(stream.get("duration") or fallback)
    except (TypeError, ValueError):
        return fallback


def build_media_info_json(path: Path, probe: dict[str, Any]) -> str:
    fmt = probe["format"]
    streams = probe.get("streams", [])
    video = next(s for s in streams if s.get("codec_type") == "video")
    audios = [s for s in streams if s.get("codec_type") == "audio"]

    duration = float(fmt.get("duration") or stream_duration(video, 0.0))
    frame_rate = rational_to_float(video.get("avg_frame_rate")) or rational_to_float(video.get("r_frame_rate"))
    frame_count = video.get("nb_frames")
    if not frame_count and frame_rate and duration:
        frame_count = str(round(frame_rate * duration))

    width = int(video["width"])
    height = int(video["height"])
    bit_rate = fmt.get("bit_rate") or video.get("bit_rate") or "0"
    codec_id = video.get("codec_tag_string") or video.get("codec_name") or ""
    compatible = fmt.get("tags", {}).get("compatible_brands", "")
    major_brand = fmt.get("tags", {}).get("major_brand", "isom")

    general: dict[str, Any] = {
        "@type": "General",
        "VideoCount": "1",
        "AudioCount": str(len(audios)),
        "FileExtension": path.suffix.lstrip(".").lower(),
        "Format": "MPEG-4",
        "Format_Profile": "Base Media",
        "CodecID": major_brand,
        "CodecID_Compatible": "/".join(
            compatible[i : i + 4] for i in range(0, len(compatible), 4)
        )
        if compatible
        else major_brand,
        "FileSize": str(path.stat().st_size),
        "Duration": seconds_text(duration),
        "OverallBitRate": str(round(float(bit_rate))) if str(bit_rate).replace(".", "", 1).isdigit() else str(bit_rate),
        "FrameRate": f"{frame_rate:.3f}" if frame_rate else "0",
        "FrameCount": str(frame_count or ""),
        "IsStreamable": "Yes",
        "File_Created_Date": utc_file_time(path, "st_ctime"),
        "File_Created_Date_Local": local_file_time(path, "st_ctime"),
        "File_Modified_Date": utc_file_time(path, "st_mtime"),
        "File_Modified_Date_Local": local_file_time(path, "st_mtime"),
        "Encoded_Application": fmt.get("tags", {}).get("encoder", ""),
    }

    v_duration = stream_duration(video, duration)
    video_track: dict[str, Any] = {
        "@type": "Video",
        "StreamOrder": str(video.get("index", 0)),
        "ID": str(video.get("id", "1")).replace("0x", ""),
        "Format": "AV1" if video.get("codec_name") == "av1" else str(video.get("codec_long_name", "")).split()[0],
        "Format_Profile": video.get("profile", ""),
        "Format_Level": str(video.get("level", "")),
        "CodecID": codec_id,
        "Duration": seconds_text(v_duration),
        "BitRate": str(video.get("bit_rate") or ""),
        "Width": str(width),
        "Height": str(height),
        "Sampled_Width": str(width),
        "Sampled_Height": str(height),
        "PixelAspectRatio": "1.000",
        "DisplayAspectRatio": f"{(width / height):.3f}" if height else "0",
        "Rotation": "0.000",
        "FrameRate_Mode": "VFR",
        "FrameRate": f"{frame_rate:.3f}" if frame_rate else "0",
        "FrameCount": str(video.get("nb_frames") or frame_count or ""),
        "ColorSpace": "YUV" if str(video.get("pix_fmt", "")).startswith("yuv") else "",
        "ChromaSubsampling": "4:2:0" if "420" in str(video.get("pix_fmt", "")) else "",
        "BitDepth": "10" if "10" in str(video.get("pix_fmt", "")) else "8",
        "Language": video.get("tags", {}).get("language", "und"),
    }

    tracks: list[dict[str, Any]] = [general, video_track]
    for audio_index, audio in enumerate(audios, start=1):
        a_duration = stream_duration(audio, duration)
        sample_rate = int(audio.get("sample_rate") or 0)
        channels = int(audio.get("channels") or 0)
        samples = round(a_duration * sample_rate) if sample_rate and a_duration else ""
        tracks.append(
            {
                "@type": "Audio",
                "StreamOrder": str(audio.get("index", audio_index)),
                "ID": str(audio.get("id", str(audio_index + 1))).replace("0x", ""),
                "Format": "AAC" if audio.get("codec_name") == "aac" else audio.get("codec_name", ""),
                "Format_AdditionalFeatures": audio.get("profile", ""),
                "CodecID": audio.get("codec_tag_string", ""),
                "Duration": seconds_text(a_duration),
                "BitRate_Mode": "CBR",
                "BitRate": str(audio.get("bit_rate") or ""),
                "Channels": str(channels),
                "ChannelPositions": "Front: L R" if channels == 2 else "",
                "ChannelLayout": audio.get("channel_layout", ""),
                "SamplesPerFrame": "1024" if audio.get("codec_name") == "aac" else "",
                "SamplingRate": str(sample_rate or ""),
                "SamplingCount": str(samples),
                "FrameRate": "46.875" if sample_rate == 48000 and audio.get("codec_name") == "aac" else "",
                "FrameCount": str(audio.get("nb_frames") or ""),
                "Compression_Mode": "Lossy",
                "Language": audio.get("tags", {}).get("language", "und"),
            }
        )

    media_info = {
        "creatingLibrary": {
            "name": "MediaInfoLib",
            "version": "23.03",
            "url": "https://mediaarea.net/MediaInfo",
        },
        "media": {"track": tracks},
    }
    return json.dumps({"media_info_json_str": json.dumps(media_info, ensure_ascii=False)}, ensure_ascii=False)


def extract_previews(ffmpeg: str, path: Path, duration_ms: int, file_id: int) -> tuple[Path, Path]:
    WORK_ROOT.mkdir(parents=True, exist_ok=True)
    seek = max(0.2, min(2.0, duration_ms / 1000.0 * 0.1))
    temp_hd = WORK_ROOT / f"{file_id}.hd.webp"
    temp_small = WORK_ROOT / f"{file_id}.small.webp"

    run(
        [
            ffmpeg,
            "-y",
            "-hide_banner",
            "-loglevel",
            "error",
            "-ss",
            f"{seek:.3f}",
            "-i",
            str(path),
            "-map",
            "0:v:0",
            "-frames:v",
            "1",
            "-vf",
            "scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2",
            "-quality",
            "85",
            str(temp_hd),
        ]
    )
    run(
        [
            ffmpeg,
            "-y",
            "-hide_banner",
            "-loglevel",
            "error",
            "-ss",
            f"{seek:.3f}",
            "-i",
            str(path),
            "-map",
            "0:v:0",
            "-frames:v",
            "1",
            "-vf",
            "scale=-1:480",
            "-quality",
            "82",
            str(temp_small),
        ]
    )
    return temp_small, temp_hd


def image_colors(image_path: Path, count: int = 5) -> str:
    with Image.open(image_path) as image:
        rgb = image.convert("RGB")
        rgb.thumbnail((128, 128))
        palette = rgb.quantize(colors=count, method=Image.Quantize.MEDIANCUT)
        palette_rgb = palette.convert("RGB")
        color_counts = palette.getcolors(maxcolors=256) or []
        total = sum(n for n, _idx in color_counts) or 1
        entries: list[tuple[float, int]] = []
        pal = palette.getpalette() or []
        for n, idx in sorted(color_counts, reverse=True)[:count]:
            r, g, b = pal[idx * 3 : idx * 3 + 3]
            packed = (r << 16) | (g << 8) | b
            entries.append((n * 100.0 / total, packed))
        return "|".join(f"{pct:.2f},{packed}" for pct, packed in entries)


def analyze_target(ffprobe: str, ffmpeg: str, target: dict[str, Any]) -> AnalysisResult:
    file_id = int(target["file_id"])
    path = Path(target["path"])
    if not path.exists():
        raise FileNotFoundError(path)
    probe = run(
        [
            ffprobe,
            "-v",
            "error",
            "-print_format",
            "json",
            "-show_format",
            "-show_streams",
            str(path),
        ],
        capture_json=True,
    )
    video = next((s for s in probe.get("streams", []) if s.get("codec_type") == "video"), None)
    if not video:
        raise RuntimeError(f"No video stream found: {path}")
    width = int(video["width"])
    height = int(video["height"])
    duration = float(probe.get("format", {}).get("duration") or video.get("duration") or 0.0)
    duration_ms = int(round(duration * 1000))
    rotation = int(float(video.get("tags", {}).get("rotate", 0) or 0))
    temp_small, temp_hd = extract_previews(ffmpeg, path, duration_ms, file_id)
    small_preview, hd_preview = preview_paths(file_id)
    colors = image_colors(temp_small)
    bf_extend = build_media_info_json(path, probe)
    return AnalysisResult(
        file_id=file_id,
        path=path,
        width=width,
        height=height,
        duration_ms=duration_ms,
        rotation=rotation,
        colors=colors,
        bf_extend=bf_extend,
        small_preview=small_preview,
        hd_preview=hd_preview,
        temp_small=temp_small,
        temp_hd=temp_hd,
    )


def db_connect(readonly: bool) -> sqlite3.Connection:
    if readonly:
        return sqlite3.connect(f"file:{DB_PATH}?mode=ro", uri=True)
    return sqlite3.connect(DB_PATH)


def current_db_row(conn: sqlite3.Connection, file_id: int) -> dict[str, Any]:
    conn.row_factory = sqlite3.Row
    row = conn.execute(
        """
        SELECT f.id AS file_id, f.name, f.file_size, f.mtime,
               m.status, m.thumb_tid, m.image_tid, m.w, m.h, m.is_recycle, m.colors,
               v.duration AS video_duration, v.rotation, length(v.bf_extend) AS bf_extend_len
        FROM bf_file f
        LEFT JOIN bf_material_v2 m ON m.file_id = f.id
        LEFT JOIN bf_material_video v ON v.file_id = f.id
        WHERE f.id = ?
        """,
        (file_id,),
    ).fetchone()
    if row is None:
        raise RuntimeError(f"file_id not found in DB: {file_id}")
    return dict(row)


def make_backup(results: list[AnalysisResult]) -> Path:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_dir = BACKUP_ROOT / stamp
    preview_dir = backup_dir / "preview"
    preview_dir.mkdir(parents=True, exist_ok=False)
    shutil.copy2(DB_PATH, backup_dir / "billfish.db")
    for result in results:
        for preview in (result.small_preview, result.hd_preview):
            if preview.exists():
                rel = preview.relative_to(PREVIEW_ROOT)
                dst = preview_dir / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(preview, dst)
    manifest = {
        "created_at": stamp,
        "library_root": str(LIBRARY_ROOT),
        "db_backup": "billfish.db",
        "preview_backup": "preview",
        "file_ids": [r.file_id for r in results],
    }
    (backup_dir / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    return backup_dir


def apply_results(results: list[AnalysisResult]) -> Path:
    backup_dir = make_backup(results)
    for result in results:
        result.small_preview.parent.mkdir(parents=True, exist_ok=True)
        result.hd_preview.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(result.temp_small, result.small_preview)
        shutil.copy2(result.temp_hd, result.hd_preview)

    conn = db_connect(readonly=False)
    try:
        with conn:
            for result in results:
                existing = current_db_row(conn, result.file_id)
                is_recycle = existing.get("is_recycle")
                if is_recycle is None:
                    is_recycle = 0
                conn.execute(
                    """
                    INSERT INTO bf_material_v2(file_id, status, thumb_tid, image_tid, w, h, is_recycle, colors)
                    VALUES(?, 1, 60, 60, ?, ?, ?, ?)
                    ON CONFLICT(file_id) DO UPDATE SET
                      status = excluded.status,
                      thumb_tid = excluded.thumb_tid,
                      image_tid = excluded.image_tid,
                      w = excluded.w,
                      h = excluded.h,
                      is_recycle = excluded.is_recycle,
                      colors = excluded.colors
                    """,
                    (result.file_id, result.width, result.height, is_recycle, result.colors),
                )
                conn.execute(
                    """
                    INSERT INTO bf_material_video(file_id, duration, rotation, bf_extend)
                    VALUES(?, ?, ?, ?)
                    ON CONFLICT(file_id) DO UPDATE SET
                      duration = excluded.duration,
                      rotation = excluded.rotation,
                      bf_extend = excluded.bf_extend
                    """,
                    (result.file_id, result.duration_ms, result.rotation, result.bf_extend),
                )
    finally:
        conn.close()
    return backup_dir


def restore_latest() -> None:
    check_processes_not_running()
    backups = sorted([p for p in BACKUP_ROOT.iterdir() if (p / "manifest.json").exists()])
    if not backups:
        raise SystemExit("No backup manifest found.")
    backup_dir = backups[-1]
    manifest = json.loads((backup_dir / "manifest.json").read_text(encoding="utf-8"))
    if Path(manifest.get("library_root", "")).resolve() != LIBRARY_ROOT:
        raise SystemExit("Backup belongs to a different or unspecified library; refusing restore.")
    shutil.copy2(backup_dir / "billfish.db", DB_PATH)
    preview_backup = backup_dir / "preview"
    if preview_backup.exists():
        for src in preview_backup.rglob("*"):
            if src.is_file():
                rel = src.relative_to(preview_backup)
                dst = PREVIEW_ROOT / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
    print(f"Restored backup: {backup_dir}")


def verify_rows(file_ids: list[int]) -> None:
    conn = db_connect(readonly=True)
    conn.row_factory = sqlite3.Row
    try:
        rows = conn.execute(
            """
            SELECT f.id AS file_id, f.name,
                   m.status, m.thumb_tid, m.image_tid, m.w, m.h,
                   CASE WHEN m.colors IS NOT NULL AND m.colors <> '' THEN 1 ELSE 0 END AS has_colors,
                   v.duration, v.rotation, length(v.bf_extend) AS bf_extend_len
            FROM bf_file f
            LEFT JOIN bf_material_v2 m ON m.file_id = f.id
            LEFT JOIN bf_material_video v ON v.file_id = f.id
            WHERE f.id IN ({})
            ORDER BY f.id
            """.format(",".join("?" for _ in file_ids)),
            file_ids,
        ).fetchall()
        for row in rows:
            print(dict(row))
    finally:
        conn.close()


def main() -> int:
    global LIBRARY_ROOT, DB_PATH, PREVIEW_ROOT, SCAN_CACHE_PATH
    configure_console()
    parser = argparse.ArgumentParser()
    parser.add_argument("--library-root", default=os.environ.get("BILLFISH_LIBRARY_ROOT"), help="library directory (required unless BILLFISH_LIBRARY_ROOT is set)")
    parser.add_argument("--apply", action="store_true", help="write previews and DB rows")
    parser.add_argument("--restore-latest", action="store_true", help="restore latest backup made by this script")
    parser.add_argument("--scan-av1", action="store_true", help="discover all failed or incomplete AV1 MP4 records")
    parser.add_argument("--scan-mp4", action="store_true", help="discover all failed or incomplete readable MP4 records")
    parser.add_argument("--limit", type=int, help="limit discovered AV1 repairs")
    parser.add_argument("--ffmpeg", help="path to AV1-capable ffmpeg")
    parser.add_argument("--ffprobe", help="path to AV1-capable ffprobe")
    parser.add_argument("--targets", nargs="*", type=int, help="optional file_id subset")
    args = parser.parse_args()

    if not args.library_root:
        parser.error("--library-root is required unless BILLFISH_LIBRARY_ROOT is set")
    LIBRARY_ROOT = Path(args.library_root).resolve()
    DB_PATH = LIBRARY_ROOT / ".bf" / "billfish.db"
    PREVIEW_ROOT = LIBRARY_ROOT / ".bf" / ".preview"
    if not DB_PATH.is_file():
        parser.error(f"Library database not found: {DB_PATH}")
    import hashlib
    library_key = hashlib.sha256(str(LIBRARY_ROOT).casefold().encode("utf-8")).hexdigest()[:16]
    SCAN_CACHE_PATH = STATE_ROOT / f"scan-cache-{library_key}.json"

    if args.restore_latest:
        restore_latest()
        return 0

    ffmpeg = require_tool("ffmpeg", args.ffmpeg)
    ffprobe = require_tool("ffprobe", args.ffprobe)
    if args.scan_av1 and args.scan_mp4:
        raise SystemExit("--scan-av1 and --scan-mp4 are mutually exclusive")
    if args.scan_mp4:
        selected = discover_mp4_targets(ffprobe, limit=args.limit)
    elif args.scan_av1:
        selected = discover_av1_targets(ffprobe, limit=args.limit)
    else:
        parser.error("Specify --scan-av1 or --scan-mp4; --targets filters discovered file IDs")
    if args.targets:
        selected_ids = set(args.targets)
        selected = [t for t in selected if int(t["file_id"]) in selected_ids]
        missing = selected_ids - {int(t["file_id"]) for t in selected}
        if missing:
            raise SystemExit(f"Unknown target file_id(s): {sorted(missing)}")

    if not selected:
        print("No target files need AV1 analysis repair.")
        return 0

    conn = db_connect(readonly=True)
    try:
        print("Current DB rows:")
        for target in selected:
            print(current_db_row(conn, int(target["file_id"])))
    finally:
        conn.close()

    results: list[AnalysisResult] = []
    analysis_failures: list[tuple[dict[str, Any], str]] = []
    batch_scan = args.scan_av1 or args.scan_mp4
    for target in selected:
        try:
            results.append(analyze_target(ffprobe, ffmpeg, target))
        except Exception as exc:
            if not batch_scan:
                raise
            analysis_failures.append((target, str(exc)[:1000]))

    if analysis_failures:
        print("\nSkipped files that could not be analyzed:")
        for target, error in analysis_failures:
            print({"file_id": target.get("file_id"), "path": str(target.get("path")), "error": error})

    if not results:
        print("\nNo analyzable target files remain.")
        return 0

    print("\nPlanned repair:")
    for result in results:
        print(
            {
                "file_id": result.file_id,
                "path": str(result.path),
                "w": result.width,
                "h": result.height,
                "duration": result.duration_ms,
                "colors": result.colors,
                "small_preview": str(result.small_preview),
                "hd_preview": str(result.hd_preview),
                "bf_extend_len": len(result.bf_extend),
            }
        )

    if not args.apply:
        print("\nDry-run complete. Re-run with --apply after Billfish is closed.")
        return 0

    check_processes_not_running()
    backup_dir = apply_results(results)
    print(f"\nApplied. Backup: {backup_dir}")
    print("Verification rows:")
    verify_rows([r.file_id for r in results])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
