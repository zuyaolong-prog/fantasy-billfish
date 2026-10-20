"""Billfish CLI entry point. Requires Python 3.11 or newer."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from billfish_api.cli import main

if __name__ == "__main__":
    raise SystemExit(main())
