Set-StrictMode -Version 3.0

function Get-LauncherFileFingerprint {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) {
        return $null
    }
    $item = Get-Item -LiteralPath $LiteralPath
    [pscustomobject]@{
        Sha256 = (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToUpperInvariant()
        Size = [int64]$item.Length
    }
}

function Test-LauncherFingerprint {
    param(
        [object]$Actual,
        [object]$Expected
    )

    $null -ne $Actual -and $null -ne $Expected -and
        ([string]$Actual.Sha256).Equals([string]$Expected.Sha256, [System.StringComparison]::OrdinalIgnoreCase) -and
        [int64]$Actual.Size -eq [int64]$Expected.Size
}

function Get-LauncherIniValue {
    param(
        [Parameter(Mandatory = $true)][string]$LiteralPath,
        [Parameter(Mandatory = $true)][string]$Section,
        [Parameter(Mandatory = $true)][string]$Key
    )

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) { return $null }
    $inSection = $false
    foreach ($line in [System.IO.File]::ReadAllLines($LiteralPath, [System.Text.Encoding]::UTF8)) {
        if ($line -match '^\s*\[([^]]+)\]\s*$') {
            $inSection = $Matches[1].Equals($Section, [System.StringComparison]::OrdinalIgnoreCase)
            continue
        }
        if ($inSection -and $line -match '^\s*([^=]+?)\s*=\s*(.*)$' -and
            $Matches[1].Trim().Equals($Key, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $Matches[2].Trim()
        }
    }
    return $null
}

function Test-LauncherPath {
    param(
        [Parameter(Mandatory = $true)][string]$LiteralPath,
        [ValidateSet('Any', 'Leaf', 'Container')][string]$PathType = 'Any'
    )

    try {
        return [bool](Test-Path -LiteralPath $LiteralPath -PathType $PathType -ErrorAction Stop)
    }
    catch {
        return $false
    }
}

function Resolve-BillfishLibraryRoot {
    [CmdletBinding()]
    param(
        [AllowEmptyString()][string]$LibraryRoot,
        [AllowEmptyString()][Parameter(Mandatory = $true)][string]$AppDataRoot
    )

    $configPath = $null
    if (-not [string]::IsNullOrWhiteSpace($AppDataRoot)) {
        try {
            $configPath = [System.IO.Path]::Combine(
                [System.IO.Path]::GetFullPath($AppDataRoot),
                'Billfish',
                'config',
                'recent_lib.ini'
            )
        }
        catch {
            $configPath = $null
        }
    }

    $source = 'Explicit'
    $rawValue = $LibraryRoot
    if ([string]::IsNullOrWhiteSpace($rawValue)) {
        $source = 'RecentLibraryConfig'
        if ($null -ne $configPath) {
            $rawValue = Get-LauncherIniValue -LiteralPath $configPath -Section 'LoadDefaultLibType' -Key 'LastLoadLibPath'
        }
    }

    if ([string]::IsNullOrWhiteSpace($rawValue)) {
        $diagnostic = if ($null -eq $configPath) {
            'Billfish library root is not configured and AppDataRoot is unavailable.'
        }
        elseif (-not (Test-LauncherPath -LiteralPath $configPath -PathType Leaf)) {
            "Billfish recent library configuration is missing: $configPath"
        }
        else {
            "Billfish recent library configuration has no [LoadDefaultLibType] LastLoadLibPath: $configPath"
        }
        return [pscustomobject]@{
            LibraryRoot = $null
            Root = $null
            Source = $source
            ConfigPath = $configPath
            ConfigValue = $rawValue
            RootAvailable = $false
            Diagnostic = $diagnostic
        }
    }

    # Qt stores Windows path separators escaped in INI values. Decode only the
    # value selected above; do not probe other drives or infer an alternate root.
    $decodedValue = ([string]$rawValue).Trim()
    if ($source -eq 'RecentLibraryConfig') {
        $decodedValue = $decodedValue.Replace('\\', '\')
    }
    $resolvedRoot = $decodedValue
    try {
        $resolvedRoot = [System.IO.Path]::GetFullPath($decodedValue)
    }
    catch {
        return [pscustomobject]@{
            LibraryRoot = $null
            Root = $null
            Source = $source
            ConfigPath = $configPath
            ConfigValue = $rawValue
            RootAvailable = $false
            Diagnostic = "Billfish library root is invalid: $decodedValue ($($_.Exception.Message))"
        }
    }

    $rootAvailable = Test-LauncherPath -LiteralPath $resolvedRoot -PathType Container
    [pscustomobject]@{
        LibraryRoot = $resolvedRoot
        Root = $resolvedRoot
        Source = $source
        ConfigPath = $configPath
        ConfigValue = $rawValue
        RootAvailable = [bool]$rootAvailable
        Diagnostic = if ($rootAvailable) { 'Billfish library root resolved.' } else { "Billfish library root is unavailable: $resolvedRoot" }
    }
}

function New-LauncherHealthResult {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][bool]$Healthy,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Errors
    )

    [pscustomobject]@{ Name = $Name; Healthy = $Healthy; Errors = @($Errors) }
}

function Get-BillfishLibraryDatabaseStatus {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$BillfishRoot,
        [AllowEmptyString()][Parameter(Mandatory = $true)][string]$LibraryRoot,
        [switch]$SkipCheck
    )

    $sqlite = [System.IO.Path]::Combine([System.IO.Path]::GetFullPath($BillfishRoot), 'sqlite3.exe')
    if ([string]::IsNullOrWhiteSpace($LibraryRoot)) {
        return [pscustomobject]@{
            Name = 'LibraryDatabase'
            Path = $null
            Available = $false
            Checked = $false
            Healthy = $true
            Diagnostic = 'Billfish library root is unavailable; library database check was skipped.'
            Errors = @()
        }
    }

    $libraryDb = [System.IO.Path]::Combine($LibraryRoot, '.bf', 'billfish.db')
    if ($SkipCheck) {
        return [pscustomobject]@{
            Name = 'LibraryDatabase'
            Path = $libraryDb
            Available = Test-LauncherPath -LiteralPath $libraryDb -PathType Leaf
            Checked = $false
            Healthy = $true
            Diagnostic = 'Billfish library database check skipped.'
            Errors = @()
        }
    }

    if (-not (Test-LauncherPath -LiteralPath $libraryDb -PathType Leaf)) {
        return [pscustomobject]@{
            Name = 'LibraryDatabase'
            Path = $libraryDb
            Available = $false
            Checked = $false
            Healthy = $true
            Diagnostic = "Billfish library database is unavailable: $libraryDb"
            Errors = @()
        }
    }

    try {
        if ([int64](Get-Item -LiteralPath $libraryDb -ErrorAction Stop).Length -le 0) {
            $diagnostic = "Billfish library database is empty: $libraryDb"
            return [pscustomobject]@{
                Name = 'LibraryDatabase'
                Path = $libraryDb
                Available = $true
                Checked = $true
                Healthy = $false
                Diagnostic = $diagnostic
                Errors = @($diagnostic)
            }
        }
    }
    catch {
        return [pscustomobject]@{
            Name = 'LibraryDatabase'
            Path = $libraryDb
            Available = $false
            Checked = $false
            Healthy = $true
            Diagnostic = "Billfish library database cannot be inspected: $libraryDb ($($_.Exception.Message))"
            Errors = @()
        }
    }

    if (-not (Test-LauncherPath -LiteralPath $sqlite -PathType Leaf)) {
        $diagnostic = "sqlite3.exe is missing: $sqlite"
        return [pscustomobject]@{
            Name = 'LibraryDatabase'
            Path = $libraryDb
            Available = $true
            Checked = $false
            Healthy = $false
            Diagnostic = $diagnostic
            Errors = @($diagnostic)
        }
    }

    $query = "select (select count(*) from sqlite_master where type='trigger' and name in ('bf_hdr_lut_material_video_ai','bf_hdr_lut_material_video_au')) + (select count(*) from bf_video_userdata where lut in ('18','19','{`"default`":`"true`",`"value`":`"18`"}','{`"default`":`"true`",`"value`":`"19`"}','{`"default`": `"true`", `"value`": `"18`"}','{`"default`": `"true`", `"value`": `"19`"}'));"
    try {
        $pending = & $sqlite -batch -noheader $libraryDb $query 2>&1
        $queryExit = $LASTEXITCODE
        $queryHealthy = $queryExit -eq 0 -and [string]($pending | Select-Object -Last 1) -eq '0'
    }
    catch {
        $pending = @($_.Exception.Message)
        $queryHealthy = $false
    }
    if (-not $queryHealthy) {
        $diagnostic = 'HDR database LUT cleanup is not healthy.'
        return [pscustomobject]@{
            Name = 'LibraryDatabase'
            Path = $libraryDb
            Available = $true
            Checked = $true
            Healthy = $false
            Diagnostic = $diagnostic
            Errors = @($diagnostic)
        }
    }

    [pscustomobject]@{
        Name = 'LibraryDatabase'
        Path = $libraryDb
        Available = $true
        Checked = $true
        Healthy = $true
        Diagnostic = 'Billfish library database LUT cleanup is healthy.'
        Errors = @()
    }
}

function Get-BillfishPersistentPatchHealth {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$BillfishRoot,
        [AllowEmptyString()][Parameter(Mandatory = $true)][string]$LibraryRoot,
        [Parameter(Mandatory = $true)][string]$AppDataRoot,
        [switch]$SkipLibraryDatabaseCheck
    )

    $root = [System.IO.Path]::GetFullPath($BillfishRoot)
    $results = [System.Collections.Generic.List[object]]::new()
    $requiredFiles = [ordered]@{
        Launcher = @('Billfish.exe', 'Billfish.core.exe')
        DPI = @()
        AV1 = @('plugins/media_runtime/libFFmpeg64.dll', 'plugins/media_runtime/libFFmpeg64_orig.dll', 'plugins/media_runtime/dav1d.dll')
        HDR = @('plugins/media_runtime/libMediaSDK64.dll')
        VideoTrim = @(
            'ui_plugins/preview.dll', 'ui_plugins/preview.stock.bin', 'ui_plugins/treemodule.dll',
            'tools/ffmpeg/bin/ffmpeg.exe', 'tools/ffmpeg/bin/ffprobe.exe',
            'tools/ffmpeg/bin/avcodec-62.dll', 'tools/ffmpeg/bin/avdevice-62.dll',
            'tools/ffmpeg/bin/avfilter-11.dll', 'tools/ffmpeg/bin/avformat-62.dll',
            'tools/ffmpeg/bin/avutil-60.dll', 'tools/ffmpeg/bin/swresample-6.dll',
            'tools/ffmpeg/bin/swscale-9.dll', '_hdr-preview-patch/tools/mpv/mpv.exe'
        )
    }
    $baseline = $null
    try {
        $baseline = Get-Content -LiteralPath (Join-Path $root '_launcher-patch/portable-runtime.json') -Raw | ConvertFrom-Json
        if ($baseline.schemaVersion -ne 1) { throw 'Unsupported portable runtime schema.' }
    }
    catch {
        $results.Add((New-LauncherHealthResult -Name 'RuntimeBaseline' -Healthy $false -Errors @("Portable runtime baseline is unreadable: $($_.Exception.Message)")))
    }
    foreach ($name in $requiredFiles.Keys) {
        $errors = [System.Collections.Generic.List[string]]::new()
        foreach ($relativePath in $requiredFiles[$name]) {
            $expected = @(if ($null -ne $baseline) { $baseline.files | Where-Object { $_.path -eq $relativePath } })
            if ($expected.Count -ne 1) {
                $errors.Add("Runtime baseline must contain one fingerprint for $relativePath.")
                continue
            }
            $actual = Get-LauncherFileFingerprint -LiteralPath (Join-Path $root $relativePath)
            if (-not (Test-LauncherFingerprint $actual $expected[0])) { $errors.Add("Runtime payload does not match the portable baseline: $relativePath") }
        }
        if ($name -eq 'DPI') {
            $qtArguments = Get-LauncherIniValue -LiteralPath (Join-Path $root 'qt.conf') -Section 'Platforms' -Key 'WindowsArguments'
            $tokens = @([string]$qtArguments -split '\s+' | Where-Object { $_ })
            if ($tokens -notcontains 'fontengine=freetype') { $errors.Add('qt.conf does not enable fontengine=freetype.') }
            if (@($tokens | Where-Object { $_ -like 'dpiawareness=*' }).Count -gt 0) { $errors.Add('qt.conf contains a global dpiawareness override.') }
        }
        $results.Add((New-LauncherHealthResult -Name $name -Healthy ($errors.Count -eq 0) -Errors $errors.ToArray()))
    }
    $libraryDatabase = Get-BillfishLibraryDatabaseStatus -BillfishRoot $root -LibraryRoot $LibraryRoot -SkipCheck:$SkipLibraryDatabaseCheck
    $results.Add($libraryDatabase)
    [pscustomobject]@{
        Healthy = @($results | Where-Object { -not $_.Healthy }).Count -eq 0
        LibraryRoot = $LibraryRoot
        LibraryDatabase = $libraryDatabase
        Patches = $results.ToArray()
    }
}
function Enter-BillfishLaunchMutex {
    param([Parameter(Mandatory = $true)][string]$BillfishRoot)

    $canonical = [System.IO.Path]::GetFullPath($BillfishRoot).TrimEnd('\', '/').ToUpperInvariant()
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { $hash = ([System.BitConverter]::ToString($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($canonical)))).Replace('-', '') }
    finally { $sha.Dispose() }
    $mutex = [System.Threading.Mutex]::new($false, 'Local\BillfishLauncher-' + $hash.Substring(0, 32))
    try { [void]$mutex.WaitOne() }
    catch [System.Threading.AbandonedMutexException] { }
    return $mutex
}

function ConvertTo-WindowsCommandLineArgument {
    param([AllowEmptyString()][Parameter(Mandatory = $true)][string]$Argument)

    if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') { return $Argument }
    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq '\') { $backslashes++; continue }
        if ($character -eq '"') {
            [void]$builder.Append(('\' * (($backslashes * 2) + 1)))
            [void]$builder.Append('"')
            $backslashes = 0
            continue
        }
        if ($backslashes -gt 0) { [void]$builder.Append(('\' * $backslashes)); $backslashes = 0 }
        [void]$builder.Append($character)
    }
    if ($backslashes -gt 0) { [void]$builder.Append(('\' * ($backslashes * 2))) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function ConvertTo-WindowsCommandLine {
    param([AllowEmptyCollection()][string[]]$ArgumentList)
    $quoted = @($ArgumentList | ForEach-Object { ConvertTo-WindowsCommandLineArgument -Argument ([string]$_) })
    return [string]::Join(' ', $quoted)
}

function Initialize-LauncherProcessPathApi {
    if ('Billfish.Launcher.NativeMethods' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace Billfish.Launcher {
    public static class NativeMethods {
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool QueryFullProcessImageName(IntPtr process, int flags, StringBuilder path, ref int size);
    }
}
'@
}

function Get-LauncherProcessExecutablePath {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    try {
        $path = [string]$Process.Path
        if (-not [string]::IsNullOrWhiteSpace($path)) { return [System.IO.Path]::GetFullPath($path) }
    }
    catch { }

    Initialize-LauncherProcessPathApi
    $capacity = 32768
    $builder = New-Object System.Text.StringBuilder $capacity
    try {
        if ([Billfish.Launcher.NativeMethods]::QueryFullProcessImageName($Process.Handle, 0, $builder, [ref]$capacity)) {
            return [System.IO.Path]::GetFullPath($builder.ToString())
        }
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "QueryFullProcessImageName failed for PID $($Process.Id) with Win32 error $errorCode."
    }
    catch {
        throw "Unable to determine the executable path for Billfish PID $($Process.Id); launch is blocked to avoid a duplicate instance. $($_.Exception.Message)"
    }
}

function Exit-BillfishLaunchMutex {
    param([Parameter(Mandatory = $true)][System.Threading.Mutex]$Mutex)
    try { $Mutex.ReleaseMutex() } finally { $Mutex.Dispose() }
}

function Get-BillfishProcessByExecutablePath {
    param(
        [Parameter(Mandatory = $true)][string]$ExecutablePath,
        [string[]]$FixtureProcessPaths
    )

    $expected = [System.IO.Path]::GetFullPath($ExecutablePath)
    if ($null -ne $FixtureProcessPaths) {
        return @($FixtureProcessPaths | Where-Object {
            [System.IO.Path]::GetFullPath($_).Equals($expected, [System.StringComparison]::OrdinalIgnoreCase)
        } | ForEach-Object { [pscustomobject]@{ Id = 4242; Path = $_ } })
    }
    $matching = [System.Collections.Generic.List[object]]::new()
    foreach ($process in @(Get-Process -Name 'Billfish' -ErrorAction SilentlyContinue)) {
        $processPath = Get-LauncherProcessExecutablePath -Process $process
        if ($processPath.Equals($expected, [System.StringComparison]::OrdinalIgnoreCase)) { $matching.Add($process) }
    }
    return $matching.ToArray()
}
