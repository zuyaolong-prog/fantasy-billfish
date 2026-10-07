Set-StrictMode -Version 3.0

function Get-BillfishEntryPointContext {
    param([string]$FixtureRoot)
    if ($FixtureRoot) {
        $fixture = [System.IO.Path]::GetFullPath($FixtureRoot)
        return [pscustomobject]@{
            Fixture = $true
            Roots = @((Join-Path $fixture 'UserDesktop'), (Join-Path $fixture 'UserStartMenu'), (Join-Path $fixture 'QuickLaunch'), (Join-Path $fixture 'CommonDesktop'), (Join-Path $fixture 'CommonStartMenu'))
            Required = @((Join-Path $fixture 'UserDesktop\Billfish.lnk'), (Join-Path $fixture 'UserStartMenu\Programs\Billfish\Billfish.lnk'))
            Optional = @((Join-Path $fixture 'QuickLaunch\Billfish.lnk'), (Join-Path $fixture 'CommonDesktop\Billfish.lnk'), (Join-Path $fixture 'CommonStartMenu\Programs\Billfish\Billfish.lnk'))
            RunStore = Join-Path $fixture 'HKCU-Run.json'
        }
    }
    $desktop = [Environment]::GetFolderPath('Desktop')
    $startMenu = [Environment]::GetFolderPath('StartMenu')
    $quickLaunch = Join-Path $env:APPDATA 'Microsoft\Internet Explorer\Quick Launch'
    $commonDesktop = [Environment]::GetFolderPath('CommonDesktopDirectory')
    $commonStartMenu = [Environment]::GetFolderPath('CommonStartMenu')
    [pscustomobject]@{
        Fixture = $false
        Roots = @($desktop, $startMenu, $quickLaunch, $commonDesktop, $commonStartMenu)
        Required = @((Join-Path $desktop 'Billfish.lnk'), (Join-Path $startMenu 'Programs\Billfish\Billfish.lnk'))
        Optional = @((Join-Path $quickLaunch 'Billfish.lnk'), (Join-Path $commonDesktop 'Billfish.lnk'), (Join-Path $commonStartMenu 'Programs\Billfish\Billfish.lnk'))
        RunStore = $null
    }
}

function Read-BillfishShortcut {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][bool]$Fixture)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    if ($Fixture) { return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($Path)
    [pscustomobject]@{ TargetPath = $shortcut.TargetPath; Arguments = $shortcut.Arguments; WorkingDirectory = $shortcut.WorkingDirectory; IconLocation = $shortcut.IconLocation; Description = $shortcut.Description; WindowStyle = $shortcut.WindowStyle }
}

function Write-BillfishShortcut {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][object]$Value, [Parameter(Mandatory = $true)][bool]$Fixture)
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    if ($Fixture) {
        Write-AtomicBillfishText -LiteralPath $Path -Content (($Value | ConvertTo-Json -Depth 4) + [Environment]::NewLine)
        return
    }
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($Path)
    $shortcut.TargetPath = $Value.TargetPath
    $shortcut.Arguments = $Value.Arguments
    $shortcut.WorkingDirectory = $Value.WorkingDirectory
    $shortcut.IconLocation = $Value.IconLocation
    $shortcut.Description = $Value.Description
    $shortcut.WindowStyle = $Value.WindowStyle
    $shortcut.Save()
}

function Write-AtomicBillfishText {
    param([Parameter(Mandatory = $true)][string]$LiteralPath, [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content)
    $parent = Split-Path -Parent $LiteralPath
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $temporary = Join-Path $parent ('.' + (Split-Path -Leaf $LiteralPath) + '.' + [guid]::NewGuid().ToString('N') + '.tmp')
    $replacementBackup = Join-Path $parent ('.' + (Split-Path -Leaf $LiteralPath) + '.' + [guid]::NewGuid().ToString('N') + '.previous')
    try {
        [System.IO.File]::WriteAllText($temporary, $Content, (New-Object System.Text.UTF8Encoding($false)))
        if (Test-Path -LiteralPath $LiteralPath -PathType Leaf) { [System.IO.File]::Replace($temporary, $LiteralPath, $replacementBackup, $true) }
        else { Move-Item -LiteralPath $temporary -Destination $LiteralPath }
    }
    finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
        if (Test-Path -LiteralPath $replacementBackup) { Remove-Item -LiteralPath $replacementBackup -Force }
    }
}

function Get-BillfishRunValue {
    param([Parameter(Mandatory = $true)][object]$Context)
    if ($Context.Fixture) {
        if (-not (Test-Path -LiteralPath $Context.RunStore -PathType Leaf)) { return [pscustomobject]@{ Exists = $false; Value = $null } }
        $data = Get-Content -LiteralPath $Context.RunStore -Raw | ConvertFrom-Json
        $property = $data.PSObject.Properties['Billfish']
        return [pscustomobject]@{ Exists = $null -ne $property; Value = if ($null -ne $property) { [string]$property.Value } else { $null } }
    }
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    if (-not (Test-Path -LiteralPath $key)) { return [pscustomobject]@{ Exists = $false; Value = $null } }
    $properties = Get-ItemProperty -LiteralPath $key -ErrorAction Stop
    $property = $properties.PSObject.Properties['Billfish']
    [pscustomobject]@{ Exists = $null -ne $property; Value = if ($null -ne $property) { [string]$property.Value } else { $null } }
}

function Set-BillfishRunValue {
    param([Parameter(Mandatory = $true)][object]$Context, [AllowNull()][string]$Value, [bool]$Exists = $true)
    if ($Context.Fixture) {
        Write-AtomicBillfishText -LiteralPath $Context.RunStore -Content ($(if ($Exists) { ([ordered]@{ Billfish = $Value } | ConvertTo-Json) } else { '{}' }) + [Environment]::NewLine)
        return
    }
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    if ($Exists) {
        if (-not (Test-Path -LiteralPath $key)) { New-Item -Path $key -Force | Out-Null }
        Set-ItemProperty -LiteralPath $key -Name Billfish -Value $Value
    }
    elseif (Test-Path -LiteralPath $key) { Remove-ItemProperty -LiteralPath $key -Name Billfish -ErrorAction SilentlyContinue }
}

function Get-BillfishShortcutPaths {
    param([Parameter(Mandatory = $true)][object]$Context, [Parameter(Mandatory = $true)][string]$BillfishRoot)
    $paths = @($Context.Required)
    $paths += @($Context.Optional | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    return @($paths | ForEach-Object { [IO.Path]::GetFullPath($_) } | Sort-Object -Unique)
}

function Get-BillfishEntryPointStatus {
    param([Parameter(Mandatory = $true)][string]$BillfishRoot,[string]$FixtureRoot,[string]$StatePointerPath,[object[]]$ValidatedEntries,[string]$ExpectedLauncherPath,[string]$ExpectedIconPath)
    $root = [System.IO.Path]::GetFullPath($BillfishRoot)
    $launcher = if([string]::IsNullOrWhiteSpace($ExpectedLauncherPath)){Join-Path $root 'launcher\Billfish.exe'}else{[IO.Path]::GetFullPath($ExpectedLauncherPath)}
    $vendor = if([string]::IsNullOrWhiteSpace($ExpectedIconPath)){Join-Path $root 'Billfish.exe'}else{[IO.Path]::GetFullPath($ExpectedIconPath)}
    $context = Get-BillfishEntryPointContext -FixtureRoot $FixtureRoot
    $activeManifestError=$null
    if($null-eq$ValidatedEntries){
        if([string]::IsNullOrWhiteSpace($StatePointerPath)){$StatePointerPath=if($FixtureRoot){Join-Path ([IO.Path]::GetFullPath($FixtureRoot)) 'latest-backup.txt'}else{Join-Path $PSScriptRoot 'latest-user-shortcut-backup.txt'}}
        if(Test-Path -LiteralPath $StatePointerPath -PathType Leaf){
            try{
                $activeBackup=Get-ActiveBillfishEntryPointBackup $StatePointerPath
                if($null-ne$activeBackup){$activeManifest=Read-BillfishManifestV3 $activeBackup;$validated=Assert-BillfishEntryPointManifestV3 $activeManifest $activeBackup (Resolve-BillfishRootIdentity $root) $context -AllowedState Active -VerifyBackupFingerprints;$ValidatedEntries=@($validated.entries)}
            }catch{$activeManifestError=$_.Exception.Message}
        }
    }
    $statusPaths=if($null-ne$ValidatedEntries){@($ValidatedEntries|ForEach-Object{[IO.Path]::GetFullPath([string]$_.path)})}else{@(Get-BillfishShortcutPaths -Context $context -BillfishRoot $root)}
    $expectedRun = '"' + $launcher + '" -a=auto'
    $run = Get-BillfishRunValue -Context $context
    $shortcuts = foreach ($path in $statusPaths) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { [pscustomobject]@{ Path = $path; Healthy = $false; Error = 'Missing' }; continue }
        try {
            $link = Read-BillfishShortcut -Path $path -Fixture $context.Fixture
            $targetValue = [string]$link.TargetPath
            $workingDirectoryValue = [string]$link.WorkingDirectory
            $targetHealthy = -not [string]::IsNullOrWhiteSpace($targetValue) -and [System.IO.Path]::GetFullPath($targetValue).Equals($launcher, [System.StringComparison]::OrdinalIgnoreCase)
            $workingDirectoryHealthy = -not [string]::IsNullOrWhiteSpace($workingDirectoryValue) -and [System.IO.Path]::GetFullPath($workingDirectoryValue).Equals($root, [System.StringComparison]::OrdinalIgnoreCase)
            $healthy = $targetHealthy -and [string]$link.IconLocation -eq "$vendor,0" -and [string]$link.Arguments -eq '' -and $workingDirectoryHealthy
            [pscustomobject]@{ Path = $path; Healthy = $healthy; Error = if ($healthy) { $null } else { 'Target, arguments, working directory, or icon mismatch' } }
        }
        catch { [pscustomobject]@{ Path = $path; Healthy = $false; Error = $_.Exception.Message } }
    }
    [pscustomobject]@{
        Healthy = $null-eq$activeManifestError -and (Test-Path -LiteralPath $launcher -PathType Leaf) -and $run.Exists -and $run.Value -eq $expectedRun -and @($shortcuts | Where-Object { -not $_.Healthy }).Count -eq 0
        LauncherExists = Test-Path -LiteralPath $launcher -PathType Leaf
        RunHealthy = $run.Exists -and $run.Value -eq $expectedRun
        RunValue = $run.Value
        ActiveManifestError = $activeManifestError
        ActiveManifestValidated = $null-ne$ValidatedEntries
        Shortcuts = @($shortcuts)
    }
}

function Get-ActiveBillfishEntryPointBackupLegacy {
    param([string]$StatePointerPath, [switch]$AllowLegacyBackup)
    if ([string]::IsNullOrWhiteSpace($StatePointerPath) -or -not (Test-Path -LiteralPath $StatePointerPath -PathType Leaf)) { return $null }
    $path = (Get-Content -LiteralPath $StatePointerPath -TotalCount 1).Trim()
    if ([string]::IsNullOrWhiteSpace($path)) { throw "Active entry-point pointer is empty: $StatePointerPath" }
    $manifestPath = Join-Path $path 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        $legacyArtifacts = @(Get-ChildItem -LiteralPath $path -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -eq 'HKCU-Run-Billfish.txt' -or $_.Name -like '*.lnk.bak' })
        if ($AllowLegacyBackup -and $legacyArtifacts.Count -gt 0) { return $null }
        throw "Active entry-point manifest is missing under: $path"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ([int]$manifest.schemaVersion -ne 2) {
        if ($AllowLegacyBackup) { return $null }
        throw "Unsupported active entry-point manifest schema under: $path"
    }
    return [System.IO.Path]::GetFullPath($path)
}

function Install-BillfishEntryPointsLegacy {
    [CmdletBinding(PositionalBinding = $false)]
    param(
        [Parameter(Mandatory = $true)][string]$BillfishRoot,
        [Parameter(Mandatory = $true)][string]$BackupDir,
        [string]$FixtureRoot,
        [string]$StatePointerPath,
        [switch]$DryRun,
        [ValidateSet('', 'Shortcut', 'Run', 'Manifest', 'Pointer')][string]$FailStage = '',
        [int]$FailShortcutIndex = 1
    )
    $root = [System.IO.Path]::GetFullPath($BillfishRoot)
    $launcher = Join-Path $root 'launcher\Billfish.exe'
    $vendor = Join-Path $root 'Billfish.exe'
    foreach ($required in @($launcher, $vendor)) { if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required executable not found: $required" } }
    $context = Get-BillfishEntryPointContext -FixtureRoot $FixtureRoot

    $activeBackup = Get-ActiveBillfishEntryPointBackup -StatePointerPath $StatePointerPath -AllowLegacyBackup
    if ($null -ne $activeBackup) {
        $status = Get-BillfishEntryPointStatus -BillfishRoot $root -FixtureRoot $FixtureRoot
        if (-not $status.Healthy) { throw "An origin manifest is already active at '$activeBackup', but entry points have drifted. Restore or repair that transaction before reinstalling." }
        return [pscustomobject]@{ Outcome = 'AlreadyInstalled'; Launcher = $launcher; ShortcutCount = $status.Shortcuts.Count; BackupDir = $activeBackup }
    }

    $run = Get-BillfishRunValue -Context $context
    $paths = @(Get-BillfishShortcutPaths -Context $context -BillfishRoot $root)
    $preflight = foreach ($path in $paths) {
        $exists = Test-Path -LiteralPath $path -PathType Leaf
        if ($exists) { [void](Read-BillfishShortcut -Path $path -Fixture $context.Fixture) }
        [pscustomobject]@{ Path = $path; Existed = $exists; Backup = if ($exists) { [guid]::NewGuid().ToString('N') + '.lnk' } else { $null } }
    }
    $manifest = [ordered]@{ schemaVersion = 2; billfishRoot = $root; run = [ordered]@{ existed = $run.Exists; value = $run.Value }; shortcuts = @($preflight | ForEach-Object { [ordered]@{ path = $_.Path; existed = $_.Existed; backup = $_.Backup } }) }
    if ($DryRun) { return [pscustomobject]@{ Outcome = 'WouldInstall'; Launcher = $launcher; ShortcutCount = $paths.Count; BackupDir = $BackupDir; Manifest = $manifest } }

    if (Test-Path -LiteralPath $BackupDir) { throw "Backup directory already exists: $BackupDir" }
    New-Item -ItemType Directory -Path $BackupDir -ErrorAction Stop | Out-Null
    foreach ($entry in $preflight | Where-Object Existed) { Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $BackupDir $entry.Backup) -ErrorAction Stop }
    $desired = [pscustomobject]@{ TargetPath = $launcher; Arguments = ''; WorkingDirectory = $root; IconLocation = "$vendor,0"; Description = 'Billfish'; WindowStyle = 7 }
    $changed = [System.Collections.Generic.List[object]]::new()
    $runChanged = $false
    $manifestPath = Join-Path $BackupDir 'manifest.json'
    try {
        $index = 0
        foreach ($entry in $preflight) {
            $index++
            $changed.Add($entry)
            if ($FailStage -eq 'Shortcut' -and $index -eq $FailShortcutIndex) { throw "Injected shortcut failure at index $index." }
            Write-BillfishShortcut -Path $entry.Path -Value $desired -Fixture $context.Fixture
        }
        if ($FailStage -eq 'Run') { throw 'Injected Run failure.' }
        $runChanged = $true
        Set-BillfishRunValue -Context $context -Value ('"' + $launcher + '" -a=auto')
        if ($FailStage -eq 'Manifest') { throw 'Injected manifest failure.' }
        Write-AtomicBillfishText -LiteralPath $manifestPath -Content (($manifest | ConvertTo-Json -Depth 8) + [Environment]::NewLine)
        if ($FailStage -eq 'Pointer') { throw 'Injected pointer failure.' }
        if (-not [string]::IsNullOrWhiteSpace($StatePointerPath)) { Write-AtomicBillfishText -LiteralPath $StatePointerPath -Content ([System.IO.Path]::GetFullPath($BackupDir) + [Environment]::NewLine) }
    }
    catch {
        $failure = $_
        $rollbackErrors = [System.Collections.Generic.List[string]]::new()
        if ($runChanged) {
            try { Set-BillfishRunValue -Context $context -Exists $run.Exists -Value $run.Value }
            catch { $rollbackErrors.Add("Run rollback failed: $($_.Exception.Message)") }
        }
        $rollbackItems = @($changed)
        [array]::Reverse($rollbackItems)
        foreach ($entry in $rollbackItems) {
            try {
                if ($entry.Existed) { Copy-Item -LiteralPath (Join-Path $BackupDir $entry.Backup) -Destination $entry.Path -Force }
                else { Remove-Item -LiteralPath $entry.Path -Force -ErrorAction Stop }
            }
            catch [System.Management.Automation.ItemNotFoundException] { }
            catch { $rollbackErrors.Add("Shortcut rollback failed for '$($entry.Path)': $($_.Exception.Message)") }
        }
        try { if (Test-Path -LiteralPath $manifestPath) { Remove-Item -LiteralPath $manifestPath -Force } }
        catch { $rollbackErrors.Add("Manifest rollback failed: $($_.Exception.Message)") }
        try {
            if (-not [string]::IsNullOrWhiteSpace($StatePointerPath) -and (Test-Path -LiteralPath $StatePointerPath -PathType Leaf)) {
                $publishedPath = (Get-Content -LiteralPath $StatePointerPath -TotalCount 1).Trim()
                if ($publishedPath -and [System.IO.Path]::GetFullPath($publishedPath).Equals([System.IO.Path]::GetFullPath($BackupDir), [System.StringComparison]::OrdinalIgnoreCase)) {
                    Remove-Item -LiteralPath $StatePointerPath -Force
                }
            }
        }
        catch { $rollbackErrors.Add("Pointer rollback failed: $($_.Exception.Message)") }
        if ($rollbackErrors.Count -gt 0) { throw "Entry-point install transaction failed and rollback is INCOMPLETE. Original error: $($failure.Exception.Message) Rollback errors: $($rollbackErrors -join '; ')" }
        throw "Entry-point install transaction failed and was rolled back: $($failure.Exception.Message)"
    }
    [pscustomobject]@{ Outcome = 'Installed'; Launcher = $launcher; ShortcutCount = $paths.Count; BackupDir = [System.IO.Path]::GetFullPath($BackupDir) }
}

function Restore-BillfishEntryPointsLegacy {
    [CmdletBinding(PositionalBinding = $false)]
    param(
        [Parameter(Mandatory = $true)][string]$BackupDir,
        [string]$FixtureRoot,
        [string]$StatePointerPath,
        [switch]$DryRun,
        [ValidateSet('', 'Shortcut', 'Run', 'Pointer')][string]$FailStage = '',
        [int]$FailShortcutIndex = 1
    )
    $manifestPath = Join-Path $BackupDir 'manifest.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -ErrorAction Stop | ConvertFrom-Json
    if ([int]$manifest.schemaVersion -ne 2) { throw "Unsupported entry-point manifest schema: $($manifest.schemaVersion)" }
    $context = Get-BillfishEntryPointContext -FixtureRoot $FixtureRoot
    foreach ($entry in @($manifest.shortcuts)) {
        if ($entry.existed -and -not (Test-Path -LiteralPath (Join-Path $BackupDir ([string]$entry.backup)) -PathType Leaf)) { throw "Shortcut backup is missing for $($entry.path)." }
    }
    $activeBackup = Get-ActiveBillfishEntryPointBackup -StatePointerPath $StatePointerPath
    if ($null -ne $activeBackup -and -not $activeBackup.Equals([System.IO.Path]::GetFullPath($BackupDir), [System.StringComparison]::OrdinalIgnoreCase)) { throw "Active pointer refers to a different backup: $activeBackup" }
    if ($DryRun) { return [pscustomobject]@{ Outcome = 'WouldRestore'; ShortcutCount = @($manifest.shortcuts).Count } }

    $rollbackDir = Join-Path $BackupDir ('.restore-rollback-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $rollbackDir | Out-Null
    $rollbackEntries = foreach ($entry in @($manifest.shortcuts)) {
        $exists = Test-Path -LiteralPath ([string]$entry.path) -PathType Leaf
        $backupName = if ($exists) { [guid]::NewGuid().ToString('N') + '.lnk' } else { $null }
        if ($exists) { Copy-Item -LiteralPath ([string]$entry.path) -Destination (Join-Path $rollbackDir $backupName) }
        [pscustomobject]@{ Path = [string]$entry.path; Existed = $exists; Backup = $backupName }
    }
    $currentRun = Get-BillfishRunValue -Context $context
    $changed = [System.Collections.Generic.List[object]]::new()
    $runChanged = $false
    $tombstone = if ($StatePointerPath) { $StatePointerPath + '.restored-' + [guid]::NewGuid().ToString('N') } else { $null }
    $pointerMoved = $false
    try {
        $index = 0
        foreach ($entry in @($manifest.shortcuts)) {
            $index++
            $changed.Add($rollbackEntries[$index - 1])
            if ($FailStage -eq 'Shortcut' -and $index -eq $FailShortcutIndex) { throw "Injected restore shortcut failure at index $index." }
            if ($entry.existed) {
                $parent = Split-Path -Parent ([string]$entry.path)
                if (-not (Test-Path -LiteralPath $parent -PathType Container)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
                Copy-Item -LiteralPath (Join-Path $BackupDir ([string]$entry.backup)) -Destination ([string]$entry.path) -Force
            }
            else { Remove-Item -LiteralPath ([string]$entry.path) -Force -ErrorAction SilentlyContinue }
        }
        if ($FailStage -eq 'Run') { throw 'Injected restore Run failure.' }
        $runChanged = $true
        Set-BillfishRunValue -Context $context -Exists ([bool]$manifest.run.existed) -Value ([string]$manifest.run.value)
        if ($FailStage -eq 'Pointer') { throw 'Injected restore pointer failure.' }
        if ($StatePointerPath -and (Test-Path -LiteralPath $StatePointerPath -PathType Leaf)) { Move-Item -LiteralPath $StatePointerPath -Destination $tombstone; $pointerMoved = $true }
    }
    catch {
        $failure = $_
        $rollbackErrors = [System.Collections.Generic.List[string]]::new()
        if ($pointerMoved) {
            try { Move-Item -LiteralPath $tombstone -Destination $StatePointerPath -Force }
            catch { $rollbackErrors.Add("Pointer rollback failed: $($_.Exception.Message)") }
        }
        if ($runChanged) {
            try { Set-BillfishRunValue -Context $context -Exists $currentRun.Exists -Value $currentRun.Value }
            catch { $rollbackErrors.Add("Run rollback failed: $($_.Exception.Message)") }
        }
        $rollbackItems = @($changed)
        [array]::Reverse($rollbackItems)
        foreach ($entry in $rollbackItems) {
            try {
                if ($entry.Existed) { Copy-Item -LiteralPath (Join-Path $rollbackDir $entry.Backup) -Destination $entry.Path -Force }
                else { Remove-Item -LiteralPath $entry.Path -Force -ErrorAction Stop }
            }
            catch [System.Management.Automation.ItemNotFoundException] { }
            catch { $rollbackErrors.Add("Shortcut rollback failed for '$($entry.Path)': $($_.Exception.Message)") }
        }
        if ($rollbackErrors.Count -gt 0) { throw "Entry-point restore transaction failed and rollback is INCOMPLETE. Original error: $($failure.Exception.Message) Rollback errors: $($rollbackErrors -join '; ')" }
        throw "Entry-point restore transaction failed and was rolled back: $($failure.Exception.Message)"
    }
    finally {
        if (Test-Path -LiteralPath $rollbackDir) { Remove-Item -LiteralPath $rollbackDir -Recurse -Force -ErrorAction SilentlyContinue }
    }
    if ($pointerMoved -and (Test-Path -LiteralPath $tombstone)) { Remove-Item -LiteralPath $tombstone -Force -ErrorAction SilentlyContinue }
    [pscustomobject]@{ Outcome = 'Restored'; ShortcutCount = @($manifest.shortcuts).Count }
}

# Transactional v3 overrides are kept separate so the entry-point discovery and
# COM/fixture adapters above remain independently testable.
. (Join-Path $PSScriptRoot 'EntryPoints.Transactions.ps1')
