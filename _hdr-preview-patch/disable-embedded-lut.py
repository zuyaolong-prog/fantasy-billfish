import argparse
import hashlib
import json
import shutil
import sqlite3
import sys
from datetime import datetime
from pathlib import Path


PATCH_DIR = Path(__file__).resolve().parent
BACKUP_DIR = PATCH_DIR / "backups"
STATE_DIR = PATCH_DIR / "state"
STATE_PATH = STATE_DIR / "render-chain-disable-lut.json"
TRIGGER_NAMES = (
    "bf_hdr_lut_material_video_ai",
    "bf_hdr_lut_material_video_au",
)
OWN_LUT_VALUES = (
    "18",
    "19",
    '{"default":"true","value":"18"}',
    '{"default":"true","value":"19"}',
    '{"default": "true", "value": "18"}',
    '{"default": "true", "value": "19"}',
)


def timestamp() -> str:
    return datetime.now().strftime("%Y%m%d-%H%M%S")


def copy_backup(path: Path, label: str) -> Path:
    BACKUP_DIR.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha1(str(path).encode("utf-8", errors="replace")).hexdigest()[:10]
    out = BACKUP_DIR / f"{label}-{digest}-{timestamp()}{path.suffix}"
    shutil.copy2(path, out)
    return out


def db_path_for_library(library_root: str) -> Path:
    return Path(library_root) / ".bf" / "billfish.db"


def value_placeholders(values: tuple[str, ...]) -> str:
    return ",".join("?" for _ in values)


def clean_database(db_path: Path, dry_run: bool) -> dict:
    if not db_path.exists():
        raise FileNotFoundError(f"Billfish database not found: {db_path}")

    con = sqlite3.connect(db_path)
    cur = con.cursor()
    placeholders = value_placeholders(OWN_LUT_VALUES)

    total_own_lut = cur.execute(
        f"select count(*) from bf_video_userdata where lut in ({placeholders})",
        OWN_LUT_VALUES,
    ).fetchone()[0]
    empty_rows = cur.execute(
        f"""
        select count(*) from bf_video_userdata
        where lut in ({placeholders})
          and (captions is null or trim(captions) = '')
          and (comments is null or trim(comments) = '')
          and (rules is null or trim(rules) = '')
          and (play_setting is null or trim(play_setting) = '')
        """,
        OWN_LUT_VALUES,
    ).fetchone()[0]
    keep_rows = total_own_lut - empty_rows
    existing_triggers = [
        row[0]
        for row in cur.execute(
            f"select name from sqlite_master where type='trigger' and name in ({value_placeholders(TRIGGER_NAMES)})",
            TRIGGER_NAMES,
        )
    ]
    changed = total_own_lut > 0 or bool(existing_triggers)
    backup_path = None

    if changed and not dry_run:
        backup_path = copy_backup(db_path, "billfish_db-render-chain-disable-lut")
        con.execute("BEGIN")
        try:
            for trigger_name in TRIGGER_NAMES:
                cur.execute(f"DROP TRIGGER IF EXISTS {trigger_name}")
            cur.execute(
                f"""
                delete from bf_video_userdata
                where lut in ({placeholders})
                  and (captions is null or trim(captions) = '')
                  and (comments is null or trim(comments) = '')
                  and (rules is null or trim(rules) = '')
                  and (play_setting is null or trim(play_setting) = '')
                """,
                OWN_LUT_VALUES,
            )
            deleted_rows = cur.rowcount
            cur.execute(
                f"update bf_video_userdata set lut = null where lut in ({placeholders})",
                OWN_LUT_VALUES,
            )
            cleared_rows = cur.rowcount
            con.commit()
        except Exception:
            con.rollback()
            raise
    else:
        deleted_rows = empty_rows
        cleared_rows = keep_rows

    result = {
        "dry_run": dry_run,
        "db_path": str(db_path),
        "changed": changed,
        "backup_path": None if backup_path is None else str(backup_path),
        "own_lut_rows": total_own_lut,
        "empty_own_lut_rows": empty_rows,
        "rows_with_other_userdata": keep_rows,
        "deleted_rows": deleted_rows,
        "cleared_lut_rows": cleared_rows,
        "dropped_triggers": existing_triggers,
    }
    if changed and not dry_run:
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        STATE_PATH.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--library-root", required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    result = clean_database(db_path_for_library(args.library_root), args.dry_run)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - maintenance CLI
        print(f"error={exc}", file=sys.stderr)
        raise SystemExit(1)
