[CmdletBinding()]
param(
    [string]$BackupDir
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$PatchDir = $PSScriptRoot
$BillfishRoot = Split-Path -Parent $PatchDir
$MediaDir = Join-Path $BillfishRoot 'plugins\media_runtime'
$BackupsDir = Join-Path $PatchDir 'backups'
$Dav1dDll = Join-Path $PatchDir 'vendor\dav1d.dll'
$TargetDll = Join-Path $MediaDir 'libFFmpeg64.dll'
$OrigDll = Join-Path $MediaDir 'libFFmpeg64_orig.dll'
$TargetDav1dDll = Join-Path $MediaDir 'dav1d.dll'

function Get-Sha256([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
    }
    return ''
}

$running = Get-Process -Name Billfish,Billfish.core,PluginWorker,PluginWorker64 -ErrorAction SilentlyContinue
if ($running) {
    $names = ($running | ForEach-Object { "$($_.ProcessName)#$($_.Id)" }) -join ', '
    throw "Close Billfish before restoring the AV1 internal-player patch. Running: $names"
}

if (-not $BackupDir) {
    $latestFile = Join-Path $BackupsDir 'latest.txt'
    if (Test-Path -LiteralPath $latestFile -PathType Leaf) {
        $BackupDir = Join-Path $BackupsDir (Split-Path -Leaf (Get-Content -LiteralPath $latestFile -TotalCount 1).Trim())
    } else {
        $BackupDir = Get-ChildItem -Path $BackupsDir -Directory |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1 -ExpandProperty FullName
    }
}

if (-not $BackupDir -or -not (Test-Path -LiteralPath $BackupDir -PathType Container)) {
    throw "Backup directory not found."
}

$backupMain = Join-Path $BackupDir 'libFFmpeg64.dll'
if (-not (Test-Path -LiteralPath $backupMain -PathType Leaf)) {
    throw "Backup libFFmpeg64.dll not found: $backupMain"
}

Copy-Item -Force -LiteralPath $backupMain -Destination $TargetDll

$backupOrig = Join-Path $BackupDir 'libFFmpeg64_orig.dll'
if (Test-Path -LiteralPath $backupOrig -PathType Leaf) {
    Copy-Item -Force -LiteralPath $backupOrig -Destination $OrigDll
} elseif (Test-Path -LiteralPath $OrigDll -PathType Leaf) {
    Remove-Item -Force -LiteralPath $OrigDll
}

$backupDav1d = Join-Path $BackupDir 'dav1d.dll'
if (Test-Path -LiteralPath $backupDav1d -PathType Leaf) {
    Copy-Item -Force -LiteralPath $backupDav1d -Destination $TargetDav1dDll
} elseif ((Get-Sha256 $TargetDav1dDll) -eq (Get-Sha256 $Dav1dDll)) {
    Remove-Item -Force -LiteralPath $TargetDav1dDll
}

Write-Host "Restored AV1 internal-player patch backup: $BackupDir"
