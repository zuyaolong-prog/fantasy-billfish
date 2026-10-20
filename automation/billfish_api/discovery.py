"""Discover the ordinary on-disk Billfish library without changing it."""

import configparser
import os
from pathlib import Path

from .errors import ApiError


def resolve_library(explicit=None):
    """Select --library, BILLFISH_LIBRARY_ROOT, then Qt's last-open library."""
    candidate = explicit or os.environ.get("BILLFISH_LIBRARY_ROOT")
    if not candidate:
        roaming = Path(os.environ.get("APPDATA") or Path.home() / "AppData/Roaming")
        config_path = roaming / "Billfish/config/recent_lib.ini"
        parser = configparser.ConfigParser(interpolation=None)
        try:
            with config_path.open(encoding="utf-8-sig") as stream:
                parser.read_file(stream)
            candidate = parser.get("LoadDefaultLibType", "LastLoadLibPath", fallback="")
            # QSettings writes each literal backslash as two backslashes.
            candidate = candidate.strip().strip('"').replace("\\\\", "\\")
        except (OSError, UnicodeError, configparser.Error) as exc:
            raise ApiError("library_not_configured", "Provide --library or BILLFISH_LIBRARY_ROOT.",
                           {"config_path": str(config_path), "reason": str(exc)}, 404) from exc
    if not candidate:
        raise ApiError("library_not_configured", "Provide --library or BILLFISH_LIBRARY_ROOT.", status=404)
    root = Path(candidate).expanduser().absolute()
    if not root.is_dir() or not (root / ".bf/billfish.db").is_file():
        raise ApiError("library_not_found", "The library must contain .bf/billfish.db.",
                       {"library_root": str(root)}, 404)
    return root
