"""Stage a bounded Eagle ZIP or development directory without executing its code."""
import json
import os
from pathlib import Path
import shutil
import sys
import zipfile

MAX_FILES = 4096
MAX_BYTES = 512 * 1024 * 1024
MAX_FILE = 384 * 1024 * 1024


def emit(**value):
    print(json.dumps(value, ensure_ascii=True), flush=True)


def relative_name(name):
    name = name.replace("\\", "/")
    parts = name.rstrip("/").split("/")
    if (not name or name.startswith("/") or any(
            not p or p in (".", "..") or ":" in p or p.endswith((".", " "))
            or any(ord(c) < 32 for c in p) for p in parts)):
        raise ValueError("Invalid package path: " + name)
    reserved = {"CON", "PRN", "AUX", "NUL", *["COM" + str(i) for i in range(1, 10)],
                *["LPT" + str(i) for i in range(1, 10)]}
    if any(p.split(".")[0].upper() in reserved or any(c in p for c in '<>"|?*') for p in parts):
        raise ValueError("Invalid Windows package path: " + name)
    return "/".join(parts)


def stage(source, destination):
    source, destination = Path(source), Path(destination)
    if destination.exists():
        raise ValueError("Staging directory already exists")
    entries, names, total = [], {}, 0

    def add(name, size, entry, directory=False):
        nonlocal total
        name = relative_name(name)
        key = name.casefold()
        if key in names:
            raise ValueError("Duplicate package path: " + name)
        names[key] = directory
        if size < 0 or size > MAX_FILE:
            raise ValueError("Package file exceeds 384 MiB: " + name)
        if not directory:
            total += size
        entries.append((name, size, entry, directory))
        if len(entries) > MAX_FILES or total > MAX_BYTES:
            raise ValueError("Package limit is 4096 entries and 512 MiB")

    archive = None
    try:
        if source.is_dir():
            for root, directories, files in os.walk(source):
                for name in directories:
                    path = Path(root) / name
                    add(path.relative_to(source).as_posix(), 0, path, True)
                for name in files:
                    path = Path(root) / name
                    add(path.relative_to(source).as_posix(), path.stat().st_size, path)
        else:
            if source.suffix.lower() not in (".eagleplugin", ".zip") or source.stat().st_size > MAX_BYTES:
                raise ValueError("Choose an .eagleplugin package or development directory")
            archive = zipfile.ZipFile(source)
            for info in archive.infolist():
                if info.flag_bits & 1:
                    raise ValueError("Encrypted packages are unsupported")
                add(info.filename, info.file_size, info, info.is_dir())
        if "manifest.json" not in names or names["manifest.json"]:
            raise ValueError("Package requires manifest.json at its root")
        for name, _, _, _ in entries:
            parts = name.casefold().split("/")
            if any(names.get("/".join(parts[:i])) is False for i in range(1, len(parts))):
                raise ValueError("Conflicting package path: " + name)
        destination.mkdir()
        completed = 0
        emit(completed=0, total=total, message="正在解包插件" if archive else "正在复制插件")
        for name, expected, entry, directory in entries:
            target = destination / name
            if directory:
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            copied = 0
            with (archive.open(entry) if archive else entry.open("rb")) as src, target.open("xb") as dst:
                while True:
                    chunk = src.read(1024 * 1024)
                    if not chunk:
                        break
                    copied += len(chunk)
                    if copied > expected:
                        raise ValueError("Package file changed or exceeds declared size: " + name)
                    dst.write(chunk)
                    completed += len(chunk)
                    emit(completed=completed, total=total, message="正在安装插件")
            if copied != expected:
                raise ValueError("Package file size changed: " + name)
        emit(staged=True, completed=total, total=total)
    except Exception:
        if destination.exists():
            shutil.rmtree(destination)
        raise
    finally:
        if archive:
            archive.close()


if __name__ == "__main__":
    try:
        stage(sys.argv[1], sys.argv[2])
    except Exception as error:
        emit(error=str(error))
        sys.exit(1)
