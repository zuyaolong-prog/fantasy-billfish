# Roadmap (self-managed)

## M0 Baseline (done)

- Define module boundaries
- Lock API naming with `BF*` style
- Implement SQLite schema + migration

## M1 Data kernel (in progress)

- [x] library lifecycle
- [x] import/tag/search/recycle/export core path
- [ ] consistency checker (`bf_file.pid`, recycle dual-state)
- [ ] transaction retry and error mapping refinement

## M2 Monitor + incremental index

- [ ] file-monitor abstraction
- [ ] Windows `ReadDirectoryChangesW` adapter
- [ ] debounce + ignore-self + delayed retry

## M3 App shell (Qt6/QML)

- [ ] app-shell skeleton (navigation/command bus)
- [ ] list/grid/waterfall model + virtualization
- [ ] basic batch actions and context menu

## M4 Preview/media-service

- [ ] image thumbnail pipeline
- [ ] video keyframe extraction
- [ ] audio metadata probe

## M5 Stability/perf

- [ ] 100k metadata load benchmark
- [ ] search latency benchmark
- [ ] crash recovery and reindex rebuild tool
