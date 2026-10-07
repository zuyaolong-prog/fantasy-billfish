[CmdletBinding(PositionalBinding = $false)]
param()

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
$patchRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $patchRoot
. (Join-Path $patchRoot 'EntryPoints.Core.ps1')

function Assert-True([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function New-OriginShortcut([string]$Target = 'C:\Origin\Billfish.exe') { [ordered]@{TargetPath=$Target;Arguments='--origin';WorkingDirectory='C:\Origin';IconLocation="$Target,0";Description='Origin';WindowStyle=1} }
function New-InstalledFixture([string]$Root, [string]$BackupName = 'backup') {
    New-Item -ItemType Directory -Path (Join-Path $Root 'UserDesktop'), (Join-Path $Root 'CommonDesktop') -Force | Out-Null
    Write-BillfishShortcut (Join-Path $Root 'UserDesktop\Billfish.lnk') (New-OriginShortcut) $true
    Write-BillfishShortcut (Join-Path $Root 'CommonDesktop\Billfish.lnk') (New-OriginShortcut) $true
    $unrelated=Join-Path $Root 'UserDesktop\Unrelated Important.lnk';Write-BillfishShortcut $unrelated (New-OriginShortcut) $true
    $startup=Join-Path $Root 'UserStartMenu\Programs\Startup\Billfish.lnk';Write-BillfishShortcut $startup (New-OriginShortcut) $true
    $context=Get-BillfishEntryPointContext $Root;Set-BillfishRunValue $context 'origin-run' $true
    $pointer=Join-Path $Root 'latest.txt';$backup=Join-Path $Root $BackupName
    [void](Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir $backup -FixtureRoot $Root -StatePointerPath $pointer)
    [pscustomobject]@{Root=$Root;Pointer=$pointer;Backup=$backup;Context=$context}
}

$fixture=Join-Path ([IO.Path]::GetTempPath()) ('billfish-transaction-security-'+[guid]::NewGuid().ToString('N'))
$rootAlias=$null
$crossVolumeStateRoot=$null
try {
    $repositoryRoot = Join-Path $fixture 'runtime'
    [void][IO.Directory]::CreateDirectory((Join-Path $repositoryRoot 'launcher'))
    [IO.File]::WriteAllText((Join-Path $repositoryRoot 'Billfish.exe'), 'fixture vendor')
    [IO.File]::WriteAllText((Join-Path $repositoryRoot 'launcher/Billfish.exe'), 'fixture v3 wrapper')
    $case=New-InstalledFixture (Join-Path $fixture 'manifest')
    $manifestPath=Join-Path $case.Backup 'manifest.json'
    $originalManifest=[IO.File]::ReadAllText($manifestPath)
    function Assert-MaliciousManifestRejected([scriptblock]$Mutate,[string]$Label){
        $manifest=$originalManifest|ConvertFrom-Json;&$Mutate $manifest
        Write-AtomicBillfishText $manifestPath (($manifest|ConvertTo-Json -Depth 10)+[Environment]::NewLine)
        $failed=$false;try{Restore-BillfishEntryPoints -BackupDir $case.Backup -BillfishRoot $repositoryRoot -FixtureRoot $case.Root -StatePointerPath $case.Pointer|Out-Null}catch{$failed=$true}
        Assert-True $failed "$Label manifest was accepted."
        Assert-True (@(Get-ChildItem $case.Backup -Directory -Filter '.restore-rollback-*').Count -eq 0) "$Label created rollback state before validation."
        Assert-True (Get-BillfishEntryPointStatus $repositoryRoot $case.Root).Healthy "$Label changed installed entry points."
        Write-AtomicBillfishText $manifestPath $originalManifest
    }
    Assert-MaliciousManifestRejected {param($m)$m.schemaVersion='3'} 'wrong-type schema'
    Assert-MaliciousManifestRejected {param($m)$m.billfishRoot='C:\WrongRoot'} 'wrong-root'
    Assert-MaliciousManifestRejected {param($m)$m.shortcuts[0].path=(Join-Path $case.Root 'UserDesktop\..\Outside\Billfish.lnk')} 'path-traversal'
    Assert-MaliciousManifestRejected {param($m)$m.shortcuts[0].backup='..\escape.lnk'} 'backup-traversal'

    $detachedPointer=Join-Path $case.Root 'detached-pointer.txt';$detachedFailed=$false
    try{Restore-BillfishEntryPoints -BackupDir $case.Backup -BillfishRoot $repositoryRoot -FixtureRoot $case.Root -StatePointerPath $detachedPointer|Out-Null}catch{$detachedFailed=$true}
    Assert-True $detachedFailed 'Detached manifest restore was accepted.'

    $manifest=$originalManifest|ConvertFrom-Json;$backupEntry=@($manifest.shortcuts|Where-Object existed)[0];$backupPath=Join-Path $case.Backup ([string]$backupEntry.backup);$backupBytes=[IO.File]::ReadAllBytes($backupPath)
    [IO.File]::WriteAllBytes($backupPath,([byte[]]($backupBytes+0x42)))
    $tamperedFailed=$false;try{Restore-BillfishEntryPoints -BackupDir $case.Backup -BillfishRoot $repositoryRoot -FixtureRoot $case.Root -StatePointerPath $case.Pointer|Out-Null}catch{$tamperedFailed=$true}
    Assert-True $tamperedFailed 'Tampered backup was accepted.';Assert-True (Get-BillfishEntryPointStatus $repositoryRoot $case.Root).Healthy 'Tampered backup check modified entry points.';[IO.File]::WriteAllBytes($backupPath,$backupBytes)

    Move-Item $case.Pointer ($case.Pointer+'.saved');$missingFailed=$false
    try{Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir (Join-Path $case.Root 'new-baseline') -FixtureRoot $case.Root -StatePointerPath $case.Pointer|Out-Null}catch{$missingFailed=$_.Exception.Message-match'missing'}
    Assert-True $missingFailed 'Healthy entry points without an active pointer were re-baselined.';Move-Item ($case.Pointer+'.saved') $case.Pointer
    $unrelatedPath=Join-Path $case.Root 'UserDesktop\Unrelated Important.lnk';$startupPath=Join-Path $case.Root 'UserStartMenu\Programs\Startup\Billfish.lnk'
    Assert-True ((Read-BillfishShortcut $unrelatedPath $true).Description-eq'Origin') 'Install rewrote Unrelated Important.lnk.'
    Assert-True ((Read-BillfishShortcut $startupPath $true).Description-eq'Origin') 'Install rewrote the Startup shortcut.'
    Assert-True (@(($originalManifest|ConvertFrom-Json).shortcuts|Where-Object{$_.path-eq$unrelatedPath-or$_.path-eq$startupPath}).Count-eq0) 'Manifest captured a shortcut outside the exact allowlist.'
    Assert-MaliciousManifestRejected {param($m)$m.shortcuts+=([pscustomobject]@{path=$unrelatedPath;existed=$false;backup=$null;backupSize=$null;backupSha256=$null})} 'unrelated-shortcut'
    Assert-MaliciousManifestRejected {param($m)$m.shortcuts+=([pscustomobject]@{path=$startupPath;existed=$false;backup=$null;backupSize=$null;backupSha256=$null})} 'startup-shortcut'
    Assert-MaliciousManifestRejected {param($m)$required=[IO.Path]::GetFullPath((Join-Path $case.Root 'UserDesktop\Billfish.lnk'));$m.shortcuts=@($m.shortcuts|Where-Object{-not([IO.Path]::GetFullPath([string]$_.path).Equals($required,[StringComparison]::OrdinalIgnoreCase))})} 'missing-required-shortcut'
    $deletedNonRequired=Join-Path $case.Root 'CommonDesktop\Billfish.lnk';Remove-Item -LiteralPath $deletedNonRequired -Force
    Restore-BillfishEntryPoints -BackupDir $case.Backup -BillfishRoot $repositoryRoot -FixtureRoot $case.Root -StatePointerPath $case.Pointer|Out-Null
    Assert-True (Test-Path -LiteralPath $deletedNonRequired -PathType Leaf) 'Restore depended on current discovery and did not recreate a deleted nonrequired shortcut.'
    Assert-True ((Read-BillfishShortcut $unrelatedPath $true).Description-eq'Origin') 'Restore rewrote Unrelated Important.lnk.'
    Assert-True ((Read-BillfishShortcut $startupPath $true).Description-eq'Origin') 'Restore rewrote the Startup shortcut.'

    $deniedOptionalRoot=Join-Path $fixture 'denied-optional';New-Item -ItemType Directory -Path (Join-Path $deniedOptionalRoot 'UserDesktop'),(Join-Path $deniedOptionalRoot 'CommonDesktop') -Force|Out-Null;Write-BillfishShortcut (Join-Path $deniedOptionalRoot 'UserDesktop\Billfish.lnk') (New-OriginShortcut) $true;$deniedOptionalPath=Join-Path $deniedOptionalRoot 'CommonDesktop\Billfish.lnk';Write-BillfishShortcut $deniedOptionalPath (New-OriginShortcut) $true
    $deniedOptionalPointer=Join-Path $deniedOptionalRoot 'latest.txt';$deniedOptionalBackup=Join-Path $deniedOptionalRoot 'backup';$deniedOptionalResult=Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir $deniedOptionalBackup -FixtureRoot $deniedOptionalRoot -StatePointerPath $deniedOptionalPointer -TestDeniedShortcutPaths $deniedOptionalPath
    Assert-True ($deniedOptionalResult.Outcome-eq'Installed'-and@($deniedOptionalResult.UnmanagedShortcuts).Count-eq1-and$deniedOptionalResult.UnmanagedShortcuts[0].Path-eq$deniedOptionalPath) 'Unwritable optional shortcut was not reported as unmanaged.'
    $deniedOptionalManifest=Get-Content -LiteralPath (Join-Path $deniedOptionalBackup 'manifest.json') -Raw|ConvertFrom-Json;Assert-True (@($deniedOptionalManifest.shortcuts|Where-Object{$_.path-eq$deniedOptionalPath}).Count-eq0) 'Unwritable optional shortcut entered the transaction manifest.';Assert-True ((Read-BillfishShortcut $deniedOptionalPath $true).Description-eq'Origin') 'Unwritable optional shortcut was mutated.'
    Restore-BillfishEntryPoints -BackupDir $deniedOptionalBackup -BillfishRoot $repositoryRoot -FixtureRoot $deniedOptionalRoot -StatePointerPath $deniedOptionalPointer|Out-Null

    $deniedRequiredRoot=Join-Path $fixture 'denied-required';New-Item -ItemType Directory -Path (Join-Path $deniedRequiredRoot 'UserDesktop') -Force|Out-Null;$deniedRequiredPath=Join-Path $deniedRequiredRoot 'UserDesktop\Billfish.lnk';Write-BillfishShortcut $deniedRequiredPath (New-OriginShortcut) $true;$deniedRequiredPointer=Join-Path $deniedRequiredRoot 'latest.txt';$deniedRequiredBackup=Join-Path $deniedRequiredRoot 'backup';$deniedRequiredFailed=$false
    try{Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir $deniedRequiredBackup -FixtureRoot $deniedRequiredRoot -StatePointerPath $deniedRequiredPointer -TestDeniedShortcutPaths $deniedRequiredPath|Out-Null}catch{$deniedRequiredFailed=$_.Exception.Message-match'Required shortcut destination failed mutation-access preflight'}
    Assert-True ($deniedRequiredFailed-and-not(Test-Path -LiteralPath $deniedRequiredBackup)-and-not(Test-Path -LiteralPath ($deniedRequiredPointer+'.pending.json'))) 'Required access denial was not rejected before transaction state was created.'

    $selectiveRoot=Join-Path $fixture 'selective-recovery';New-Item -ItemType Directory -Path (Join-Path $selectiveRoot 'UserDesktop'),(Join-Path $selectiveRoot 'CommonDesktop') -Force|Out-Null;Write-BillfishShortcut (Join-Path $selectiveRoot 'UserDesktop\Billfish.lnk') (New-OriginShortcut) $true;Write-BillfishShortcut (Join-Path $selectiveRoot 'CommonDesktop\Billfish.lnk') (New-OriginShortcut) $true;$selectivePointer=Join-Path $selectiveRoot 'latest.txt';$selectiveBackup=Join-Path $selectiveRoot 'backup'
    $selectivePending=Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir $selectiveBackup -FixtureRoot $selectiveRoot -StatePointerPath $selectivePointer -LeavePendingAfterShortcutIndex 1;Assert-True ($selectivePending.Outcome-eq'PendingSimulated') 'Selective recovery fixture did not leave pending state.'
    $selectiveJournal=Get-Content -LiteralPath ($selectivePointer+'.pending.json') -Raw|ConvertFrom-Json;$selectiveManifest=Read-BillfishManifestV3 $selectiveBackup;$selectiveValidated=Assert-BillfishEntryPointManifestV3 $selectiveManifest $selectiveBackup $repositoryRoot (Get-BillfishEntryPointContext $selectiveRoot) -AllowedState Prepared -VerifyBackupFingerprints
    Assert-True (@($selectiveJournal.shortcutMutationIntents).Count-eq1) 'Pending journal did not durably bound shortcut recovery to mutation intents.';$selectiveJournal.PSObject.Properties.Remove('shortcutMutationIntents');$legacyRecoveryEntries=@(Get-BillfishShortcutRecoveryEntries $selectiveJournal @($selectiveValidated.entries) (Get-BillfishEntryPointContext $selectiveRoot));Assert-True ($legacyRecoveryEntries.Count-eq1) 'Legacy pending fallback did not exclude origin-identical unmutated shortcuts.'
    $selectiveRecovered=Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir (Join-Path $selectiveRoot 'unused') -FixtureRoot $selectiveRoot -StatePointerPath $selectivePointer -RecoverOnly;Assert-True ($selectiveRecovered.Outcome-eq'Recovered') 'Selective pending recovery failed.'

    $optionalDrift=New-InstalledFixture (Join-Path $fixture 'optional-drift');$defaultStatusPointer=Join-Path $optionalDrift.Root 'latest-backup.txt';Move-Item -LiteralPath $optionalDrift.Pointer -Destination $defaultStatusPointer;$optionalDrift.Pointer=$defaultStatusPointer;$optionalDriftPath=Join-Path $optionalDrift.Root 'CommonDesktop\Billfish.lnk';[IO.File]::Delete($optionalDriftPath)
    $optionalStatus=Get-BillfishEntryPointStatus -BillfishRoot $repositoryRoot -FixtureRoot $optionalDrift.Root
    Assert-True (-not$optionalStatus.Healthy) 'Active manifest status ignored a deleted optional shortcut.'
    Assert-True (@($optionalStatus.Shortcuts|Where-Object{$_.Path.Equals($optionalDriftPath,[StringComparison]::OrdinalIgnoreCase)-and$_.Error-eq'Missing'}).Count-eq1) 'Active manifest entry set did not retain the deleted optional shortcut.'
    $optionalAlreadyInstalled=$false;$optionalDriftRejected=$false
    try{$optionalResult=Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir (Join-Path $optionalDrift.Root 'unused') -FixtureRoot $optionalDrift.Root -StatePointerPath $optionalDrift.Pointer;$optionalAlreadyInstalled=$optionalResult.Outcome-eq'AlreadyInstalled'}catch{$optionalDriftRejected=$_.Exception.Message-match'drifted'}
    Assert-True (-not$optionalAlreadyInstalled-and$optionalDriftRejected) 'Install returned AlreadyInstalled after an Active-manifest optional shortcut was deleted.'
    Restore-BillfishEntryPoints -BackupDir $optionalDrift.Backup -BillfishRoot $repositoryRoot -FixtureRoot $optionalDrift.Root -StatePointerPath $optionalDrift.Pointer|Out-Null

    $crossVolumePassed=$false
    $destinationVolume = [IO.Path]::GetPathRoot($fixture)
    $otherVolume = @(Get-PSDrive -PSProvider FileSystem | Where-Object { [IO.Directory]::Exists($_.Root) -and -not $_.Root.Equals($destinationVolume, [StringComparison]::OrdinalIgnoreCase) } | Select-Object -First 1)
    if ($otherVolume.Count -gt 0) {
        $crossFixture=Join-Path $fixture 'cross-volume-destination';$crossVolumeStateRoot=Join-Path $otherVolume[0].Root ('billfish-cross-volume-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path (Join-Path $crossFixture 'UserDesktop'),$crossVolumeStateRoot -Force|Out-Null
        $crossDestination=Join-Path $crossFixture 'UserDesktop\Billfish.lnk';Write-BillfishShortcut $crossDestination (New-OriginShortcut) $true
        $crossPointer=Join-Path $crossVolumeStateRoot 'latest.txt';$crossBackup=Join-Path $crossVolumeStateRoot 'backup'
        try{$crossResult=Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir $crossBackup -FixtureRoot $crossFixture -StatePointerPath $crossPointer}catch{throw "Cross-volume staging to destination failed; cross-volume placement must not use rename across volumes. $($_.Exception.Message)"}
        Assert-True ($crossResult.Outcome-eq'Installed') 'Cross-volume install did not complete.'
        Assert-True (-not [IO.Path]::GetPathRoot($crossBackup).Equals([IO.Path]::GetPathRoot($crossDestination), [StringComparison]::OrdinalIgnoreCase)) 'Cross-volume fixture used the same volume for staging and destination.'
        Restore-BillfishEntryPoints -BackupDir $crossBackup -BillfishRoot $repositoryRoot -FixtureRoot $crossFixture -StatePointerPath $crossPointer|Out-Null
        Assert-True ((Read-BillfishShortcut $crossDestination $true).Description-eq'Origin') 'Cross-volume restore did not restore the origin shortcut.'

        $crossPendingInstallBackup=Join-Path $crossVolumeStateRoot 'backup-pending-install';$crossPendingInstall=Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir $crossPendingInstallBackup -FixtureRoot $crossFixture -StatePointerPath $crossPointer -LeavePendingAfterShortcutIndex 1
        Assert-True ($crossPendingInstall.Outcome-eq'PendingSimulated') 'Cross-volume pending install simulation did not stop after destination placement.'
        $crossInstallRecovery=Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir (Join-Path $crossVolumeStateRoot 'unused-install-recovery') -FixtureRoot $crossFixture -StatePointerPath $crossPointer -RecoverOnly
        Assert-True ($crossInstallRecovery.Outcome-eq'Recovered'-and(Read-BillfishShortcut $crossDestination $true).Description-eq'Origin') 'Cross-volume pending install recovery did not restore origin.'

        $crossPendingRestoreBackup=Join-Path $crossVolumeStateRoot 'backup-pending-restore';[void](Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir $crossPendingRestoreBackup -FixtureRoot $crossFixture -StatePointerPath $crossPointer)
        $crossPendingRestore=Restore-BillfishEntryPoints -BackupDir $crossPendingRestoreBackup -BillfishRoot $repositoryRoot -FixtureRoot $crossFixture -StatePointerPath $crossPointer -LeavePendingAfterShortcutIndex 1
        Assert-True ($crossPendingRestore.Outcome-eq'PendingSimulated') 'Cross-volume pending restore simulation did not stop after destination placement.'
        $crossRestoreRecovery=Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir (Join-Path $crossVolumeStateRoot 'unused-restore-recovery') -FixtureRoot $crossFixture -StatePointerPath $crossPointer -RecoverOnly
        Assert-True ($crossRestoreRecovery.Outcome-eq'Recovered'-and(Get-BillfishEntryPointStatus -BillfishRoot $repositoryRoot -FixtureRoot $crossFixture -StatePointerPath $crossPointer).Healthy) 'Cross-volume pending restore recovery did not restore installed state.'
        Restore-BillfishEntryPoints -BackupDir $crossPendingRestoreBackup -BillfishRoot $repositoryRoot -FixtureRoot $crossFixture -StatePointerPath $crossPointer|Out-Null
        Remove-Item -LiteralPath $crossVolumeStateRoot -Recurse -Force;$crossVolumeStateRoot=$null;$crossVolumePassed=$true
    }

    $symlinkRoot=Join-Path $fixture 'destination-symlink';$outsideDirectory=Join-Path $symlinkRoot 'outside-target';New-Item -ItemType Directory -Path (Join-Path $symlinkRoot 'UserDesktop'),$outsideDirectory -Force|Out-Null
    $outsideMarker=Join-Path $outsideDirectory 'marker.txt';[IO.File]::WriteAllText($outsideMarker,'outside-marker')
    $destinationLink=Join-Path $symlinkRoot 'UserDesktop\Billfish.lnk';New-Item -ItemType Junction -Path $destinationLink -Target $outsideDirectory|Out-Null
    $outsideBefore=[IO.File]::ReadAllText($outsideMarker);$symlinkFailed=$false
    try{Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir (Join-Path $symlinkRoot 'backup') -FixtureRoot $symlinkRoot -StatePointerPath (Join-Path $symlinkRoot 'latest.txt')|Out-Null}catch{$symlinkFailed=$_.Exception.Message-match'reparse'}
    Assert-True $symlinkFailed 'Destination shortcut reparse leaf was accepted.';Assert-True ([IO.File]::ReadAllText($outsideMarker)-ceq$outsideBefore) 'Destination reparse test modified its external target.';[IO.Directory]::Delete($destinationLink)

    $ancestorRoot=Join-Path $fixture 'ancestor-race';$ancestorDesktop=Join-Path $ancestorRoot 'UserDesktop';$ancestorHeld=Join-Path $ancestorRoot 'UserDesktop-held';$ancestorOutside=Join-Path $ancestorRoot 'outside-target'
    New-Item -ItemType Directory -Path $ancestorDesktop,$ancestorOutside -Force|Out-Null;Write-BillfishShortcut (Join-Path $ancestorDesktop 'Billfish.lnk') (New-OriginShortcut) $true
    $ancestorMarker=Join-Path $ancestorOutside 'marker.txt';[IO.File]::WriteAllText($ancestorMarker,'ancestor-outside');$ancestorState=[pscustomobject]@{Done=$false}
    $ancestorHook={param($destination,$openedParent,$index,$phase);if(-not$ancestorState.Done-and$destination.Equals((Join-Path $ancestorDesktop 'Billfish.lnk'),[StringComparison]::OrdinalIgnoreCase)){[IO.Directory]::Move($ancestorDesktop,$ancestorHeld);New-Item -ItemType Junction -Path $ancestorDesktop -Target $ancestorOutside|Out-Null;$ancestorState.Done=$true}}
    $ancestorPointer=Join-Path $ancestorRoot 'latest.txt';$ancestorBackup=Join-Path $ancestorRoot 'backup';[void](Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir $ancestorBackup -FixtureRoot $ancestorRoot -StatePointerPath $ancestorPointer -TestBeforeShortcutMutation $ancestorHook)
    Assert-True $ancestorState.Done 'Ancestor coordination hook did not run after the parent handle was opened.'
    Assert-True ([IO.File]::ReadAllText($ancestorMarker)-ceq'ancestor-outside') 'Ancestor coordination attack modified the external marker.'
    Assert-True (-not(Test-Path -LiteralPath (Join-Path $ancestorOutside 'Billfish.lnk'))) 'Ancestor coordination attack redirected placement into the external directory.'
    Assert-True ((Read-BillfishShortcut (Join-Path $ancestorHeld 'Billfish.lnk') $true).Description-eq'Billfish') 'Handle-relative placement did not remain bound to the opened directory.'
    [IO.Directory]::Delete($ancestorDesktop);[IO.Directory]::Move($ancestorHeld,$ancestorDesktop);Restore-BillfishEntryPoints -BackupDir $ancestorBackup -BillfishRoot $repositoryRoot -FixtureRoot $ancestorRoot -StatePointerPath $ancestorPointer|Out-Null

    $leafRace=New-InstalledFixture (Join-Path $fixture 'leaf-race');$leafDestination=Join-Path $leafRace.Root 'UserDesktop\Billfish.lnk';$leafOutside=Join-Path $leafRace.Root 'outside-leaf';New-Item -ItemType Directory -Path $leafOutside|Out-Null;$leafMarker=Join-Path $leafOutside 'marker.txt';[IO.File]::WriteAllText($leafMarker,'leaf-outside');$leafState=[pscustomobject]@{Done=$false}
    $leafHook={param($destination,$openedParent,$index,$phase);if(-not$leafState.Done-and$destination.Equals($leafDestination,[StringComparison]::OrdinalIgnoreCase)){[IO.File]::Delete($leafDestination);New-Item -ItemType Junction -Path $leafDestination -Target $leafOutside|Out-Null;$leafState.Done=$true}}
    $leafFailed=$false;try{Restore-BillfishEntryPoints -BackupDir $leafRace.Backup -BillfishRoot $repositoryRoot -FixtureRoot $leafRace.Root -StatePointerPath $leafRace.Pointer -TestBeforeShortcutMutation $leafHook|Out-Null}catch{$leafFailed=$true}
    Assert-True $leafState.Done 'Leaf coordination hook did not run after the parent handle was opened.'
    Assert-True $leafFailed 'A directory reparse leaf was not rejected safely by handle-relative restore.'
    Assert-True ([IO.File]::ReadAllText($leafMarker)-ceq'leaf-outside') 'Leaf coordination attack modified the external target.'
    Assert-True (@(Get-ChildItem -LiteralPath (Split-Path -Parent $leafDestination) -File -Filter '.billfish-stage-*.tmp' -ErrorAction SilentlyContinue).Count-eq0) 'Failed handle-relative placement leaked a destination staging file.'
    [IO.Directory]::Delete($leafDestination);[void](Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir (Join-Path $leafRace.Root 'unused') -FixtureRoot $leafRace.Root -StatePointerPath $leafRace.Pointer -RecoverOnly)
    Assert-True ((Read-BillfishShortcut $leafDestination $true).Description-eq'Billfish') 'Pending recovery did not restore the installed shortcut after the safe leaf-race failure.'
    Restore-BillfishEntryPoints -BackupDir $leafRace.Backup -BillfishRoot $repositoryRoot -FixtureRoot $leafRace.Root -StatePointerPath $leafRace.Pointer|Out-Null

    $backupJunctionRoot=Join-Path $fixture 'backup-junction';New-Item -ItemType Directory -Path (Join-Path $backupJunctionRoot 'UserDesktop'),(Join-Path $backupJunctionRoot 'backup-target') -Force|Out-Null;Write-BillfishShortcut (Join-Path $backupJunctionRoot 'UserDesktop\Billfish.lnk') (New-OriginShortcut) $true
    $backupJunction=Join-Path $backupJunctionRoot 'backup';New-Item -ItemType Junction -Path $backupJunction -Target (Join-Path $backupJunctionRoot 'backup-target')|Out-Null;$junctionFailed=$false
    try{Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir $backupJunction -FixtureRoot $backupJunctionRoot -StatePointerPath (Join-Path $backupJunctionRoot 'latest.txt')|Out-Null}catch{$junctionFailed=$_.Exception.Message-match'reparse'}
    Assert-True $junctionFailed 'Reparse-point BackupDir was accepted.';[IO.Directory]::Delete($backupJunction)

    $pendingInstallRoot=Join-Path $fixture 'pending-install';New-Item -ItemType Directory -Path (Join-Path $pendingInstallRoot 'UserDesktop') -Force|Out-Null;Write-BillfishShortcut (Join-Path $pendingInstallRoot 'UserDesktop\Billfish.lnk') (New-OriginShortcut) $true
    $pendingInstallPointer=Join-Path $pendingInstallRoot 'latest.txt';$pendingInstallBackup=Join-Path $pendingInstallRoot 'backup'
    $pendingResult=Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir $pendingInstallBackup -FixtureRoot $pendingInstallRoot -StatePointerPath $pendingInstallPointer -LeavePendingAfterShortcutIndex 1
    Assert-True ($pendingResult.Outcome-eq'PendingSimulated'-and(Test-Path ($pendingInstallPointer+'.pending.json'))) 'Install termination simulation did not leave a journal.'
    $recovered=Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir (Join-Path $pendingInstallRoot 'unused') -FixtureRoot $pendingInstallRoot -StatePointerPath $pendingInstallPointer -RecoverOnly
    Assert-True ($recovered.Outcome-eq'Recovered') 'Pending install was not recovered.';Assert-True (-not(Test-Path ($pendingInstallPointer+'.pending.json'))) 'Pending install journal survived recovery.';Assert-True ((Read-BillfishShortcut (Join-Path $pendingInstallRoot 'UserDesktop\Billfish.lnk') $true).Description-eq'Origin') 'Pending install recovery did not restore origin.'

    $pendingRestore=New-InstalledFixture (Join-Path $fixture 'pending-restore')
    $restorePending=Restore-BillfishEntryPoints -BackupDir $pendingRestore.Backup -BillfishRoot $repositoryRoot -FixtureRoot $pendingRestore.Root -StatePointerPath $pendingRestore.Pointer -LeavePendingAfterShortcutIndex 1
    Assert-True ($restorePending.Outcome-eq'PendingSimulated') 'Restore termination simulation did not leave pending state.'
    $restoreRecovery=Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir (Join-Path $pendingRestore.Root 'unused') -FixtureRoot $pendingRestore.Root -StatePointerPath $pendingRestore.Pointer -RecoverOnly -TestLeaveRecoveryArtifacts
    Assert-True ($restoreRecovery.Outcome-eq'Recovered') 'Pending restore was not recovered.';Assert-True (-not(Test-Path ($pendingRestore.Pointer+'.pending.json'))) 'Recovery cleanup simulation retained the completion journal.';Assert-True (@(Get-ChildItem $pendingRestore.Backup -Directory -Filter '.restore-rollback-*').Count-gt0) 'Recovery cleanup simulation did not retain cleanup material.';Assert-True (Get-BillfishEntryPointStatus $repositoryRoot $pendingRestore.Root).Healthy 'Pending restore recovery did not restore installed state.'
    $secondRecovery=Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir (Join-Path $pendingRestore.Root 'unused-two') -FixtureRoot $pendingRestore.Root -StatePointerPath $pendingRestore.Pointer -RecoverOnly;Assert-True ($secondRecovery.Outcome-eq'NoPendingTransaction') 'A secondary cleanup crash reopened a completed transaction.'
    Restore-BillfishEntryPoints -BackupDir $pendingRestore.Backup -BillfishRoot $repositoryRoot -FixtureRoot $pendingRestore.Root -StatePointerPath $pendingRestore.Pointer|Out-Null

    $concurrentRoot=Join-Path $fixture 'concurrent';New-Item -ItemType Directory -Path (Join-Path $concurrentRoot 'UserDesktop') -Force|Out-Null;Write-BillfishShortcut (Join-Path $concurrentRoot 'UserDesktop\Billfish.lnk') (New-OriginShortcut) $true
    $concurrentPointer=Join-Path $concurrentRoot 'latest.txt';$out1=Join-Path $concurrentRoot 'one.json';$out2=Join-Path $concurrentRoot 'two.json';$worker=Join-Path $PSScriptRoot 'concurrent-install-worker.ps1';$rootAlias=Join-Path $fixture 'BillfishRootAlias';New-Item -ItemType Junction -Path $rootAlias -Target $repositoryRoot|Out-Null
    $p1=Start-Process powershell.exe -PassThru -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',$worker,'-RepositoryRoot',$repositoryRoot,'-FixtureRoot',$concurrentRoot,'-BackupDir',(Join-Path $concurrentRoot 'backup-one'),'-Pointer',$concurrentPointer,'-OutputPath',$out1,'-HoldMilliseconds','1000')
    Start-Sleep -Milliseconds 200
    $p2=Start-Process powershell.exe -PassThru -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',$worker,'-RepositoryRoot',$rootAlias,'-FixtureRoot',$concurrentRoot,'-BackupDir',(Join-Path $concurrentRoot 'backup-two'),'-Pointer',$concurrentPointer,'-OutputPath',$out2)
    $p1.WaitForExit();$p2.WaitForExit();$results=@((Get-Content $out1 -Raw|ConvertFrom-Json),(Get-Content $out2 -Raw|ConvertFrom-Json));$resultText=$results|ConvertTo-Json -Compress;Assert-True (@($results|Where-Object { $_.Outcome -eq 'Installed' }).Count -eq 1) "Concurrent install did not produce exactly one installer: $resultText";Assert-True (@($results|Where-Object { $_.Outcome -eq 'AlreadyInstalled' }).Count -eq 1) "Concurrent install did not serialize the second caller: $resultText"
    [IO.Directory]::Delete($rootAlias)
    $active=(Get-Content $concurrentPointer -TotalCount 1).Trim();Restore-BillfishEntryPoints -BackupDir $active -BillfishRoot $repositoryRoot -FixtureRoot $concurrentRoot -StatePointerPath $concurrentPointer|Out-Null

    $publicRoot=Join-Path $fixture 'public-restore-recovery';$publicPointer=Join-Path $publicRoot 'latest-backup.txt';New-Item -ItemType Directory -Path (Join-Path $publicRoot 'UserDesktop') -Force|Out-Null;Write-BillfishShortcut (Join-Path $publicRoot 'UserDesktop\Billfish.lnk') (New-OriginShortcut) $true;$publicBackup=Join-Path $publicRoot 'backup';[void](Install-BillfishEntryPoints -BillfishRoot $repositoryRoot -BackupDir $publicBackup -FixtureRoot $publicRoot -StatePointerPath $publicPointer)
    [void](Restore-BillfishEntryPoints -BackupDir $publicBackup -BillfishRoot $repositoryRoot -FixtureRoot $publicRoot -StatePointerPath $publicPointer -LeavePendingAfterShortcutIndex 1);Remove-Item -LiteralPath $publicPointer -Force
    & (Join-Path $patchRoot 'restore-user-shortcuts.ps1') -BillfishRoot $repositoryRoot -FixtureRoot $publicRoot|Out-Null
    Assert-True (-not(Test-Path ($publicPointer+'.pending.json'))-and-not(Test-Path $publicPointer)) 'Public restore did not recover a pointer-missing pending transaction before resolving BackupDir.';Assert-True ((Read-BillfishShortcut (Join-Path $publicRoot 'UserDesktop\Billfish.lnk') $true).Description-eq'Origin') 'Public restore recovery did not complete origin restore.'

    [pscustomobject]@{Outcome='Passed';MaliciousManifest=$true;ExactAllowlist=$true;AccessPreflight=$true;UnmanagedOptional=$true;SelectiveRecovery=$true;ActiveOptionalDrift=$true;CrossVolume=$crossVolumePassed;Traversal=$true;WrongRoot=$true;TamperedBackup=$true;PendingRecovery=$true;ConcurrentInstall=$true;MissingPointerGuard=$true;DestinationReparse=$true;AncestorRaceSafe=$true;LeafRaceSafe=$true;BackupReparse=$true;JunctionAliasMutex=$true;PublicRestoreRecovery=$true;CleanupCrashSafe=$true;DeletedShortcutRestore=$true}
}
finally{if($rootAlias-and(Test-Path -LiteralPath $rootAlias)){[IO.Directory]::Delete($rootAlias)};if($crossVolumeStateRoot-and(Test-Path -LiteralPath $crossVolumeStateRoot)){Remove-Item -LiteralPath $crossVolumeStateRoot -Recurse -Force};if(Test-Path $fixture){Remove-Item $fixture -Recurse -Force}}
