import argparse
import hashlib
import json
import shutil
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DLL_PATH = ROOT / r"plugins\media_runtime\libMediaSDK64.dll"
BACKUP_DIR = ROOT / r"_hdr-preview-patch\backups"
STATE_PATH = ROOT / r"_hdr-preview-patch\state\mediasdk-disable-hwaccel.json"

ORIGINAL = b"hwaccels"
PATCHED = b"hwaccelx"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def load_state() -> dict:
    if STATE_PATH.exists():
        return json.loads(STATE_PATH.read_text(encoding="utf-8"))
    return {}


def save_state(state: dict) -> None:
    STATE_PATH.parent.mkdir(parents=True, exist_ok=True)
    STATE_PATH.write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding="utf-8")


def make_backup(path: Path) -> Path:
    BACKUP_DIR.mkdir(parents=True, exist_ok=True)
    digest = sha256(path)[:12]
    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup = BACKUP_DIR / f"libMediaSDK64-disable-hwaccel-{digest}-{stamp}.dll"
    shutil.copy2(path, backup)
    return backup


def install() -> int:
    data = DLL_PATH.read_bytes()
    original_hits = [i for i in range(len(data)) if data.startswith(ORIGINAL, i)]
    patched_hits = [i for i in range(len(data)) if data.startswith(PATCHED, i)]

    if len(patched_hits) == 1 and len(original_hits) == 0:
        print("status=already-installed")
        print(f"offset=0x{patched_hits[0]:x}")
        return 0
    if len(original_hits) != 1:
        raise RuntimeError(f"Expected exactly one {ORIGINAL!r} occurrence, found {len(original_hits)}")

    backup = make_backup(DLL_PATH)
    offset = original_hits[0]
    patched = bytearray(data)
    patched[offset : offset + len(ORIGINAL)] = PATCHED
    DLL_PATH.write_bytes(patched)
    save_state(
        {
            "dll_path": str(DLL_PATH),
            "backup_path": str(backup),
            "installed_at": time.strftime("%Y-%m-%d %H:%M:%S"),
            "offset": offset,
            "original_sha256": sha256(backup),
            "patched_sha256": sha256(DLL_PATH),
            "original": ORIGINAL.decode("ascii"),
            "patched": PATCHED.decode("ascii"),
        }
    )
    print("status=installed")
    print(f"offset=0x{offset:x}")
    print(f"backup={backup}")
    return 0


def restore() -> int:
    state = load_state()
    backup = Path(state.get("backup_path", ""))
    if not backup.exists():
        raise FileNotFoundError(f"Backup not found: {backup}")
    shutil.copy2(backup, DLL_PATH)
    print("status=restored")
    print(f"backup={backup}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Make libMediaSDK ignore incoming hwaccels JSON arrays.")
    parser.add_argument("--restore", action="store_true", help="Restore libMediaSDK64.dll from the recorded backup")
    args = parser.parse_args()
    if args.restore:
        return restore()
    return install()


if __name__ == "__main__":
    raise SystemExit(main())
