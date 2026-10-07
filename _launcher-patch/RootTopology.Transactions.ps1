Set-StrictMode -Version 3.0
. (Join-Path $PSScriptRoot 'Launcher.Core.ps1')
. (Join-Path $PSScriptRoot 'EntryPoints.Core.ps1')

function Get-BillfishRootTopologyPaths {
    param([string]$BillfishRoot)
    $root=Resolve-BillfishRootIdentity $BillfishRoot
    [pscustomobject]@{Root=$root;Wrapper=Join-Path $root 'Billfish.exe';Core=Join-Path $root 'Billfish.core.exe';LegacyWrapper=Join-Path $root 'launcher\Billfish.exe';StartScript=Join-Path $root 'start-billfish.ps1'}
}

function Open-BillfishTopologyParent {
    param([string]$Destination,[object]$Paths)
    $full=[IO.Path]::GetFullPath($Destination);if(-not(Test-BillfishPathHasPrefix $full $Paths.Root)){throw "Topology destination escapes BillfishRoot: $full"}
    $allowed=@($Paths.Wrapper,$Paths.Core,$Paths.LegacyWrapper);if(@($allowed|Where-Object{$_.Equals($full,[StringComparison]::OrdinalIgnoreCase)}).Count-ne1){throw "Topology destination is not fixed: $full"}
    $parent=Split-Path -Parent $full;if(-not(Test-Path -LiteralPath $parent -PathType Container)){New-Item -ItemType Directory -Path $parent -Force|Out-Null}
    $policy=New-BillfishTrustedRootPolicy $Paths.Root 'BillfishRoot topology';[void](Assert-BillfishPathWithinPolicy $full $policy -IncludeLeaf)
    $handle=[Billfish.Launcher.HandleNativeMethods]::OpenDirectoryForTopologyPlacement($parent)
    try{$opened=[IO.Path]::GetFullPath((ConvertFrom-BillfishDevicePath ([Billfish.Launcher.HandleNativeMethods]::GetFinalPath($handle)))).TrimEnd('\');$relative=$parent.Substring($policy.RawRoot.Length).TrimStart('\');$expected=$policy.FinalRoot;if($relative){$expected=Join-Path $expected $relative};if(-not$opened.Equals([IO.Path]::GetFullPath($expected).TrimEnd('\'),[StringComparison]::OrdinalIgnoreCase)){throw "Topology parent changed while opened: $parent"};[pscustomobject]@{Handle=$handle;Leaf=[IO.Path]::GetFileName($full)}}catch{$handle.Dispose();throw}
}

function Set-BillfishTopologyFile {
    param([string]$Source,[string]$Destination,[object]$Paths)
    $verified=Open-BillfishTopologyParent $Destination $Paths
    try{[Billfish.Launcher.HandleNativeMethods]::PlaceFile($Source,$verified.Handle,$verified.Leaf)}finally{$verified.Handle.Dispose()}
}

function Remove-BillfishTopologyFile {
    param([string]$Destination,[object]$Paths)
    if(-not(Test-Path -LiteralPath $Destination)){return}
    $verified=Open-BillfishTopologyParent $Destination $Paths
    try{[Billfish.Launcher.HandleNativeMethods]::ReplaceAndDeleteFile($verified.Handle,$verified.Leaf)}finally{$verified.Handle.Dispose()}
}

function Get-BillfishTopologyFingerprint {
    param([string]$Path)
    $fp=Get-BillfishEntryPointFileFingerprint $Path;[pscustomobject]@{size=[int64]$fp.size;sha256=[string]$fp.sha256}
}

function Test-BillfishTopologyFingerprint {
    param([string]$Path,[object]$Expected)
    if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){return $false};try{Test-BillfishEntryPointFingerprint (Get-BillfishTopologyFingerprint $Path) $Expected}catch{$false}
}

function Assert-BillfishTopologyNotRunning {
    param([object]$Paths,[string[]]$TestProcessPaths)
    $found=New-Object 'Collections.Generic.List[string]';$rootPrefix=$Paths.Root.TrimEnd('\')+'\'
    if($null-ne$TestProcessPaths){foreach($path in $TestProcessPaths){$full=[IO.Path]::GetFullPath($path);if($full.Equals($Paths.Wrapper,[StringComparison]::OrdinalIgnoreCase)-or$full.Equals($Paths.Core,[StringComparison]::OrdinalIgnoreCase)-or$full.Equals($Paths.LegacyWrapper,[StringComparison]::OrdinalIgnoreCase)-or($full.StartsWith($rootPrefix,[StringComparison]::OrdinalIgnoreCase)-and[IO.Path]::GetFileNameWithoutExtension($full)-like'PluginWorker*')){$found.Add($full)}}}
    else{foreach($process in @(Get-Process -Name 'Billfish','Billfish.core','PluginWorker','PluginWorker64' -ErrorAction SilentlyContinue)){try{$full=Get-LauncherProcessExecutablePath $process;if($full.StartsWith($rootPrefix,[StringComparison]::OrdinalIgnoreCase)){$found.Add($full)}}catch{throw}}}
    if($found.Count-gt0){throw "Billfish topology mutation is blocked by running processes: $($found -join ', ')"}
}

function Assert-BillfishTopologyFilesUnlocked {
    param([object]$Paths)
    foreach($path in @($Paths.Wrapper,$Paths.Core,$Paths.LegacyWrapper)){
        if(-not(Test-Path -LiteralPath $path -PathType Leaf)){continue}
        $stream=$null
        try{$stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
        catch{throw "Billfish topology mutation is blocked by a locked file: $path. $($_.Exception.Message)"}
        finally{if($stream){$stream.Dispose()}}
    }
}

function Copy-BillfishTopologySnapshot {
    param([object[]]$Entries,[object]$Context,[string]$Directory)
    $snap=@();foreach($entry in $Entries){$path=[string]$entry.path;$exists=Test-Path -LiteralPath $path -PathType Leaf;if($exists){$name=[guid]::NewGuid().ToString('N')+'.lnk';$copy=Join-Path $Directory $name;Copy-Item -LiteralPath $path -Destination $copy;$fp=Get-BillfishTopologyFingerprint $copy;$snap+=[ordered]@{path=$path;existed=$true;backup=$name;backupSize=$fp.size;backupSha256=$fp.sha256}}else{$snap+=[ordered]@{path=$path;existed=$false;backup=$null;backupSize=$null;backupSha256=$null}}};$snap
}

function Restore-BillfishTopologyEntrySnapshot {
    param([object[]]$Entries,[object]$Run,[object]$Context,[string]$Directory,[switch]$SkipRun)
    foreach($entry in $Entries){if(Test-BillfishShortcutMatchesSnapshotEntry $entry){continue};if([bool]$entry.existed){Invoke-BillfishHandleRelativeShortcutMutation -Destination ([string]$entry.path) -Context $Context -StagingDirectory $Directory -Source (Join-Path $Directory ([string]$entry.backup)) -ExpectedFingerprint ([pscustomobject]@{size=[int64]$entry.backupSize;sha256=[string]$entry.backupSha256})}else{Invoke-BillfishHandleRelativeShortcutMutation -Destination ([string]$entry.path) -Context $Context -StagingDirectory $Directory -Delete}}
    if(-not$SkipRun-and-not(Test-BillfishRunMatchesSnapshot $Context $Run)){Set-BillfishRunValue $Context ([string]$Run.value) ([bool]$Run.existed)}
}

function Get-BillfishTopologyBackupPath {
    param([string]$Directory,[string]$Leaf,[string]$ExpectedLeaf='')
    if([string]::IsNullOrWhiteSpace($Leaf)-or[IO.Path]::GetFileName($Leaf)-cne$Leaf){throw "Topology backup name is not a leaf: $Leaf"}
    if($ExpectedLeaf-and$Leaf-cne$ExpectedLeaf){throw "Topology backup name mismatch: expected $ExpectedLeaf, got $Leaf"}
    Join-Path ([IO.Path]::GetFullPath($Directory)) $Leaf
}

function Read-BillfishTopologyManifest {
    param([string]$BackupDir)
    $path=Join-Path ([IO.Path]::GetFullPath($BackupDir)) 'topology-manifest.json';if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw "Topology manifest missing: $path"};Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
}

function Assert-BillfishTopologyManifest {
    param([object]$Manifest,[string]$BackupDir,[object]$Paths,[object]$Context,[string[]]$AllowedStates)
    if([int]$Manifest.schemaVersion-ne4){throw 'Unsupported root topology manifest schema.'};if($AllowedStates-notcontains[string]$Manifest.state){throw "Topology manifest state is not allowed: $($Manifest.state)"};if(-not[IO.Path]::GetFullPath([string]$Manifest.billfishRoot).Equals($Paths.Root,[StringComparison]::OrdinalIgnoreCase)){throw 'Topology manifest root mismatch.'}
    $expectedNames=@{rootVendor='root-vendor.exe';legacyWrapper='legacy-wrapper.exe';rootWrapper='root-wrapper.exe'}
    foreach($name in @('rootVendor','legacyWrapper','rootWrapper')){$record=$Manifest.$name;$source=Get-BillfishTopologyBackupPath $BackupDir ([string]$record.backup) $expectedNames[$name];if(-not(Test-BillfishTopologyFingerprint $source $record)){throw "Topology backup fingerprint mismatch: $name"}}
    Assert-BillfishAllowedShortcutSet @($Manifest.v3Installed.shortcuts) $Context 'v3 installed snapshot';foreach($entry in @($Manifest.v3Installed.shortcuts)){if([bool]$entry.existed){$source=Get-BillfishTopologyBackupPath $BackupDir ([string]$entry.backup);$expected=[pscustomobject]@{size=[int64]$entry.backupSize;sha256=[string]$entry.backupSha256};if(-not(Test-BillfishTopologyFingerprint $source $expected)){throw "Topology entry backup fingerprint mismatch: $source"}}}
    $Manifest
}

function Assert-BillfishTopologyPendingJournal {
    param([object]$Journal,[object]$Manifest,[string]$BackupDir,[object]$Paths)
    if([int]$Journal.schemaVersion-ne1){throw 'Unsupported topology pending journal schema.'}
    if([string]$Journal.transactionId-cne[string]$Manifest.transactionId){throw 'Topology pending transaction mismatch.'}
    if(-not[IO.Path]::GetFullPath([string]$Journal.billfishRoot).Equals($Paths.Root,[StringComparison]::OrdinalIgnoreCase)){throw 'Topology pending root mismatch.'}
    if(-not[IO.Path]::GetFullPath([string]$Journal.backupDir).Equals([IO.Path]::GetFullPath($BackupDir),[StringComparison]::OrdinalIgnoreCase)){throw 'Topology pending backup mismatch.'}
    $operation=[string]$Journal.operation;if(@('Install','Restore')-notcontains$operation){throw 'Unknown topology pending operation.'}
    $allowed=if($operation-eq'Install'){@('Core','RootWrapper','Entries','Run','LegacyWrapper','Pointer')}else{@('RootVendor','LegacyWrapper','Core','Entries','Run','Pointer')}
    $intents=@($Journal.phaseIntents);for($index=0;$index-lt$intents.Count;$index++){if([string]$intents[$index]-cne$allowed[$index]){throw 'Topology pending phase intents are not an ordered prefix.'}}
    $operation
}

function Assert-BillfishTopologyRollbackSnapshot {
    param([object]$Snapshot,[string]$RollbackDir,[string]$BackupDir,[object]$Context)
    $policy=New-BillfishTrustedRootPolicy ([IO.Path]::GetFullPath($BackupDir)) 'topology backup root'
    [void](Assert-BillfishPathWithinPolicy $RollbackDir $policy -IncludeLeaf)
    if(-not([IO.Path]::GetFileName($RollbackDir)-like'.topology-restore-*')){throw 'Topology rollback directory name is invalid.'}
    foreach($name in @('wrapper','core')){$expectedLeaf=if($name-eq'wrapper'){'wrapper.exe'}else{'core.exe'};$record=$Snapshot.$name;$source=Get-BillfishTopologyBackupPath $RollbackDir ([string]$record.backup) $expectedLeaf;if(-not(Test-BillfishTopologyFingerprint $source $record)){throw "Topology rollback fingerprint mismatch: $name"}}
    Assert-BillfishAllowedShortcutSet @($Snapshot.entries) $Context 'v4 rollback snapshot'
    foreach($entry in @($Snapshot.entries)){if([bool]$entry.existed){$source=Get-BillfishTopologyBackupPath $RollbackDir ([string]$entry.backup);$expected=[pscustomobject]@{size=[int64]$entry.backupSize;sha256=[string]$entry.backupSha256};if(-not(Test-BillfishTopologyFingerprint $source $expected)){throw "Topology rollback entry fingerprint mismatch: $source"}}}
    $Snapshot
}

function Write-BillfishTopologyManifestState {
    param([string]$BackupDir,[string]$State)
    $manifest=Read-BillfishTopologyManifest $BackupDir;$manifest.state=$State;Write-AtomicBillfishText (Join-Path $BackupDir 'topology-manifest.json') (($manifest|ConvertTo-Json -Depth 12)+[Environment]::NewLine)
}

function Get-BillfishActiveTopologyBackup {
    param([string]$Pointer)
    if(-not(Test-Path -LiteralPath $Pointer -PathType Leaf)){return $null};$value=(Get-Content -LiteralPath $Pointer -TotalCount 1).Trim();if(-not$value){throw 'Topology pointer is empty.'};[IO.Path]::GetFullPath($value)
}

function Test-BillfishTopologyPointerEquals {
    param([string]$Pointer,[string]$BackupDir)
    if(-not(Test-Path -LiteralPath $Pointer -PathType Leaf)){return $false}
    try{$value=(Get-Content -LiteralPath $Pointer -TotalCount 1).Trim();if(-not$value){return $false};[IO.Path]::GetFullPath($value).Equals([IO.Path]::GetFullPath($BackupDir),[StringComparison]::OrdinalIgnoreCase)}catch{$false}
}

function Restore-BillfishTopologyPointerSnapshot {
    param([string]$Pointer,[object]$Snapshot,[string]$OwnedBackupDir)
    if(-not(Test-BillfishTopologyPointerEquals $Pointer $OwnedBackupDir)){return}
    if([bool]$Snapshot.existed){Write-AtomicBillfishText $Pointer ([string]$Snapshot.value+[Environment]::NewLine);return}
    $tombstone=$Pointer+'.rollback-'+[guid]::NewGuid().ToString('N');Move-Item -LiteralPath $Pointer -Destination $tombstone;Remove-Item -LiteralPath $tombstone -Force
}

function Test-BillfishInstalledV4Topology {
    param([object]$Manifest,[object]$Paths,[object]$Context)
    if(-not(Test-BillfishTopologyFingerprint $Paths.Wrapper $Manifest.rootWrapper)){return $false}
    if(-not(Test-BillfishTopologyFingerprint $Paths.Core $Manifest.rootVendor)){return $false}
    if(Test-Path -LiteralPath $Paths.LegacyWrapper){return $false}
    $fixtureRoot=if($Context.Fixture){Split-Path -Parent $Context.RunStore}else{$null};$entryStatus=Get-BillfishEntryPointStatus -BillfishRoot $Paths.Root -FixtureRoot $fixtureRoot -ValidatedEntries @($Manifest.v3Installed.shortcuts) -ExpectedLauncherPath $Paths.Wrapper -ExpectedIconPath $Paths.Core
    [bool]$entryStatus.Healthy
}

function Repair-BillfishPendingRootTopology {
    param([string]$Pointer,[object]$Paths,[object]$Context)
    $pending=$Pointer+'.pending.json';if(-not(Test-Path -LiteralPath $pending -PathType Leaf)){return [pscustomobject]@{Recovered=$false}}
    $journal=Get-Content -LiteralPath $pending -Raw|ConvertFrom-Json;$backup=[IO.Path]::GetFullPath([string]$journal.backupDir);[void](Assert-BillfishTransactionStoragePaths $Pointer $backup);$manifest=Assert-BillfishTopologyManifest (Read-BillfishTopologyManifest $backup) $backup $Paths $Context @('Prepared','Active','Restored','RolledBack');$operation=Assert-BillfishTopologyPendingJournal $journal $manifest $backup $Paths
    if($operation-eq'Install'){
        if((Test-BillfishTopologyPointerEquals $Pointer $backup)-and[string]$manifest.state-eq'Active'-and(Test-BillfishInstalledV4Topology $manifest $Paths $Context)){Remove-Item -LiteralPath $pending -Force;return [pscustomobject]@{Recovered=$true;Operation='InstallCommit';BackupDir=$backup}}
        if(-not(Test-BillfishTopologyFingerprint $Paths.Wrapper $manifest.rootVendor)){Set-BillfishTopologyFile (Join-Path $backup $manifest.rootVendor.backup) $Paths.Wrapper $Paths}
        if(-not(Test-BillfishTopologyFingerprint $Paths.LegacyWrapper $manifest.legacyWrapper)){Set-BillfishTopologyFile (Join-Path $backup $manifest.legacyWrapper.backup) $Paths.LegacyWrapper $Paths}
        Remove-BillfishTopologyFile $Paths.Core $Paths;Restore-BillfishTopologyEntrySnapshot @($manifest.v3Installed.shortcuts) $manifest.v3Installed.run $Context $backup;Restore-BillfishTopologyPointerSnapshot $Pointer $journal.pointerBefore $backup;Write-BillfishTopologyManifestState $backup 'RolledBack'
    }elseif($operation-eq'Restore'){
        $rollback=[IO.Path]::GetFullPath([string]$journal.rollbackDir);$snapshot=Get-Content -LiteralPath (Join-Path $rollback 'snapshot.json') -Raw|ConvertFrom-Json;[void](Assert-BillfishTopologyRollbackSnapshot $snapshot $rollback $backup $Context)
        Set-BillfishTopologyFile (Join-Path $rollback $snapshot.wrapper.backup) $Paths.Wrapper $Paths;Set-BillfishTopologyFile (Join-Path $rollback $snapshot.core.backup) $Paths.Core $Paths;Remove-BillfishTopologyFile $Paths.LegacyWrapper $Paths;Restore-BillfishTopologyEntrySnapshot @($snapshot.entries) $snapshot.run $Context $rollback;Write-BillfishTopologyManifestState $backup 'Active';Write-AtomicBillfishText $Pointer ($backup+[Environment]::NewLine)
    }else{throw 'Unknown topology pending operation.'}
    Remove-Item -LiteralPath $pending -Force;[pscustomobject]@{Recovered=$true;Operation=$operation;BackupDir=$backup}
}

function Install-BillfishRootTopology {
    [CmdletBinding(PositionalBinding=$false)]param([Parameter(Mandatory=$true)][string]$BillfishRoot,[Parameter(Mandatory=$true)][string]$WrapperStagingPath,[Parameter(Mandatory=$true)][string]$BackupDir,[Parameter(Mandatory=$true)][string]$V3StatePointerPath,[Parameter(Mandatory=$true)][string]$V4StatePointerPath,[string]$FixtureRoot,[switch]$DryRun,[switch]$RecoverOnly,[ValidateSet('','Core','RootWrapper','Entries','Run','LegacyWrapper','Pointer')][string]$FailPhase='',[string]$LeavePendingAfterPhase='',[string[]]$TestProcessPaths)
    $paths=Get-BillfishRootTopologyPaths $BillfishRoot;$entryMutex=Enter-BillfishEntryPointTransactionMutex $paths.Root;$launchMutex=$null
    try{$launchMutex=Enter-BillfishLaunchMutex $paths.Root;Assert-BillfishTopologyNotRunning $paths $TestProcessPaths;Assert-BillfishTopologyFilesUnlocked $paths;[void](Assert-BillfishTransactionStoragePaths $V4StatePointerPath $BackupDir);$context=Get-BillfishEntryPointContext $FixtureRoot;$recovery=Repair-BillfishPendingRootTopology $V4StatePointerPath $paths $context;if($RecoverOnly){return [pscustomobject]@{Outcome=if($recovery.Recovered){'Recovered'}else{'NoPendingTransaction'}}}
        $active=Get-BillfishActiveTopologyBackup $V4StatePointerPath;if($active){$manifest=Assert-BillfishTopologyManifest (Read-BillfishTopologyManifest $active) $active $paths $context @('Active');$entryStatus=Get-BillfishEntryPointStatus -BillfishRoot $paths.Root -FixtureRoot $FixtureRoot -ValidatedEntries @($manifest.v3Installed.shortcuts) -ExpectedLauncherPath $paths.Wrapper -ExpectedIconPath $paths.Core;if((Test-BillfishTopologyFingerprint $paths.Wrapper $manifest.rootWrapper)-and(Test-BillfishTopologyFingerprint $paths.Core $manifest.rootVendor)-and-not(Test-Path -LiteralPath $paths.LegacyWrapper)-and$entryStatus.Healthy){return [pscustomobject]@{Outcome='AlreadyInstalled';BackupDir=$active}};throw 'Active v4 topology has drifted.'}
        foreach($required in @($paths.Wrapper,$paths.LegacyWrapper,$WrapperStagingPath,$paths.StartScript,$V3StatePointerPath)){if(-not(Test-Path -LiteralPath $required -PathType Leaf)){throw "Required v3 topology input missing: $required"}}
        if(Test-Path -LiteralPath $paths.Core){throw 'Unexpected Billfish.core.exe exists outside an active or recoverable v4 transaction.'}
        if(-not$FixtureRoot){$metadata=(Get-Item -LiteralPath $WrapperStagingPath).VersionInfo;if([string]$metadata.ProductName-ne'Billfish'-or[string]$metadata.FileDescription-ne'Billfish'-or[string]$metadata.OriginalFilename-ne'Billfish.dll'){throw 'Staged root wrapper metadata is invalid.'}}
        $v3Backup=Get-ActiveBillfishEntryPointBackup $V3StatePointerPath;$v3Manifest=Read-BillfishManifestV3 $v3Backup;$v3=Assert-BillfishEntryPointManifestV3 $v3Manifest $v3Backup $paths.Root $context -AllowedState Active -VerifyBackupFingerprints;$v3Status=Get-BillfishEntryPointStatus -BillfishRoot $paths.Root -FixtureRoot $FixtureRoot -ValidatedEntries @($v3.entries);if(-not$v3Status.Healthy){throw 'v3 installed entry state is not healthy.'}
        if($DryRun){return [pscustomobject]@{Outcome='WouldInstall';ManagedEntries=$v3.entries.Count;BackupDir=$BackupDir}}
        if(Test-Path -LiteralPath $BackupDir){throw "Topology BackupDir already exists: $BackupDir"};New-Item -ItemType Directory -Path $BackupDir|Out-Null
        Copy-Item $paths.Wrapper (Join-Path $BackupDir 'root-vendor.exe');Copy-Item $paths.LegacyWrapper (Join-Path $BackupDir 'legacy-wrapper.exe');Copy-Item $WrapperStagingPath (Join-Path $BackupDir 'root-wrapper.exe')
        $entries=Copy-BillfishTopologySnapshot @($v3.entries) $context $BackupDir;$run=Get-BillfishRunValue $context;$vendorFp=Get-BillfishTopologyFingerprint (Join-Path $BackupDir 'root-vendor.exe');$legacyFp=Get-BillfishTopologyFingerprint (Join-Path $BackupDir 'legacy-wrapper.exe');$wrapperFp=Get-BillfishTopologyFingerprint (Join-Path $BackupDir 'root-wrapper.exe');$id=[guid]::NewGuid().ToString('N')
        $manifest=[ordered]@{schemaVersion=4;transactionId=$id;state='Prepared';billfishRoot=$paths.Root;v3BackupDir=$v3Backup;rootVendor=[ordered]@{backup='root-vendor.exe';size=$vendorFp.size;sha256=$vendorFp.sha256};legacyWrapper=[ordered]@{backup='legacy-wrapper.exe';size=$legacyFp.size;sha256=$legacyFp.sha256};rootWrapper=[ordered]@{backup='root-wrapper.exe';size=$wrapperFp.size;sha256=$wrapperFp.sha256};v3Installed=[ordered]@{run=[ordered]@{existed=$run.Exists;value=$run.Value};shortcuts=$entries}}
        Write-AtomicBillfishText (Join-Path $BackupDir 'topology-manifest.json') (($manifest|ConvertTo-Json -Depth 12)+[Environment]::NewLine);$journal=[ordered]@{schemaVersion=1;transactionId=$id;operation='Install';billfishRoot=$paths.Root;backupDir=[IO.Path]::GetFullPath($BackupDir);pointerBefore=[ordered]@{existed=$false;value=$null};phaseIntents=@()};Write-AtomicBillfishText ($V4StatePointerPath+'.pending.json') (($journal|ConvertTo-Json -Depth 8)+[Environment]::NewLine)
        try{$desired=[pscustomobject]@{TargetPath=$paths.Wrapper;Arguments='';WorkingDirectory=$paths.Root;IconLocation="$($paths.Core),0";Description='Billfish';WindowStyle=7};foreach($phase in @('Core','RootWrapper','Entries','Run','LegacyWrapper','Pointer')){$journal.phaseIntents=@($journal.phaseIntents)+$phase;Write-AtomicBillfishText ($V4StatePointerPath+'.pending.json') (($journal|ConvertTo-Json -Depth 8)+[Environment]::NewLine);if($FailPhase-eq$phase){throw "Injected topology install failure: $phase"};switch($phase){'Core'{Set-BillfishTopologyFile (Join-Path $BackupDir 'root-vendor.exe') $paths.Core $paths};'RootWrapper'{Set-BillfishTopologyFile (Join-Path $BackupDir 'root-wrapper.exe') $paths.Wrapper $paths};'Entries'{foreach($entry in @($v3.entries)){Set-BillfishShortcutHandleRelative ([string]$entry.path) $desired $context $BackupDir}};'Run'{Set-BillfishRunValue $context ('"'+$paths.Wrapper+'" -a=auto') $true};'LegacyWrapper'{Remove-BillfishTopologyFile $paths.LegacyWrapper $paths};'Pointer'{Write-BillfishTopologyManifestState $BackupDir 'Active';$activeManifest=Assert-BillfishTopologyManifest (Read-BillfishTopologyManifest $BackupDir) $BackupDir $paths $context @('Active');if(-not(Test-BillfishInstalledV4Topology $activeManifest $paths $context)){throw 'v4 topology validation failed before pointer commit.'};Write-AtomicBillfishText $V4StatePointerPath ([IO.Path]::GetFullPath($BackupDir)+[Environment]::NewLine)}};if($LeavePendingAfterPhase-eq$phase){return [pscustomobject]@{Outcome='PendingSimulated';Phase=$phase}}};Remove-Item ($V4StatePointerPath+'.pending.json') -Force}catch{$failure=$_;try{[void](Repair-BillfishPendingRootTopology $V4StatePointerPath $paths $context)}catch{throw "Topology install failed and recovery is INCOMPLETE. Original: $($failure.Exception.Message) Recovery: $($_.Exception.Message)"};throw "Topology install failed and v3 was recovered: $($failure.Exception.Message)"}
        [pscustomobject]@{Outcome='Installed';BackupDir=[IO.Path]::GetFullPath($BackupDir);ManagedEntries=$v3.entries.Count}
    }finally{if($launchMutex){Exit-BillfishLaunchMutex $launchMutex};Exit-BillfishEntryPointTransactionMutex $entryMutex}
}

function Restore-BillfishRootTopology {
    [CmdletBinding(PositionalBinding=$false)]param([Parameter(Mandatory=$true)][string]$BillfishRoot,[Parameter(Mandatory=$true)][string]$BackupDir,[Parameter(Mandatory=$true)][string]$V4StatePointerPath,[string]$FixtureRoot,[switch]$DryRun,[ValidateSet('','RootVendor','LegacyWrapper','Core','Entries','Run','Pointer')][string]$FailPhase='',[string]$LeavePendingAfterPhase='',[string[]]$TestProcessPaths)
    $paths=Get-BillfishRootTopologyPaths $BillfishRoot;$entryMutex=Enter-BillfishEntryPointTransactionMutex $paths.Root;$launchMutex=$null
    try{$launchMutex=Enter-BillfishLaunchMutex $paths.Root;Assert-BillfishTopologyNotRunning $paths $TestProcessPaths;Assert-BillfishTopologyFilesUnlocked $paths;[void](Assert-BillfishTransactionStoragePaths $V4StatePointerPath $BackupDir);$context=Get-BillfishEntryPointContext $FixtureRoot;[void](Repair-BillfishPendingRootTopology $V4StatePointerPath $paths $context);$active=Get-BillfishActiveTopologyBackup $V4StatePointerPath;if($null-eq$active-or-not$active.Equals([IO.Path]::GetFullPath($BackupDir),[StringComparison]::OrdinalIgnoreCase)){throw 'v4 restore is detached from active pointer.'};$manifest=Assert-BillfishTopologyManifest (Read-BillfishTopologyManifest $BackupDir) $BackupDir $paths $context @('Active');if($DryRun){return [pscustomobject]@{Outcome='WouldRestore'}}
        $rollback=Join-Path $BackupDir ('.topology-restore-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory $rollback|Out-Null;Copy-Item $paths.Wrapper (Join-Path $rollback 'wrapper.exe');Copy-Item $paths.Core (Join-Path $rollback 'core.exe');$entries=Copy-BillfishTopologySnapshot @($manifest.v3Installed.shortcuts) $context $rollback;$run=Get-BillfishRunValue $context;$wf=Get-BillfishTopologyFingerprint (Join-Path $rollback 'wrapper.exe');$cf=Get-BillfishTopologyFingerprint (Join-Path $rollback 'core.exe');$snapshot=[ordered]@{wrapper=[ordered]@{backup='wrapper.exe';size=$wf.size;sha256=$wf.sha256};core=[ordered]@{backup='core.exe';size=$cf.size;sha256=$cf.sha256};entries=$entries;run=[ordered]@{existed=$run.Exists;value=$run.Value}};Write-AtomicBillfishText (Join-Path $rollback 'snapshot.json') (($snapshot|ConvertTo-Json -Depth 10)+[Environment]::NewLine);$journal=[ordered]@{schemaVersion=1;transactionId=$manifest.transactionId;operation='Restore';billfishRoot=$paths.Root;backupDir=[IO.Path]::GetFullPath($BackupDir);rollbackDir=$rollback;phaseIntents=@()};Write-AtomicBillfishText ($V4StatePointerPath+'.pending.json') (($journal|ConvertTo-Json -Depth 8)+[Environment]::NewLine)
        try{foreach($phase in @('RootVendor','LegacyWrapper','Core','Entries','Run','Pointer')){$journal.phaseIntents=@($journal.phaseIntents)+$phase;Write-AtomicBillfishText ($V4StatePointerPath+'.pending.json') (($journal|ConvertTo-Json -Depth 8)+[Environment]::NewLine);if($FailPhase-eq$phase){throw "Injected topology restore failure: $phase"};switch($phase){'RootVendor'{Set-BillfishTopologyFile (Join-Path $BackupDir $manifest.rootVendor.backup) $paths.Wrapper $paths};'LegacyWrapper'{Set-BillfishTopologyFile (Join-Path $BackupDir $manifest.legacyWrapper.backup) $paths.LegacyWrapper $paths};'Core'{Remove-BillfishTopologyFile $paths.Core $paths};'Entries'{Restore-BillfishTopologyEntrySnapshot @($manifest.v3Installed.shortcuts) $manifest.v3Installed.run $context $BackupDir -SkipRun};'Run'{if(-not(Test-BillfishRunMatchesSnapshot $context $manifest.v3Installed.run)){Set-BillfishRunValue $context ([string]$manifest.v3Installed.run.value) ([bool]$manifest.v3Installed.run.existed)}};'Pointer'{Write-BillfishTopologyManifestState $BackupDir 'Restored';Remove-Item $V4StatePointerPath -Force}};if($LeavePendingAfterPhase-eq$phase){return [pscustomobject]@{Outcome='PendingSimulated';Phase=$phase}}};Remove-Item ($V4StatePointerPath+'.pending.json') -Force;Remove-Item $rollback -Recurse -Force -ErrorAction SilentlyContinue}catch{$failure=$_;try{[void](Repair-BillfishPendingRootTopology $V4StatePointerPath $paths $context)}catch{throw "Topology restore failed and recovery is INCOMPLETE. Original: $($failure.Exception.Message) Recovery: $($_.Exception.Message)"};throw "Topology restore failed and v4 was recovered: $($failure.Exception.Message)"};[pscustomobject]@{Outcome='Restored';BackupDir=[IO.Path]::GetFullPath($BackupDir)}
    }finally{if($launchMutex){Exit-BillfishLaunchMutex $launchMutex};Exit-BillfishEntryPointTransactionMutex $entryMutex}
}
