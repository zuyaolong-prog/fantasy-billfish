# file-monitor (planned)

Status: placeholder.

Responsibilities:

- Directory watch
- Debounce and coalescing
- Lock detection and delayed retry
- Ignore-self behavior

Primary work next:

- Windows adapter via ReadDirectoryChangesW
- Event normalization to BFUpdateParam
- Incremental reindex scheduler
