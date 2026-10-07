[CmdletBinding(PositionalBinding = $false)]
param()

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
$patchRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $patchRoot
. (Join-Path $patchRoot 'EntryPoints.Core.ps1')

function New-TestShortcut([string]$Target, [string]$Description = 'Old') {
    [ordered]@{ TargetPath = $Target; Arguments = '--old'; WorkingDirectory = (Split-Path -Parent $Target); IconLocation = "$Target,0"; Description = $Description; WindowStyle = 1 }
}

function Assert-True([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }

$fixture = Join-Path ([System.IO.Path]::GetTempPath()) ('billfish-launcher-' + [guid]::NewGuid().ToString('N'))
try {
    $repositoryRoot = Join-Path $fixture 'runtime'
    [void][IO.Directory]::CreateDirectory((Join-Path $repositoryRoot 'launcher'))
    [IO.File]::WriteAllText((Join-Path $repositoryRoot 'Billfish.exe'), 'fixture vendor')
    [IO.File]::WriteAllText((Join-Path $repositoryRoot 'launcher/Billfish.exe'), 'fixture v3 wrapper')
    New-Item -ItemType Directory -Path (Join-Path $fixture 'UserDesktop'), (Join-Path $fixture 'CommonDesktop') -Force | Out-Null
    $userLink = Join-Path $fixture 'UserDesktop\Billfish.lnk'
    $publicLink = Join-Path $fixture 'CommonDesktop\Billfish.lnk'
    Write-BillfishShortcut -Path $userLink -Value (New-TestShortcut 'C:\Old\Billfish.exe' 'Old user') -Fixture $true
    Write-BillfishShortcut -Path $publicLink -Value (New-TestShortcut 'C:\Old\Billfish.exe' 'Old public') -Fixture $true
    $context = Get-BillfishEntryPointContext -FixtureRoot $fixture
    Set-BillfishRunValue -Context $context -Value '"C:\Old\Billfish.exe" -a=auto'

    $pointer = Join-Path $fixture 'latest.txt'
    $backup = Join-Path $fixture 'backup-first'
    $installed = Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir $backup -FixtureRoot $fixture -StatePointerPath $pointer
    Assert-True ($installed.ShortcutCount -ge 3) 'Fixture did not cover existing and newly-created shortcuts.'
    $firstPointer = (Get-Content -LiteralPath $pointer -TotalCount 1).Trim()
    $firstManifestHash = (Get-FileHash -LiteralPath (Join-Path $backup 'manifest.json') -Algorithm SHA256).Hash

    $repeated = Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir (Join-Path $fixture 'backup-second') -FixtureRoot $fixture -StatePointerPath $pointer
    Assert-True ($repeated.Outcome -eq 'AlreadyInstalled') 'Repeated install was not idempotent.'
    Assert-True ((Get-Content -LiteralPath $pointer -TotalCount 1).Trim() -eq $firstPointer) 'Repeated install replaced the origin pointer.'
    Assert-True ((Get-FileHash -LiteralPath (Join-Path $backup 'manifest.json') -Algorithm SHA256).Hash -eq $firstManifestHash) 'Repeated install changed the origin manifest.'

    $status = Get-BillfishEntryPointStatus -BillfishRoot $repositoryRoot -FixtureRoot $fixture
    Assert-True $status.Healthy 'Installed fixture entry points are unhealthy.'
    $statusProbe = Read-BillfishShortcut -Path $userLink -Fixture $true
    $statusProbe.Arguments = '--unexpected'
    Write-BillfishShortcut -Path $userLink -Value $statusProbe -Fixture $true
    Assert-True (-not (Get-BillfishEntryPointStatus -BillfishRoot $repositoryRoot -FixtureRoot $fixture).Healthy) 'Status ignored shortcut arguments.'
    $statusProbe.Arguments = ''
    $statusProbe.WorkingDirectory = 'C:\Unexpected'
    Write-BillfishShortcut -Path $userLink -Value $statusProbe -Fixture $true
    Assert-True (-not (Get-BillfishEntryPointStatus -BillfishRoot $repositoryRoot -FixtureRoot $fixture).Healthy) 'Status ignored shortcut working directory.'
    $statusProbe.WorkingDirectory = $repositoryRoot
    Write-BillfishShortcut -Path $userLink -Value $statusProbe -Fixture $true

    $restoreFailed = $false
    try { Restore-BillfishEntryPoints -BackupDir $backup -BillfishRoot $repositoryRoot -FixtureRoot $fixture -StatePointerPath $pointer -FailStage Run | Out-Null }
    catch { $restoreFailed = $true }
    Assert-True $restoreFailed 'Injected restore failure did not fail.'
    Assert-True (Get-BillfishEntryPointStatus -BillfishRoot $repositoryRoot -FixtureRoot $fixture).Healthy 'Failed restore did not roll back to installed state.'
    Assert-True (Test-Path -LiteralPath $pointer -PathType Leaf) 'Failed restore lost the active pointer.'

    Restore-BillfishEntryPoints -BackupDir $backup -BillfishRoot $repositoryRoot -FixtureRoot $fixture -StatePointerPath $pointer | Out-Null
    $restoredRun = Get-BillfishRunValue -Context $context
    Assert-True ($restoredRun.Value -eq '"C:\Old\Billfish.exe" -a=auto') 'Run value was not restored.'
    Assert-True ((Read-BillfishShortcut -Path $userLink -Fixture $true).Description -eq 'Old user') 'User shortcut was not restored.'
    Assert-True ((Read-BillfishShortcut -Path $publicLink -Fixture $true).Description -eq 'Old public') 'Public shortcut was not restored.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $fixture 'UserStartMenu\Programs\Billfish\Billfish.lnk'))) 'Install-created shortcut was not removed.'
    Assert-True (-not (Test-Path -LiteralPath $pointer)) 'Successful restore retained the active pointer.'

    # Missing Run key/value must be StrictMode-safe and restore to absence.
    $missingRunFixture = Join-Path $fixture 'missing-run'
    New-Item -ItemType Directory -Path (Join-Path $missingRunFixture 'UserDesktop') -Force | Out-Null
    $missingContext = Get-BillfishEntryPointContext -FixtureRoot $missingRunFixture
    Assert-True (-not (Get-BillfishRunValue -Context $missingContext).Exists) 'Missing Run fixture unexpectedly exists.'
    $missingPointer = Join-Path $missingRunFixture 'latest.txt'
    $missingBackup = Join-Path $missingRunFixture 'backup'
    Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir $missingBackup -FixtureRoot $missingRunFixture -StatePointerPath $missingPointer | Out-Null
    Restore-BillfishEntryPoints -BackupDir $missingBackup -BillfishRoot $repositoryRoot -FixtureRoot $missingRunFixture -StatePointerPath $missingPointer | Out-Null
    Assert-True (-not (Get-BillfishRunValue -Context $missingContext).Exists) 'Missing Run value was not restored to absence.'

    # A failure after every shortcut write (including public) must compensate all changes.
    $failureFixture = Join-Path $fixture 'install-failure'
    New-Item -ItemType Directory -Path (Join-Path $failureFixture 'UserDesktop'), (Join-Path $failureFixture 'CommonDesktop') -Force | Out-Null
    $failureUser = Join-Path $failureFixture 'UserDesktop\Billfish.lnk'
    $failurePublic = Join-Path $failureFixture 'CommonDesktop\Billfish.lnk'
    Write-BillfishShortcut -Path $failureUser -Value (New-TestShortcut 'C:\Origin\Billfish.exe' 'Origin user') -Fixture $true
    Write-BillfishShortcut -Path $failurePublic -Value (New-TestShortcut 'C:\Origin\Billfish.exe' 'Origin public') -Fixture $true
    $failureContext = Get-BillfishEntryPointContext -FixtureRoot $failureFixture
    Set-BillfishRunValue -Context $failureContext -Value 'origin-run'
    $failed = $false
    try { Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir (Join-Path $failureFixture 'backup') -FixtureRoot $failureFixture -StatePointerPath (Join-Path $failureFixture 'latest.txt') -FailStage Run | Out-Null }
    catch { $failed = $true }
    Assert-True $failed 'Injected install failure did not fail.'
    Assert-True ((Read-BillfishShortcut -Path $failureUser -Fixture $true).Description -eq 'Origin user') 'Install rollback lost the user shortcut.'
    Assert-True ((Read-BillfishShortcut -Path $failurePublic -Fixture $true).Description -eq 'Origin public') 'Install rollback lost the public shortcut.'
    Assert-True ((Get-BillfishRunValue -Context $failureContext).Value -eq 'origin-run') 'Install rollback changed Run.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $failureFixture 'latest.txt'))) 'Failed install published an active pointer.'

    [pscustomobject]@{ Outcome = 'Passed'; ShortcutCount = $installed.ShortcutCount; RepeatedInstall = $repeated.Outcome; MissingRunRestored = $true; FailureRollback = $true }
}
finally {
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
