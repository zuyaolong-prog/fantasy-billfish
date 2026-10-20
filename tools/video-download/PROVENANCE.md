# Video download dependency

Billfish's media transfer worker uses the official Windows x64 standalone
`yt-dlp.exe`, pinned to stable release `2026.08.19`. No global application or
browser configuration is changed. Automatic updates, user configuration files,
plugin discovery, cookies and browser credential access are not used.

- Release: https://github.com/yt-dlp/yt-dlp/releases/tag/2026.08.19
- Binary: https://github.com/yt-dlp/yt-dlp/releases/download/2026.08.19/yt-dlp.exe
- SHA-256: `66674953fe251b89f4d08c5f0e35e0728679bd67ab3d7d05c0562af101dd3e7a`
- Verification: official release `SHA2-256SUMS`, retained alongside the binary;
  the worker verifies the pinned digest before every downloader invocation.
- Upstream license: `LICENSE` (Unlicense).
- Bundled third-party licenses: `THIRD_PARTY_LICENSES.txt`, fetched from the
  matching `2026.08.19` tag. The official executable includes EJS support.
- Downloaded and verified: 2026-10-18.

JavaScript challenges use existing `tools/electron/electron.exe` (Electron
38.8.6 / Node v22.22.0), with `ELECTRON_RUN_AS_NODE=1` set only in child process
environment. It supports Node's `crypto.webcrypto.subtle` and satisfies yt-dlp's
Node >=22 requirement. No extra Node binary is installed.

Video merge and verification use existing `tools/ffmpeg/bin/ffmpeg.exe` and
`ffprobe.exe`. Their provenance and licenses remain in `THIRD_PARTY_FFMPEG.md`.

The worker makes one single-video request, returns actual extracted resolutions,
downloads only the selected format (plus same-video audio where necessary), and
verifies the produced file with ffprobe before returning it for Billfish import.
No DRM bypass, login flow or 4K Video Downloader process automation is included.
