"""Export the full portable runtime or maintained source without local history."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path


SOURCE_ROOT_FILES = {
    "README.md", "THIRD_PARTY_FFMPEG.md", "start-billfish.ps1",
    ".gitignore", ".gitattributes",
}
SOURCE_BINARY_SUFFIXES = {
    ".exe", ".dll", ".bin", ".pak", ".dat", ".zip", ".7z", ".gz",
    ".tar", ".com", ".lib", ".a", ".o", ".obj", ".pdb", ".exp",
    ".pyc", ".db", ".cache", ".bak", ".lnk", ".png", ".jpg",
    ".jpeg", ".webp", ".gif", ".ico", ".pdf", ".ttf", ".woff",
    ".woff2",
}
SOURCE_ATTRIBUTES = "* text=auto\n*.ps1 text eol=crlf\n*.cmd text eol=crlf\n*.bat text eol=crlf\n"
SOURCE_RUNTIME_IGNORE = """
# Runtime payloads are distributed in the portable Release archive.
*.exe
*.dll
*.bin
*.pak
*.dat
*.zip
*.7z
/bearer/
/iconengines/
/imageformats/
/platforms/
/plugins/
/qmltooling/
/QtGraphicalEffects/
/QtQml/
/QtQuick/
/QtQuick.2/
/Resources/
/scenegraph/
/styles/
/translations/
/ui_plugins/
/x86/
/_hdr-preview-patch/tools/mpv/
/tools/ffmpeg/bin/
"""


def is_source(encoded_path):
    relative = Path(encoded_path.decode("utf-8"))
    if len(relative.parts) == 1:
        return relative.name in SOURCE_ROOT_FILES
    top = relative.parts[0]
    if not (top.startswith("_") and top.endswith("patch") or top in {"reimpl", "reimpl-cpp", "tools"}):
        return False
    if "bin" in relative.parts[:-1] or relative.as_posix().startswith("_hdr-preview-patch/tools/mpv/"):
        return False
    return relative.suffix.lower() not in SOURCE_BINARY_SUFFIXES


def git(root, *args, input=None):
    result = subprocess.run(
        ["git", "-C", str(root), *args], input=input, capture_output=True, check=False
    )
    if result.returncode not in (0, 1):
        raise RuntimeError(result.stderr.decode("utf-8", errors="replace"))
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--source-only", action="store_true", help="Export maintained code, scripts, tests and documentation only; omit runtime binaries.")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    destination = args.destination.resolve()
    if destination == root or root in destination.parents:
        parser.error("Destination must be outside the source tree.")
    if destination.exists() and any(destination.iterdir()):
        parser.error("Destination must be new or empty; existing files will not be overwritten.")
    files = set(git(root, "ls-files", "--cached", "--others", "--exclude-standard", "-z").split(b"\0"))
    files.discard(b"")
    ignored = set(git(root, "check-ignore", "--no-index", "-z", "--stdin", input=b"\0".join(files) + b"\0").split(b"\0"))
    ignored.discard(b"")
    included = sorted(files - ignored)
    if args.source_only:
        included = [path for path in included if is_source(path)]
    required = {
        b"Billfish.exe", b"Billfish.core.exe", b"ui_plugins/preview.stock.bin",
        b"_launcher-patch/portable-runtime.json", b"Qt5WebEngineCore.dll",
        b"_hdr-preview-patch/tools/mpv/mpv.exe", b"tools/ffmpeg/bin/ffmpeg.exe",
        b"tools/ffmpeg/bin/ffprobe.exe",
    }
    if not args.source_only and required - set(included):
        parser.error(
            "Full portable export requires installed, unignored runtime files: "
            + ", ".join(path.decode("utf-8") for path in sorted(required - set(included)))
            + ". Use --source-only for a source checkout, or download the portable archive from GitHub Releases."
        )
    destination.mkdir(parents=True, exist_ok=True)
    total = 0
    for encoded_path in included:
        relative = Path(encoded_path.decode("utf-8"))
        source = root / relative
        if not source.is_file():
            raise RuntimeError(f"Missing tracked input: {relative}")
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if args.source_only and relative.as_posix() == ".gitattributes":
            target.write_text(SOURCE_ATTRIBUTES, encoding="utf-8", newline="")
        elif args.source_only and relative.as_posix() == ".gitignore":
            target.write_text(source.read_text(encoding="utf-8").rstrip() + "\n" + SOURCE_RUNTIME_IGNORE, encoding="utf-8", newline="")
        else:
            shutil.copyfile(source, target)
        total += target.stat().st_size
    print(json.dumps({"destination": str(destination), "mode": "source" if args.source_only else "portable", "files": len(included), "bytes": total, "excluded": len(files) - len(included)}, ensure_ascii=True))


if __name__ == "__main__":
    main()
