import argparse
import hashlib
import json
import os
import shutil
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
BACKUP_DIR = ROOT / r"_hdr-preview-patch\backups"
STATE_PATH = ROOT / r"_hdr-preview-patch\state\software-decode-config.json"
CONFIG_PATH = Path(os.environ.get("APPDATA", "")) / r"Billfish\config\user_config.ini"

SHOW_CONFIG_VALUES = {
    "EnableHardwareAcceleration": "0",
    "EnableHardwareAccelerationV2": "0",
    "EnableHardwareRender": "0",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


def write_text(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def split_newline(text: str) -> str:
    return "\r\n" if "\r\n" in text else "\n"


def set_ini_values(text: str, section: str, values: dict[str, str]) -> str:
    newline = split_newline(text)
    lines = text.splitlines()
    output: list[str] = []
    in_section = False
    found_section = False
    written = set()

    def flush_missing() -> None:
        for key, value in values.items():
            if key not in written:
                output.append(f"{key}={value}")
                written.add(key)

    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if in_section:
                flush_missing()
            in_section = stripped[1:-1] == section
            found_section = found_section or in_section
            output.append(line)
            continue

        if in_section and "=" in line:
            key = line.split("=", 1)[0].strip()
            if key in values:
                output.append(f"{key}={values[key]}")
                written.add(key)
                continue

        output.append(line)

    if in_section:
        flush_missing()

    if not found_section:
        if output and output[-1] != "":
            output.append("")
        output.append(f"[{section}]")
        for key, value in values.items():
            output.append(f"{key}={value}")

    return newline.join(output) + newline


def load_state() -> dict:
    if not STATE_PATH.exists():
        return {}
    return json.loads(STATE_PATH.read_text(encoding="utf-8"))


def save_state(state: dict) -> None:
    STATE_PATH.parent.mkdir(parents=True, exist_ok=True)
    STATE_PATH.write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding="utf-8")


def make_backup(path: Path) -> Path:
    BACKUP_DIR.mkdir(parents=True, exist_ok=True)
    digest = sha256(path)[:10].lower()
    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup = BACKUP_DIR / f"user_config-software-decode-{digest}-{stamp}.ini"
    shutil.copy2(path, backup)
    return backup


def install(config_path: Path) -> int:
    if not config_path.exists():
        raise FileNotFoundError(f"user_config.ini not found: {config_path}")

    before = read_text(config_path)
    after = set_ini_values(before, "ShowConfig", SHOW_CONFIG_VALUES)
    if after == before:
        print("status=already-configured")
        return 0

    backup = make_backup(config_path)
    write_text(config_path, after)
    save_state(
        {
            "config_path": str(config_path),
            "backup_path": str(backup),
            "installed_at": time.strftime("%Y-%m-%d %H:%M:%S"),
            "values": SHOW_CONFIG_VALUES,
        }
    )
    print("status=installed")
    print(f"backup={backup}")
    return 0


def restore(config_path: Path) -> int:
    state = load_state()
    backup_path = state.get("backup_path")
    if not backup_path:
        raise RuntimeError("No backup recorded for software decode config")

    backup = Path(backup_path)
    if not backup.exists():
        raise FileNotFoundError(f"Backup not found: {backup}")

    shutil.copy2(backup, config_path)
    print("status=restored")
    print(f"backup={backup}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Force Billfish preview config away from hardware decode.")
    parser.add_argument("--config", default=str(CONFIG_PATH), help="Path to Billfish user_config.ini")
    parser.add_argument("--restore", action="store_true", help="Restore the most recent backed up user_config.ini")
    args = parser.parse_args()

    config_path = Path(args.config)
    if args.restore:
        return restore(config_path)
    return install(config_path)


if __name__ == "__main__":
    raise SystemExit(main())
