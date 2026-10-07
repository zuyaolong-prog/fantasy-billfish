from __future__ import annotations

from typing import Iterable

MAIN_SCHEMA_SQL = """
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
PRAGMA foreign_keys=ON;

CREATE TABLE IF NOT EXISTS library (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS app_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS bf_folder (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    pid INTEGER NOT NULL,
    name TEXT NOT NULL,
    path TEXT NOT NULL UNIQUE,
    cover_id INTEGER,
    is_recycle INTEGER NOT NULL DEFAULT 0,
    create_time INTEGER NOT NULL,
    update_time INTEGER NOT NULL,
    FOREIGN KEY (pid) REFERENCES bf_folder(id)
);

CREATE TABLE IF NOT EXISTS bf_file (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    pid INTEGER NOT NULL,
    name TEXT NOT NULL,
    ext_name TEXT NOT NULL,
    path TEXT NOT NULL UNIQUE,
    size INTEGER NOT NULL,
    hash TEXT NOT NULL DEFAULT '',
    ctime INTEGER NOT NULL,
    mtime INTEGER NOT NULL,
    import_mode INTEGER NOT NULL,
    is_index INTEGER NOT NULL DEFAULT 1,
    is_recycle INTEGER NOT NULL DEFAULT 0,
    create_time INTEGER NOT NULL,
    update_time INTEGER NOT NULL,
    FOREIGN KEY (pid) REFERENCES bf_folder(id)
);

CREATE TABLE IF NOT EXISTS bf_tag_group (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    parent_id INTEGER NOT NULL DEFAULT 0,
    sort_index INTEGER NOT NULL DEFAULT 0,
    create_time INTEGER NOT NULL,
    update_time INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS bf_tag (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    group_id INTEGER NOT NULL DEFAULT 0,
    parent_id INTEGER NOT NULL DEFAULT 0,
    sort_index INTEGER NOT NULL DEFAULT 0,
    create_time INTEGER NOT NULL,
    update_time INTEGER NOT NULL,
    UNIQUE(name, group_id)
);

CREATE TABLE IF NOT EXISTS bf_file_tag (
    file_id INTEGER NOT NULL,
    tag_id INTEGER NOT NULL,
    create_time INTEGER NOT NULL,
    PRIMARY KEY(file_id, tag_id),
    FOREIGN KEY(file_id) REFERENCES bf_file(id) ON DELETE CASCADE,
    FOREIGN KEY(tag_id) REFERENCES bf_tag(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS bf_material_userdata (
    file_id INTEGER PRIMARY KEY,
    comment TEXT NOT NULL DEFAULT '',
    note TEXT NOT NULL DEFAULT '',
    FOREIGN KEY(file_id) REFERENCES bf_file(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS bf_recycle (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    entity_type TEXT NOT NULL,
    entity_id INTEGER NOT NULL,
    original_path TEXT NOT NULL,
    deleted_at INTEGER NOT NULL,
    UNIQUE(entity_type, entity_id)
);

CREATE TABLE IF NOT EXISTS bf_type (
    ext_name TEXT PRIMARY KEY,
    type_name TEXT NOT NULL,
    can_analyze INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_bf_file_pid ON bf_file(pid);
CREATE INDEX IF NOT EXISTS idx_bf_file_recycle ON bf_file(is_recycle);
CREATE INDEX IF NOT EXISTS idx_bf_file_ext ON bf_file(ext_name);
CREATE INDEX IF NOT EXISTS idx_bf_tag_group ON bf_tag(group_id);
CREATE INDEX IF NOT EXISTS idx_bf_recycle_entity ON bf_recycle(entity_type, entity_id);
"""

MIGRATION_40_TO_41_SQL = """
CREATE TABLE IF NOT EXISTS app_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_bf_file_pid ON bf_file(pid);
CREATE INDEX IF NOT EXISTS idx_bf_file_recycle ON bf_file(is_recycle);
CREATE INDEX IF NOT EXISTS idx_bf_file_ext ON bf_file(ext_name);
CREATE INDEX IF NOT EXISTS idx_bf_tag_group ON bf_tag(group_id);
CREATE INDEX IF NOT EXISTS idx_bf_recycle_entity ON bf_recycle(entity_type, entity_id);

-- Consistency fix: orphaned files should point to root folder id=1.
UPDATE bf_file SET pid = 1 WHERE pid NOT IN (SELECT id FROM bf_folder);

INSERT INTO app_meta(key, value) VALUES('schema_version', '41')
ON CONFLICT(key) DO UPDATE SET value='41';
"""

SUMMARY_SCHEMA_SQL = """
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;

CREATE TABLE IF NOT EXISTS summary_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE VIRTUAL TABLE IF NOT EXISTS bf_summary USING fts5(
    iid UNINDEXED,
    name,
    ext_name,
    tags,
    folder_name,
    comment,
    note,
    tokenize='unicode61 remove_diacritics 2'
);
"""

TYPE_SEED: list[tuple[str, str, int]] = [
    ("png", "image", 1),
    ("jpg", "image", 1),
    ("jpeg", "image", 1),
    ("webp", "image", 1),
    ("gif", "image", 1),
    ("bmp", "image", 1),
    ("tiff", "image", 1),
    ("psd", "design", 1),
    ("ai", "design", 1),
    ("svg", "design", 1),
    ("mp4", "video", 1),
    ("mov", "video", 1),
    ("mkv", "video", 1),
    ("avi", "video", 1),
    ("mp3", "audio", 1),
    ("wav", "audio", 1),
    ("flac", "audio", 1),
    ("pdf", "document", 1),
    ("doc", "document", 0),
    ("docx", "document", 0),
    ("ppt", "document", 0),
    ("pptx", "document", 0),
    ("xls", "document", 0),
    ("xlsx", "document", 0),
    ("zip", "archive", 0),
    ("rar", "archive", 0),
    ("7z", "archive", 0),
]


def iter_type_seed() -> Iterable[tuple[str, str, int]]:
    return TYPE_SEED
