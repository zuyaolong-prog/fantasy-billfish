from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "fix-av1-analysis.py"
SPEC = importlib.util.spec_from_file_location("fix_av1_analysis", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class ProcessPreflightTests(unittest.TestCase):
    def run_preflight(self, records: list[dict[str, object]]) -> None:
        completed = subprocess.CompletedProcess([], 0, stdout=json.dumps(records), stderr="")
        with mock.patch.object(MODULE.shutil, "which", return_value="powershell.exe"), mock.patch.object(
            MODULE.subprocess, "run", return_value=completed
        ):
            MODULE.check_processes_not_running(Path(r"C:\Fixture\Billfish"))

    def test_core_blocks_apply(self) -> None:
        with self.assertRaises(SystemExit):
            self.run_preflight([{"name": "Billfish.core", "pid": 11, "path": r"C:\Fixture\Billfish\Billfish.core.exe", "reason": "billfish-root"}])

    def test_plugin_worker_blocks_apply(self) -> None:
        with self.assertRaises(SystemExit):
            self.run_preflight([{"name": "PluginWorker64", "pid": 12, "path": r"C:\Fixture\Billfish\workers\PluginWorker64.exe", "reason": "billfish-root"}])

    def test_unreadable_path_blocks_apply(self) -> None:
        with self.assertRaises(SystemExit):
            self.run_preflight([{"name": "Billfish", "pid": 13, "path": None, "reason": "path-unavailable"}])

    def test_empty_match_set_allows_apply(self) -> None:
        self.run_preflight([])


if __name__ == "__main__":
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout))
