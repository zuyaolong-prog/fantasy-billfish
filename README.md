# fantasy-billfish

Windows 11 x64 Billfish with local DPI, HDR, AV1, video trimming, application-scoped automation, plugins, browser capture, and a built-in 3D model and animation viewer. This Git repository contains the maintenance source. The complete runnable software, including the frozen vendor core and dependencies, is distributed as a portable ZIP in Releases.

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

## Edge capture extension

The portable ZIP includes the maintained Edge capture extension 4.2.0 under `browser-extension/maintained`. After extracting to the final location, run `tools/initialize-portable.cmd` to register the current user's Native Messaging bridge with bundled Python. In `edge://extensions`, enable Developer mode, choose **Load unpacked**, and select that directory. Disable the old official Billfish capture extension. The maintained extension ID is `aloippladlgkfkcabiknfpokgcppcjkp`.

For an update in the same directory, reload the maintained extension and refresh the webpage. Before moving an existing installation, run `tools/initialize-portable.ps1 -Action RestoreBrowserHost` from the old directory, then initialize the new directory and load its extension. Registration contains absolute paths and is not copied between computers. The optional extension-only ZIP contains the same extension files; video capture still requires the complete Billfish runtime and its registered bridge. See [portable migration](./docs/portable-migration.md) for checks and recovery boundaries.

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

## Local automation

The maintained runtime also provides a JSON CLI and an explicitly started, authenticated localhost HTTP API. Use `billfish-cli.cmd schema` to discover commands, `billfish-cli.cmd info` to inspect the selected library, and `billfish-cli.cmd selected` to read the actual GUI selection. While the GUI is open, note, source and native tag writes use the host's native business actions and update the UI, with nonmodal progress in the main window. Successful online changes are independently checked against persisted metadata before completion is reported. When the GUI is closed, offline maintenance retains its database transactions and backups. Online batches can partially succeed and report individual results. JavaScript plugins run in independent Electron processes, use Eagle-style window/service manifests and the documented SDK subset, and can be enabled or disabled in preferences. Billfish command plugins declare optional commands and host permissions under the `billfish` manifest extension. Unsupported plugin types and API calls fail explicitly; this is not a claim of complete Eagle plugin compatibility. Original media stay at their ordinary filesystem paths. The full maintenance package includes official Python 3.14.8 x64 embeddable and Electron 38.8.6; the CLI prefers bundled Python and does not need a system PATH entry. Source-only development can use Python 3.11+ x64. See [automation instructions](./automation/README.md), [AI entrypoint](./automation/AI_GUIDE.md), [plugin development](./automation/PLUGINS.md), [online behavior and GUI acceptance](./docs/online-automation.md), [OpenAPI](./automation/openapi.json), and [Electron notices](./THIRD_PARTY_ELECTRON.md).

Plugin installation accepts local `.eagleplugin`/`.zip` packages and development directories through `plugins.install` with `source_path`. New plugins remain disabled; updates replace the resource tree and wait for explicit activation. The root launcher handles package opening while Billfish is running or closed. The current user's `.eagleplugin` default association has been verified against the unified launcher on the maintenance machine; extracting a portable release does not register it on another machine. Actual GUI acceptance covers Pinterest 1.1.9 similar-image results, saving its results into the library with full filenames, and video2gif 1.0.2 saving a valid GIF to an ordinary folder. Native local-file and URL-image imports are deployed. Browser video capture implements explicit resolution selection and the real configured capture folder. Actual Edge/Bilibili acceptance now covers the 4.2.0 capture button, the user's resolution choice and confirmation, download, native import into the real browser-capture folder, and playback in Billfish. The output was independently probed. Earlier Native Messaging lookup failures were real and recovered after complete Edge restarts, but the failing branch was not captured in logs; no permanent fix or confirmed root cause is claimed. Direct host tests or that partial path are not treated as complete browser acceptance. Image replacement supports same-extension owned PNG/JPG/JPEG/WebP with retained originals under the library's .bf/automation-replacements; GIF and video replacement remain unsupported. See [automation operations and current acceptance](./automation/README.md) and [package installation boundaries](./docs/plugin-installation.md). The browser tool previously blocked an Edge package-download URL, so that complete package download-to-install workflow was not verified; browser video capture is a separate workflow.

Billfish also provides [application-scoped UI control](./docs/billfish-ui-control.md) through `automation/billfish-ui.cjs`: structured Qt snapshots, control references, local pointer/keyboard input and widget captures. Actual background layout/menu operations preserve the desktop pointer. OS dialogs and independent Electron plugin windows remain outside this Qt control surface. The video trim source resolver now uses the currently open native library; a real same-process library switch and MKV export were verified. The maintained [3D Format Extensions 1.9.0 package](./docs/eagle-plugin-compatibility.md) has real native OBJ covers and animated FBX double-click preview acceptance, plus 12-format runtime rendering checks. A fresh unselected GLB produced its native cover automatically; OBJ and FBX covers survived restart unchanged. See [current acceptance](./docs/online-automation.md) and [portable migration](./docs/portable-migration.md).

## 3D models and animation

Double-click a supported model in Billfish to open the built-in 3D viewer. FBX and GLB previews include library-scoped animation discovery, compatibility reports, playback speed and frame stepping, skeleton display, coordinate profiles, and free navigation. Separate Solid and Material buttons select geometry inspection or the original-quality materials. The viewer displays loading feedback before model preparation completes and keeps errors visible when a file cannot be opened. It initializes automatically when the active library becomes ready after startup.

Animation switching retains the previous pose while the next clip is prepared, then commits the validated clip. Parsed clips and playback objects are reused where valid; the current catalog and source files are still checked. Model and animation derivatives stay in the viewer cache, separate from the original media. See the [viewer requirements and verified behavior](./docs/animation-browser/requirements-and-validation.md) and [protocol](./docs/animation-browser/protocol.md).

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
