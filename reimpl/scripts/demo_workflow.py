from __future__ import annotations

import tempfile
from pathlib import Path

from billfish_reimpl import (
    BFExportRequest,
    BFImportMode,
    BFImportRequest,
    BFRemoveOrRestoreRequest,
    BFSearchSummaryRequest,
    BillfishDataCenter,
)


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="billfish_reimpl_demo_") as td:
        root = Path(td)
        samples = root / "samples"
        samples.mkdir()
        (samples / "cat.png").write_bytes(b"cat")
        (samples / "dog.png").write_bytes(b"dog")

        dc = BillfishDataCenter()
        print(dc.createLibrary(str(root / "library"), "Demo"))
        print(dc.openLibrary(str(root / "library")))

        req = BFImportRequest(entries=[str(samples)], import_mode=BFImportMode.ImportIndex)
        imported = dc.import_(req)
        print(imported)

        print(dc.tagNew("pet"))
        tag_id = dc.tagNew("animal").get("tag_id")
        if tag_id:
            dc.tagAttach(imported["imported"], [tag_id])

        print(dc.searchSummary(BFSearchSummaryRequest(query="cat")))

        if imported.get("imported"):
            first_id = imported["imported"][0]
            print(dc.removeOrRestore(BFRemoveOrRestoreRequest(file_ids=[first_id], action="remove")))
            print(dc.removeOrRestore(BFRemoveOrRestoreRequest(file_ids=[first_id], action="restore")))

        export_dir = root / "exports"
        print(
            dc.export(
                BFExportRequest(
                    mode="Folder",
                    file_ids=imported.get("imported", []),
                    destination=str(export_dir),
                )
            )
        )

        print(dc.closeLibrary())


if __name__ == "__main__":
    main()
