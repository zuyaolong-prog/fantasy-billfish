from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class ErrorCode:
    code: int
    message: str


OK = ErrorCode(0, "ok")
ERR_INVALID_ARGUMENT = ErrorCode(1001, "invalid argument")
ERR_LIBRARY_NOT_OPEN = ErrorCode(1002, "library not open")
ERR_LIBRARY_ALREADY_OPEN = ErrorCode(1003, "library already open")
ERR_DATABASE = ErrorCode(1004, "database error")
ERR_NOT_FOUND = ErrorCode(1005, "not found")
ERR_IO = ErrorCode(1006, "io error")
ERR_UNSUPPORTED = ErrorCode(1007, "unsupported")
ERR_VERSION = ErrorCode(1008, "version mismatch")
ERR_CONFLICT = ErrorCode(1009, "conflict")
