[CmdletBinding()]
param(
    [switch]$Quiet,
    [switch]$SkipValidation,
    [string[]]$ValidationSamples
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$PatchDir = $PSScriptRoot
$BillfishRoot = Split-Path -Parent $PatchDir
$MediaDir = Join-Path $BillfishRoot 'plugins\media_runtime'
$BackupsDir = Join-Path $PatchDir 'backups'
$ShimDll = Join-Path $PatchDir 'build\libFFmpeg64.dll'
$Dav1dDll = Join-Path $PatchDir 'vendor\dav1d.dll'
$TargetDll = Join-Path $MediaDir 'libFFmpeg64.dll'
$OrigDll = Join-Path $MediaDir 'libFFmpeg64_orig.dll'
$TargetDav1dDll = Join-Path $MediaDir 'dav1d.dll'
$FfmpegExe = Join-Path $MediaDir 'ffmpeg64.exe'

function Write-Step([string]$Message) {
    if (-not $Quiet) {
        Write-Host $Message
    }
}

function Get-Sha256([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
    }
    return ''
}

function Assert-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file not found: $Path"
    }
}

function Assert-BillfishStopped() {
    $running = Get-Process -Name Billfish,Billfish.core,PluginWorker,PluginWorker64 -ErrorAction SilentlyContinue
    if ($running) {
        $names = ($running | ForEach-Object { "$($_.ProcessName)#$($_.Id)" }) -join ', '
        throw "Close Billfish before installing the AV1 internal-player patch. Running: $names"
    }
}

function Copy-IfExists([string]$Source, [string]$DestinationDir) {
    if (Test-Path -LiteralPath $Source -PathType Leaf) {
        Copy-Item -Force -LiteralPath $Source -Destination (Join-Path $DestinationDir (Split-Path -Leaf $Source))
    }
}

function Restore-Backup([string]$BackupDir) {
    $backupMain = Join-Path $BackupDir 'libFFmpeg64.dll'
    if (Test-Path -LiteralPath $backupMain -PathType Leaf) {
        Copy-Item -Force -LiteralPath $backupMain -Destination $TargetDll
    }

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
}

function Test-Decode([string]$Name, [string]$Path) {
    Assert-File $Path

    $output = & $FfmpegExe -hide_banner -loglevel error -i $Path -frames:v 1 -f null - 2>&1
    $decodeExitCode = $LASTEXITCODE
    if ($decodeExitCode -ne 0) {
        $text = ($output | Select-Object -First 20) -join [Environment]::NewLine
        throw "Decode validation failed for $Name. Exit=$decodeExitCode`n$text"
    }
    Write-Step "PASS $Name"
}

Assert-File $ShimDll
Assert-File $Dav1dDll
Assert-File $TargetDll
Assert-File $FfmpegExe
Assert-BillfishStopped

New-Item -ItemType Directory -Force -Path $BackupsDir | Out-Null

$shimHash = Get-Sha256 $ShimDll
$currentHash = Get-Sha256 $TargetDll
$alreadyInstalled = $currentHash -eq $shimHash
$backupDir = $null

try {
    if ($alreadyInstalled) {
        Write-Step "AV1 internal-player patch is already installed."
        if (-not (Test-Path -LiteralPath $OrigDll -PathType Leaf)) {
            throw "Patch DLL is installed, but libFFmpeg64_orig.dll is missing: $OrigDll"
        }
    } else {
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $backupDir = Join-Path $BackupsDir $stamp
        New-Item -ItemType Directory -Force -Path $backupDir | Out-Null

        Copy-IfExists $TargetDll $backupDir
        Copy-IfExists $OrigDll $backupDir
        Copy-IfExists $TargetDav1dDll $backupDir

        @(
            "InstalledAt=$((Get-Date).ToString('o'))",
            "BillfishRoot=$BillfishRoot",
            "PreviousLibFFmpeg64Sha256=$currentHash",
            "ShimSha256=$shimHash",
            "Dav1dSha256=$(Get-Sha256 $Dav1dDll)"
        ) | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $backupDir 'manifest.txt')

        if (Test-Path -LiteralPath $OrigDll -PathType Leaf) {
            Write-Step "Preserved existing original FFmpeg backup: $OrigDll"
        } else {
            Copy-Item -Force -LiteralPath $TargetDll -Destination $OrigDll
        }
        Copy-Item -Force -LiteralPath $ShimDll -Destination $TargetDll
        Copy-Item -Force -LiteralPath $Dav1dDll -Destination $TargetDav1dDll
        Set-Content -Encoding UTF8 -LiteralPath (Join-Path $BackupsDir 'latest.txt') -Value (Split-Path -Leaf $backupDir)
        Write-Step "Installed AV1 internal-player patch."
    }

    if ((Get-Sha256 $TargetDav1dDll) -ne (Get-Sha256 $Dav1dDll)) {
        Copy-Item -Force -LiteralPath $Dav1dDll -Destination $TargetDav1dDll
    }

    if (-not $SkipValidation) {
        if (-not $ValidationSamples) { Write-Step 'No decode samples supplied. Use -ValidationSamples to validate media decoding.' }
        foreach ($sample in $ValidationSamples) { Test-Decode (Split-Path -Leaf $sample) $sample }
    }
} catch {
    if ($backupDir) {
        Restore-Backup $backupDir
        Write-Step "Install failed; restored backup: $backupDir"
    }
    throw
}

Write-Step "Done."
