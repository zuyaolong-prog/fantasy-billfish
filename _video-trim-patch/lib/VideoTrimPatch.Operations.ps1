Set-StrictMode -Version Latest

if ($null -eq (Get-Command -Name 'Get-VideoTrimFileFingerprint' -ErrorAction SilentlyContinue)) {
    . (Join-Path -Path $PSScriptRoot -ChildPath 'VideoTrimPatch.Common.ps1')
}

function ConvertTo-VideoTrimPreparedManifest {
    param(
        [Parameter(Mandatory = $true)]
        [object] $Original,

        [Parameter(Mandatory = $true)]
        [object] $Patched,

        [Parameter(Mandatory = $true)]
        [string] $PluginVersion,

        [Parameter(Mandatory = $true)]
        [string] $QtAbi
    )

    $originalHash = ConvertTo-VideoTrimNormalizedHash -Hash ([string] $Original.sha256)
    return [ordered]@{
        schemaVersion = $script:VideoTrimPatchSchemaVersion
        patchId = $script:VideoTrimPatchId
        pluginVersion = $PluginVersion
        qtAbi = $QtAbi
        createdUtc = [datetime]::UtcNow.ToString('o')
        target = [ordered]@{
            relativePath = $script:VideoTrimTargetRelativePath.Replace('\', '/')
        }
        original = [ordered]@{
            sha256 = $originalHash
            size = [int64] $Original.size
        }
        patched = [ordered]@{
            sha256 = (ConvertTo-VideoTrimNormalizedHash -Hash ([string] $Patched.sha256))
            size = [int64] $Patched.size
        }
        stockBackupRelativePath = $script:VideoTrimRuntimeStockRelativePath.Replace('\', '/')
        recoveryBackupRelativePath = ('backups/stock/{0}/preview.dll' -f $originalHash)
    }
}

function ConvertTo-VideoTrimInstallationReceipt {
    param(
        [Parameter(Mandatory = $true)]
        [object] $Manifest,

        [Parameter(Mandatory = $true)]
        [string] $TransactionId,

        [Parameter(Mandatory = $true)]
        [string] $PreparedManifestSha256
    )

    return [ordered]@{
        schemaVersion = [int] $Manifest.schemaVersion
        patchId = [string] $Manifest.patchId
        pluginVersion = [string] $Manifest.pluginVersion
        qtAbi = [string] $Manifest.qtAbi
        createdUtc = [string] $Manifest.createdUtc
        target = [ordered]@{
            relativePath = [string] $Manifest.target.relativePath
        }
        original = [ordered]@{
            sha256 = [string] $Manifest.original.sha256
            size = [int64] $Manifest.original.size
        }
        patched = [ordered]@{
            sha256 = [string] $Manifest.patched.sha256
            size = [int64] $Manifest.patched.size
        }
        stockBackupRelativePath = [string] $Manifest.stockBackupRelativePath
        recoveryBackupRelativePath = [string] $Manifest.recoveryBackupRelativePath
        transactionId = $TransactionId
        installedUtc = [datetime]::UtcNow.ToString('o')
        preparedManifestSha256 = (ConvertTo-VideoTrimNormalizedHash -Hash $PreparedManifestSha256)
    }
}

function Copy-VideoTrimFileVerified {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Source,

        [Parameter(Mandatory = $true)]
        [string] $Destination,

        [Parameter(Mandatory = $true)]
        [object] $ExpectedFingerprint
    )

    Copy-Item -LiteralPath $Source -Destination $Destination -Force -ErrorAction Stop
    $actual = Get-VideoTrimFileFingerprint -LiteralPath $Destination
    if (-not (Test-VideoTrimFingerprintEqual -Actual $actual -Expected $ExpectedFingerprint)) {
        Remove-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue
        throw "Copied file fingerprint mismatch: $Destination"
    }
}

function Publish-VideoTrimPreparedPackageCore {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $PatchRoot,

        [Parameter(Mandatory = $true)]
        [string] $BillfishRoot,

        [switch] $DryRun
    )

    $patchRootFull = [System.IO.Path]::GetFullPath($PatchRoot)
    $billfishRootFull = [System.IO.Path]::GetFullPath($BillfishRoot)
    $targetPath = Resolve-VideoTrimContainedPath -Root $billfishRootFull -RelativePath $script:VideoTrimTargetRelativePath -Context 'Billfish target path'
    $runtimeStockPath = Resolve-VideoTrimContainedPath -Root $billfishRootFull -RelativePath $script:VideoTrimRuntimeStockRelativePath -Context 'Runtime stock path'
    $buildPayloadPath = Resolve-VideoTrimContainedPath -Root $patchRootFull -RelativePath 'build\bin\preview.dll' -Context 'Build payload path'
    $statePath = Resolve-VideoTrimContainedPath -Root $patchRootFull -RelativePath $script:VideoTrimInstallationStateRelativePath -Context 'Installation state path'

    if (Test-Path -LiteralPath $statePath) {
        throw 'An installation state already exists. Restore the installed patch before preparing a new payload.'
    }

    if (Test-Path -LiteralPath $runtimeStockPath) {
        throw "Unexpected runtime stock file exists without an installation state: $runtimeStockPath"
    }

    $originalFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $targetPath
    $patchedFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $buildPayloadPath
    if (Test-VideoTrimFingerprintEqual -Actual $originalFingerprint -Expected $patchedFingerprint) {
        throw 'The build payload is byte-identical to the stock preview.dll; nothing can be prepared.'
    }

    $allowlist = Read-VideoTrimOriginalAllowlist -PatchRoot $patchRootFull
    $allowlistEntry = Assert-VideoTrimAllowedOriginal -Allowlist $allowlist -Fingerprint $originalFingerprint -Context 'Installed ui_plugins/preview.dll'
    $runtimeMetadata = Get-VideoTrimRuntimeInfo -BillfishRoot $billfishRootFull -AllowlistEntry $allowlistEntry
    $manifest = ConvertTo-VideoTrimPreparedManifest -Original $originalFingerprint -Patched $patchedFingerprint -PluginVersion $runtimeMetadata.pluginVersion -QtAbi $runtimeMetadata.qtAbi

    # Second allowlist gate: validate the independently serialized manifest values and metadata.
    [void] (Assert-VideoTrimDescriptor -Descriptor ([pscustomobject] $manifest) -Context 'Generated prepared manifest')
    [void] (Assert-VideoTrimAllowedOriginal -Allowlist $allowlist -Fingerprint ([pscustomobject] $manifest.original) -PluginVersion $manifest.pluginVersion -QtAbi $manifest.qtAbi -Context 'Generated manifest original')

    if ($DryRun) {
        return [pscustomobject]@{
            operation = 'Prepare'
            outcome = 'WouldPrepare'
            changed = $false
            simulated = $true
            source = $buildPayloadPath
            destination = (Resolve-VideoTrimContainedPath -Root $patchRootFull -RelativePath $script:VideoTrimPreparedPayloadRelativePath)
            originalSha256 = $originalFingerprint.sha256
            patchedSha256 = $patchedFingerprint.sha256
            message = 'Prepared payload and manifest passed all preflight checks; no files were changed.'
        }
    }

    $transactionId = [guid]::NewGuid().ToString('N')
    $stagingDirectory = Join-Path -Path $patchRootFull -ChildPath ('.prepared-staging-' + $transactionId)
    $preparedDirectory = Join-Path -Path $patchRootFull -ChildPath 'prepared'
    $previousDirectory = Join-Path -Path $patchRootFull -ChildPath ('.prepared-previous-' + $transactionId)
    $previousMoved = $false
    $newPreparedInstalled = $false

    try {
        New-Item -ItemType Directory -Path $stagingDirectory -ErrorAction Stop | Out-Null
        $stagedPayloadPath = Join-Path -Path $stagingDirectory -ChildPath 'preview.dll'
        $stagedManifestPath = Join-Path -Path $stagingDirectory -ChildPath 'manifest.json'
        Copy-VideoTrimFileVerified -Source $buildPayloadPath -Destination $stagedPayloadPath -ExpectedFingerprint $patchedFingerprint
        Write-VideoTrimUtf8Json -InputObject $manifest -LiteralPath $stagedManifestPath

        $serializedManifest = Read-VideoTrimJsonFile -LiteralPath $stagedManifestPath -Context 'staged prepared manifest'
        [void] (Assert-VideoTrimDescriptor -Descriptor $serializedManifest -Context 'Staged prepared manifest')
        [void] (Assert-VideoTrimAllowedOriginal -Allowlist $allowlist -Fingerprint $serializedManifest.original -PluginVersion ([string] $serializedManifest.pluginVersion) -QtAbi ([string] $serializedManifest.qtAbi) -Context 'Staged manifest original')
        $stagedPayloadFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $stagedPayloadPath
        if (-not (Test-VideoTrimFingerprintEqual -Actual $stagedPayloadFingerprint -Expected $serializedManifest.patched)) {
            throw 'Staged payload does not match the serialized manifest.'
        }

        if (Test-Path -LiteralPath $preparedDirectory) {
            Move-Item -LiteralPath $preparedDirectory -Destination $previousDirectory -ErrorAction Stop
            $previousMoved = $true
        }

        Move-Item -LiteralPath $stagingDirectory -Destination $preparedDirectory -ErrorAction Stop
        $newPreparedInstalled = $true
        [void] (Read-VideoTrimPreparedPackage -PatchRoot $patchRootFull)

        if ($previousMoved) {
            # Publication is already committed. Cleanup failure must not replace a valid
            # new package with a possibly partially deleted previous directory.
            Remove-Item -LiteralPath $previousDirectory -Recurse -Force -ErrorAction SilentlyContinue
            $previousMoved = $false
        }
    }
    catch {
        $failureMessage = $_.Exception.Message
        if ($newPreparedInstalled -and (Test-Path -LiteralPath $preparedDirectory)) {
            Remove-Item -LiteralPath $preparedDirectory -Recurse -Force -ErrorAction SilentlyContinue
        }

        if ($previousMoved -and (Test-Path -LiteralPath $previousDirectory) -and -not (Test-Path -LiteralPath $preparedDirectory)) {
            Move-Item -LiteralPath $previousDirectory -Destination $preparedDirectory -ErrorAction SilentlyContinue
        }

        if (Test-Path -LiteralPath $stagingDirectory) {
            Remove-Item -LiteralPath $stagingDirectory -Recurse -Force -ErrorAction SilentlyContinue
        }

        throw "Prepare failed; the previous prepared package was restored: $failureMessage"
    }

    return [pscustomobject]@{
        operation = 'Prepare'
        outcome = 'Prepared'
        changed = $true
        simulated = $false
        source = $buildPayloadPath
        destination = (Join-Path -Path $preparedDirectory -ChildPath 'preview.dll')
        originalSha256 = $originalFingerprint.sha256
        patchedSha256 = $patchedFingerprint.sha256
        message = 'Prepared payload and manifest were published atomically.'
    }
}

function Install-VideoTrimPatchCore {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $PatchRoot,

        [Parameter(Mandatory = $true)]
        [string] $BillfishRoot,

        [switch] $DryRun,

        [switch] $SkipProcessCheck,

        [ValidateSet('None', 'AfterStockMoved', 'AfterTargetInstalled', 'BeforeStateCommit')]
        [string] $TestFailurePoint = 'None'
    )

    $patchRootFull = [System.IO.Path]::GetFullPath($PatchRoot)
    $billfishRootFull = [System.IO.Path]::GetFullPath($BillfishRoot)
    $package = Read-VideoTrimPreparedPackage -PatchRoot $patchRootFull
    $manifest = $package.manifest
    $allowlist = Read-VideoTrimOriginalAllowlist -PatchRoot $patchRootFull
    [void] (Assert-VideoTrimAllowedOriginal -Allowlist $allowlist -Fingerprint $manifest.original -PluginVersion ([string] $manifest.pluginVersion) -QtAbi ([string] $manifest.qtAbi) -Context 'Prepared manifest original')

    $targetPath = Resolve-VideoTrimContainedPath -Root $billfishRootFull -RelativePath ([string] $manifest.target.relativePath) -Context 'Billfish target path'
    $runtimeStockPath = Resolve-VideoTrimContainedPath -Root $billfishRootFull -RelativePath ([string] $manifest.stockBackupRelativePath) -Context 'Runtime stock path'
    $recoveryBackupPath = Resolve-VideoTrimContainedPath -Root $patchRootFull -RelativePath ([string] $manifest.recoveryBackupRelativePath) -Context 'Recovery backup path'
    $statePath = Resolve-VideoTrimContainedPath -Root $patchRootFull -RelativePath $script:VideoTrimInstallationStateRelativePath -Context 'Installation state path'
    $targetFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $targetPath

    if (Test-Path -LiteralPath $statePath) {
        $existingState = Read-VideoTrimInstallationState -PatchRoot $patchRootFull
        if (-not (Test-VideoTrimFingerprintEqual -Actual $targetFingerprint -Expected $existingState.patched)) {
            throw 'Installation state exists but ui_plugins/preview.dll has drifted. Refusing to overwrite it.'
        }

        $existingStockFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $runtimeStockPath
        if (-not (Test-VideoTrimFingerprintEqual -Actual $existingStockFingerprint -Expected $existingState.original)) {
            throw 'Installation state exists but ui_plugins/preview.stock.bin is missing or has drifted.'
        }

        if (-not (Test-VideoTrimFingerprintEqual -Actual $existingState.patched -Expected $manifest.patched)) {
            throw 'A different video trim patch is already installed. Restore it before installing this prepared payload.'
        }

        return [pscustomobject]@{
            operation = 'Install'
            outcome = 'AlreadyInstalled'
            changed = $false
            simulated = [bool] $DryRun
            target = $targetPath
            message = 'The prepared video trim patch is already installed and its runtime stock file is valid.'
        }
    }

    if (Test-VideoTrimFingerprintEqual -Actual $targetFingerprint -Expected $manifest.patched) {
        throw 'ui_plugins/preview.dll matches the prepared patch but no installation state exists. Refusing to adopt an unmanaged install.'
    }

    if (-not (Test-VideoTrimFingerprintEqual -Actual $targetFingerprint -Expected $manifest.original)) {
        throw 'ui_plugins/preview.dll does not match either the prepared original or patched fingerprint. Refusing to overwrite drifted bytes.'
    }

    [void] (Assert-VideoTrimAllowedOriginal -Allowlist $allowlist -Fingerprint $targetFingerprint -PluginVersion ([string] $manifest.pluginVersion) -QtAbi ([string] $manifest.qtAbi) -Context 'Installed ui_plugins/preview.dll')

    if (Test-Path -LiteralPath $runtimeStockPath) {
        throw "Runtime stock path already exists without installation state: $runtimeStockPath"
    }

    $recoveryBackupExists = Test-Path -LiteralPath $recoveryBackupPath -PathType Leaf
    if ($recoveryBackupExists) {
        $recoveryFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $recoveryBackupPath
        if (-not (Test-VideoTrimFingerprintEqual -Actual $recoveryFingerprint -Expected $manifest.original)) {
            throw "Recovery backup exists but does not match the stock fingerprint: $recoveryBackupPath"
        }
    }

    $runningProcesses = @()
    if (-not $SkipProcessCheck) {
        $runningProcesses = @(Get-VideoTrimBillfishProcess -BillfishRoot $billfishRootFull)
    }

    if ($DryRun) {
        $dryRunOutcome = 'WouldInstall'
        $dryRunMessage = 'Install preflight passed; no files were changed.'
        if ($runningProcesses.Count -gt 0) {
            $dryRunOutcome = 'BlockedByRunningBillfish'
            $dryRunMessage = 'Install file checks passed, but Billfish is running. Close it before installation.'
        }

        return [pscustomobject]@{
            operation = 'Install'
            outcome = $dryRunOutcome
            changed = $false
            simulated = $true
            target = $targetPath
            runtimeStock = $runtimeStockPath
            recoveryBackup = $recoveryBackupPath
            billfishRunning = ($runningProcesses.Count -gt 0)
            originalSha256 = [string] $manifest.original.sha256
            patchedSha256 = [string] $manifest.patched.sha256
            message = $dryRunMessage
        }
    }

    if (-not $SkipProcessCheck) {
        Assert-VideoTrimBillfishStopped -BillfishRoot $billfishRootFull
    }

    $transactionId = [guid]::NewGuid().ToString()
    $targetDirectory = Split-Path -Parent $targetPath
    $candidatePath = Join-Path -Path $targetDirectory -ChildPath ('.preview.video-trim-' + $transactionId + '.new')
    $stateDirectory = Split-Path -Parent $statePath
    $stateTemporaryPath = Join-Path -Path $stateDirectory -ChildPath ('.installation-' + $transactionId + '.json.tmp')
    $receipt = ConvertTo-VideoTrimInstallationReceipt -Manifest $manifest -TransactionId $transactionId -PreparedManifestSha256 $package.manifestSha256
    $backupCreated = $false
    $stockMoved = $false
    $stateCommitted = $false
    $rollbackSucceeded = $false
    $recoveryTemporaryPath = $null

    try {
        if (-not $recoveryBackupExists) {
            $recoveryDirectory = Split-Path -Parent $recoveryBackupPath
            New-Item -ItemType Directory -Path $recoveryDirectory -Force -ErrorAction Stop | Out-Null
            $recoveryTemporaryPath = Join-Path -Path $recoveryDirectory -ChildPath ('.preview-' + $transactionId + '.tmp')
            Copy-VideoTrimFileVerified -Source $targetPath -Destination $recoveryTemporaryPath -ExpectedFingerprint $manifest.original
            Move-Item -LiteralPath $recoveryTemporaryPath -Destination $recoveryBackupPath -ErrorAction Stop
            $backupCreated = $true
        }

        Copy-VideoTrimFileVerified -Source $package.payloadPath -Destination $candidatePath -ExpectedFingerprint $manifest.patched
        New-Item -ItemType Directory -Path $stateDirectory -Force -ErrorAction Stop | Out-Null
        Write-VideoTrimUtf8Json -InputObject $receipt -LiteralPath $stateTemporaryPath
        $serializedReceipt = Read-VideoTrimJsonFile -LiteralPath $stateTemporaryPath -Context 'temporary installation state'
        [void] (Assert-VideoTrimInstallationStateObject -State $serializedReceipt -Context 'Temporary installation state')

        Move-Item -LiteralPath $targetPath -Destination $runtimeStockPath -ErrorAction Stop
        $stockMoved = $true
        $stockFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $runtimeStockPath
        if (-not (Test-VideoTrimFingerprintEqual -Actual $stockFingerprint -Expected $manifest.original)) {
            throw 'Runtime stock fingerprint changed during the install transaction.'
        }

        if ($TestFailurePoint -eq 'AfterStockMoved') {
            throw 'Injected fixture failure after stock move.'
        }

        Move-Item -LiteralPath $candidatePath -Destination $targetPath -ErrorAction Stop
        $installedFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $targetPath
        if (-not (Test-VideoTrimFingerprintEqual -Actual $installedFingerprint -Expected $manifest.patched)) {
            throw 'Installed preview.dll does not match the prepared patched fingerprint.'
        }

        if ($TestFailurePoint -eq 'AfterTargetInstalled') {
            throw 'Injected fixture failure after target install.'
        }

        if ($TestFailurePoint -eq 'BeforeStateCommit') {
            throw 'Injected fixture failure before state commit.'
        }

        Move-Item -LiteralPath $stateTemporaryPath -Destination $statePath -ErrorAction Stop
        $stateCommitted = $true
        [void] (Read-VideoTrimInstallationState -PatchRoot $patchRootFull)
    }
    catch {
        $failureMessage = $_.Exception.Message

        if ($stateCommitted -and (Test-Path -LiteralPath $statePath)) {
            try {
                $currentState = Read-VideoTrimJsonFile -LiteralPath $statePath -Context 'failed transaction state'
                if ([string] $currentState.transactionId -eq $transactionId) {
                    Remove-Item -LiteralPath $statePath -Force -ErrorAction Stop
                    $stateCommitted = $false
                }
            }
            catch {
                # Continue to target rollback; the final error reports any incomplete rollback.
                Write-Verbose "Unable to remove a failed transaction state before target rollback: $($_.Exception.Message)"
            }
        }

        if ($stockMoved -and (Test-Path -LiteralPath $runtimeStockPath -PathType Leaf)) {
            $canRestoreTarget = $true
            if (Test-Path -LiteralPath $targetPath -PathType Leaf) {
                try {
                    $failedTargetFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $targetPath
                    if (Test-VideoTrimFingerprintEqual -Actual $failedTargetFingerprint -Expected $manifest.patched) {
                        Remove-Item -LiteralPath $targetPath -Force -ErrorAction Stop
                    }
                    else {
                        $canRestoreTarget = $false
                    }
                }
                catch {
                    $canRestoreTarget = $false
                }
            }

            if ($canRestoreTarget -and -not (Test-Path -LiteralPath $targetPath)) {
                try {
                    Move-Item -LiteralPath $runtimeStockPath -Destination $targetPath -ErrorAction Stop
                    $restoredFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $targetPath
                    $rollbackSucceeded = Test-VideoTrimFingerprintEqual -Actual $restoredFingerprint -Expected $manifest.original
                }
                catch {
                    $rollbackSucceeded = $false
                }
            }
        }
        elseif (-not $stockMoved) {
            $rollbackSucceeded = $true
        }

        foreach ($temporaryPath in @($candidatePath, $stateTemporaryPath, $recoveryTemporaryPath)) {
            if (-not [string]::IsNullOrWhiteSpace($temporaryPath) -and (Test-Path -LiteralPath $temporaryPath)) {
                Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
            }
        }

        if ($backupCreated -and $rollbackSucceeded -and (Test-Path -LiteralPath $recoveryBackupPath)) {
            Remove-Item -LiteralPath $recoveryBackupPath -Force -ErrorAction SilentlyContinue
        }

        if (-not $rollbackSucceeded -or $stateCommitted) {
            throw "Install failed and automatic rollback was incomplete. Original failure: $failureMessage"
        }

        throw "Install failed and was rolled back. Original failure: $failureMessage"
    }

    return [pscustomobject]@{
        operation = 'Install'
        outcome = 'Installed'
        changed = $true
        simulated = $false
        target = $targetPath
        runtimeStock = $runtimeStockPath
        recoveryBackup = $recoveryBackupPath
        transactionId = $transactionId
        originalSha256 = [string] $manifest.original.sha256
        patchedSha256 = [string] $manifest.patched.sha256
        message = 'The proxy preview.dll was installed atomically; stock bytes are available only as preview.stock.bin and a recovery backup.'
    }
}

function Restore-VideoTrimPatchCore {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $PatchRoot,

        [Parameter(Mandatory = $true)]
        [string] $BillfishRoot,

        [switch] $DryRun,

        [switch] $SkipProcessCheck,

        [ValidateSet('None', 'AfterStockRestored')]
        [string] $TestFailurePoint = 'None'
    )

    $patchRootFull = [System.IO.Path]::GetFullPath($PatchRoot)
    $billfishRootFull = [System.IO.Path]::GetFullPath($BillfishRoot)
    $statePath = Resolve-VideoTrimContainedPath -Root $patchRootFull -RelativePath $script:VideoTrimInstallationStateRelativePath -Context 'Installation state path'
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) {
        $unexpectedRuntimeStockPath = Resolve-VideoTrimContainedPath -Root $billfishRootFull -RelativePath $script:VideoTrimRuntimeStockRelativePath -Context 'Runtime stock path'
        if (Test-Path -LiteralPath $unexpectedRuntimeStockPath) {
            throw "Restore drift protection: runtime stock exists without installation state: $unexpectedRuntimeStockPath"
        }

        return [pscustomobject]@{
            operation = 'Restore'
            outcome = 'NotInstalled'
            changed = $false
            simulated = [bool] $DryRun
            message = 'No installation state exists; nothing was restored.'
        }
    }

    $state = Read-VideoTrimInstallationState -PatchRoot $patchRootFull
    $allowlist = Read-VideoTrimOriginalAllowlist -PatchRoot $patchRootFull
    [void] (Assert-VideoTrimAllowedOriginal -Allowlist $allowlist -Fingerprint $state.original -PluginVersion ([string] $state.pluginVersion) -QtAbi ([string] $state.qtAbi) -Context 'Installation state original')

    $targetPath = Resolve-VideoTrimContainedPath -Root $billfishRootFull -RelativePath ([string] $state.target.relativePath) -Context 'Billfish target path'
    $runtimeStockPath = Resolve-VideoTrimContainedPath -Root $billfishRootFull -RelativePath ([string] $state.stockBackupRelativePath) -Context 'Runtime stock path'
    $recoveryBackupPath = Resolve-VideoTrimContainedPath -Root $patchRootFull -RelativePath ([string] $state.recoveryBackupRelativePath) -Context 'Recovery backup path'

    $targetFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $targetPath
    if (-not (Test-VideoTrimFingerprintEqual -Actual $targetFingerprint -Expected $state.patched)) {
        throw 'Restore drift protection: ui_plugins/preview.dll no longer matches the installed patched fingerprint. Nothing was changed.'
    }

    $runtimeStockFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $runtimeStockPath
    if (-not (Test-VideoTrimFingerprintEqual -Actual $runtimeStockFingerprint -Expected $state.original)) {
        throw 'Restore drift protection: ui_plugins/preview.stock.bin is missing or no longer matches the recorded stock fingerprint. Nothing was changed.'
    }

    $recoveryFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $recoveryBackupPath
    if (-not (Test-VideoTrimFingerprintEqual -Actual $recoveryFingerprint -Expected $state.original)) {
        throw 'Restore drift protection: the recovery backup is missing or no longer matches the recorded stock fingerprint. Nothing was changed.'
    }

    $runningProcesses = @()
    if (-not $SkipProcessCheck) {
        $runningProcesses = @(Get-VideoTrimBillfishProcess -BillfishRoot $billfishRootFull)
    }

    if ($DryRun) {
        $dryRunOutcome = 'WouldRestore'
        $dryRunMessage = 'Restore preflight and all drift checks passed; no files were changed.'
        if ($runningProcesses.Count -gt 0) {
            $dryRunOutcome = 'BlockedByRunningBillfish'
            $dryRunMessage = 'Restore drift checks passed, but Billfish is running. Close it before restore.'
        }

        return [pscustomobject]@{
            operation = 'Restore'
            outcome = $dryRunOutcome
            changed = $false
            simulated = $true
            target = $targetPath
            runtimeStock = $runtimeStockPath
            recoveryBackup = $recoveryBackupPath
            billfishRunning = ($runningProcesses.Count -gt 0)
            restoredSha256 = [string] $state.original.sha256
            message = $dryRunMessage
        }
    }

    if (-not $SkipProcessCheck) {
        Assert-VideoTrimBillfishStopped -BillfishRoot $billfishRootFull
    }

    $transactionId = [guid]::NewGuid().ToString()
    $targetDirectory = Split-Path -Parent $targetPath
    $patchedRollbackPath = Join-Path -Path $targetDirectory -ChildPath ('.preview.video-trim-' + $transactionId + '.rollback')
    $stateDirectory = Split-Path -Parent $statePath
    $stateRollbackPath = Join-Path -Path $stateDirectory -ChildPath ('.installation-' + $transactionId + '.rollback')
    $targetMoved = $false
    $stockRestored = $false
    $stateMoved = $false

    try {
        Move-Item -LiteralPath $targetPath -Destination $patchedRollbackPath -ErrorAction Stop
        $targetMoved = $true
        Move-Item -LiteralPath $runtimeStockPath -Destination $targetPath -ErrorAction Stop
        $stockRestored = $true

        $restoredFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $targetPath
        if (-not (Test-VideoTrimFingerprintEqual -Actual $restoredFingerprint -Expected $state.original)) {
            throw 'Restored preview.dll fingerprint does not match installation state.'
        }

        if ($TestFailurePoint -eq 'AfterStockRestored') {
            throw 'Injected fixture failure after stock restore.'
        }

        Move-Item -LiteralPath $statePath -Destination $stateRollbackPath -ErrorAction Stop
        $stateMoved = $true
    }
    catch {
        $failureMessage = $_.Exception.Message
        $rollbackSucceeded = $true

        if ($stateMoved -and (Test-Path -LiteralPath $stateRollbackPath) -and -not (Test-Path -LiteralPath $statePath)) {
            try {
                Move-Item -LiteralPath $stateRollbackPath -Destination $statePath -ErrorAction Stop
                $stateMoved = $false
            }
            catch {
                $rollbackSucceeded = $false
            }
        }

        if ($stockRestored) {
            try {
                $currentTargetFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $targetPath
                if (-not (Test-VideoTrimFingerprintEqual -Actual $currentTargetFingerprint -Expected $state.original)) {
                    throw 'The restored target changed during rollback.'
                }

                Move-Item -LiteralPath $targetPath -Destination $runtimeStockPath -ErrorAction Stop
                $stockRestored = $false
            }
            catch {
                $rollbackSucceeded = $false
            }
        }

        if ($targetMoved -and (Test-Path -LiteralPath $patchedRollbackPath) -and -not (Test-Path -LiteralPath $targetPath)) {
            try {
                Move-Item -LiteralPath $patchedRollbackPath -Destination $targetPath -ErrorAction Stop
                $rolledBackTargetFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $targetPath
                if (-not (Test-VideoTrimFingerprintEqual -Actual $rolledBackTargetFingerprint -Expected $state.patched)) {
                    throw 'Rolled-back patched target fingerprint mismatch.'
                }

                $targetMoved = $false
            }
            catch {
                $rollbackSucceeded = $false
            }
        }

        if (-not $rollbackSucceeded -or $stateMoved -or $stockRestored -or $targetMoved) {
            throw "Restore failed and automatic rollback was incomplete. Original failure: $failureMessage"
        }

        throw "Restore failed and was rolled back. Original failure: $failureMessage"
    }

    # The commit point is the state rename. Cleanup is safe and retryable; active state is already absent.
    Remove-Item -LiteralPath $patchedRollbackPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stateRollbackPath -Force -ErrorAction SilentlyContinue

    if (Test-Path -LiteralPath $runtimeStockPath) {
        throw 'Restore completed but preview.stock.bin still exists; manual inspection is required.'
    }

    $finalFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $targetPath
    if (-not (Test-VideoTrimFingerprintEqual -Actual $finalFingerprint -Expected $state.original)) {
        throw 'Restore completed but the final preview.dll fingerprint is unexpected.'
    }

    return [pscustomobject]@{
        operation = 'Restore'
        outcome = 'Restored'
        changed = $true
        simulated = $false
        target = $targetPath
        recoveryBackup = $recoveryBackupPath
        restoredSha256 = [string] $state.original.sha256
        message = 'Stock preview.dll was restored atomically; preview.stock.bin and installation state were removed.'
    }
}

function Get-VideoTrimPatchStatusCore {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $PatchRoot,

        [Parameter(Mandatory = $true)]
        [string] $BillfishRoot
    )

    $patchRootFull = [System.IO.Path]::GetFullPath($PatchRoot)
    $billfishRootFull = [System.IO.Path]::GetFullPath($BillfishRoot)
    $errors = New-Object 'System.Collections.Generic.List[string]'
    $allowlist = $null
    $package = $null
    $state = $null
    $preparedState = 'Missing'
    $installationState = 'Absent'

    try {
        $allowlist = Read-VideoTrimOriginalAllowlist -PatchRoot $patchRootFull
    }
    catch {
        $errors.Add($_.Exception.Message)
    }

    $manifestPath = Resolve-VideoTrimContainedPath -Root $patchRootFull -RelativePath $script:VideoTrimPreparedManifestRelativePath
    if (Test-Path -LiteralPath $manifestPath) {
        try {
            $package = Read-VideoTrimPreparedPackage -PatchRoot $patchRootFull
            if ($null -ne $allowlist) {
                [void] (Assert-VideoTrimAllowedOriginal -Allowlist $allowlist -Fingerprint $package.manifest.original -PluginVersion ([string] $package.manifest.pluginVersion) -QtAbi ([string] $package.manifest.qtAbi) -Context 'Prepared manifest original')
            }
            $preparedState = 'Valid'
        }
        catch {
            $preparedState = 'Invalid'
            $errors.Add($_.Exception.Message)
        }
    }

    $statePath = Resolve-VideoTrimContainedPath -Root $patchRootFull -RelativePath $script:VideoTrimInstallationStateRelativePath
    if (Test-Path -LiteralPath $statePath) {
        try {
            $state = Read-VideoTrimInstallationState -PatchRoot $patchRootFull
            if ($null -ne $allowlist) {
                [void] (Assert-VideoTrimAllowedOriginal -Allowlist $allowlist -Fingerprint $state.original -PluginVersion ([string] $state.pluginVersion) -QtAbi ([string] $state.qtAbi) -Context 'Installation state original')
            }
            $installationState = 'Valid'
        }
        catch {
            $installationState = 'Invalid'
            $errors.Add($_.Exception.Message)
        }
    }

    $descriptor = $null
    if ($null -ne $state) {
        $descriptor = $state
    }
    elseif ($null -ne $package) {
        $descriptor = $package.manifest
    }

    $targetPath = Resolve-VideoTrimContainedPath -Root $billfishRootFull -RelativePath $script:VideoTrimTargetRelativePath
    $targetFingerprint = $null
    if (Test-Path -LiteralPath $targetPath -PathType Leaf) {
        try {
            $targetFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $targetPath
        }
        catch {
            $errors.Add($_.Exception.Message)
        }
    }

    $targetState = 'Missing'
    if ($null -ne $targetFingerprint) {
        if ($null -ne $state) {
            if (Test-VideoTrimFingerprintEqual -Actual $targetFingerprint -Expected $state.patched) {
                $targetState = 'Installed'
            }
            elseif (Test-VideoTrimFingerprintEqual -Actual $targetFingerprint -Expected $state.original) {
                $targetState = 'StockWithStaleState'
            }
            else {
                $targetState = 'Drifted'
            }
        }
        elseif ($null -ne $package -and (Test-VideoTrimFingerprintEqual -Actual $targetFingerprint -Expected $package.manifest.patched)) {
            $targetState = 'PatchedUnmanaged'
        }
        elseif ($null -ne $allowlist -and $null -ne (Find-VideoTrimAllowedOriginal -Allowlist $allowlist -Fingerprint $targetFingerprint)) {
            $targetState = 'Stock'
        }
        else {
            $targetState = 'Unknown'
        }
    }

    $runtimeStockState = 'Missing'
    $recoveryBackupState = 'NotApplicable'
    $runtimeStockPath = Resolve-VideoTrimContainedPath -Root $billfishRootFull -RelativePath $script:VideoTrimRuntimeStockRelativePath
    $recoveryBackupPath = $null
    if (Test-Path -LiteralPath $runtimeStockPath -PathType Leaf) {
        if ($null -ne $descriptor) {
            try {
                $runtimeStockFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $runtimeStockPath
                if (Test-VideoTrimFingerprintEqual -Actual $runtimeStockFingerprint -Expected $descriptor.original) {
                    $runtimeStockState = 'Valid'
                }
                else {
                    $runtimeStockState = 'Drifted'
                }
            }
            catch {
                $runtimeStockState = 'Invalid'
                $errors.Add($_.Exception.Message)
            }
        }
        else {
            $runtimeStockState = 'Unexpected'
        }
    }

    if ($null -ne $descriptor) {
        try {
            $recoveryBackupPath = Resolve-VideoTrimContainedPath -Root $patchRootFull -RelativePath ([string] $descriptor.recoveryBackupRelativePath)
            if (Test-Path -LiteralPath $recoveryBackupPath -PathType Leaf) {
                $recoveryFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $recoveryBackupPath
                if (Test-VideoTrimFingerprintEqual -Actual $recoveryFingerprint -Expected $descriptor.original) {
                    $recoveryBackupState = 'Valid'
                }
                else {
                    $recoveryBackupState = 'Drifted'
                }
            }
            else {
                $recoveryBackupState = 'Missing'
            }
        }
        catch {
            $recoveryBackupState = 'Invalid'
            $errors.Add($_.Exception.Message)
        }
    }

    $runningProcesses = @(Get-VideoTrimBillfishProcess -BillfishRoot $billfishRootFull)
    $readyForInstall = ($preparedState -eq 'Valid' -and $installationState -eq 'Absent' -and $targetState -eq 'Stock' -and $runtimeStockState -eq 'Missing' -and $runningProcesses.Count -eq 0)
    $readyForRestore = ($installationState -eq 'Valid' -and $targetState -eq 'Installed' -and $runtimeStockState -eq 'Valid' -and $recoveryBackupState -eq 'Valid' -and $runningProcesses.Count -eq 0)
    $healthy = ($errors.Count -eq 0 -and (($targetState -eq 'Stock' -and $runtimeStockState -eq 'Missing' -and $installationState -eq 'Absent') -or ($targetState -eq 'Installed' -and $runtimeStockState -eq 'Valid' -and $installationState -eq 'Valid')))

    return [pscustomobject]@{
        patchRoot = $patchRootFull
        billfishRoot = $billfishRootFull
        targetPath = $targetPath
        targetState = $targetState
        preparedState = $preparedState
        installationState = $installationState
        runtimeStockPath = $runtimeStockPath
        runtimeStockState = $runtimeStockState
        recoveryBackupPath = $recoveryBackupPath
        recoveryBackupState = $recoveryBackupState
        billfishRunning = ($runningProcesses.Count -gt 0)
        billfishProcessIds = @($runningProcesses | ForEach-Object { $_.Id })
        readyForInstall = $readyForInstall
        readyForRestore = $readyForRestore
        healthy = $healthy
        errors = [string[]] $errors
    }
}
