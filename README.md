# fantasy-billfish

Windows 11 x64 Billfish with local DPI, HDR, AV1, video trimming, playback shortcuts and immersion improvements. This Git repository contains the maintenance source. The complete runnable software, including the frozen vendor core and dependencies, is distributed as a portable ZIP in Releases.

## Download and run

1. Open [the latest release](https://github.com/zuyaolong-prog/fantasy-billfish/releases/latest).
2. Download `fantasy-billfish-win11-x64.zip` and extract it to any writable directory.
3. Run `Billfish.exe` inside the extracted `fantasy-billfish` folder.

For example, after downloading the ZIP:

```powershell
Expand-Archive -LiteralPath .\fantasy-billfish-win11-x64.zip -DestinationPath .
& .\fantasy-billfish\Billfish.exe
```

Keep the extracted contents together. Paths with spaces or Chinese characters are supported. Windows PowerShell 5.1 is sufficient; PowerShell 7 is optional. The launcher is self-contained and does not require a separate .NET 9 installation. Qt, MSVC runtime, mpv and FFmpeg dependencies are bundled. No patch installation is needed.

The first launch creates the current user's Billfish preferences under `%APPDATA%\Billfish`. Select or create your own library in Billfish. No personal library, user configuration or backup from the development computer is included. Moving the software does not move your media library; select its new location if the library was moved separately.

Git clone and GitHub's source-code ZIP contain source, not the complete runtime. Use the release asset above to run the software. Git LFS is not used by the published source repository.

## Verify the portable release

The release provides `SHA256SUMS.txt` for the ZIP. Compare its value with:

```powershell
Get-FileHash .\fantasy-billfish-win11-x64.zip -Algorithm SHA256
```

From the extracted runtime directory:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\_launcher-patch\status-unified-billfish.ps1 -AsJson
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\start-billfish.ps1 -DryRun
```

Normal status checks the shipped runtime using relative paths and SHA-256 fingerprints. Local shortcuts, autostart, installation receipts and rollback backups are separate maintenance state. Extracting a release does not change shortcuts or autostart. If you create a shortcut manually, target `Billfish.exe` in the extracted directory.

## Maintenance source

```powershell
git clone https://github.com/zuyaolong-prog/fantasy-billfish.git
```

Launcher source and tests: [_launcher-patch](./_launcher-patch/README.md). Video trim source and build instructions: [_video-trim-patch](./_video-trim-patch/README.md). FFmpeg version, licenses and source: [THIRD_PARTY_FFMPEG.md](./THIRD_PARTY_FFMPEG.md). Third-party runtime components retain their original rights and licenses.

Runtime-dependent checks and maintenance need the matching release files. Extract the portable ZIP outside the source checkout to inspect the runnable distribution. Patch installation and rollback also require locally generated build products and local backups; those are excluded from publishing. Building the AV1 shim requires explicitly supplied FFmpeg 4.2.1 and dav1d 1.5.0 headers and MSVC x64 tools. Building the launcher requires the .NET SDK; building the video trim plugin requires CMake, MSVC and a compatible Qt development SDK.

From a complete maintenance workspace, prepare clean exports without personal libraries, backups or old Git history:

```powershell
python .\tools\export-portable.py <empty-runtime-directory-outside-this-checkout>
python .\tools\export-portable.py --source-only <empty-source-directory-outside-this-checkout>
```

The exporter does not remove or stage anything in the source workspace. Runtime binaries belong in the portable release asset; the source export excludes them.
