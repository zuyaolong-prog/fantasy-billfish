[CmdletBinding(PositionalBinding = $false)]
param([string]$BackupDir, [string]$BillfishRoot, [string]$FixtureRoot, [switch]$DryRun)

if ([string]::IsNullOrWhiteSpace($BillfishRoot)) { $BillfishRoot = Split-Path -Parent $PSScriptRoot }
Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EntryPoints.Core.ps1')

$pointer = if ($FixtureRoot) { Join-Path $FixtureRoot 'latest-backup.txt' } else { Join-Path $PSScriptRoot 'latest-user-shortcut-backup.txt' }
[void](Install-BillfishEntryPoints -BillfishRoot $BillfishRoot -BackupDir (Join-Path (Split-Path -Parent $pointer) '.recovery-placeholder') -FixtureRoot $FixtureRoot -StatePointerPath $pointer -RecoverOnly)
if ([string]::IsNullOrWhiteSpace($BackupDir)) {
    if (Test-Path -LiteralPath $pointer -PathType Leaf) { $BackupDir = (Get-Content -LiteralPath $pointer -TotalCount 1).Trim() }
}
if (-not $BackupDir -or -not (Test-Path -LiteralPath $BackupDir -PathType Container)) { throw 'Backup directory not found.' }
Restore-BillfishEntryPoints -BackupDir $BackupDir -BillfishRoot $BillfishRoot -FixtureRoot $FixtureRoot -StatePointerPath $pointer -DryRun:$DryRun
