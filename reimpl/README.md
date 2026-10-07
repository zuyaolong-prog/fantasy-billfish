# Billfish Reimplementation (Phase-1 Core)

This folder contains a clean-room backend implementation seed for the Billfish local library workflow.

## What is implemented now

- Local library lifecycle:
  - `createLibrary(path, name)`
  - `openLibrary(path)`
  - `upgradeLibrary(path, 40, 41)`
  - `closeLibrary()`
- `.bf` structure bootstrap:
  - `.bf/billfish.db`
  - `.bf/summary_v2.db`
  - `.bf/.preview`
  - `.bf/.recycle`
  - `.bf/.temp`
  - `.bf/.ui_config`
- Data-center APIs (phase-1 core):
  - `import_(BFImportRequest)` (`ImportCopy/ImportCut/ImportIndex`)
  - `reindex(BFReIndexRequest)`
  - `move(BFMoveRequest)`
  - `removeOrRestore(BFRemoveOrRestoreRequest)`
  - `setCover(BFSetCoverRequest)`
  - `tagNew/tagUpdate/tagRemove/tagMove/tagAttach/tagDetach`
  - `searchSummary(BFSearchSummaryRequest)`
  - `scanDuplication(BFScanDuplicationRequest)`
  - `export(BFExportRequest)` (`Folder` / `Packet`)
- Schema migration:
  - `40 -> 41` (indexes + consistency repair + schema marker)
- Search:
  - `summary_v2.db` with FTS5 table `bf_summary`
  - CJK bigram + lowercase normalization done in Python pre-tokenization

## Not implemented yet

- Qt6/QML app-shell and visual waterfall/grid/list UI.
- Windows native watcher (`ReadDirectoryChangesW`) and `ignore_self` semantics.
- Plugin-host process isolation.
- Media decoding pipeline.
- Cloud/transfer APIs (stubbed with `unsupported`).

## Run smoke test

```powershell
cd .\reimpl
$env:PYTHONPATH = (Get-Location).Path
python .\tests\smoke_test.py
```

## Run demo workflow

```powershell
cd .\reimpl
$env:PYTHONPATH = (Get-Location).Path
python .\scripts\demo_workflow.py
```

## Structure

- `billfish_reimpl/api_types.py`: `BF*` request/response/data models.
- `billfish_reimpl/schema.py`: schema + migration SQL.
- `billfish_reimpl/db.py`: SQLite bootstrap and transaction helpers.
- `billfish_reimpl/datacenter.py`: phase-1 core implementation.
- `proto/bf_datacenter.proto`: internal protobuf contract seed.
- `modules/*`: planned module boundaries aligned with architecture.
- `tests/smoke_test.py`: E2E workflow validation.
