[CmdletBinding()]
param(
    [ValidateSet('Status', 'Register', 'Restore')][string]$Action = 'Status',
    [string]$BillfishRoot,
    [string]$PythonPath
)
$ErrorActionPreference = 'Stop'
if (-not $BillfishRoot) { $BillfishRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent }
$root = [IO.Path]::GetFullPath($BillfishRoot)
$nativeRoot = Join-Path $root 'browser-extension\native-host'
$generated = Join-Path $nativeRoot 'generated'
$manifestPath = Join-Path $generated 'com.billfish.capture.json'
$hostPath = Join-Path $generated 'billfish-capture-host.cmd'
$scriptPath = Join-Path $root 'automation\browser_native_host.py'
$statePath = Join-Path $nativeRoot 'registration-state.json'
$registryPath = 'Software\Google\Chrome\NativeMessagingHosts\com.billfish.capture'
$origin = 'chrome-extension://aloippladlgkfkcabiknfpokgcppcjkp/'
$utf8 = [Text.UTF8Encoding]::new($false)

function Read-Registration {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($registryPath)
    if ($null -eq $key) { return $null }
    try {
        $names = @($key.GetValueNames())
        return [pscustomobject]@{
            Default = $key.GetValue('', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            DefaultKind = if ($names -contains '') { $key.GetValueKind('').ToString() } else { $null }
            ValueNames = $names
            SubKeys = @($key.GetSubKeyNames())
        }
    } finally { $key.Dispose() }
}

function Read-State {
    if (-not [IO.File]::Exists($statePath)) { return $null }
    return ([IO.File]::ReadAllText($statePath) | ConvertFrom-Json)
}

function File-Hash([string]$Path) {
    if (-not [IO.File]::Exists($Path)) { return $null }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Owned-Registration($Current, $State) {
    return ($null -ne $Current -and $null -ne $State -and
        $State.Schema -eq 1 -and $State.Root -ceq $root -and
        $State.RegistryPath -ceq $registryPath -and $State.ManifestPath -ceq $manifestPath -and
        $Current.Default -ceq $manifestPath -and $Current.DefaultKind -ceq 'String' -and
        @($Current.ValueNames).Count -eq 1 -and @($Current.ValueNames)[0] -ceq '' -and
        @($Current.SubKeys).Count -eq 0)
}

function Write-State($State) {
    [IO.File]::WriteAllText($statePath, ($State | ConvertTo-Json -Depth 12), $utf8)
}

$current = Read-Registration
$state = Read-State
$owned = Owned-Registration $current $state
if ($Action -eq 'Status') {
    $filesMatch = ($null -ne $state -and
        (File-Hash $manifestPath) -ceq $state.ManifestSha256 -and
        (File-Hash $hostPath) -ceq $state.HostSha256)
    [pscustomobject]@{
        Name = 'com.billfish.capture'; Scope = 'HKCU Chromium (Edge fallback)'; Registered = ($null -ne $current)
        Owned = $owned; Healthy = ($owned -and $filesMatch); ManifestPath = $manifestPath
        RegistryDefault = if ($null -ne $current) { $current.Default } else { $null }
        AllowedOrigin = $origin; StatePath = $statePath
    } | ConvertTo-Json -Depth 5
    return
}

if ($null -ne $current -and -not $owned) {
    throw 'The Chromium native-host key exists without a matching Billfish ownership record. No registry or generated files were changed.'
}
if ($Action -eq 'Restore') {
    if ($null -eq $state) { throw 'No Billfish native-host registration backup exists.' }
    if ($null -ne $current) {
        # Registration only accepts an absent original key, so rollback removes exactly our key.
        if (-not $state.OriginalAbsent) { throw 'Unsupported registration backup. No changes were made.' }
        [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKey($registryPath, $false)
    }
    $state.Phase = 'restored'
    Write-State $state
    [pscustomobject]@{ Restored = $true; RegistryPath = "HKCU\$registryPath"; BackupPath = $state.BackupPath } | ConvertTo-Json
    return
}

foreach ($required in @((Join-Path $root 'Billfish.exe'), $scriptPath,
    (Join-Path $nativeRoot 'billfish-capture-host.cmd.in'), (Join-Path $nativeRoot 'com.billfish.capture.json.in'))) {
    if (-not [IO.File]::Exists($required)) { throw "Required file is missing: $required" }
}
if (-not $PythonPath) {
    $bundledPython = Join-Path $root 'tools\python\python.exe'
    if ([IO.File]::Exists($bundledPython)) { $PythonPath = $bundledPython }
    elseif ($env:BILLFISH_PYTHON) { $PythonPath = $env:BILLFISH_PYTHON }
    else {
        $python = Get-Command python.exe -ErrorAction Stop
        $PythonPath = $python.Source
    }
}
# Resolve launcher shims to the interpreter that will actually run the native host.
$pythonReply = & $PythonPath -I -c 'import json,sys; print(json.dumps(sys.executable,ensure_ascii=True))'
$pythonExit = $LASTEXITCODE
if ($pythonExit -ne 0 -or @($pythonReply).Count -ne 1) { throw 'Could not resolve a working Python interpreter.' }
$resolvedPython = $pythonReply | ConvertFrom-Json -ErrorAction Stop
if ($resolvedPython -isnot [string] -or [string]::IsNullOrWhiteSpace($resolvedPython)) { throw 'Python did not return an executable path string.' }
$PythonPath = [IO.Path]::GetFullPath([string]$resolvedPython)
if (-not [IO.File]::Exists($PythonPath)) { throw 'The resolved Python interpreter does not exist.' }
foreach ($pathValue in @($PythonPath, $scriptPath)) {
    if ($pathValue.IndexOfAny([char[]]@('%', '"', "`r", "`n")) -ge 0) {
        throw 'Native-host wrapper paths cannot contain percent, quote, or line break characters.'
    }
}
if ($owned -and ((File-Hash $manifestPath) -cne $state.ManifestSha256 -or (File-Hash $hostPath) -cne $state.HostSha256)) {
    throw 'Generated native-host files have changed since registration. Preserve and inspect those edits before replacing them.'
}
if (-not $owned -and ([IO.File]::Exists($manifestPath) -or [IO.File]::Exists($hostPath)) -and
    ($null -eq $state -or (File-Hash $manifestPath) -cne $state.ManifestSha256 -or (File-Hash $hostPath) -cne $state.HostSha256)) {
    throw 'Untracked generated native-host files already exist. No files or registry values were replaced.'
}
[IO.Directory]::CreateDirectory($generated) | Out-Null
$backupPath = Join-Path $nativeRoot ('backups\' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ'))
[IO.Directory]::CreateDirectory($backupPath) | Out-Null
[IO.File]::WriteAllText((Join-Path $backupPath 'registry-before.json'),
    (@{ RegistryPath = $registryPath; Original = $current } | ConvertTo-Json -Depth 12), $utf8)
foreach ($existing in @($statePath, $manifestPath, $hostPath)) {
    if ([IO.File]::Exists($existing)) { [IO.File]::Copy($existing, (Join-Path $backupPath ([IO.Path]::GetFileName($existing))), $false) }
}
$wrapper = [IO.File]::ReadAllText((Join-Path $nativeRoot 'billfish-capture-host.cmd.in')).Replace('@@PYTHON@@', $PythonPath).Replace('@@SCRIPT@@', $scriptPath)
# Explicit CRLF and BOM-free bytes keep cmd.exe output and Chromium's framing clean.
$wrapper = ($wrapper -replace "`r?`n", "`r`n")
[IO.File]::WriteAllText($hostPath, $wrapper, $utf8)
$manifest = [IO.File]::ReadAllText((Join-Path $nativeRoot 'com.billfish.capture.json.in')) | ConvertFrom-Json
$manifest.path = $hostPath
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 5), $utf8)
$nextState = [pscustomobject]@{
    Schema = 1; Root = $root; RegistryPath = $registryPath; ManifestPath = $manifestPath
    OriginalAbsent = $true; BackupPath = $backupPath; Phase = 'prepared'
    ManifestSha256 = File-Hash $manifestPath; HostSha256 = File-Hash $hostPath; PythonPath = $PythonPath
}
# Journal before setting the key, allowing Restore after an interrupted registration.
Write-State $nextState
$key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($registryPath)
try { $key.SetValue('', $manifestPath, [Microsoft.Win32.RegistryValueKind]::String) }
finally { $key.Dispose() }
$nextState.Phase = 'registered'
Write-State $nextState
[pscustomobject]@{ Registered = $true; ManifestPath = $manifestPath; HostPath = $hostPath; BackupPath = $backupPath; AllowedOrigin = $origin } | ConvertTo-Json
