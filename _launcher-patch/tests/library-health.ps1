[CmdletBinding(PositionalBinding = $false)]
param()

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
$patchRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $patchRoot
. (Join-Path $patchRoot 'Launcher.Core.ps1')

function Assert-LibraryTest([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Remove-LibraryTestPath([string]$Path) {
    if ([System.IO.Directory]::Exists($Path)) {
        [System.IO.Directory]::Delete($Path, $true)
    }
    elseif ([System.IO.File]::Exists($Path)) {
        [System.IO.File]::Delete($Path)
    }
}

$fixture = [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(), 'billfish-library-health-' + [guid]::NewGuid().ToString('N'))
$appDataRoot = [System.IO.Path]::Combine($fixture, 'AppData')
$configRoot = [System.IO.Path]::Combine($appDataRoot, 'Billfish', 'config')
$missingRoot = [System.IO.Path]::Combine($fixture, 'missing-library')
$invalidRoot = [System.IO.Path]::Combine($fixture, 'invalid-library')
$recentConfig = [System.IO.Path]::Combine($configRoot, 'recent_lib.ini')
$userConfig = [System.IO.Path]::Combine($configRoot, 'user_config.ini')

try {
    [System.IO.Directory]::CreateDirectory($configRoot) | Out-Null
    [System.IO.Directory]::CreateDirectory([System.IO.Path]::Combine($invalidRoot, '.bf')) | Out-Null
    [System.IO.File]::WriteAllText(
        $recentConfig,
        "[LoadDefaultLibType]`r`nType=0`r`nLastLoadLibPath=X:\\Fixture\\示例资料库\\`r`nCustomSettingLibPath=H:\\Fixture\\备用资料库\\`r`n`r`n[ResourceLibsVector]`r`n1\\Path=X:/Fixture/示例资料库`r`n",
        [System.Text.UTF8Encoding]::new($false)
    )
    [System.IO.File]::WriteAllText(
        $userConfig,
        "[ShowConfig]`r`nEnableHardwareAcceleration=0`r`nEnableHardwareAccelerationV2=0`r`nEnableHardwareRender=0`r`n",
        [System.Text.UTF8Encoding]::new($false)
    )
    [System.IO.File]::WriteAllText([System.IO.Path]::Combine($invalidRoot, '.bf', 'billfish.db'), 'not-a-sqlite-database', [System.Text.UTF8Encoding]::new($false))

    $resolved = Resolve-BillfishLibraryRoot -AppDataRoot $appDataRoot
    Assert-LibraryTest ($resolved.Source -eq 'RecentLibraryConfig') 'Default library resolver did not use recent_lib.ini.'
    Assert-LibraryTest ($resolved.LibraryRoot -eq 'X:\Fixture\示例资料库\') "Qt path decoding failed: $($resolved.LibraryRoot)"
    Assert-LibraryTest ((Resolve-BillfishLibraryRoot -LibraryRoot '\\server\share\library' -AppDataRoot $appDataRoot).LibraryRoot -eq '\\server\share\library') 'Explicit UNC library root was rewritten.'

    $dryRun = & (Join-Path $repositoryRoot 'start-billfish.ps1') -DryRun -SkipUiPreferences -TestFixtureRoot $repositoryRoot -TestAppDataRoot $appDataRoot -LibraryRoot $missingRoot -TestProcessPaths 'C:\Fixture\OtherBillfish.exe'
    Assert-LibraryTest ($LASTEXITCODE -eq 0 -and $dryRun.Outcome -eq 'WouldStart') 'Missing library database blocked a dry-run start.'
    Assert-LibraryTest (-not $dryRun.LibraryDatabase.Available -and -not $dryRun.LibraryDatabase.Checked -and $dryRun.LibraryDatabase.Healthy) 'Missing library database diagnostic was not nonblocking.'
    Assert-LibraryTest ($dryRun.LibraryDatabase.Name -eq 'LibraryDatabase' -and $dryRun.PatchHealth.Where({ $_.Name -eq 'HDR' }).Healthy) 'Missing library database leaked into HDR health.'

    $existingDb = Get-BillfishLibraryDatabaseStatus -BillfishRoot $repositoryRoot -LibraryRoot $invalidRoot
    Assert-LibraryTest ($existingDb.Available -and $existingDb.Checked -and -not $existingDb.Healthy) 'An existing database LUT/query failure was not blocking.'

    [pscustomobject]@{ Outcome = 'Passed'; DefaultConfigResolution = $true; MissingDatabaseNonBlocking = $true; ExistingDatabaseBlocking = $true }
}
finally {
    if ([System.IO.Directory]::Exists($fixture)) { Remove-LibraryTestPath $fixture }
}
