# Billfish video trim patch operations

This directory packages the `ui_plugins/preview.dll` video-trim proxy as a reversible, hash-gated Billfish patch. The build output is never installed directly. It must first be published as a verified `prepared` package, then installed through the transaction scripts in this directory.

## Supported stock build

The current allowlist contains exactly one stock plugin:

| Billfish | Qt ABI | Path | Size | SHA-256 |
| --- | --- | --- | ---: | --- |
| 3.1.5.12 | Qt 5.15.8 x64 | `ui_plugins/preview.dll` | 828928 | `962947229E0E76AE1B882FB3D86308B8DC7E38B7CF51CCB4173E588E0E69021D` |

The canonical allowlist is [lib/preview-original-hashes.json](./lib/preview-original-hashes.json). Preparation and installation both require a hash and size match. The serialized manifest is checked against the allowlist again, including `pluginVersion` and `qtAbi`. Unknown or drifted plugin builds are never overwritten.

## Proxy runtime contract

The patched `preview.dll` is a proxy. During install, the stock plugin is atomically renamed to:

```text
ui_plugins/preview.stock.bin
```

The `.bin` suffix is deliberate: it prevents a plugin scanner from loading the stock DLL as a second plugin. The proxy must load this sibling by absolute path.

An independent recovery copy is stored under:

```text
_video-trim-patch/backups/stock/<stock-sha256>/preview.dll
```

Restore requires the active proxy, `preview.stock.bin`, the recovery copy, and the installation receipt to match their recorded fingerprints. It then atomically moves stock bytes back to `ui_plugins/preview.dll` and verifies that `preview.stock.bin` is gone.

## Video trimming workflow

Open a video in Billfish and choose `剪辑视频...` from the preview frame menu or title-bar entry. The stock preview and stock playback timeline remain untouched. A modeless, compact editor opens with:

- An isolated mpv video surface that does not reparent or modify Billfish's native renderer.
- One full-width timeline with draggable blue `In` and `Out` handles and the current playhead.
- Play/pause and current/selected-duration readouts in the footer.
- `导出片段` and `复制到剪贴板` as the only primary actions.
- `精确` and `快速` export modes in the export button menu instead of permanent toolbar controls.

The editor snapshots the source that was active when it opened. Each trim entry keeps its controls on the actual `PreviewControlWidget`; the source ID comes from exactly one globally visible `PreviewDetailsWidget`. The injector observes that widget's `signalPreviewFileIdChanged(qint64)` signal as soon as it appears, and the action selects the currently visible source before locating the file. Opening fails closed when the visible source is missing or ambiguous, its ID has not been observed, or the library lookup fails; a previous source is never retained as a substitute. Switching materials in the Billfish main window never retargets an open editor; close and reopen the editor to work on a different source. Export and clipboard generation run in a background worker with progress, cancellation, validation, and error reporting.

The preview backend locates mpv in this order: `BILLFISH_TRIM_MPV`, `_hdr-preview-patch/tools/mpv/mpv.exe` under the Billfish root, then `mpv.exe` on `PATH`. The local portable build is launched as an isolated child process with a random Windows named-pipe IPC endpoint, D3D11 `gpu-next`, AV1/HEVC/H.264 support, and HDR-to-SDR tone mapping. Closing the editor performs bounded `quit -> terminate -> kill` cleanup before destroying its native video host.

## Video playback keyboard shortcuts

The selected video supports these keys in ordinary preview, immersion, and native fullscreen:

| Key | Action |
| --- | --- |
| Up / Down | Seek forward / backward by the current step, initially 5 seconds. Preserve playing or paused state. |
| S / D | Pause and move backward / forward one native SDK frame. Holding either key repeats using the keyboard's normal repeat rate. |
| C / V | Decrease / increase the seek step by 1 second per physical press, with a minimum of 1 second. Show the new step in a tooltip. |

The seek step is shared across preview modes during the current application session and resets to 5 seconds on restart. Text fields, popup menus, modal dialogs, and the trim editor retain their own keyboard behavior. Frame stepping uses the decoder's native frame operations; it does not estimate frame duration. Paused time readouts refresh again when asynchronous decoding reports its position.

Seeking or frame stepping shows a two-line overlay at the upper-left of the video for 2 seconds. It displays current time and frame, followed by total duration and frame count. Time includes milliseconds; frame counts come directly from the SDK and show `--` when unavailable. Keyboard operations, native frame buttons, and the native timeline share this feedback. Repeated operations extend its visibility, and asynchronous decoding updates the displayed values without extending the timeout.

## Local compact shell

The maintained proxy also hosts the local compact shell: the material toolbar occupies the former web-tab row, and Resource Cloud Library, KuQue and Creative Inspiration entry points are removed. The header omits the Billfish logo/name, avatar and observed login entries, while retaining the native main menu, sidebar toggle and window controls. Import History lives in the main menu with its original action and Alt+L shortcut. Native local-library controls, automation and Billfish plugins remain available. See [compact shell](../docs/compact-shell.md) for the feature boundary and runtime evidence.

## Bounded diagnostics

Runtime diagnostics are disabled by default. Set `BILLFISH_TRIM_DEBUG=1` only while troubleshooting; any other value, including an unset variable, keeps diagnostics off. The log is written to `%LOCALAPPDATA%/Billfish/VideoTrim/diagnostic.log` and contains concise path-resolution and playback-transfer summaries. It never dumps the QObject tree or enumerates methods into the log.

Duplicate records are debounced, writes are rate-limited, and player discovery scans are coalesced with a one-second minimum interval. The diagnostic file has a hard 5 MiB ceiling; reaching the limit atomically replaces it with a reset marker and the newest record instead of growing without bound.

## Build and prepare contract

The compiler or build agent must produce only:

```text
_video-trim-patch/build/bin/preview.dll
```

Run preparation after the build:

```powershell
./prepare-video-trim-patch.ps1 -DryRun
./prepare-video-trim-patch.ps1 -WhatIf
./prepare-video-trim-patch.ps1
```

Preparation verifies the installed stock plugin against the allowlist, verifies the Billfish and Qt metadata when available, hashes the build output, rejects a byte-identical payload, and atomically publishes:

```text
prepared/preview.dll
prepared/manifest.json
```

`manifest.json` uses schema version 1 and contains:

- `patchId`, `pluginVersion`, `qtAbi`, and `createdUtc`.
- `target.relativePath` fixed to `ui_plugins/preview.dll`.
- `original.sha256` and `original.size`.
- `patched.sha256` and `patched.size`.
- `stockBackupRelativePath` fixed to `ui_plugins/preview.stock.bin`, relative to the Billfish root.
- `recoveryBackupRelativePath`, relative to this patch directory.

Preparation is refused while an installation receipt or unexpected `preview.stock.bin` exists. Restore first before publishing a new proxy build.

## Install

The proxy also enhances the existing video immersion mode (`ImmersionBtn`).
Entering and leaving immersion restores the current millisecond position and
playing/paused state after the target SDK finishes loading. The bottom playback
controls overlay the video without resizing it and stay hidden until the pointer
approaches the bottom edge. Both the close button and Escape retain playback.
Previous/next
buttons appear near their respective side edges; moving away hides controls
after 800 ms. Timeline drags and volume/menu interactions keep playback controls
visible. The existing immersive title-bar behavior and full-screen entry remain.

Runtime contracts and the read-only inspection tool are documented in
`diagnostics/window_probe/README.md`.

Close Billfish, then run:

```powershell
./install-video-trim-patch.ps1 -DryRun
./install-video-trim-patch.ps1 -WhatIf
./install-video-trim-patch.ps1
```

The matching `.cmd` wrapper accepts the same arguments:

```bat
install-video-trim-patch.cmd -DryRun
install-video-trim-patch.cmd
```

Install performs a complete preflight before changing files. It validates the prepared payload, manifest, allowlist, live stock target, any existing recovery copy, and Billfish process state. The transaction then:

1. Creates and hash-verifies the persistent recovery copy, if it does not already exist.
2. Stages and verifies the proxy beside the target so final renames stay on one volume.
3. Writes and validates a temporary installation receipt.
4. Atomically renames stock `preview.dll` to `preview.stock.bin`.
5. Atomically renames the staged proxy to `preview.dll` and verifies its fingerprint.
6. Commits `state/installation.json` as the transaction commit point.

Any failure before the commit point restores stock `preview.dll`, removes `preview.stock.bin`, removes temporary state, and reports whether rollback completed. An already installed matching patch is an idempotent success. An unmanaged patched target or any fingerprint drift is a hard failure.

Prepare, install, restore, and launch commits are serialized with a named per-patch mutex. A second concurrent operation is refused, and each mutating core re-runs preflight after it acquires the lock.

## Status and launch

Inspect the complete state without changing it:

```powershell
./status-video-trim-patch.ps1
./status-video-trim-patch.ps1 -AsJson
```

Important states are:

- `targetState=Stock`: stock plugin is active and no install receipt exists.
- `targetState=Installed`: the verified proxy is active.
- `runtimeStockState=Valid`: `preview.stock.bin` matches the recorded original.
- `Drifted`, `Unknown`, `PatchedUnmanaged`, `Invalid`, or `StockWithStaleState`: stop and inspect; mutation scripts will refuse to continue.

Status exits with code `0` for a healthy stock or installed state, `2` for an unhealthy/drifted state, and `1` if status evaluation itself fails.

Launch only after status identifies a healthy installed patch:

```powershell
./start-billfish-video-trim.ps1 -DryRun
./start-billfish-video-trim.ps1 -WhatIf
./start-billfish-video-trim.ps1
```

Arguments after the script parameters are forwarded to `Billfish.exe`. Add `-Wait` when the caller should wait for Billfish to exit.

## Restore

Close Billfish, then run:

```powershell
./restore-video-trim-patch.ps1 -DryRun
./restore-video-trim-patch.ps1 -WhatIf
./restore-video-trim-patch.ps1
```

Restore has no force bypass. Before mutation it independently validates:

- Active `ui_plugins/preview.dll` against the receipt's patched fingerprint.
- Runtime `ui_plugins/preview.stock.bin` against the original fingerprint.
- The persistent recovery copy against the same original fingerprint.
- The receipt's original fingerprint and metadata against the allowlist.

If any check fails, restore leaves every file untouched. A successful restore removes the active installation receipt and `preview.stock.bin`; the hash-addressed recovery copy remains available for disaster recovery.

## `-DryRun` and `-WhatIf`

All mutating public scripts support both modes:

- `-DryRun` runs the operation-specific preflight and returns a structured `WouldPrepare`, `WouldInstall`, `WouldRestore`, or `WouldStart` result without invoking `ShouldProcess`.
- `-WhatIf` runs the same preflight, then uses PowerShell `ShouldProcess` to show the proposed mutation without performing it.

Install or restore preflight returns `BlockedByRunningBillfish` with exit code `2` when the matching Billfish process is active. Close it and rerun the command.

Neither mode creates prepared output, backups, runtime stock, installation state, or a Billfish process.

## Tests

The tests use a disposable directory under the OS temp directory. They never touch the real Billfish plugin, generated build output, prepared package, or runtime state.

```powershell
./tests/patch/run-tests.ps1
```

Coverage includes:

- Parser validation for every PowerShell file.
- Prepared manifest publication and allowlist metadata.
- Install and restore `DryRun` non-mutation.
- Full fixture prepare -> install -> status -> restore.
- Restore drift refusal without overwriting changed bytes.
- Injected install failure with automatic stock rollback.
- Injected restore failure with automatic patched-state rollback.
- Removal of `preview.stock.bin` after a successful restore.

The suite is compatible with Windows PowerShell 5.1 and PowerShell 7.
