"""Export the full portable runtime or maintained source without local history."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path


SOURCE_ROOT_FILES = {
    "README.md", "THIRD_PARTY_FFMPEG.md", "THIRD_PARTY_ELECTRON.md", "start-billfish.ps1",
    ".gitignore", ".gitattributes", "billfish-cli.cmd",
}
SOURCE_BINARY_SUFFIXES = {
    ".exe", ".dll", ".bin", ".pak", ".dat", ".zip", ".7z", ".gz",
    ".tar", ".com", ".lib", ".a", ".o", ".obj", ".pdb", ".exp",
    ".pyc", ".db", ".cache", ".bak", ".lnk", ".png", ".jpg",
    ".jpeg", ".webp", ".gif", ".ico", ".pdf", ".ttf", ".woff",
    ".woff2", ".eagleplugin",
}
SOURCE_ATTRIBUTES = """* text=auto
*.ps1 text eol=crlf
*.cmd text eol=crlf
*.bat text eol=crlf
# Frozen resources and rebuild inputs must retain their recorded bytes.
automation/animation_browser/vendor/** -text
browser-extension/maintained/** -text
_sidebar-patch/src/** -text
_animation-browser-parser/vendor/** -text
"""
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
/tools/electron/
/tools/python/*.pyd
/tools/python/*.cat
"""
REQUIRED_SOURCE_FILES = {
    b"start-billfish.ps1", b"THIRD_PARTY_ELECTRON.md",
    b"_animation-browser-parser/CMakeLists.txt", b"_animation-browser-parser/src/main.cpp",
    b"_animation-browser-parser/vendor/ufbx/ufbx.c", b"_animation-browser-parser/vendor/ufbx/ufbx.h",
    b"_animation-browser-parser/vendor/ufbx/LICENSE",
    b"_animation-browser-parser/vendor/nlohmann/json.hpp", b"_animation-browser-parser/vendor/nlohmann/LICENSE.MIT",
    b"_video-trim-patch/src/automation/format_preview_bridge.cpp",
    b"_video-trim-patch/src/automation/native_adapter.cpp",
    b"_sidebar-patch/src/BfFolderTreeView.qml",
    b"automation/animation_catalog_helper.py",
    b"automation/animation_browser/backend/service.cjs", b"automation/animation_browser/ui/app.mjs",
    b"automation/plugin_runtime/animation-browser-runtime.cjs",
    b"_launcher-patch/PluginIngress.Core.ps1",
    b"_launcher-patch/deliver-plugin-package.py",
    b"_launcher-patch/plugin-package-association.ps1",
    b"automation/billfish_api/online.py", b"automation/billfish_api/errors.py",
    b"automation/plugin_runtime/main.cjs", b"automation/plugin_runtime/preload.cjs",
    b"automation/plugin_runtime/eagle-sdk.cjs", b"automation/plugin_runtime/schema.cjs",
    b"automation/plugin_runtime/context-menu.cjs", b"automation/plugin_runtime/extra-modules.cjs",
    b"automation/plugin_runtime/vendor/i18next.cjs", b"automation/plugin_runtime/vendor/i18next.LICENSE",
    b"automation/plugin_runtime/package_installer.py",
    b"automation/plugin_runtime/package.json", b"automation/plugin_runtime/package-lock.json",
    b"automation/media_transfer.py", b"automation/MEDIA_TRANSFER.md",
    b"automation/browser_native_host.py",
    b"browser-extension/maintained/README.md", b"browser-extension/maintained/manifest.json",
    b"browser-extension/maintained/provenance.json", b"browser-extension/maintained/identity.json",
    b"browser-extension/maintained/build-fork.cjs",
    b"browser-extension/maintained/maintained/video-background.js",
    b"browser-extension/maintained/maintained/video-common.js",
    b"browser-extension/maintained/maintained/video-content.js",
    b"browser-extension/native-host/README.md",
    b"browser-extension/native-host/manage-native-host.ps1",
    b"browser-extension/native-host/billfish-capture-host.cmd.in",
    b"browser-extension/native-host/com.billfish.capture.json.in",
    b"tools/video-download/LICENSE", b"tools/video-download/THIRD_PARTY_LICENSES.txt",
    b"tools/video-download/PROVENANCE.md", b"tools/video-download/SHA2-256SUMS",
    b"tools/initialize-portable.ps1", b"tools/initialize-portable.cmd",
    b"docs/portable-migration.md", b"tools/python/PROVENANCE.md",
    b"tools/python/LICENSE.txt", b"tools/python/runtime-files.json", b"tools/python/python314._pth",
}
REQUIRED_PORTABLE_FILES = {
    b"Billfish.exe", b"Billfish.core.exe", b"ui_plugins/preview.stock.bin",
    b"_launcher-patch/portable-runtime.json", b"Qt5WebEngineCore.dll",
    b"_hdr-preview-patch/tools/mpv/mpv.exe", b"tools/ffmpeg/bin/ffmpeg.exe",
    b"tools/ffmpeg/bin/ffprobe.exe",
    b"tools/video-download/yt-dlp.exe",
} | {
    ("tools/electron/" + name).encode("utf-8") for name in (
        "electron.exe", "LICENSE", "LICENSES.chromium.html", "version",
        "chrome_100_percent.pak", "chrome_200_percent.pak", "resources.pak",
        "icudtl.dat", "snapshot_blob.bin", "v8_context_snapshot.bin",
        "d3dcompiler_47.dll", "dxcompiler.dll", "dxil.dll", "ffmpeg.dll",
        "libEGL.dll", "libGLESv2.dll", "vk_swiftshader.dll",
        "vk_swiftshader_icd.json", "vulkan-1.dll",
        "resources/default_app.asar", "locales/en-US.pak", "locales/zh-CN.pak",
    )
}


def is_source(encoded_path):
    relative = Path(encoded_path.decode("utf-8"))
    if relative.name == "AGENTS.md":
        return False
    if len(relative.parts) == 1:
        return relative.name in SOURCE_ROOT_FILES
    top = relative.parts[0]
    if not (top.startswith("_") and top.endswith("patch") or top in {"_animation-browser-parser", "reimpl", "reimpl-cpp", "tools", "automation", "docs", "browser-extension"}):
        return False
    if "bin" in relative.parts[:-1] or relative.as_posix().startswith(("_hdr-preview-patch/tools/mpv/", "tools/electron/")):
        return False
    if relative.as_posix().startswith("tools/python/"):
        return relative.name in {"PROVENANCE.md", "LICENSE.txt", "runtime-files.json", "python314._pth"}
    # Keep the maintained extension loadable, including its small packaged UI assets.
    if relative.as_posix().startswith("browser-extension/maintained/") and relative.suffix.lower() in {".png", ".gif", ".wasm"}:
        return True
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
    required = REQUIRED_SOURCE_FILES if args.source_only else REQUIRED_SOURCE_FILES | REQUIRED_PORTABLE_FILES
    provenance = root / "browser-extension/maintained/provenance.json"
    if provenance.is_file():
        # The frozen fork's recorded baseline is authoritative for all 57 original
        # resources, including indirect CSS/JS/image/WASM loads in bundled scripts.
        declared = json.loads(provenance.read_text(encoding="utf-8"))["source_hashes"]
        required = required | {("browser-extension/maintained/" + path).encode("utf-8") for path in declared}
    python_manifest = root / "tools/python/runtime-files.json"
    if not args.source_only and python_manifest.is_file():
        declared = json.loads(python_manifest.read_text(encoding="utf-8"))["files"]
        required = required | {("tools/python/" + file["path"]).encode("utf-8") for file in declared}
    missing = required - set(included)
    missing |= {path for path in required if not (root / path.decode("utf-8")).is_file()}
    if missing:
        description = "Source export requires maintained, unignored files: " if args.source_only else "Full portable export requires installed, unignored runtime files: "
        parser.error(
            description + ", ".join(path.decode("utf-8") for path in sorted(missing))
            + ("." if args.source_only else ". Use --source-only for a source checkout, or download the portable archive from GitHub Releases.")
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
