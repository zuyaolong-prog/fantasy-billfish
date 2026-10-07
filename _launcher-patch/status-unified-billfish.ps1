[CmdletBinding()]
param(
    [string]$BillfishRoot,
    [AllowEmptyString()][string]$LibraryRoot,
    [string]$AppDataRoot = $env:APPDATA,
    [string]$FixtureRoot,
    [switch]$SkipLibraryDatabaseCheck,
    [switch]$CheckInstallationHistory,
    [switch]$TestSkipWrapperMetadataCheck,
    [switch]$TestSkipPersistentPatchHealth,
    [switch]$AsJson
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($BillfishRoot)) { $BillfishRoot = Split-Path -Parent $PSScriptRoot }
. (Join-Path $PSScriptRoot 'RootTopology.Transactions.ps1')
. (Join-Path $PSScriptRoot 'Launcher.Core.ps1')

$root = [System.IO.Path]::GetFullPath($BillfishRoot)
$libraryResolution = Resolve-BillfishLibraryRoot -LibraryRoot $LibraryRoot -AppDataRoot $AppDataRoot
$resolvedLibraryRoot = [string]$libraryResolution.LibraryRoot
if ($libraryResolution.Source -eq 'RecentLibraryConfig' -and -not $libraryResolution.RootAvailable) { $resolvedLibraryRoot = '' }
$launcher = Join-Path $root 'Billfish.exe'
$core = Join-Path $root 'Billfish.core.exe'
$legacyWrapper = Join-Path $root 'launcher\Billfish.exe'
$versionInfo = if (Test-Path -LiteralPath $launcher -PathType Leaf) { (Get-Item -LiteralPath $launcher).VersionInfo } else { $null }
$productName = if ($null -ne $versionInfo) { [string]$versionInfo.ProductName } else { '' }
$fileDescription = if ($null -ne $versionInfo) { [string]$versionInfo.FileDescription } else { '' }
$originalFilename = if ($null -ne $versionInfo) { [string]$versionInfo.OriginalFilename } else { '' }
$metadataHealthy=$TestSkipWrapperMetadataCheck-or($productName -eq 'Billfish' -and $fileDescription -eq 'Billfish' -and $originalFilename -eq 'Billfish.dll')
$stateRoot=if($FixtureRoot){[IO.Path]::GetFullPath($FixtureRoot)}else{$PSScriptRoot};$topologyPointer=Join-Path $stateRoot 'latest-root-topology-backup.txt';$topologyBackup=$null;$topologyManifest=$null;$topologyError=$null;$topologyHealthy=$false
if ($CheckInstallationHistory) { try{$topologyBackup=Get-BillfishActiveTopologyBackup $topologyPointer;if(-not$topologyBackup){throw 'Active v4 topology pointer is missing.'};[void](Assert-BillfishTransactionStoragePaths $topologyPointer $topologyBackup);$context=Get-BillfishEntryPointContext $FixtureRoot;$topologyManifest=Assert-BillfishTopologyManifest (Read-BillfishTopologyManifest $topologyBackup) $topologyBackup (Get-BillfishRootTopologyPaths $root) $context @('Active');$topologyHealthy=Test-BillfishInstalledV4Topology $topologyManifest (Get-BillfishRootTopologyPaths $root) $context}catch{$topologyError=$_.Exception.Message;$topologyHealthy=$false} } else { $topologyHealthy = (Test-Path -LiteralPath $launcher -PathType Leaf) -and (Test-Path -LiteralPath $core -PathType Leaf) -and -not (Test-Path -LiteralPath $legacyWrapper) }
$launcherHealthy = $metadataHealthy -and $topologyHealthy
$validatedEntries=if($topologyManifest){@($topologyManifest.v3Installed.shortcuts)}else{$null};$entryPoints = if ($CheckInstallationHistory) { Get-BillfishEntryPointStatus -BillfishRoot $root -FixtureRoot $FixtureRoot -ValidatedEntries $validatedEntries -ExpectedLauncherPath $launcher -ExpectedIconPath $core } else { [pscustomobject]@{ Healthy = $true; Checked = $false; Diagnostic = 'System shortcuts and autostart are optional machine-local installation state.' } }
$patches = if($TestSkipPersistentPatchHealth){[pscustomobject]@{Healthy=$true;LibraryRoot=$resolvedLibraryRoot;LibraryDatabase=(Get-BillfishLibraryDatabaseStatus -BillfishRoot $root -LibraryRoot $resolvedLibraryRoot -SkipCheck);Patches=@()}}else{Get-BillfishPersistentPatchHealth -BillfishRoot $root -LibraryRoot $resolvedLibraryRoot -AppDataRoot $AppDataRoot -SkipLibraryDatabaseCheck:$SkipLibraryDatabaseCheck}
$status = [pscustomobject]@{
    Mode = if ($CheckInstallationHistory) { 'InstallationHistory' } else { 'PortableRuntime' }
    Healthy = $launcherHealthy -and $entryPoints.Healthy -and $patches.Healthy
    LibraryRoot = $resolvedLibraryRoot
    LibraryResolution = $libraryResolution
    LibraryDatabase = $patches.LibraryDatabase
    Launcher = [pscustomobject]@{ Path = $launcher; Exists = Test-Path -LiteralPath $launcher -PathType Leaf; CorePath=$core;CoreExists=Test-Path -LiteralPath $core -PathType Leaf;LegacyWrapperPath=$legacyWrapper;LegacyWrapperExists=Test-Path -LiteralPath $legacyWrapper -PathType Leaf;ProductName = $productName; FileDescription = $fileDescription; OriginalFilename = $originalFilename;TopologyPointer=$topologyPointer;TopologyBackup=$topologyBackup;TopologyState=if($topologyManifest){[string]$topologyManifest.state}else{$null};TopologyError=$topologyError;Healthy = $launcherHealthy }
    EntryPoints = $entryPoints
    Patches = $patches.Patches
}

if ($AsJson) { $status | ConvertTo-Json -Depth 10 } else { $status }
if (-not $status.Healthy) { exit 2 }
