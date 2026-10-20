# Edge Native Messaging host

This host accepts video capture requests from the maintained Billfish extension
`aloippladlgkfkcabiknfpokgcppcjkp` only. The browser sends one UTF-8 JSON message
with a four-byte little-endian length prefix. The maximum message size is 1 MiB.

```javascript
chrome.runtime.sendNativeMessage('com.billfish.capture', {
  action: 'captureVideo',
  url: 'https://example.com/watch/123',
  page_url: 'https://example.com/watch/123',
  title: 'Video title',
  media_url: 'https://example.com/video.mp4'
});
```

Only `action` and `url` are required. Optional URL fields must also use HTTP(S).
The host does not accept arbitrary Billfish operations, cookies, shell commands,
or a caller-selected installation path. URLs are sent through stdin only.

The host calls the existing current-user Billfish named pipe. If Billfish is not
ready, it launches the root `Billfish.exe` once and waits up to 45 seconds. It
submits `video.capture` once after readiness and returns
`{"ok":true,"accepted":true,"job_id":"..."}`. The Billfish UI handles source
inspection, resolution selection, download, and import asynchronously. An
accepted response means the task was queued, not that its download finished.

On a submission transport failure, the response includes `job_id` alongside
`ok:false`. Query the existing `jobs.get` API using that ID before retrying.
The host never automatically repeats the capture request. Backend error details
and source URLs are excluded from native-host diagnostics.

## Register, inspect, and restore

Run from the Billfish installation root:

```powershell
& .\browser-extension\native-host\manage-native-host.ps1 -Action Status
& .\browser-extension\native-host\manage-native-host.ps1 -Action Register
& .\browser-extension\native-host\manage-native-host.ps1 -Action Restore
```

Registration prefers the bundled `tools/python/python.exe`, respects an explicit
`-PythonPath` or `BILLFISH_PYTHON`, and verifies the actual interpreter with
`sys.executable`. It creates an absolute-path
manifest and binary-stdio `.cmd` wrapper in `generated`, then registers only:

`HKCU\Software\Google\Chrome\NativeMessagingHosts\com.billfish.capture`

The allowed origin is exactly
`chrome-extension://aloippladlgkfkcabiknfpokgcppcjkp/`. Registration records
ownership and SHA-256 hashes in `registration-state.json`, with pre-change
backups under `backups`. Existing registry keys or generated files without a
matching record are rejected. Modified generated files are preserved for
inspection. Restore removes the owned registry key; it retains backup and host
files so rollback does not discard local evidence. Machine-wide keys, extension
installation, browser policies, and the Billfish launcher are not modified.

Microsoft Edge officially falls back to the Chromium/Chrome host locations when
its own host key is absent. This script uses one canonical current-user Chrome
location, which is shared with Edge. The installed Codex MSIX package explicitly
excludes this location from Windows 11 registry virtualization; its Edge location
is outside that exclusion. A registration written to the private package view
can appear healthy to the installer while remaining invisible to the browser.
The older owned Edge registration was restored before migrating this installation.

Check registration from an independent process context and perform an ordinary
browser capture after registration. A healthy Status result checks the caller's
registry view and installed files; it does not replace a browser capture test.
Browser restarts and browser-policy changes are not registration repair steps.

Framing and the `.cmd` host approach follow the official
[Chromium nativeMessaging host example](https://github.com/GoogleChrome/chrome-extensions-samples/tree/main/api-samples/nativeMessaging/host).
On Windows the host sets stdin/stdout to binary mode. The wrapper suppresses
console output and keeps its stdio handles connected to Python.

## Validation

```powershell
python -m unittest discover -s automation/tests -p test_browser_native_host.py -v
```

These tests cover frame boundaries, partial reads, UTF-8, disallowed origins and
operations, field validation, forwarding, task identity, cold-start readiness,
and no replay after a timeout. Windows also runs the actual `.cmd` wrapper with
framed binary stdin/stdout. Browser installation and end-to-end capture require
the maintained extension and Billfish UI runtime.
