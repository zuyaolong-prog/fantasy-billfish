from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum
from pathlib import Path
from typing import Any


class BFImportMode(IntEnum):
    ImportNone = 0
    ImportCopy = 1
    ImportCut = 2
    ImportIndex = 3


@dataclass(slots=True)
class BFResponseInfo:
    code: int
    message: str


@dataclass(slots=True)
class BFLibraryInfo:
    root: Path
    name: str
    version: int


@dataclass(slots=True)
class BFFile:
    id: int
    pid: int
    name: str
    ext_name: str
    path: str
    size: int
    hash: str
    is_recycle: bool


@dataclass(slots=True)
class BFFolder:
    id: int
    pid: int
    name: str
    path: str


@dataclass(slots=True)
class BFTag:
    id: int
    name: str
    group_id: int


@dataclass(slots=True)
class BFTagGroup:
    id: int
    name: str
    parent_id: int


@dataclass(slots=True)
class BFRecycle:
    id: int
    entity_type: str
    entity_id: int
    original_path: str
    deleted_at: int


@dataclass(slots=True)
class BFType:
    ext_name: str
    type_name: str
    can_analyze: bool


@dataclass(slots=True)
class BFImportRequest:
    entries: list[str]
    import_mode: BFImportMode = BFImportMode.ImportIndex
    target_folder_id: int = 1
    recursive: bool = True


@dataclass(slots=True)
class BFReIndexRequest:
    file_ids: list[int] = field(default_factory=list)


@dataclass(slots=True)
class BFMoveRequest:
    file_ids: list[int]
    target_folder_id: int


@dataclass(slots=True)
class BFRemoveOrRestoreRequest:
    file_ids: list[int]
    action: str


@dataclass(slots=True)
class BFSetCoverRequest:
    folder_id: int
    file_id: int


@dataclass(slots=True)
class BFSearchSummaryRequest:
    query: str
    limit: int = 100
    offset: int = 0
    include_recycle: bool = False


@dataclass(slots=True)
class BFScanDuplicationRequest:
    folder_ids: list[int] = field(default_factory=list)


@dataclass(slots=True)
class BFExportRequest:
    mode: str
    file_ids: list[int]
    destination: str


@dataclass(slots=True)
class BFUpdateParam:
    key: str
    value: Any
