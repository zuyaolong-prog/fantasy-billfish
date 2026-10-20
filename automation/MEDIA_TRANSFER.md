# Media transfer worker

`python -I automation/media_transfer.py` reads one UTF-8 JSON line from stdin.
The Qt caller supplies a fresh absolute temporary `workspace` for each operation
sequence. The worker never writes a Billfish library. Keep the workspace until
native import completes, then remove that task directory.

Every stdout line is a UTF-8 JSON object. The worker writes zero or more
`{"event":"progress","stage":"probe|download|verify","message":"...","percent":42}`
events (message and percent are optional), then exactly one terminal event:

```json
{"event":"result","ok":true,"result":{}}
```

```json
{"event":"result","ok":false,"error":{"code":"download_failed","message":"..."}}
```

Diagnostics may be written to stderr. Wait for process exit and drain both pipes
before resolving the operation. Terminating the worker cancels the operation:
its Windows Job Object kills every descendant, including yt-dlp and FFmpeg.
The job setup fails explicitly if lifetime ownership cannot be established.

## Probe

```json
{"mode":"probe_video","url":"https://www.youtube.com/watch?v=...","referer":"https://www.youtube.com/","workspace":"E:\\Temp\\Billfish-Media-job"}
```

`referer` is optional. For Bilibili/YouTube use the video page as `url`. For a
generic page with a discovered direct media URL, pass that media URL and use the
page as `referer`. The caller should preserve the original page for item origin.

Result fields: `workspace`, `title`, `origin`, `choices`. Each choice contains
`choice_id`, `label`, `format_id`, `width`, `height`, `fps`, `video_codec`,
`audio_codec`, `has_audio`, `estimated_bytes`. Unknown numeric fields are null.
Only formats actually returned by the website are offered. `format_id` includes
the same-video audio format when streams are separate. No automatic resolution
fallback is allowed. The probe saves private `info.json` and `probe.json` in the
workspace so the next request uses the exact discovered choice.

Playlist and live-stream inputs are rejected. Authentication restrictions and
DRM remain errors; no cookies or browser credentials are accessed.

## Download the selected video

```json
{"mode":"download_video","workspace":"E:\\Temp\\Billfish-Media-job","choice_id":"choice returned by probe"}
```

Only a choice saved by the probe is accepted. The result contains `path`,
`title`, `origin`, `width`, `height`, `fps`, `has_audio`. `path` is a nonempty
ordinary file directly inside the supplied workspace. FFprobe verifies video,
dimensions, frame rate and expected audio before success. The Qt caller can now
import the file through Billfish's existing native import flow.
Missing HLS/DASH fragments abort the download, including a missing final fragment;
partial media is never reported as a successful import source.

## Download an image

```json
{"mode":"download_image","url":"https://i.pinimg.com/.../image.jpg","referer":"https://www.pinterest.com/","workspace":"E:\\Temp\\Billfish-Image-job"}
```

Result contains `path`, `title`, `origin`, `extension`. Download size is limited
to 100 MiB. The worker identifies PNG, JPEG, GIF, WebP, BMP and TIFF by content
signature, chooses a safe extension, and rejects HTML/error pages. The Qt caller
must decode the image before importing it.

## Validation

`python -m unittest discover -s automation/tests -p test_media_transfer.py -v`
tests actual local HTTP video/image transfers, separate audio/video merging,
format mismatch handling, invalid responses and Windows cancellation cleanup.
Dependency source and pinned hash are in `tools/video-download/PROVENANCE.md`.
