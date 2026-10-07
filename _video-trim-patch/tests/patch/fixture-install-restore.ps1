[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sourcePatchRoot = [System.IO.Path]::GetFullPath((Join-Path -Path $PSScriptRoot -ChildPath '..\..'))
. (Join-Path -Path $sourcePatchRoot -ChildPath 'lib\VideoTrimPatch.Common.ps1')
. (Join-Path -Path $sourcePatchRoot -ChildPath 'lib\VideoTrimPatch.Operations.ps1')

$script:assertionCount = 0

function Assert-FixtureTrue {
    param(
        [Parameter(Mandatory = $true)]
        [bool] $Condition,

        [Parameter(Mandatory = $true)]
        [string] $Message
    )

    $script:assertionCount++
    if (-not $Condition) {
        throw "Fixture assertion failed: $Message"
    }
}

function Assert-FixtureFingerprint {
    param(
        [Parameter(Mandatory = $true)]
        [string] $LiteralPath,

        [Parameter(Mandatory = $true)]
        [object] $Expected,

        [Parameter(Mandatory = $true)]
        [string] $Message
    )

    $actual = Get-VideoTrimFileFingerprint -LiteralPath $LiteralPath
    Assert-FixtureTrue -Condition (Test-VideoTrimFingerprintEqual -Actual $actual -Expected $Expected) -Message $Message
}

function Assert-FixtureThrow {
    param(
        [Parameter(Mandatory = $true)]
        [scriptblock] $Action,

        [Parameter(Mandatory = $true)]
        [string] $MessagePattern,

        [Parameter(Mandatory = $true)]
        [string] $Message
    )

    $script:assertionCount++
    $thrownMessage = $null
    try {
        & $Action
    }
    catch {
        $thrownMessage = $_.Exception.Message
    }

    if ([string]::IsNullOrWhiteSpace($thrownMessage)) {
        throw "Fixture assertion failed: $Message (no exception was thrown)"
    }

    if ($thrownMessage -notmatch $MessagePattern) {
        throw "Fixture assertion failed: $Message (unexpected exception: $thrownMessage)"
    }
}

function Invoke-FixtureWrapper {
    param(
        [Parameter(Mandatory = $true)]
        [string] $ScriptPath,

        [Parameter(Mandatory = $true)]
        [string] $BillfishRoot,

        [Parameter(Mandatory = $true)]
        [string[]] $AdditionalArguments
    )

    $hostExecutable = (Get-Process -Id $PID -ErrorAction Stop).Path
    $nativeArguments = @('-NoLogo', '-NoProfile')
    if ([System.IO.Path]::GetFileName($hostExecutable) -ieq 'powershell.exe') {
        $nativeArguments += @('-ExecutionPolicy', 'Bypass')
    }

    $nativeArguments += @('-File', $ScriptPath, '-BillfishRoot', $BillfishRoot)
    $nativeArguments += $AdditionalArguments
    $outputLines = @(& $hostExecutable @nativeArguments 2>&1 | ForEach-Object { [string] $_ })
    $nativeExitCode = $LASTEXITCODE

    return [pscustomobject]@{
        exitCode = $nativeExitCode
        output = ($outputLines -join [Environment]::NewLine)
    }
}

$temporaryBase = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\', '/')
$fixtureRoot = Join-Path -Path $temporaryBase -ChildPath ('billfish-video-trim-fixture-' + [guid]::NewGuid().ToString('N'))
$fixturePatchRoot = Join-Path -Path $fixtureRoot -ChildPath 'patch'
$fixtureBillfishRoot = Join-Path -Path $fixtureRoot -ChildPath 'billfish'
$targetPath = Join-Path -Path $fixtureBillfishRoot -ChildPath 'ui_plugins\preview.dll'
$runtimeStockPath = Join-Path -Path $fixtureBillfishRoot -ChildPath 'ui_plugins\preview.stock.bin'
$buildPayloadPath = Join-Path -Path $fixturePatchRoot -ChildPath 'build\bin\preview.dll'
$statePath = Join-Path -Path $fixturePatchRoot -ChildPath 'state\installation.json'

try {
    foreach ($directory in @(
        (Join-Path -Path $fixturePatchRoot -ChildPath 'lib'),
        (Split-Path -Parent $buildPayloadPath),
        (Split-Path -Parent $targetPath)
    )) {
        New-Item -ItemType Directory -Path $directory -Force -ErrorAction Stop | Out-Null
    }

    $originalBytes = [System.Text.Encoding]::UTF8.GetBytes("fixture-stock-preview-v1`n")
    $patchedBytes = [System.Text.Encoding]::UTF8.GetBytes("fixture-video-trim-proxy-v1`n")
    $driftBytes = [System.Text.Encoding]::UTF8.GetBytes("fixture-unmanaged-drift`n")
    [System.IO.File]::WriteAllBytes($targetPath, $originalBytes)
    [System.IO.File]::WriteAllBytes($buildPayloadPath, $patchedBytes)
    [System.IO.File]::WriteAllBytes((Join-Path -Path $fixtureBillfishRoot -ChildPath 'Billfish.exe'), [System.Text.Encoding]::ASCII.GetBytes('fixture executable placeholder'))
    [System.IO.File]::WriteAllBytes((Join-Path -Path $fixtureBillfishRoot -ChildPath 'Billfish.core.exe'), [System.Text.Encoding]::ASCII.GetBytes('fixture core executable placeholder'))
    [System.IO.File]::WriteAllText((Join-Path -Path $fixtureBillfishRoot -ChildPath 'start-billfish.ps1'), "param([switch]`$DryRun)`r`n[pscustomobject]@{Outcome='WouldStart'}`r`n", (New-Object System.Text.UTF8Encoding($false)))

    foreach ($libraryName in @('VideoTrimPatch.Common.ps1', 'VideoTrimPatch.Operations.ps1')) {
        Copy-Item -LiteralPath (Join-Path -Path $sourcePatchRoot -ChildPath ('lib\' + $libraryName)) -Destination (Join-Path -Path $fixturePatchRoot -ChildPath ('lib\' + $libraryName)) -Force -ErrorAction Stop
    }

    foreach ($wrapperName in @('prepare-video-trim-patch.ps1', 'install-video-trim-patch.ps1', 'restore-video-trim-patch.ps1', 'start-billfish-video-trim.ps1')) {
        Copy-Item -LiteralPath (Join-Path -Path $sourcePatchRoot -ChildPath $wrapperName) -Destination (Join-Path -Path $fixturePatchRoot -ChildPath $wrapperName) -Force -ErrorAction Stop
    }

    $originalFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $targetPath
    $patchedFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $buildPayloadPath
    $allowlist = [ordered]@{
        schemaVersion = 1
        targetRelativePath = 'ui_plugins/preview.dll'
        entries = @(
            [ordered]@{
                sha256 = $originalFingerprint.sha256
                size = $originalFingerprint.size
                pluginVersion = 'fixture-1.0'
                qtAbi = 'Qt fixture x64'
                description = 'Synthetic test fixture'
            }
        )
    }
    Write-VideoTrimUtf8Json -InputObject $allowlist -LiteralPath (Join-Path -Path $fixturePatchRoot -ChildPath 'lib\preview-original-hashes.json')

    $operationLock = Enter-VideoTrimOperationLock -PatchRoot $fixturePatchRoot
    Assert-FixtureTrue -Condition ($operationLock -is [System.Threading.Mutex]) -Message 'Operation lock must return an acquired named mutex.'
    Exit-VideoTrimOperationLock -Mutex $operationLock
    $operationLock = Enter-VideoTrimOperationLock -PatchRoot $fixturePatchRoot
    Assert-FixtureTrue -Condition ($operationLock -is [System.Threading.Mutex]) -Message 'Operation lock must be reusable after release.'
    Exit-VideoTrimOperationLock -Mutex $operationLock

    $prepareWhatIf = Invoke-FixtureWrapper -ScriptPath (Join-Path -Path $fixturePatchRoot -ChildPath 'prepare-video-trim-patch.ps1') -BillfishRoot $fixtureBillfishRoot -AdditionalArguments @('-WhatIf')
    Assert-FixtureTrue -Condition ($prepareWhatIf.exitCode -eq 0) -Message 'Prepare -WhatIf wrapper must exit successfully.'
    Assert-FixtureTrue -Condition ($prepareWhatIf.output -match 'WouldPrepare') -Message 'Prepare -WhatIf wrapper must return its simulated plan.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath (Join-Path -Path $fixturePatchRoot -ChildPath 'prepared'))) -Message 'Prepare -WhatIf wrapper must not create prepared output.'

    $prepareDryRun = Publish-VideoTrimPreparedPackageCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot -DryRun
    Assert-FixtureTrue -Condition ($prepareDryRun.outcome -eq 'WouldPrepare') -Message 'Prepare DryRun should report WouldPrepare.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath (Join-Path -Path $fixturePatchRoot -ChildPath 'prepared'))) -Message 'Prepare DryRun must not create prepared output.'

    $prepared = Publish-VideoTrimPreparedPackageCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot
    Assert-FixtureTrue -Condition ($prepared.outcome -eq 'Prepared') -Message 'Prepare should publish the fixture package.'
    $package = Read-VideoTrimPreparedPackage -PatchRoot $fixturePatchRoot
    Assert-FixtureTrue -Condition ([string] $package.manifest.stockBackupRelativePath -eq 'ui_plugins/preview.stock.bin') -Message 'Manifest must use the proxy runtime stock path.'
    Assert-FixtureTrue -Condition ([string] $package.manifest.pluginVersion -eq 'fixture-1.0') -Message 'Manifest must preserve the allowlisted pluginVersion.'
    Assert-FixtureTrue -Condition ([string] $package.manifest.qtAbi -eq 'Qt fixture x64') -Message 'Manifest must preserve the allowlisted qtAbi.'

    $installWhatIf = Invoke-FixtureWrapper -ScriptPath (Join-Path -Path $fixturePatchRoot -ChildPath 'install-video-trim-patch.ps1') -BillfishRoot $fixtureBillfishRoot -AdditionalArguments @('-WhatIf')
    Assert-FixtureTrue -Condition ($installWhatIf.exitCode -eq 0) -Message 'Install -WhatIf wrapper must exit successfully.'
    Assert-FixtureTrue -Condition ($installWhatIf.output -match 'WouldInstall') -Message 'Install -WhatIf wrapper must return its simulated plan.'
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $originalFingerprint -Message 'Install -WhatIf wrapper must leave stock target unchanged.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath $runtimeStockPath)) -Message 'Install -WhatIf wrapper must not create runtime stock.'

    $installDryRun = Install-VideoTrimPatchCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot -DryRun -SkipProcessCheck
    Assert-FixtureTrue -Condition ($installDryRun.outcome -eq 'WouldInstall') -Message 'Install DryRun should report WouldInstall.'
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $originalFingerprint -Message 'Install DryRun must leave stock target unchanged.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath $runtimeStockPath)) -Message 'Install DryRun must not create preview.stock.bin.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath $statePath)) -Message 'Install DryRun must not create installation state.'

    $recoveryBackupPath = Resolve-VideoTrimContainedPath -Root $fixturePatchRoot -RelativePath ([string] $package.manifest.recoveryBackupRelativePath)
    Assert-FixtureThrow -Action {
        Install-VideoTrimPatchCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot -SkipProcessCheck -TestFailurePoint 'AfterTargetInstalled' | Out-Null
    } -MessagePattern 'rolled back' -Message 'First install failure must report automatic rollback.'
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $originalFingerprint -Message 'First install failure rollback must recover original target.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath $runtimeStockPath)) -Message 'First install failure rollback must remove runtime stock.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath $statePath)) -Message 'First install failure rollback must not leave active state.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath $recoveryBackupPath)) -Message 'First install failure rollback must remove its newly created recovery backup.'

    $installed = Install-VideoTrimPatchCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot -SkipProcessCheck
    Assert-FixtureTrue -Condition ($installed.outcome -eq 'Installed') -Message 'Install should complete.'
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $patchedFingerprint -Message 'Installed target must equal prepared proxy payload.'
    Assert-FixtureFingerprint -LiteralPath $runtimeStockPath -Expected $originalFingerprint -Message 'Runtime stock file must equal original target.'
    Assert-FixtureTrue -Condition (Test-Path -LiteralPath $statePath -PathType Leaf) -Message 'Install must commit installation state.'
    Assert-FixtureFingerprint -LiteralPath $installed.recoveryBackup -Expected $originalFingerprint -Message 'Install must retain a verified recovery backup.'

    $installedStatus = Get-VideoTrimPatchStatusCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot
    Assert-FixtureTrue -Condition ($installedStatus.targetState -eq 'Installed') -Message 'Status must identify the installed proxy.'
    Assert-FixtureTrue -Condition ($installedStatus.runtimeStockState -eq 'Valid') -Message 'Status must validate preview.stock.bin.'
    Assert-FixtureTrue -Condition $installedStatus.healthy -Message 'Installed fixture status must be healthy.'

    $startWhatIf = Invoke-FixtureWrapper -ScriptPath (Join-Path -Path $fixturePatchRoot -ChildPath 'start-billfish-video-trim.ps1') -BillfishRoot $fixtureBillfishRoot -AdditionalArguments @('-WhatIf')
    Assert-FixtureTrue -Condition ($startWhatIf.exitCode -eq 0) -Message 'Start -WhatIf wrapper must exit successfully.'
    Assert-FixtureTrue -Condition ($startWhatIf.output -match 'WouldStart') -Message "Start -WhatIf wrapper must return its simulated plan. Output: $($startWhatIf.output)"
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $patchedFingerprint -Message 'Start -WhatIf wrapper must not alter installed proxy.'

    $restoreWhatIf = Invoke-FixtureWrapper -ScriptPath (Join-Path -Path $fixturePatchRoot -ChildPath 'restore-video-trim-patch.ps1') -BillfishRoot $fixtureBillfishRoot -AdditionalArguments @('-WhatIf')
    Assert-FixtureTrue -Condition ($restoreWhatIf.exitCode -eq 0) -Message 'Restore -WhatIf wrapper must exit successfully.'
    Assert-FixtureTrue -Condition ($restoreWhatIf.output -match 'WouldRestore') -Message 'Restore -WhatIf wrapper must return its simulated plan.'
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $patchedFingerprint -Message 'Restore -WhatIf wrapper must leave patched target unchanged.'
    Assert-FixtureFingerprint -LiteralPath $runtimeStockPath -Expected $originalFingerprint -Message 'Restore -WhatIf wrapper must leave runtime stock unchanged.'

    $restoreDryRun = Restore-VideoTrimPatchCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot -DryRun -SkipProcessCheck
    Assert-FixtureTrue -Condition ($restoreDryRun.outcome -eq 'WouldRestore') -Message 'Restore DryRun should report WouldRestore.'
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $patchedFingerprint -Message 'Restore DryRun must leave patched target unchanged.'
    Assert-FixtureFingerprint -LiteralPath $runtimeStockPath -Expected $originalFingerprint -Message 'Restore DryRun must leave runtime stock unchanged.'

    [System.IO.File]::WriteAllBytes($targetPath, $driftBytes)
    $driftFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $targetPath
    Assert-FixtureThrow -Action {
        Restore-VideoTrimPatchCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot -SkipProcessCheck | Out-Null
    } -MessagePattern 'drift protection' -Message 'Restore must refuse a drifted patched target.'
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $driftFingerprint -Message 'Drift refusal must not overwrite the unmanaged target.'
    Copy-VideoTrimFileVerified -Source $package.payloadPath -Destination $targetPath -ExpectedFingerprint $patchedFingerprint

    [System.IO.File]::WriteAllBytes($runtimeStockPath, $driftBytes)
    Assert-FixtureThrow -Action {
        Restore-VideoTrimPatchCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot -SkipProcessCheck | Out-Null
    } -MessagePattern 'drift protection' -Message 'Restore must refuse a drifted runtime stock file.'
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $patchedFingerprint -Message 'Runtime stock drift refusal must leave proxy target unchanged.'
    [System.IO.File]::WriteAllBytes($runtimeStockPath, $originalBytes)

    [System.IO.File]::WriteAllBytes($installed.recoveryBackup, $driftBytes)
    Assert-FixtureThrow -Action {
        Restore-VideoTrimPatchCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot -SkipProcessCheck | Out-Null
    } -MessagePattern 'drift protection' -Message 'Restore must refuse a drifted persistent recovery backup.'
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $patchedFingerprint -Message 'Recovery backup drift refusal must leave proxy target unchanged.'
    [System.IO.File]::WriteAllBytes($installed.recoveryBackup, $originalBytes)

    $restored = Restore-VideoTrimPatchCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot -SkipProcessCheck
    Assert-FixtureTrue -Condition ($restored.outcome -eq 'Restored') -Message 'Restore should complete after drift is repaired.'
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $originalFingerprint -Message 'Restore must recover stock preview.dll.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath $runtimeStockPath)) -Message 'Restore must remove preview.stock.bin.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath $statePath)) -Message 'Restore must remove active installation state.'

    $stockStatus = Get-VideoTrimPatchStatusCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot
    Assert-FixtureTrue -Condition ($stockStatus.targetState -eq 'Stock') -Message 'Status must identify restored stock target.'
    Assert-FixtureTrue -Condition $stockStatus.healthy -Message 'Restored fixture status must be healthy.'

    Assert-FixtureThrow -Action {
        Install-VideoTrimPatchCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot -SkipProcessCheck -TestFailurePoint 'AfterTargetInstalled' | Out-Null
    } -MessagePattern 'rolled back' -Message 'Injected install failure must report rollback.'
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $originalFingerprint -Message 'Install failure rollback must recover original target.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath $runtimeStockPath)) -Message 'Install failure rollback must remove runtime stock.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath $statePath)) -Message 'Install failure rollback must not leave active state.'

    Install-VideoTrimPatchCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot -SkipProcessCheck | Out-Null
    Assert-FixtureThrow -Action {
        Restore-VideoTrimPatchCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot -SkipProcessCheck -TestFailurePoint 'AfterStockRestored' | Out-Null
    } -MessagePattern 'rolled back' -Message 'Injected restore failure must report rollback.'
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $patchedFingerprint -Message 'Restore failure rollback must recover patched target.'
    Assert-FixtureFingerprint -LiteralPath $runtimeStockPath -Expected $originalFingerprint -Message 'Restore failure rollback must recover runtime stock.'
    Assert-FixtureTrue -Condition (Test-Path -LiteralPath $statePath -PathType Leaf) -Message 'Restore failure rollback must recover installation state.'

    Restore-VideoTrimPatchCore -PatchRoot $fixturePatchRoot -BillfishRoot $fixtureBillfishRoot -SkipProcessCheck | Out-Null
    Assert-FixtureFingerprint -LiteralPath $targetPath -Expected $originalFingerprint -Message 'Final cleanup restore must recover stock target.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath $runtimeStockPath)) -Message 'Final cleanup restore must remove runtime stock.'
    Assert-FixtureTrue -Condition (-not (Test-Path -LiteralPath $statePath)) -Message 'Final cleanup restore must remove installation state.'

    [pscustomobject]@{
        test = 'FixtureInstallRestore'
        outcome = 'Passed'
        assertions = $script:assertionCount
        powerShellVersion = $PSVersionTable.PSVersion.ToString()
    }
}
finally {
    $fixtureFullPath = [System.IO.Path]::GetFullPath($fixtureRoot)
    $requiredPrefix = $temporaryBase + [System.IO.Path]::DirectorySeparatorChar + 'billfish-video-trim-fixture-'
    if ($fixtureFullPath.StartsWith($requiredPrefix, [System.StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $fixtureFullPath)) {
        Remove-Item -LiteralPath $fixtureFullPath -Recurse -Force -ErrorAction SilentlyContinue
    }
}
