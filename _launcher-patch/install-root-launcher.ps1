[CmdletBinding(PositionalBinding=$false)]param([string]$BillfishRoot,[string]$WrapperStagingPath,[string]$FixtureRoot,[switch]$DryRun,[switch]$RecoverOnly)
if ([string]::IsNullOrWhiteSpace($BillfishRoot)) { $BillfishRoot = Split-Path -Parent $PSScriptRoot }
if ([string]::IsNullOrWhiteSpace($WrapperStagingPath)) { $WrapperStagingPath = Join-Path $PSScriptRoot 'staging\root-wrapper\Billfish.exe' }
Set-StrictMode -Version 3.0;$ErrorActionPreference='Stop';. (Join-Path $PSScriptRoot 'RootTopology.Transactions.ps1')
$stateRoot=if($FixtureRoot){[IO.Path]::GetFullPath($FixtureRoot)}else{$PSScriptRoot};$v3Pointer=Join-Path $stateRoot 'latest-user-shortcut-backup.txt';$v4Pointer=Join-Path $stateRoot 'latest-root-topology-backup.txt';$backup=Join-Path $stateRoot ('backups\topology-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
Install-BillfishRootTopology -BillfishRoot $BillfishRoot -WrapperStagingPath $WrapperStagingPath -BackupDir $backup -V3StatePointerPath $v3Pointer -V4StatePointerPath $v4Pointer -FixtureRoot $FixtureRoot -DryRun:$DryRun -RecoverOnly:$RecoverOnly
