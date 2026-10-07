[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)][string]$RepositoryRoot,
    [Parameter(Mandatory = $true)][string]$FixtureRoot,
    [Parameter(Mandatory = $true)][string]$BackupDir,
    [Parameter(Mandatory = $true)][string]$Pointer,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [int]$HoldMilliseconds = 0
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'EntryPoints.Core.ps1')
try {
    $result = Install-BillfishEntryPoints -BillfishRoot $RepositoryRoot -BackupDir $BackupDir -FixtureRoot $FixtureRoot -StatePointerPath $Pointer -TestHoldMutexMilliseconds $HoldMilliseconds
    [IO.File]::WriteAllText($OutputPath, (($result | ConvertTo-Json -Depth 5) + [Environment]::NewLine), (New-Object Text.UTF8Encoding($false)))
    exit 0
}
catch {
    [IO.File]::WriteAllText($OutputPath, (([pscustomobject]@{ Outcome = 'Failed'; Error = $_.Exception.Message } | ConvertTo-Json) + [Environment]::NewLine), (New-Object Text.UTF8Encoding($false)))
    exit 1
}
