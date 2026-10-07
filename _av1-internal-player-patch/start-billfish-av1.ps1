[CmdletBinding(PositionalBinding = $false)]
param(
    [AllowEmptyString()][string]$LibraryRoot,
    [switch]$DryRun,
    [string]$TestFixtureRoot,
    [switch]$TestSkipPersistentPatchHealth,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$BillfishArgs
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$BillfishRoot = Split-Path -Parent $PSScriptRoot
$UnifiedLauncher = Join-Path $BillfishRoot 'start-billfish.ps1'
if (-not (Test-Path -LiteralPath $UnifiedLauncher -PathType Leaf)) {
    throw "Unified Billfish launcher not found: $UnifiedLauncher"
}

& $UnifiedLauncher -LibraryRoot $LibraryRoot -DryRun:$DryRun -TestFixtureRoot $TestFixtureRoot -TestSkipPersistentPatchHealth:$TestSkipPersistentPatchHealth @BillfishArgs
exit 0
