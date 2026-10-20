[CmdletBinding()]
param(
    [ValidateSet('Check', 'Initialize', 'RestoreBrowserHost')][string]$Action = 'Check',
    [string]$BillfishRoot
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
if (-not $BillfishRoot) { $BillfishRoot = Split-Path -Parent $PSScriptRoot }
$root = [IO.Path]::GetFullPath($BillfishRoot)
$nativeScript = Join-Path $root 'browser-extension\native-host\manage-native-host.ps1'
if ($Action -eq 'RestoreBrowserHost') {
    & $nativeScript -Action Restore -BillfishRoot $root
    return
}
$pythonRoot = Join-Path $root 'tools\python'
$python = Join-Path $pythonRoot 'python.exe'
$runtime = [IO.File]::ReadAllText((Join-Path $pythonRoot 'runtime-files.json')) | ConvertFrom-Json
foreach ($file in $runtime.files) {
    $path = Join-Path $pythonRoot $file.path
    if (-not [IO.File]::Exists($path) -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $file.sha256) {
        throw "Bundled Python is incomplete or changed: $($file.path). Extract the complete portable package again."
    }
}
$probeCode = "import json,sys,struct,ctypes,ssl,sqlite3,urllib.request,zipfile; sys.path.insert(0,sys.argv[1]); import browser_native_host,media_transfer; print(json.dumps(dict(version=sys.version.split()[0],bits=struct.calcsize('P')*8,executable=sys.executable)))"
$probe = & $python -I -c $probeCode (Join-Path $root 'automation')
$probeExit = $LASTEXITCODE
if ($probeExit -ne 0) { throw 'Bundled Python cannot load the required standard-library helpers.' }
$pythonInfo = $probe | ConvertFrom-Json
if ($pythonInfo.bits -ne 64 -or [version]$pythonInfo.version -lt [version]'3.11') { throw 'Python 3.11+ x64 is required.' }
$baseline = & (Join-Path $root '_launcher-patch\status-unified-billfish.ps1') -BillfishRoot $root -AsJson | ConvertFrom-Json
if (-not $baseline.Healthy) { throw 'The portable Billfish baseline is incomplete. Inspect status-unified-billfish.ps1 before initialization.' }
$nativeStatus = & $nativeScript -Action Status -BillfishRoot $root | ConvertFrom-Json
$nativePythonMatch = $false
if ([IO.File]::Exists($nativeStatus.StatePath)) {
    $nativeState = [IO.File]::ReadAllText($nativeStatus.StatePath) | ConvertFrom-Json
    $nativePythonMatch = [string]::Equals($nativeState.PythonPath, $python, [StringComparison]::OrdinalIgnoreCase)
}
if ($Action -eq 'Initialize' -and (-not $nativeStatus.Healthy -or -not $nativePythonMatch)) {
    & $nativeScript -Action Register -BillfishRoot $root -PythonPath $python | Out-Null
    $nativeStatus = & $nativeScript -Action Status -BillfishRoot $root | ConvertFrom-Json
    if (-not $nativeStatus.Healthy) { throw 'Native host registration did not pass its local status check.' }
    $nativePythonMatch = $true
}
[pscustomobject]@{
    Action = $Action
    Root = $root
    BaselineHealthy = $baseline.Healthy
    Python = $pythonInfo
    NativeHost = $nativeStatus
    NativeHostUsesBundledPython = $nativePythonMatch
    EdgeExtensionPath = (Join-Path $root 'browser-extension\maintained')
    EdgeExtensionId = 'aloippladlgkfkcabiknfpokgcppcjkp'
    NextStep = 'Load the maintained directory in edge://extensions, then start the root Billfish.exe and open or create your library. Browser end-to-end acceptance is a separate check.'
} | ConvertTo-Json -Depth 8
