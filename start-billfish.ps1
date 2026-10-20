[CmdletBinding(PositionalBinding = $false)]
param(
    [AllowEmptyString()][string]$LibraryRoot,
    [switch]$DryRun,
    [string]$InstallPlugin,
    [switch]$StandardRender,
    [switch]$SkipUiPreferences,
    [int]$LeftFolderHeight = 2000,
    [string]$TestFixtureRoot,
    [string]$TestAppDataRoot,
    [string[]]$TestProcessPaths,
    [switch]$TestSkipLibraryDatabaseCheck,
    [switch]$TestSkipPersistentPatchHealth,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$BillfishArgs
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$BillfishRoot = if ([string]::IsNullOrWhiteSpace($TestFixtureRoot)) { $PSScriptRoot } else { [System.IO.Path]::GetFullPath($TestFixtureRoot) }
$BillfishExe = Join-Path $BillfishRoot 'Billfish.core.exe'
$CoreScript = Join-Path $PSScriptRoot '_launcher-patch\Launcher.Core.ps1'
$AppDataRoot = if ([string]::IsNullOrWhiteSpace($TestAppDataRoot)) { $env:APPDATA } else { [System.IO.Path]::GetFullPath($TestAppDataRoot) }
$UserConfig = Join-Path $AppDataRoot 'Billfish\config\user_config.ini'
$PassThroughArgs = @($BillfishArgs | Where-Object { $null -ne $_ })
$bundledPython = Join-Path $BillfishRoot 'tools\python\python.exe'
if (-not $env:BILLFISH_PYTHON -and [IO.File]::Exists($bundledPython)) { $env:BILLFISH_PYTHON = $bundledPython }

. $CoreScript
. (Join-Path $PSScriptRoot '_launcher-patch\PluginIngress.Core.ps1')
$pluginRequest = Resolve-BillfishPluginIngress -ExplicitPath $InstallPlugin -Arguments $PassThroughArgs
$PassThroughArgs = @($pluginRequest.Arguments)

function Add-ChromiumFlag {
    param([Parameter(Mandatory = $true)][string]$Flag)
    $current = $env:QTWEBENGINE_CHROMIUM_FLAGS
    if ([string]::IsNullOrWhiteSpace($current)) { $env:QTWEBENGINE_CHROMIUM_FLAGS = $Flag; return }
    $tokens = @($current -split '\s+' | Where-Object { $_ })
    if ($tokens -notcontains $Flag) { $env:QTWEBENGINE_CHROMIUM_FLAGS = "$current $Flag" }
}

function Set-IniValue {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Section,
        [Parameter(Mandatory = $true)][string]$Key,
        [Parameter(Mandatory = $true)][string]$Value
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($Path))
        [System.IO.File]::WriteAllText($Path, '', [System.Text.UTF8Encoding]::new($false))
    }
    $encoding = [System.Text.UTF8Encoding]::new($false)
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.AddRange([System.IO.File]::ReadAllLines($Path, [System.Text.Encoding]::UTF8))
    $sectionIndex = -1
    for ($index = 0; $index -lt $lines.Count; $index++) {
        if ($lines[$index] -match "^\s*\[$([regex]::Escape($Section))\]\s*$") { $sectionIndex = $index; break }
    }
    if ($sectionIndex -lt 0) {
        if ($lines.Count -gt 0 -and $lines[$lines.Count - 1].Length -ne 0) { $lines.Add('') }
        $lines.Add("[$Section]"); $lines.Add("$Key=$Value")
        [System.IO.File]::WriteAllLines($Path, $lines, $encoding); return
    }
    $insertIndex = $lines.Count
    for ($index = $sectionIndex + 1; $index -lt $lines.Count; $index++) {
        if ($lines[$index] -match '^\s*\[.+\]\s*$') { $insertIndex = $index; break }
        if ($lines[$index] -match "^\s*$([regex]::Escape($Key))\s*=") {
            $lines[$index] = "$Key=$Value"
            [System.IO.File]::WriteAllLines($Path, $lines, $encoding); return
        }
    }
    $lines.Insert($insertIndex, "$Key=$Value")
    [System.IO.File]::WriteAllLines($Path, $lines, $encoding)
}

if (-not (Test-Path -LiteralPath $BillfishExe -PathType Leaf)) { throw "Billfish.core.exe not found: $BillfishExe" }

$libraryResolution = Resolve-BillfishLibraryRoot -LibraryRoot $LibraryRoot -AppDataRoot $AppDataRoot
$resolvedLibraryRoot = [string]$libraryResolution.LibraryRoot
if ($libraryResolution.Source -eq 'RecentLibraryConfig' -and -not $libraryResolution.RootAvailable) { $resolvedLibraryRoot = '' }

$mutex = Enter-BillfishLaunchMutex -BillfishRoot $BillfishRoot
try {
    # The full executable path is intentionally checked only after acquiring the
    # user-session mutex. This covers the complete health-check/start transaction.
    $running = @(Get-BillfishProcessByExecutablePath -ExecutablePath $BillfishExe -FixtureProcessPaths $TestProcessPaths)
    if ($running.Count -gt 0) {
        $plan = [pscustomobject]@{ Outcome = 'AlreadyRunning'; Executable = $BillfishExe; ProcessIds = @($running.Id); PluginPackage = $pluginRequest.SourcePath }
    }
    else {
        $health = if($TestSkipPersistentPatchHealth){[pscustomobject]@{Healthy=$true;LibraryRoot=$resolvedLibraryRoot;LibraryDatabase=(Get-BillfishLibraryDatabaseStatus -BillfishRoot $BillfishRoot -LibraryRoot $resolvedLibraryRoot -SkipCheck);Patches=@()}}else{Get-BillfishPersistentPatchHealth -BillfishRoot $BillfishRoot -LibraryRoot $resolvedLibraryRoot -AppDataRoot $AppDataRoot -SkipLibraryDatabaseCheck:$TestSkipLibraryDatabaseCheck}
        if (-not $health.Healthy) {
            $details = @($health.Patches | Where-Object { -not $_.Healthy } | ForEach-Object {
                "[$($_.Name)] " + ($_.Errors -join '; ')
            }) -join [Environment]::NewLine
            throw "Persistent patch health check failed. Billfish was not started.`n$details"
        }

        $env:BILLFISH_LIBRARY_ROOT = $resolvedLibraryRoot
        $env:BILLFISH_INSTALL_ROOT = $BillfishRoot
        if (-not $StandardRender) {
            $env:QT_OPENGL = 'software'
            Add-ChromiumFlag '--disable-gpu'
            Add-ChromiumFlag '--disable-gpu-compositing'
        }

        if (-not $DryRun -and -not $SkipUiPreferences -and $LeftFolderHeight -gt 0) {
            Set-IniValue -Path $UserConfig -Section 'ShowConfig' -Key 'LeftFolderHeight' -Value ([string]$LeftFolderHeight)
        }
        if (-not $DryRun) {
            foreach ($key in @('EnableHardwareAcceleration', 'EnableHardwareAccelerationV2', 'EnableHardwareRender')) {
                Set-IniValue -Path $UserConfig -Section 'ShowConfig' -Key $key -Value '0'
            }
        }

        $plan = [pscustomobject]@{
            Outcome = if ($DryRun) { 'WouldStart' } else { 'Started' }
            Executable = $BillfishExe
            WorkingDirectory = $BillfishRoot
            LibraryRoot = $resolvedLibraryRoot
            LibraryResolution = $libraryResolution
            LibraryDatabase = $health.LibraryDatabase
            Arguments = $PassThroughArgs
            PluginPackage = $pluginRequest.SourcePath
            PatchHealth = @($health.Patches | Select-Object Name, Healthy)
        }
        if ($DryRun) { $plan; exit 0 }

        $startParameters = @{ FilePath = $BillfishExe; WorkingDirectory = $BillfishRoot }
        if ($PassThroughArgs.Count -gt 0) { $startParameters.ArgumentList = (ConvertTo-WindowsCommandLine -ArgumentList $PassThroughArgs) }
        Start-Process @startParameters
    }
}
finally {
    Exit-BillfishLaunchMutex -Mutex $mutex
}

if (-not $DryRun -and $pluginRequest.SourcePath) {
    Invoke-BillfishPluginIngress -BillfishRoot $BillfishRoot -SourcePath $pluginRequest.SourcePath
}
$plan
