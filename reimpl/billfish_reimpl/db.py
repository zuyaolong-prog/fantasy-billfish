from __future__ import annotations

import sqlite3
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator

from .schema import MAIN_SCHEMA_SQL, SUMMARY_SCHEMA_SQL, iter_type_seed


class LibraryDB:
    def __init__(self, bf_dir: Path) -> None:
        self._bf_dir = bf_dir
        self.main_path = bf_dir / "billfish.db"
        self.summary_path = bf_dir / "summary_v2.db"
        self.main = sqlite3.connect(self.main_path)
        self.main.row_factory = sqlite3.Row
        self.summary = sqlite3.connect(self.summary_path)
        self.summary.row_factory = sqlite3.Row
        self.main.execute("PRAGMA foreign_keys=ON")

    def close(self) -> None:
        self.summary.close()
        self.main.close()

    @contextmanager
    def tx(self) -> Iterator[sqlite3.Connection]:
        try:
            self.main.execute("BEGIN")
            yield self.main
            self.main.commit()
        except Exception:
            self.main.rollback()
            raise

    @contextmanager
    def summary_tx(self) -> Iterator[sqlite3.Connection]:
        try:
            self.summary.execute("BEGIN")
            yield self.summary
            self.summary.commit()
        except Exception:
            self.summary.rollback()
            raise

    @staticmethod
    def bootstrap(bf_dir: Path, lib_name: str) -> None:
        bf_dir.mkdir(parents=True, exist_ok=True)
        db = LibraryDB(bf_dir)
        try:
            db.main.executescript(MAIN_SCHEMA_SQL)
            db.summary.executescript(SUMMARY_SCHEMA_SQL)

            now = _now()
            db.main.execute(
                "INSERT OR REPLACE INTO library(key, value) VALUES (?, ?)",
                ("name", lib_name),
            )
            db.main.execute(
                "INSERT OR REPLACE INTO library(key, value) VALUES (?, ?)",
                ("version", "40"),
            )
            db.main.execute(
                "INSERT OR REPLACE INTO app_meta(key, value) VALUES (?, ?)",
                ("schema_version", "41"),
            )
            db.main.execute(
                "INSERT OR IGNORE INTO bf_folder(id, pid, name, path, create_time, update_time) VALUES (1, 1, ?, '/', ?, ?)",
                ("root", now, now),
            )
            db.main.executemany(
                "INSERT OR IGNORE INTO bf_type(ext_name, type_name, can_analyze) VALUES (?, ?, ?)",
                list(iter_type_seed()),
            )
            db.main.commit()

            db.summary.execute(
                "INSERT OR REPLACE INTO summary_meta(key, value) VALUES (?, ?)",
                ("schema_version", "41"),
            )
            db.summary.commit()
        finally:
            db.close()


def _now() -> int:
    import time

    return int(time.time())
