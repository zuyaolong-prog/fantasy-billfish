[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Configuration = 'Release',
    [string]$DotNetPath = 'dotnet',
    [string]$StagingDirectory,
    [string]$TestFixtureRoot
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$PatchDir = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($StagingDirectory)) { $StagingDirectory = Join-Path $PatchDir 'staging\root-wrapper' }
$BillfishRoot = Split-Path -Parent $PatchDir
$Project = Join-Path $PatchDir 'BillfishPatchedLauncher\BillfishPatchedLauncher.csproj'
$normalizedStaging=[IO.Path]::GetFullPath($StagingDirectory).TrimEnd('\','/');$allowedStagingRoot=[IO.Path]::GetFullPath((Join-Path $PatchDir 'staging')).TrimEnd('\','/');$allowed=$normalizedStaging.Equals($allowedStagingRoot,[StringComparison]::OrdinalIgnoreCase)-or$normalizedStaging.StartsWith($allowedStagingRoot+'\',[StringComparison]::OrdinalIgnoreCase)
if($TestFixtureRoot){$fixture=[IO.Path]::GetFullPath($TestFixtureRoot).TrimEnd('\','/');$temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/');if(-not($fixture.StartsWith($temp+'\',[StringComparison]::OrdinalIgnoreCase))){throw 'TestFixtureRoot must be a child of the system temp directory.'};$allowed=$normalizedStaging.Equals($fixture,[StringComparison]::OrdinalIgnoreCase)-or$normalizedStaging.StartsWith($fixture+'\',[StringComparison]::OrdinalIgnoreCase)}
if(-not$allowed){throw "StagingDirectory is outside the approved staging boundary: $normalizedStaging"}
$StagingDirectory=$normalizedStaging;$WrapperExe = Join-Path $StagingDirectory 'Billfish.exe'
$transactionId = [guid]::NewGuid().ToString('N')
$TempRoot = Join-Path $PatchDir ('.publish-' + $transactionId)
$OutputDir = Join-Path $TempRoot 'publish'
$ArtifactsDir = Join-Path $TempRoot 'artifacts'

if (-not ('Billfish.Launcher.BuildNativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
namespace Billfish.Launcher {
    public static class BuildNativeMethods {
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool MoveFileEx(string existingFile, string newFile, int flags);
    }
}
'@
}

foreach ($required in @($Project, (Join-Path $BillfishRoot 'start-billfish.ps1'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required launcher input not found: $required" }
}

try {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    & $DotNetPath publish $Project `
        --configuration $Configuration `
        --runtime win-x64 `
        --self-contained true `
        --artifacts-path $ArtifactsDir `
        -p:PublishSingleFile=true `
        -p:DebugType=none `
        -p:DebugSymbols=false `
        --output $OutputDir
    $publishExitCode = $LASTEXITCODE
    if ($publishExitCode -ne 0) { throw "dotnet publish failed with exit code $publishExitCode." }

    $PublishedExe = Join-Path $OutputDir 'Billfish.exe'
    if (-not (Test-Path -LiteralPath $PublishedExe -PathType Leaf)) { throw "Published wrapper not found: $PublishedExe" }
    if ((Get-Item -LiteralPath $PublishedExe).Length -le 0) { throw 'Published wrapper is empty.' }
    if (-not (Test-Path -LiteralPath $StagingDirectory -PathType Container)) { New-Item -ItemType Directory -Path $StagingDirectory -Force | Out-Null }

    if (Test-Path -LiteralPath $WrapperExe -PathType Leaf) {
        if (-not [Billfish.Launcher.BuildNativeMethods]::MoveFileEx($PublishedExe, $WrapperExe, 0x9)) {
            $moveError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
            throw "Atomic wrapper replacement failed with Win32 error $moveError."
        }
    }
    else {
        Move-Item -LiteralPath $PublishedExe -Destination $WrapperExe
    }
    Get-Item -LiteralPath $WrapperExe | Select-Object FullName, Length, LastWriteTime
}
finally {
    $tempFull = [System.IO.Path]::GetFullPath($TempRoot)
    $patchFull = [System.IO.Path]::GetFullPath($PatchDir).TrimEnd('\') + '\'
    if ($tempFull.StartsWith($patchFull, [System.StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $tempFull)) {
        Remove-Item -LiteralPath $tempFull -Recurse -Force
    }
}
