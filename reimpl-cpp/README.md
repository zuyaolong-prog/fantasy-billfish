# Billfish Clone (C++ Path)

Clean-room C++ reimplementation project for a Billfish-like waterfall file manager that coexists with Windows Explorer.

## Current status (Iteration-1)

Implemented core backend (C++20):

- Library lifecycle: create/open/upgrade(40->41)/close
- Import: Index/Copy/Cut
- Tag: create/update/delete/move/attach/detach
- Search summary (FTS5 in `summary_v2.db`)
- Recycle flow: remove/restore/purge
- Export: Folder + Packet directory (`.billfishpack`)
- SQLite runtime binding via dynamic loading (`sqlite3.dll`)

## Directory

- `src/core/api`: shared BF request/response types
- `src/core/db`: sqlite runtime and schema/migration
- `src/core/datacenter`: data center implementation
- `src/core/util`: filesystem/hash/search normalization utilities
- `src/app`: CLI bootstrap
- `src/modules/*`: reserved for app-shell/media/file-monitor/plugin-host

## Build

```powershell
cd .\reimpl-cpp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Run

```powershell
# create/open/close a library
.\build\Release\bfcli.exe init "$env:TEMP\bf-lib"

# run demo flow (sample->import->tag->search->export)
.\build\Release\bfcli.exe demo "$env:TEMP\bf-demo"
```

## Notes

- This iteration focuses on backend kernel only.
- No file association/shell extension is registered.
- No Explorer replacement behavior is introduced.
