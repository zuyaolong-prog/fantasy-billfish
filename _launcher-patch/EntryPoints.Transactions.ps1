Set-StrictMode -Version 3.0
. (Join-Path $PSScriptRoot 'EntryPoints.PathSecurity.ps1')

function Get-BillfishEntryPointFileFingerprint {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)
    $item = Get-Item -LiteralPath $LiteralPath -ErrorAction Stop
    if ($item.PSIsContainer -or (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)) { throw "Expected an ordinary file: $LiteralPath" }
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($item.FullName)
    try { $hash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') }
    finally { $stream.Dispose(); $sha.Dispose() }
    [pscustomobject]@{ size = [int64]$item.Length; sha256 = $hash }
}

function Test-BillfishEntryPointFingerprint {
    param([object]$Actual, [object]$Expected)
    [int64]$Actual.size -eq [int64]$Expected.size -and ([string]$Actual.sha256).Equals([string]$Expected.sha256, [System.StringComparison]::OrdinalIgnoreCase)
}

function New-BillfishStagedShortcutFile {
    param([Parameter(Mandatory = $true)][string]$StagingDirectory,[Parameter(Mandatory = $true)][object]$Value,[Parameter(Mandatory = $true)][bool]$Fixture)
    $path=Join-Path ([IO.Path]::GetFullPath($StagingDirectory)) ('.shortcut-stage-'+[guid]::NewGuid().ToString('N')+'.lnk')
    if($Fixture){[IO.File]::WriteAllText($path,(($Value|ConvertTo-Json -Depth 4)+[Environment]::NewLine),(New-Object Text.UTF8Encoding($false)))}
    else{
        $shell=New-Object -ComObject WScript.Shell;$shortcut=$shell.CreateShortcut($path)
        $shortcut.TargetPath=$Value.TargetPath;$shortcut.Arguments=$Value.Arguments;$shortcut.WorkingDirectory=$Value.WorkingDirectory;$shortcut.IconLocation=$Value.IconLocation;$shortcut.Description=$Value.Description;$shortcut.WindowStyle=$Value.WindowStyle;$shortcut.Save()
    }
    [void](Get-BillfishEntryPointFileFingerprint $path)
    $path
}

function New-BillfishStagedShortcutCopy {
    param([Parameter(Mandatory = $true)][string]$Source,[Parameter(Mandatory = $true)][string]$StagingDirectory,[object]$ExpectedFingerprint)
    $path=Join-Path ([IO.Path]::GetFullPath($StagingDirectory)) ('.shortcut-stage-'+[guid]::NewGuid().ToString('N')+'.lnk')
    try{
        Copy-Item -LiteralPath $Source -Destination $path -ErrorAction Stop
        $actual=Get-BillfishEntryPointFileFingerprint $path
        if($null-ne$ExpectedFingerprint-and-not(Test-BillfishEntryPointFingerprint $actual $ExpectedFingerprint)){throw "Staged shortcut fingerprint does not match its validated backup: $Source"}
        $path
    }catch{if(Test-Path -LiteralPath $path){[IO.File]::Delete($path)};throw}
}

function Invoke-BillfishHandleRelativeShortcutMutation {
    param(
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][object]$Context,
        [Parameter(Mandatory = $true)][string]$StagingDirectory,
        [string]$Source,
        [object]$ExpectedFingerprint,
        [switch]$Delete,
        [scriptblock]$TestBeforeMutation,
        [int]$Index=0,
        [string]$Phase='Mutation'
    )
    $staged=$null
    if(-not$Delete){$staged=New-BillfishStagedShortcutCopy $Source $StagingDirectory $ExpectedFingerprint}
    $verified=$null
    try{
        $verified=Open-BillfishVerifiedShortcutParent $Destination $Context -AllowCreateParent
        if($null-ne$TestBeforeMutation){&$TestBeforeMutation $verified.Destination $verified.OpenedParent $Index $Phase}
        if($Delete){[Billfish.Launcher.HandleNativeMethods]::ReplaceAndDeleteFile($verified.Handle,$verified.Leaf)}
        else{[Billfish.Launcher.HandleNativeMethods]::PlaceFile($staged,$verified.Handle,$verified.Leaf)}
    }
    finally{
        if($null-ne$verified){$verified.Handle.Dispose()}
        if($null-ne$staged-and(Test-Path -LiteralPath $staged)){[IO.File]::Delete($staged)}
    }
}

function Set-BillfishShortcutHandleRelative {
    param([string]$Destination,[object]$Value,[object]$Context,[string]$StagingDirectory,[scriptblock]$TestBeforeMutation,[int]$Index=0,[string]$Phase='Install')
    $staged=New-BillfishStagedShortcutFile $StagingDirectory $Value $Context.Fixture
    try{
        $verified=Open-BillfishVerifiedShortcutParent $Destination $Context -AllowCreateParent
        try{
            if($null-ne$TestBeforeMutation){&$TestBeforeMutation $verified.Destination $verified.OpenedParent $Index $Phase}
            [Billfish.Launcher.HandleNativeMethods]::PlaceFile($staged,$verified.Handle,$verified.Leaf)
        }finally{$verified.Handle.Dispose()}
    }finally{if(Test-Path -LiteralPath $staged){[IO.File]::Delete($staged)}}
}

function Test-BillfishShortcutMutationAccess {
    param([string]$Destination,[object]$Context,[string[]]$TestDeniedPaths)
    $full=[IO.Path]::GetFullPath($Destination)
    if($null-ne$TestDeniedPaths-and@($TestDeniedPaths|Where-Object{[IO.Path]::GetFullPath([string]$_).Equals($full,[StringComparison]::OrdinalIgnoreCase)}).Count-gt0){throw "Injected shortcut parent access denial: $full"}
    $verified=Open-BillfishVerifiedShortcutParent $full $Context -AllowCreateParent
    $verified.Handle.Dispose()
}

function Test-BillfishShortcutMatchesSnapshotEntry {
    param([object]$Entry)
    $path=[string]$Entry.path
    if(-not[bool]$Entry.existed){return -not(Test-Path -LiteralPath $path)}
    if(-not(Test-Path -LiteralPath $path -PathType Leaf)){return $false}
    try{$actual=Get-BillfishEntryPointFileFingerprint $path;Test-BillfishEntryPointFingerprint $actual ([pscustomobject]@{size=[int64]$Entry.backupSize;sha256=[string]$Entry.backupSha256})}catch{$false}
}

function Get-BillfishShortcutRecoveryEntries {
    param([object]$Journal,[object[]]$Entries,[object]$Context)
    $byPath=@{};foreach($entry in $Entries){$byPath[[IO.Path]::GetFullPath([string]$entry.path)]=$entry}
    $intentProperty=$Journal.PSObject.Properties['shortcutMutationIntents']
    $candidatePaths=if($null-eq$intentProperty){@($byPath.Keys)}else{
        $value=$intentProperty.Value
        if($value-is[string]-or$value-isnot[Collections.IEnumerable]){throw 'pending journal shortcutMutationIntents must be an array.'}
        @($value)
    }
    $seen=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach($candidate in $candidatePaths){
        if($candidate-isnot[string]){throw 'pending journal shortcutMutationIntents entries must be strings.'}
        $path=Assert-BillfishAllowedShortcutPath ([string]$candidate) $Context
        if(-not$seen.Add($path)){throw "pending journal contains a duplicate shortcut mutation intent: $path"}
        if(-not$byPath.ContainsKey($path)){throw "pending journal shortcut mutation intent is absent from its snapshot: $path"}
        $entry=$byPath[$path]
        if(-not(Test-BillfishShortcutMatchesSnapshotEntry $entry)){$entry}
    }
}

function Test-BillfishRunMatchesSnapshot {
    param([object]$Context,[object]$Run)
    $current=Get-BillfishRunValue $Context
    if([bool]$Run.existed){$current.Exists-and[string]$current.Value-ceq[string]$Run.value}else{-not$current.Exists}
}

function Assert-BillfishTransactionStoragePaths {
    param([string]$StatePointerPath,[string]$BackupDir)
    $stateRoot=Split-Path -Parent ([IO.Path]::GetFullPath($StatePointerPath))
    $statePolicy=New-BillfishTrustedRootPolicy $stateRoot 'transaction state root'
    [void](Assert-BillfishPathWithinPolicy $StatePointerPath $statePolicy -IncludeLeaf)
    [void](Assert-BillfishPathWithinPolicy ($StatePointerPath+'.pending.json') $statePolicy -IncludeLeaf)
    if(-not[string]::IsNullOrWhiteSpace($BackupDir)){[void](Assert-BillfishPathWithinPolicy $BackupDir $statePolicy -IncludeLeaf)}
    $statePolicy
}

function Enter-BillfishEntryPointTransactionMutex {
    param([Parameter(Mandatory = $true)][string]$BillfishRoot)
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $key = $identity + '|' + (Resolve-BillfishRootIdentity $BillfishRoot).ToUpperInvariant()
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($key)))).Replace('-', '') }
    finally { $sha.Dispose() }
    $mutex = New-Object System.Threading.Mutex($false, ('Local\BillfishEntryPoints-' + $hash.Substring(0, 32)))
    try { [void]$mutex.WaitOne() } catch [Threading.AbandonedMutexException] { }
    $mutex
}

function Exit-BillfishEntryPointTransactionMutex {
    param([Parameter(Mandatory = $true)][Threading.Mutex]$Mutex)
    try { $Mutex.ReleaseMutex() } finally { $Mutex.Dispose() }
}

function Get-BillfishRequiredProperty {
    param([Parameter(Mandatory = $true)][object]$Object, [Parameter(Mandatory = $true)][string]$Name, [string]$Context = 'object')
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { throw "$Context is missing required property '$Name'." }
    $property.Value
}

function Assert-BillfishBooleanProperty {
    param([object]$Object, [string]$Name, [string]$Context)
    $value = Get-BillfishRequiredProperty $Object $Name $Context
    if ($value -isnot [bool]) { throw "$Context.$Name must be a JSON boolean." }
    [bool]$value
}

function Get-BillfishContainedLeafPath {
    param([string]$Root, [string]$LeafName, [string]$Context)
    if ([string]::IsNullOrWhiteSpace($LeafName) -or [IO.Path]::GetFileName($LeafName) -ne $LeafName) { throw "$Context must be a plain leaf name." }
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $candidate = [IO.Path]::GetFullPath((Join-Path $rootFull $LeafName))
    if (-not $candidate.StartsWith($rootFull + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "$Context escapes its backup directory." }
    $candidate
}

function Assert-BillfishEntryPointManifestV3 {
    param(
        [Parameter(Mandatory = $true)][object]$Manifest,
        [Parameter(Mandatory = $true)][string]$BackupDir,
        [Parameter(Mandatory = $true)][string]$BillfishRoot,
        [Parameter(Mandatory = $true)][object]$Context,
        [ValidateSet('Prepared', 'Active', 'Restored', 'RolledBack')][string[]]$AllowedState = @('Prepared', 'Active'),
        [switch]$VerifyBackupFingerprints
    )
    $schema = Get-BillfishRequiredProperty $Manifest 'schemaVersion' 'manifest'
    if ($schema -isnot [int] -and $schema -isnot [long]) { throw 'manifest.schemaVersion must be an integer.' }
    if ([int]$schema -ne 3) { throw "Unsupported manifest schemaVersion '$schema'." }
    $transactionValue = Get-BillfishRequiredProperty $Manifest 'transactionId' 'manifest'
    if ($transactionValue -isnot [string]) { throw 'manifest.transactionId must be a string.' }
    $transactionId = [string]$transactionValue
    $guid = [guid]::Empty
    if (-not [guid]::TryParse($transactionId, [ref]$guid)) { throw 'manifest.transactionId is invalid.' }
    $stateValue = Get-BillfishRequiredProperty $Manifest 'state' 'manifest'
    if ($stateValue -isnot [string]) { throw 'manifest.state must be a string.' }
    $state = [string]$stateValue
    if ($AllowedState -notcontains $state) { throw "Manifest state '$state' is not allowed here." }
    $expectedRoot = [IO.Path]::GetFullPath($BillfishRoot).TrimEnd('\', '/')
    $recordedRootValue = Get-BillfishRequiredProperty $Manifest 'billfishRoot' 'manifest'
    if ($recordedRootValue -isnot [string]) { throw 'manifest.billfishRoot must be a string.' }
    $recordedRoot = [string]$recordedRootValue
    if ([string]::IsNullOrWhiteSpace($recordedRoot) -or -not [IO.Path]::GetFullPath($recordedRoot).TrimEnd('\', '/').Equals($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest billfishRoot does not match the current installation.' }
    $run = Get-BillfishRequiredProperty $Manifest 'run' 'manifest'
    $runExisted = Assert-BillfishBooleanProperty $run 'existed' 'manifest.run'
    $runValue = Get-BillfishRequiredProperty $run 'value' 'manifest.run'
    if ($runExisted -and $runValue -isnot [string]) { throw 'manifest.run.value must be a string when the value existed.' }
    if (-not $runExisted -and $null -ne $runValue) { throw 'manifest.run.value must be null when the value did not exist.' }
    $entriesValue = Get-BillfishRequiredProperty $Manifest 'shortcuts' 'manifest'
    if ($entriesValue -is [string] -or $entriesValue -isnot [System.Collections.IEnumerable]) { throw 'manifest.shortcuts must be an array.' }
    $entries = @($entriesValue)
    if ($entries.Count -eq 0) { throw 'manifest.shortcuts must not be empty.' }
    $policies = Get-BillfishEntryPointRootPolicies $Context
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $backupFull = [IO.Path]::GetFullPath($BackupDir).TrimEnd('\', '/')
    foreach ($entry in $entries) {
        $rawPathValue = Get-BillfishRequiredProperty $entry 'path' 'manifest shortcut'
        if ($rawPathValue -isnot [string]) { throw 'manifest shortcut path must be a string.' }
        $rawPath = [string]$rawPathValue
        if ([string]::IsNullOrWhiteSpace($rawPath) -or -not [IO.Path]::IsPathRooted($rawPath) -or [IO.Path]::GetExtension($rawPath) -ne '.lnk') { throw "Manifest shortcut path must be an absolute .lnk: $rawPath" }
        $canonical = [IO.Path]::GetFullPath($rawPath)
        if (-not $canonical.Equals($rawPath, [StringComparison]::OrdinalIgnoreCase)) { throw "Manifest shortcut path is not canonical: $rawPath" }
        [void](Assert-BillfishAllowedShortcutPath $canonical $Context)
        [void](Resolve-BillfishShortcutPolicy $canonical $policies -IncludeLeaf)
        if (-not $seen.Add($canonical)) { throw "Manifest contains a duplicate shortcut path: $canonical" }
        $existed = Assert-BillfishBooleanProperty $entry 'existed' 'manifest shortcut'
        $backup = Get-BillfishRequiredProperty $entry 'backup' 'manifest shortcut'
        $size = Get-BillfishRequiredProperty $entry 'backupSize' 'manifest shortcut'
        $hash = Get-BillfishRequiredProperty $entry 'backupSha256' 'manifest shortcut'
        if ($existed) {
            if ($backup -isnot [string] -or $hash -isnot [string]) { throw 'manifest shortcut backup and backupSha256 must be strings.' }
            if ($size -isnot [long] -and $size -isnot [int]) { throw 'manifest shortcut backupSize must be an integer.' }
            if ([int64]$size -lt 0 -or [string]$hash -notmatch '^[0-9A-Fa-f]{64}$') { throw 'manifest shortcut backup fingerprint is invalid.' }
            $backupPath = Get-BillfishContainedLeafPath -Root $backupFull -LeafName ([string]$backup) -Context 'manifest shortcut backup'
            $backupPolicy = New-BillfishTrustedRootPolicy $backupFull 'BackupDir'
            [void](Assert-BillfishPathWithinPolicy $backupPath $backupPolicy -IncludeLeaf)
            if (-not (Test-Path -LiteralPath $backupPath -PathType Leaf)) { throw "Manifest shortcut backup is missing: $backupPath" }
            $item = Get-Item -LiteralPath $backupPath
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Manifest shortcut backup is a reparse point: $backupPath" }
            if ($VerifyBackupFingerprints) {
                $actual = Get-BillfishEntryPointFileFingerprint $backupPath
                if (-not (Test-BillfishEntryPointFingerprint $actual ([pscustomobject]@{ size = [int64]$size; sha256 = [string]$hash }))) { throw "Manifest shortcut backup fingerprint mismatch: $backupPath" }
            }
        }
        elseif ($null -ne $backup -or $null -ne $size -or $null -ne $hash) { throw 'A shortcut that did not exist must have null backup fields.' }
    }
    Assert-BillfishAllowedShortcutSet $entries $Context 'manifest.shortcuts'
    [pscustomobject]@{ transactionId = $transactionId; state = $state; root = $expectedRoot; entries = $entries; run = $run; backupDir = $backupFull }
}

function Read-BillfishManifestV3 {
    param([string]$BackupDir)
    $path = Join-Path ([IO.Path]::GetFullPath($BackupDir)) 'manifest.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Manifest is missing: $path" }
    if (((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Manifest must be an ordinary file: $path" }
    Get-Content -LiteralPath $path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
}

function Set-BillfishManifestStateV3 {
    param([string]$BackupDir, [string]$State)
    $manifest = Read-BillfishManifestV3 $BackupDir
    $manifest.state = $State
    Write-AtomicBillfishText -LiteralPath (Join-Path $BackupDir 'manifest.json') -Content (($manifest | ConvertTo-Json -Depth 10) + [Environment]::NewLine)
}

function Get-ActiveBillfishEntryPointBackup {
    param([string]$StatePointerPath, [switch]$AllowLegacyBackup)
    if ([string]::IsNullOrWhiteSpace($StatePointerPath) -or -not (Test-Path -LiteralPath $StatePointerPath -PathType Leaf)) { return $null }
    $path = (Get-Content -LiteralPath $StatePointerPath -TotalCount 1).Trim()
    if ([string]::IsNullOrWhiteSpace($path)) { throw "Active pointer is empty: $StatePointerPath" }
    $manifestPath = Join-Path $path 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        $legacy = @(Get-ChildItem -LiteralPath $path -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -eq 'HKCU-Run-Billfish.txt' -or $_.Name -like '*.lnk.bak' })
        if ($AllowLegacyBackup -and $legacy.Count -gt 0) { return $null }
        throw "Active manifest is missing under: $path"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ([int](Get-BillfishRequiredProperty $manifest 'schemaVersion' 'active manifest') -ne 3) {
        if ($AllowLegacyBackup) { return $null }
        throw "Unsupported active manifest schema under: $path"
    }
    [IO.Path]::GetFullPath($path)
}

function Write-BillfishPendingJournal {
    param([string]$StatePointerPath, [object]$Journal)
    Write-AtomicBillfishText -LiteralPath ($StatePointerPath + '.pending.json') -Content (($Journal | ConvertTo-Json -Depth 10) + [Environment]::NewLine)
}

function Remove-BillfishPendingJournal {
    param([string]$StatePointerPath)
    $pending = $StatePointerPath + '.pending.json'
    if (Test-Path -LiteralPath $pending) { Remove-Item -LiteralPath $pending -Force }
}

function Restore-BillfishOriginFromManifestV3 {
    param([object]$Validated,[object]$Context,[object]$Journal)
    $entries=if($null-ne$Journal){@(Get-BillfishShortcutRecoveryEntries $Journal @($Validated.entries) $Context)}else{@($Validated.entries|Where-Object{-not(Test-BillfishShortcutMatchesSnapshotEntry $_)})}
    foreach ($entry in $entries) {
        $policies=Get-BillfishEntryPointRootPolicies $Context
        [void](Resolve-BillfishShortcutPolicy ([string]$entry.path) $policies -IncludeLeaf)
        if ([bool]$entry.existed) { Invoke-BillfishHandleRelativeShortcutMutation -Destination ([string]$entry.path) -Context $Context -StagingDirectory $Validated.backupDir -Source (Join-Path $Validated.backupDir ([string]$entry.backup)) -ExpectedFingerprint ([pscustomobject]@{size=[int64]$entry.backupSize;sha256=[string]$entry.backupSha256}) }
        else { Invoke-BillfishHandleRelativeShortcutMutation -Destination ([string]$entry.path) -Context $Context -StagingDirectory $Validated.backupDir -Delete }
    }
    if(-not(Test-BillfishRunMatchesSnapshot $Context $Validated.run)){Set-BillfishRunValue -Context $Context -Exists ([bool]$Validated.run.existed) -Value ([string]$Validated.run.value)}
}

function Assert-BillfishRollbackSnapshotV1 {
    param([object]$Snapshot, [string]$RollbackDir, [string]$BillfishRoot, [object]$Context, [switch]$VerifyFingerprints)
    if ([int](Get-BillfishRequiredProperty $Snapshot 'schemaVersion' 'rollback snapshot') -ne 1) { throw 'Unsupported rollback snapshot schema.' }
    $root = [IO.Path]::GetFullPath($BillfishRoot).TrimEnd('\', '/')
    $snapshotRoot = Get-BillfishRequiredProperty $Snapshot 'billfishRoot' 'rollback snapshot'
    if ($snapshotRoot -isnot [string] -or -not [IO.Path]::GetFullPath([string]$snapshotRoot).TrimEnd('\', '/').Equals($root, [StringComparison]::OrdinalIgnoreCase)) { throw 'Rollback snapshot root mismatch.' }
    $policies = Get-BillfishEntryPointRootPolicies $Context
    $entriesValue = Get-BillfishRequiredProperty $Snapshot 'shortcuts' 'rollback snapshot'
    if ($entriesValue -is [string] -or $entriesValue -isnot [System.Collections.IEnumerable]) { throw 'rollback snapshot shortcuts must be an array.' }
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($entriesValue)) {
        $pathValue = Get-BillfishRequiredProperty $entry 'path' 'rollback shortcut'
        if ($pathValue -isnot [string]) { throw 'rollback shortcut path must be a string.' }
        $path = [IO.Path]::GetFullPath([string]$pathValue)
        if (-not $path.Equals([string]$pathValue, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetExtension($path) -ne '.lnk' -or -not $seen.Add($path)) { throw "Rollback snapshot path is not canonical and unique: $path" }
        [void](Assert-BillfishAllowedShortcutPath $path $Context)
        [void](Resolve-BillfishShortcutPolicy $path $policies -IncludeLeaf)
        $existed = Assert-BillfishBooleanProperty $entry 'existed' 'rollback shortcut'
        $backup = Get-BillfishRequiredProperty $entry 'backup' 'rollback shortcut'
        $size = Get-BillfishRequiredProperty $entry 'backupSize' 'rollback shortcut'
        $hash = Get-BillfishRequiredProperty $entry 'backupSha256' 'rollback shortcut'
        if ($existed) {
            if ($backup -isnot [string] -or $hash -isnot [string] -or ($size -isnot [int] -and $size -isnot [long]) -or [string]$hash -notmatch '^[0-9A-Fa-f]{64}$') { throw 'rollback shortcut backup fingerprint fields are invalid.' }
            $backupPath = Get-BillfishContainedLeafPath $RollbackDir ([string]$backup) 'rollback backup'
            if (-not (Test-Path -LiteralPath $backupPath -PathType Leaf)) { throw "Rollback backup missing: $backupPath" }
            if ($VerifyFingerprints) {
                $actual = Get-BillfishEntryPointFileFingerprint $backupPath
                if (-not (Test-BillfishEntryPointFingerprint $actual ([pscustomobject]@{ size = [int64]$entry.backupSize; sha256 = [string]$entry.backupSha256 }))) { throw "Rollback backup fingerprint mismatch: $backupPath" }
            }
        }
    }
    Assert-BillfishAllowedShortcutSet @($entriesValue) $Context 'rollback snapshot shortcuts'
    $run = Get-BillfishRequiredProperty $Snapshot 'run' 'rollback snapshot'
    $runExisted = Assert-BillfishBooleanProperty $run 'existed' 'rollback run'
    $runValue = Get-BillfishRequiredProperty $run 'value' 'rollback run'
    if ($runExisted -and $runValue -isnot [string]) { throw 'rollback run value must be a string when present.' }
    if (-not $runExisted -and $null -ne $runValue) { throw 'rollback run value must be null when absent.' }
    $Snapshot
}

function Repair-BillfishPendingEntryPointTransaction {
    param([string]$StatePointerPath, [string]$BillfishRoot, [object]$Context, [switch]$TestLeaveCleanupArtifacts)
    if ([string]::IsNullOrWhiteSpace($StatePointerPath)) { return [pscustomobject]@{ Recovered = $false } }
    $pendingPath = $StatePointerPath + '.pending.json'
    if (-not (Test-Path -LiteralPath $pendingPath -PathType Leaf)) { return [pscustomobject]@{ Recovered = $false } }
    $statePolicy=Assert-BillfishTransactionStoragePaths $StatePointerPath $null
    $journal = Get-Content -LiteralPath $pendingPath -Raw | ConvertFrom-Json
    if ([int](Get-BillfishRequiredProperty $journal 'schemaVersion' 'pending journal') -ne 1) { throw 'Unsupported pending journal schema.' }
    $root = [IO.Path]::GetFullPath($BillfishRoot).TrimEnd('\', '/')
    $journalRoot = Get-BillfishRequiredProperty $journal 'billfishRoot' 'pending journal'
    $operationValue = Get-BillfishRequiredProperty $journal 'operation' 'pending journal'
    $backupValue = Get-BillfishRequiredProperty $journal 'backupDir' 'pending journal'
    $journalTransaction = Get-BillfishRequiredProperty $journal 'transactionId' 'pending journal'
    if ($journalRoot -isnot [string] -or $operationValue -isnot [string] -or $backupValue -isnot [string] -or $journalTransaction -isnot [string]) { throw 'Pending journal root, operation, backupDir, and transactionId must be strings.' }
    if (-not [IO.Path]::GetFullPath([string]$journalRoot).TrimEnd('\', '/').Equals($root, [StringComparison]::OrdinalIgnoreCase)) { throw 'Pending journal root mismatch; automatic recovery is blocked.' }
    $operation = [string]$operationValue
    $backupDir = [IO.Path]::GetFullPath([string]$backupValue)
    [void](Assert-BillfishPathWithinPolicy $backupDir $statePolicy -IncludeLeaf)
    if ($operation -eq 'Install') {
        $manifest = Read-BillfishManifestV3 $backupDir
        $validated = Assert-BillfishEntryPointManifestV3 $manifest $backupDir $root $Context -AllowedState @('Prepared', 'Active', 'RolledBack') -VerifyBackupFingerprints
        if (-not $validated.transactionId.Equals([string]$journalTransaction, [StringComparison]::OrdinalIgnoreCase)) { throw 'Pending install journal transactionId does not match its manifest.' }
        Restore-BillfishOriginFromManifestV3 $validated $Context $journal
        Set-BillfishManifestStateV3 $backupDir 'RolledBack'
        if (Test-Path -LiteralPath $StatePointerPath) {
            $active = (Get-Content -LiteralPath $StatePointerPath -TotalCount 1).Trim()
            if ($active -and [IO.Path]::GetFullPath($active).Equals($backupDir, [StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $StatePointerPath -Force }
        }
    }
    elseif ($operation -eq 'Restore') {
        $originManifest = Read-BillfishManifestV3 $backupDir
        $origin = Assert-BillfishEntryPointManifestV3 $originManifest $backupDir $root $Context -AllowedState @('Active', 'Restored') -VerifyBackupFingerprints
        if (-not $origin.transactionId.Equals([string]$journalTransaction, [StringComparison]::OrdinalIgnoreCase)) { throw 'Pending restore journal transactionId does not match its manifest.' }
        if (Test-Path -LiteralPath $StatePointerPath -PathType Leaf) {
            $recordedActive = (Get-Content -LiteralPath $StatePointerPath -TotalCount 1).Trim()
            if ($recordedActive -and -not [IO.Path]::GetFullPath($recordedActive).Equals($backupDir, [StringComparison]::OrdinalIgnoreCase)) { throw 'Pending restore journal is detached from the active pointer.' }
        }
        $rollbackDir = [IO.Path]::GetFullPath([string](Get-BillfishRequiredProperty $journal 'rollbackDir' 'pending journal'))
        $backupPrefix = $backupDir.TrimEnd('\', '/') + '\'
        if (-not $rollbackDir.StartsWith($backupPrefix, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $rollbackDir) -notlike '.restore-rollback-*') { throw 'Pending restore rollbackDir is detached from BackupDir.' }
        $backupPolicy=New-BillfishTrustedRootPolicy $backupDir 'BackupDir'
        [void](Assert-BillfishPathWithinPolicy $rollbackDir $backupPolicy -IncludeLeaf)
        $snapshotPath=Join-Path $rollbackDir 'snapshot.json'
        [void](Assert-BillfishPathWithinPolicy $snapshotPath $backupPolicy -IncludeLeaf)
        $snapshot = Get-Content -LiteralPath $snapshotPath -Raw | ConvertFrom-Json
        [void](Assert-BillfishRollbackSnapshotV1 $snapshot $rollbackDir $root $Context -VerifyFingerprints)
        $policies=Get-BillfishEntryPointRootPolicies $Context
        foreach ($entry in @(Get-BillfishShortcutRecoveryEntries $journal @($snapshot.shortcuts) $Context)) {
            [void](Resolve-BillfishShortcutPolicy ([string]$entry.path) $policies -IncludeLeaf)
            if ([bool]$entry.existed) { Invoke-BillfishHandleRelativeShortcutMutation -Destination ([string]$entry.path) -Context $Context -StagingDirectory $rollbackDir -Source (Join-Path $rollbackDir ([string]$entry.backup)) -ExpectedFingerprint ([pscustomobject]@{size=[int64]$entry.backupSize;sha256=[string]$entry.backupSha256}) }
            else { Invoke-BillfishHandleRelativeShortcutMutation -Destination ([string]$entry.path) -Context $Context -StagingDirectory $rollbackDir -Delete }
        }
        if(-not(Test-BillfishRunMatchesSnapshot $Context $snapshot.run)){Set-BillfishRunValue $Context ([string]$snapshot.run.value) ([bool]$snapshot.run.existed)}
        Set-BillfishManifestStateV3 $backupDir 'Active'
        Write-AtomicBillfishText $StatePointerPath ($backupDir + [Environment]::NewLine)
        Remove-BillfishPendingJournal $StatePointerPath
        if($TestLeaveCleanupArtifacts){return [pscustomobject]@{Recovered=$true;Operation=$operation;BackupDir=$backupDir;CleanupDeferred=$true}}
        $tombstone = $StatePointerPath + '.restoring-' + [string]$journalTransaction
        if (Test-Path -LiteralPath $tombstone) { Remove-Item -LiteralPath $tombstone -Force -ErrorAction SilentlyContinue }
        Remove-Item -LiteralPath $rollbackDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    else { throw "Unknown pending transaction operation '$operation'." }
    if($operation-eq'Install'){Remove-BillfishPendingJournal $StatePointerPath}
    [pscustomobject]@{ Recovered = $true; Operation = $operation; BackupDir = $backupDir }
}

function Install-BillfishEntryPoints {
    [CmdletBinding(PositionalBinding = $false)]
    param(
        [Parameter(Mandatory = $true)][string]$BillfishRoot,
        [Parameter(Mandatory = $true)][string]$BackupDir,
        [string]$FixtureRoot,
        [Parameter(Mandatory = $true)][string]$StatePointerPath,
        [switch]$DryRun,
        [switch]$RecoverOnly,
        [ValidateSet('', 'Shortcut', 'Run', 'Manifest', 'Pointer')][string]$FailStage = '',
        [int]$FailShortcutIndex = 1,
        [int]$LeavePendingAfterShortcutIndex = 0,
        [int]$TestHoldMutexMilliseconds = 0,
        [switch]$TestLeaveRecoveryArtifacts,
        [scriptblock]$TestBeforeShortcutMutation,
        [string[]]$TestDeniedShortcutPaths
    )
    $root = Resolve-BillfishRootIdentity $BillfishRoot
    $mutex = Enter-BillfishEntryPointTransactionMutex $root
    try {
        if ($TestHoldMutexMilliseconds -gt 0) { Start-Sleep -Milliseconds $TestHoldMutexMilliseconds }
        $context = Get-BillfishEntryPointContext $FixtureRoot
        $statePolicy=Assert-BillfishTransactionStoragePaths $StatePointerPath $BackupDir
        $recovery = Repair-BillfishPendingEntryPointTransaction $StatePointerPath $root $context -TestLeaveCleanupArtifacts:$TestLeaveRecoveryArtifacts
        if ($RecoverOnly) { return [pscustomobject]@{ Outcome = if ($recovery.Recovered) { 'Recovered' } else { 'NoPendingTransaction' } } }
        $launcher = Join-Path $root 'launcher\Billfish.exe'; $vendor = Join-Path $root 'Billfish.exe'
        foreach ($required in @($launcher, $vendor)) { if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required executable not found: $required" } }
        $active = Get-ActiveBillfishEntryPointBackup $StatePointerPath -AllowLegacyBackup
        if ($null -ne $active) {
            $manifest = Read-BillfishManifestV3 $active
            $validatedActive=Assert-BillfishEntryPointManifestV3 $manifest $active $root $context -AllowedState Active -VerifyBackupFingerprints
            $status = Get-BillfishEntryPointStatus -BillfishRoot $root -FixtureRoot $FixtureRoot -StatePointerPath $StatePointerPath -ValidatedEntries @($validatedActive.entries)
            if (-not $status.Healthy) { throw "Active origin manifest exists at '$active', but entry points have drifted." }
            return [pscustomobject]@{ Outcome = 'AlreadyInstalled'; Launcher = $launcher; ShortcutCount = $status.Shortcuts.Count; BackupDir = $active }
        }
        $statusWithoutPointer = Get-BillfishEntryPointStatus $root $FixtureRoot
        if ($statusWithoutPointer.Healthy) { throw 'Entry points already target the unified launcher, but the active origin pointer is missing. Refusing to re-baseline; repair or migrate the original manifest first.' }
        $run = Get-BillfishRunValue $context
        $paths = @(Get-BillfishShortcutPaths $context $root)
        $entryPolicies=Get-BillfishEntryPointRootPolicies $context
        $requiredPaths=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase);foreach($requiredPath in @($context.Required)){[void]$requiredPaths.Add([IO.Path]::GetFullPath([string]$requiredPath))}
        $unmanaged=New-Object 'Collections.Generic.List[object]'
        $preflight = foreach ($path in $paths) {
            $canonical=Assert-BillfishAllowedShortcutPath $path $context;[void](Resolve-BillfishShortcutPolicy $canonical $entryPolicies -IncludeLeaf)
            try{Test-BillfishShortcutMutationAccess $canonical $context $TestDeniedShortcutPaths}catch{
                if($requiredPaths.Contains($canonical)){throw "Required shortcut destination failed mutation-access preflight: $canonical. $($_.Exception.Message)"}
                $unmanaged.Add([pscustomobject]@{Path=$canonical;Reason=$_.Exception.Message});continue
            }
            $exists = Test-Path -LiteralPath $canonical -PathType Leaf;if($exists){[void](Read-BillfishShortcut $canonical $context.Fixture)}
            [pscustomobject]@{path=$canonical;existed=$exists;backup=if($exists){[guid]::NewGuid().ToString('N')+'.lnk'}else{$null}}
        }
        Assert-BillfishAllowedShortcutSet @($preflight) $context 'install shortcut set'
        if ($DryRun) { return [pscustomobject]@{ Outcome='WouldInstall'; Launcher=$launcher; ShortcutCount=@($preflight).Count; BackupDir=$BackupDir;UnmanagedShortcuts=@($unmanaged|ForEach-Object{$_}) } }
        if (Test-Path -LiteralPath $BackupDir) { throw "Backup directory already exists: $BackupDir" }
        New-Item -ItemType Directory -Path $BackupDir | Out-Null
        [void](Assert-BillfishPathWithinPolicy $BackupDir $statePolicy -IncludeLeaf)
        $manifestEntries = @()
        foreach ($entry in $preflight) {
            if ($entry.existed) { $destination=Join-Path $BackupDir $entry.backup; Copy-Item -LiteralPath $entry.path -Destination $destination; $fp=Get-BillfishEntryPointFileFingerprint $destination; $manifestEntries += [ordered]@{path=$entry.path;existed=$true;backup=$entry.backup;backupSize=$fp.size;backupSha256=$fp.sha256} }
            else { $manifestEntries += [ordered]@{path=$entry.path;existed=$false;backup=$null;backupSize=$null;backupSha256=$null} }
        }
        $transactionId=[guid]::NewGuid().ToString('N')
        $manifest=[ordered]@{schemaVersion=3;transactionId=$transactionId;state='Prepared';billfishRoot=$root;run=[ordered]@{existed=$run.Exists;value=$run.Value};shortcuts=$manifestEntries}
        Write-AtomicBillfishText (Join-Path $BackupDir 'manifest.json') (($manifest|ConvertTo-Json -Depth 10)+[Environment]::NewLine)
        $pendingJournal=[ordered]@{schemaVersion=1;transactionId=$transactionId;operation='Install';billfishRoot=$root;backupDir=[IO.Path]::GetFullPath($BackupDir);shortcutMutationIntents=@();runMutationIntent=$false}
        Write-BillfishPendingJournal $StatePointerPath $pendingJournal
        try {
            $desired=[pscustomobject]@{TargetPath=$launcher;Arguments='';WorkingDirectory=$root;IconLocation="$vendor,0";Description='Billfish';WindowStyle=7}
            $index=0
            foreach($entry in $preflight){$index++;if($FailStage-eq'Shortcut'-and$index-eq$FailShortcutIndex){throw "Injected shortcut failure $index."};[void](Resolve-BillfishShortcutPolicy $entry.path $entryPolicies -IncludeLeaf);$pendingJournal.shortcutMutationIntents=@($pendingJournal.shortcutMutationIntents)+@([string]$entry.path);Write-BillfishPendingJournal $StatePointerPath $pendingJournal;Set-BillfishShortcutHandleRelative -Destination $entry.path -Value $desired -Context $context -StagingDirectory $BackupDir -TestBeforeMutation $TestBeforeShortcutMutation -Index $index -Phase 'Install';if($LeavePendingAfterShortcutIndex-eq$index){return [pscustomobject]@{Outcome='PendingSimulated';Operation='Install';ShortcutIndex=$index}}}
            if($FailStage-eq'Run'){throw'Injected Run failure.'};$pendingJournal.runMutationIntent=$true;Write-BillfishPendingJournal $StatePointerPath $pendingJournal;Set-BillfishRunValue $context ('"'+$launcher+'" -a=auto') $true
            if($FailStage-eq'Manifest'){throw'Injected manifest failure.'};Set-BillfishManifestStateV3 $BackupDir 'Active'
            if($FailStage-eq'Pointer'){throw'Injected pointer failure.'};Write-AtomicBillfishText $StatePointerPath ([IO.Path]::GetFullPath($BackupDir)+[Environment]::NewLine)
            Remove-BillfishPendingJournal $StatePointerPath
        } catch { $failure=$_;try{[void](Repair-BillfishPendingEntryPointTransaction $StatePointerPath $root $context)}catch{throw "Install failed and pending recovery is INCOMPLETE. Original: $($failure.Exception.Message) Recovery: $($_.Exception.Message)"};throw "Install failed and was rolled back: $($failure.Exception.Message)" }
        [pscustomobject]@{Outcome='Installed';Launcher=$launcher;ShortcutCount=@($preflight).Count;BackupDir=[IO.Path]::GetFullPath($BackupDir);UnmanagedShortcuts=@($unmanaged|ForEach-Object{$_})}
    } finally { Exit-BillfishEntryPointTransactionMutex $mutex }
}

function Restore-BillfishEntryPoints {
    [CmdletBinding(PositionalBinding = $false)]
    param(
        [Parameter(Mandatory = $true)][string]$BackupDir,[Parameter(Mandatory = $true)][string]$BillfishRoot,[string]$FixtureRoot,[Parameter(Mandatory = $true)][string]$StatePointerPath,[switch]$DryRun,
        [ValidateSet('', 'Shortcut', 'Run', 'Pointer')][string]$FailStage='',[int]$FailShortcutIndex=1,[int]$LeavePendingAfterShortcutIndex=0,[int]$TestHoldMutexMilliseconds=0,[scriptblock]$TestBeforeShortcutMutation
    )
    $backupFull=[IO.Path]::GetFullPath($BackupDir)
    $root=Resolve-BillfishRootIdentity $BillfishRoot
    $mutex=Enter-BillfishEntryPointTransactionMutex $root
    try{
        if($TestHoldMutexMilliseconds-gt0){Start-Sleep -Milliseconds $TestHoldMutexMilliseconds}
        $context=Get-BillfishEntryPointContext $FixtureRoot
        $statePolicy=Assert-BillfishTransactionStoragePaths $StatePointerPath $backupFull
        [void](Repair-BillfishPendingEntryPointTransaction $StatePointerPath $root $context)
        $active=Get-ActiveBillfishEntryPointBackup $StatePointerPath
        if($null-eq$active-or-not$active.Equals($backupFull,[StringComparison]::OrdinalIgnoreCase)){throw'Restore manifest is detached from the active pointer.'}
        $manifest=Read-BillfishManifestV3 $backupFull
        $validated=Assert-BillfishEntryPointManifestV3 $manifest $backupFull $root $context -AllowedState Active -VerifyBackupFingerprints
        foreach($entry in @($validated.entries)){try{Test-BillfishShortcutMutationAccess ([string]$entry.path) $context}catch{throw "Restore shortcut destination failed mutation-access preflight: $([string]$entry.path). $($_.Exception.Message)"}}
        if($DryRun){return [pscustomobject]@{Outcome='WouldRestore';ShortcutCount=$validated.entries.Count}}
        $rollbackDir=Join-Path $backupFull ('.restore-rollback-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory $rollbackDir|Out-Null
        $backupPolicy=New-BillfishTrustedRootPolicy $backupFull 'BackupDir';[void](Assert-BillfishPathWithinPolicy $rollbackDir $backupPolicy -IncludeLeaf)
        $currentRun=Get-BillfishRunValue $context;$snapEntries=@()
        foreach($entry in @($validated.entries)){$exists=Test-Path -LiteralPath ([string]$entry.path)-PathType Leaf;if($exists){$name=[guid]::NewGuid().ToString('N')+'.lnk';$dest=Join-Path $rollbackDir $name;Copy-Item -LiteralPath ([string]$entry.path)-Destination $dest;$fp=Get-BillfishEntryPointFileFingerprint $dest;$snapEntries+=[ordered]@{path=[string]$entry.path;existed=$true;backup=$name;backupSize=$fp.size;backupSha256=$fp.sha256}}else{$snapEntries+=[ordered]@{path=[string]$entry.path;existed=$false;backup=$null;backupSize=$null;backupSha256=$null}}}
        $snapshot=[ordered]@{schemaVersion=1;billfishRoot=$root;run=[ordered]@{existed=$currentRun.Exists;value=$currentRun.Value};shortcuts=$snapEntries}
        Write-AtomicBillfishText (Join-Path $rollbackDir 'snapshot.json') (($snapshot|ConvertTo-Json -Depth 10)+[Environment]::NewLine)
        $pendingJournal=[ordered]@{schemaVersion=1;transactionId=$validated.transactionId;operation='Restore';billfishRoot=$root;backupDir=$backupFull;rollbackDir=$rollbackDir;shortcutMutationIntents=@();runMutationIntent=$false}
        Write-BillfishPendingJournal $StatePointerPath $pendingJournal
        try {
            $index=0
            $policies=Get-BillfishEntryPointRootPolicies $context
            foreach($entry in @($validated.entries)) {
                $index++
                if($FailStage-eq'Shortcut'-and$index-eq$FailShortcutIndex){throw "Injected restore shortcut failure $index."}
                [void](Resolve-BillfishShortcutPolicy ([string]$entry.path) $policies -IncludeLeaf)
                $pendingJournal.shortcutMutationIntents=@($pendingJournal.shortcutMutationIntents)+@([string]$entry.path);Write-BillfishPendingJournal $StatePointerPath $pendingJournal
                if([bool]$entry.existed){Invoke-BillfishHandleRelativeShortcutMutation -Destination ([string]$entry.path) -Context $context -StagingDirectory $rollbackDir -Source (Join-Path $backupFull ([string]$entry.backup)) -ExpectedFingerprint ([pscustomobject]@{size=[int64]$entry.backupSize;sha256=[string]$entry.backupSha256}) -TestBeforeMutation $TestBeforeShortcutMutation -Index $index -Phase 'Restore'}else{Invoke-BillfishHandleRelativeShortcutMutation -Destination ([string]$entry.path) -Context $context -StagingDirectory $rollbackDir -Delete -TestBeforeMutation $TestBeforeShortcutMutation -Index $index -Phase 'Restore'}
                if($LeavePendingAfterShortcutIndex-eq$index){return [pscustomobject]@{Outcome='PendingSimulated';Operation='Restore';ShortcutIndex=$index}}
            }
            if($FailStage-eq'Run'){throw 'Injected restore Run failure.'}
            $pendingJournal.runMutationIntent=$true;Write-BillfishPendingJournal $StatePointerPath $pendingJournal
            Set-BillfishRunValue $context ([string]$validated.run.value) ([bool]$validated.run.existed)
            Set-BillfishManifestStateV3 $backupFull 'Restored'
            if($FailStage-eq'Pointer'){throw 'Injected restore pointer failure.'}
            $tombstone=$StatePointerPath+'.restoring-'+$validated.transactionId
            Move-Item -LiteralPath $StatePointerPath -Destination $tombstone
            Remove-BillfishPendingJournal $StatePointerPath
            Remove-Item -LiteralPath $tombstone -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $rollbackDir -Recurse -Force -ErrorAction SilentlyContinue
        }
        catch {
            $failure=$_
            try{[void](Repair-BillfishPendingEntryPointTransaction $StatePointerPath $root $context)}catch{throw "Restore failed and pending recovery is INCOMPLETE. Original: $($failure.Exception.Message) Recovery: $($_.Exception.Message)"}
            throw "Restore failed and installed state was recovered: $($failure.Exception.Message)"
        }
        [pscustomobject]@{Outcome='Restored';ShortcutCount=$validated.entries.Count}
    }finally{Exit-BillfishEntryPointTransactionMutex $mutex}
}
