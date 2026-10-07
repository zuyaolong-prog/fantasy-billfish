# FFmpeg third-party component

Billfish bundles unmodified FFmpeg and FFprobe for video trimming in `tools/ffmpeg/bin`. These standalone programs are invoked as subprocesses. The bundle does not require LosslessCut or an FFmpeg installation on the receiving computer.

- Version: `n8.0-23-gd1f31a829d-20251022`.
- Platform: Windows x86_64, shared-library build.
- Copyright: FFmpeg developers, 2000-2025; FFprobe developers, 2007-2025.
- License: GNU GPL version 3 or later. This build enables `--enable-gpl` and `--enable-version3`. The complete license is in [COPYING.GPLv3](tools/ffmpeg/COPYING.GPLv3).
- Binary provenance: the existing LosslessCut Windows x64 distribution used by this workspace. The executables and their seven FFmpeg DLLs are copied without modification. Their SHA-256 hashes and PE imports are in [binary-manifest.json](tools/ffmpeg/binary-manifest.json).
- Build configuration: [build-configuration.txt](tools/ffmpeg/build-configuration.txt).

Source and build information:

- [Exact FFmpeg source revision](https://github.com/FFmpeg/FFmpeg/tree/d1f31a829d018a3d9d46da93480182e565762000).
- [Download that FFmpeg source revision](https://github.com/FFmpeg/FFmpeg/archive/d1f31a829d018a3d9d46da93480182e565762000.tar.gz).
- [BtbN FFmpeg-Builds upstream](https://github.com/BtbN/FFmpeg-Builds), the Windows shared-build provider.
- [Build recipes and dependency source URLs at the build date](https://github.com/BtbN/FFmpeg-Builds/tree/acd33f8caf59ec011c73674c0c6ff4bb80db2587). This is the upstream recipe snapshot from 2025-10-22; the binary version identifies the FFmpeg source revision but does not embed the recipe repository revision.
- [LosslessCut source](https://github.com/mifi/lossless-cut), the distribution from which this bundle was obtained.

Keep the GPL license, source directions, build configuration and binary manifest with redistributions of this bundle. FFmpeg and its enabled third-party libraries retain their upstream licenses; this notice does not relicense other Billfish components.

All seven DLLs must stay beside both executables. Verification copied only these nine files to a fresh temporary directory, cleared `PATH`, and successfully ran both `-version` commands, encoded a one-second H.264 MP4 using `libx264`, and read its codec and dimensions using FFprobe. PE import inspection found no external non-Windows DLL dependency. Optional GPU/device features can still require the relevant hardware and drivers.
