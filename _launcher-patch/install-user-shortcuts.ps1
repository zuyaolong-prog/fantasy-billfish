[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$BillfishRoot,
    [string]$FixtureRoot,
    [switch]$DryRun
)

if ([string]::IsNullOrWhiteSpace($BillfishRoot)) { $BillfishRoot = Split-Path -Parent $PSScriptRoot }
Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EntryPoints.Core.ps1')

$backupDir = Join-Path $PSScriptRoot ('backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$pointer = if ($FixtureRoot) { Join-Path $FixtureRoot 'latest-backup.txt' } else { Join-Path $PSScriptRoot 'latest-user-shortcut-backup.txt' }
$result = Install-BillfishEntryPoints -BillfishRoot $BillfishRoot -BackupDir $backupDir -FixtureRoot $FixtureRoot -StatePointerPath $pointer -DryRun:$DryRun
$result
