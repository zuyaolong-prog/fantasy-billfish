from __future__ import annotations

import tempfile
from pathlib import Path

from billfish_reimpl import (
    BFExportRequest,
    BFImportMode,
    BFImportRequest,
    BFRemoveOrRestoreRequest,
    BFScanDuplicationRequest,
    BFSearchSummaryRequest,
    BillfishDataCenter,
)


def assert_ok(resp: dict, label: str) -> None:
    assert resp["code"] == 0, f"{label} failed: {resp}"


def run() -> None:
    with tempfile.TemporaryDirectory(prefix="billfish_reimpl_test_") as td:
        root = Path(td)
        source = root / "source"
        source.mkdir()
        (source / "a.png").write_bytes(b"same-content")
        (source / "b.png").write_bytes(b"same-content")
        (source / "c.txt").write_text("hello world", encoding="utf-8")

        lib_root = root / "library"
        dc = BillfishDataCenter()
        try:
            assert_ok(dc.createLibrary(str(lib_root), "TestLib"), "createLibrary")
            opened = dc.openLibrary(str(lib_root))
            assert_ok(opened, "openLibrary")
            assert opened["libInfo"]["schema_version"] == 41

            imported = dc.import_(BFImportRequest(entries=[str(source)], import_mode=BFImportMode.ImportIndex))
            assert_ok(imported, "import")
            file_ids = imported["imported"]
            assert len(file_ids) == 3

            t = dc.tagNew("demo")
            assert_ok(t, "tagNew")
            assert_ok(dc.tagAttach(file_ids, [t["tag_id"]]), "tagAttach")

            searched = dc.searchSummary(BFSearchSummaryRequest(query="demo"))
            assert_ok(searched, "searchSummary")
            assert len(searched["items"]) == 3

            removed = dc.removeOrRestore(BFRemoveOrRestoreRequest(file_ids=[file_ids[0]], action="remove"))
            assert_ok(removed, "remove")
            searched_after_remove = dc.searchSummary(BFSearchSummaryRequest(query="demo"))
            assert_ok(searched_after_remove, "search after remove")
            assert len(searched_after_remove["items"]) == 2

            restored = dc.removeOrRestore(BFRemoveOrRestoreRequest(file_ids=[file_ids[0]], action="restore"))
            assert_ok(restored, "restore")
            searched_after_restore = dc.searchSummary(BFSearchSummaryRequest(query="demo"))
            assert_ok(searched_after_restore, "search after restore")
            assert len(searched_after_restore["items"]) == 3

            dup = dc.scanDuplication(BFScanDuplicationRequest())
            assert_ok(dup, "scanDuplication")
            assert len(dup["groups"]) >= 1

            out = root / "output"
            exported_folder = dc.export(BFExportRequest(mode="Folder", file_ids=file_ids, destination=str(out)))
            assert_ok(exported_folder, "export folder")
            assert exported_folder["exported"] >= 3

            exported_packet = dc.export(BFExportRequest(mode="Packet", file_ids=file_ids, destination=str(out)))
            assert_ok(exported_packet, "export packet")
            assert Path(exported_packet["packet"]).exists()
        finally:
            dc.closeLibrary()


if __name__ == "__main__":
    run()
    print("smoke_test passed")
