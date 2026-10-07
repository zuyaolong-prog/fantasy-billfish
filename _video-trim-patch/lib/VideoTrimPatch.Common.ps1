Set-StrictMode -Version Latest

$script:VideoTrimPatchSchemaVersion = 1
$script:VideoTrimPatchId = 'billfish-video-trim-preview'
$script:VideoTrimTargetRelativePath = 'ui_plugins\preview.dll'
$script:VideoTrimRuntimeStockRelativePath = 'ui_plugins\preview.stock.bin'
$script:VideoTrimPreparedPayloadRelativePath = 'prepared\preview.dll'
$script:VideoTrimPreparedManifestRelativePath = 'prepared\manifest.json'
$script:VideoTrimInstallationStateRelativePath = 'state\installation.json'

function ConvertTo-VideoTrimNormalizedHash {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Hash,

        [string] $Context = 'SHA-256'
    )

    $normalized = $Hash.Trim().ToUpperInvariant()
    if ($normalized -notmatch '^[0-9A-F]{64}$') {
        throw "$Context must be a 64-character SHA-256 value."
    }

    return $normalized
}

function ConvertTo-VideoTrimSafeRelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string] $RelativePath,

        [string] $Context = 'Relative path'
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        throw "$Context must not be empty."
    }

    $normalized = $RelativePath.Replace('/', '\')
    if ([System.IO.Path]::IsPathRooted($normalized) -or $normalized.Contains(':')) {
        throw "$Context must be relative: $RelativePath"
    }

    $segments = $normalized.Split([char]'\')
    foreach ($segment in $segments) {
        if ([string]::IsNullOrWhiteSpace($segment) -or $segment -eq '.' -or $segment -eq '..') {
            throw "$Context contains an unsafe path segment: $RelativePath"
        }
    }

    return [string]::Join('\', $segments)
}

function Resolve-VideoTrimContainedPath {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Root,

        [Parameter(Mandatory = $true)]
        [string] $RelativePath,

        [string] $Context = 'Relative path'
    )

    $safeRelativePath = ConvertTo-VideoTrimSafeRelativePath -RelativePath $RelativePath -Context $Context
    $rootFullPath = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $candidatePath = [System.IO.Path]::GetFullPath((Join-Path -Path $rootFullPath -ChildPath $safeRelativePath))
    $requiredPrefix = $rootFullPath + [System.IO.Path]::DirectorySeparatorChar

    if (-not $candidatePath.StartsWith($requiredPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context escapes its root: $RelativePath"
    }

    return $candidatePath
}

function Get-VideoTrimRequiredProperty {
    param(
        [Parameter(Mandatory = $true)]
        [object] $InputObject,

        [Parameter(Mandatory = $true)]
        [string] $Name,

        [string] $Context = 'Object'
    )

    if ($null -eq $InputObject) {
        throw "$Context is null; required property '$Name' is unavailable."
    }

    if ($InputObject -is [System.Collections.IDictionary] -and $InputObject.Contains($Name)) {
        return $InputObject[$Name]
    }

    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $property) {
        throw "$Context is missing required property '$Name'."
    }

    return $property.Value
}

function Get-VideoTrimFileFingerprint {
    param(
        [Parameter(Mandatory = $true)]
        [string] $LiteralPath
    )

    $file = Get-Item -LiteralPath $LiteralPath -ErrorAction Stop
    if ($file.PSIsContainer) {
        throw "Expected a file but found a directory: $LiteralPath"
    }

    $hash = Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256 -ErrorAction Stop
    return [pscustomobject]@{
        sha256 = (ConvertTo-VideoTrimNormalizedHash -Hash $hash.Hash -Context "Hash for $LiteralPath")
        size = [int64] $file.Length
    }
}

function Test-VideoTrimFingerprintEqual {
    param(
        [Parameter(Mandatory = $true)]
        [object] $Actual,

        [Parameter(Mandatory = $true)]
        [object] $Expected
    )

    $actualHash = ConvertTo-VideoTrimNormalizedHash -Hash ([string] (Get-VideoTrimRequiredProperty -InputObject $Actual -Name 'sha256' -Context 'Actual fingerprint'))
    $expectedHash = ConvertTo-VideoTrimNormalizedHash -Hash ([string] (Get-VideoTrimRequiredProperty -InputObject $Expected -Name 'sha256' -Context 'Expected fingerprint'))
    $actualSize = [int64] (Get-VideoTrimRequiredProperty -InputObject $Actual -Name 'size' -Context 'Actual fingerprint')
    $expectedSize = [int64] (Get-VideoTrimRequiredProperty -InputObject $Expected -Name 'size' -Context 'Expected fingerprint')

    return ($actualHash -eq $expectedHash -and $actualSize -eq $expectedSize)
}

function Read-VideoTrimJsonFile {
    param(
        [Parameter(Mandatory = $true)]
        [string] $LiteralPath,

        [string] $Context = 'JSON file'
    )

    try {
        $text = Get-Content -LiteralPath $LiteralPath -Raw -ErrorAction Stop
        return ($text | ConvertFrom-Json -ErrorAction Stop)
    }
    catch {
        throw "Unable to read $Context '$LiteralPath': $($_.Exception.Message)"
    }
}

function Write-VideoTrimUtf8Json {
    param(
        [Parameter(Mandatory = $true)]
        [object] $InputObject,

        [Parameter(Mandatory = $true)]
        [string] $LiteralPath
    )

    $parentDirectory = Split-Path -Parent $LiteralPath
    if (-not (Test-Path -LiteralPath $parentDirectory -PathType Container)) {
        New-Item -ItemType Directory -Path $parentDirectory -Force -ErrorAction Stop | Out-Null
    }

    $json = $InputObject | ConvertTo-Json -Depth 10
    $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($LiteralPath, ($json + [Environment]::NewLine), $utf8WithoutBom)
}

function Read-VideoTrimOriginalAllowlist {
    param(
        [Parameter(Mandatory = $true)]
        [string] $PatchRoot
    )

    $allowlistPath = Resolve-VideoTrimContainedPath -Root $PatchRoot -RelativePath 'lib\preview-original-hashes.json' -Context 'Allowlist path'
    $allowlist = Read-VideoTrimJsonFile -LiteralPath $allowlistPath -Context 'preview.dll allowlist'
    $schemaVersion = [int] (Get-VideoTrimRequiredProperty -InputObject $allowlist -Name 'schemaVersion' -Context 'Allowlist')
    if ($schemaVersion -ne $script:VideoTrimPatchSchemaVersion) {
        throw "Unsupported allowlist schemaVersion '$schemaVersion'."
    }

    $targetRelativePath = ConvertTo-VideoTrimSafeRelativePath -RelativePath ([string] (Get-VideoTrimRequiredProperty -InputObject $allowlist -Name 'targetRelativePath' -Context 'Allowlist')) -Context 'Allowlist targetRelativePath'
    if ($targetRelativePath -ne $script:VideoTrimTargetRelativePath) {
        throw "Allowlist targetRelativePath must be '$script:VideoTrimTargetRelativePath'."
    }

    $entries = @(Get-VideoTrimRequiredProperty -InputObject $allowlist -Name 'entries' -Context 'Allowlist')
    if ($entries.Count -eq 0) {
        throw 'The preview.dll allowlist is empty.'
    }

    foreach ($entry in $entries) {
        [void] (ConvertTo-VideoTrimNormalizedHash -Hash ([string] (Get-VideoTrimRequiredProperty -InputObject $entry -Name 'sha256' -Context 'Allowlist entry')) -Context 'Allowlist SHA-256')
        $size = [int64] (Get-VideoTrimRequiredProperty -InputObject $entry -Name 'size' -Context 'Allowlist entry')
        if ($size -le 0) {
            throw 'Allowlist entry size must be greater than zero.'
        }

        foreach ($metadataName in @('pluginVersion', 'qtAbi')) {
            $metadataValue = [string] (Get-VideoTrimRequiredProperty -InputObject $entry -Name $metadataName -Context 'Allowlist entry')
            if ([string]::IsNullOrWhiteSpace($metadataValue)) {
                throw "Allowlist entry $metadataName must not be empty."
            }
        }
    }

    return $allowlist
}

function Find-VideoTrimAllowedOriginal {
    param(
        [Parameter(Mandatory = $true)]
        [object] $Allowlist,

        [Parameter(Mandatory = $true)]
        [object] $Fingerprint,

        [string] $PluginVersion,

        [string] $QtAbi
    )

    $hash = ConvertTo-VideoTrimNormalizedHash -Hash ([string] (Get-VideoTrimRequiredProperty -InputObject $Fingerprint -Name 'sha256' -Context 'Original fingerprint'))
    $size = [int64] (Get-VideoTrimRequiredProperty -InputObject $Fingerprint -Name 'size' -Context 'Original fingerprint')

    foreach ($entry in @(Get-VideoTrimRequiredProperty -InputObject $Allowlist -Name 'entries' -Context 'Allowlist')) {
        $entryHash = ConvertTo-VideoTrimNormalizedHash -Hash ([string] $entry.sha256)
        $entrySize = [int64] $entry.size
        if ($entryHash -ne $hash -or $entrySize -ne $size) {
            continue
        }

        if (-not [string]::IsNullOrWhiteSpace($PluginVersion) -and [string] $entry.pluginVersion -ne $PluginVersion) {
            continue
        }

        if (-not [string]::IsNullOrWhiteSpace($QtAbi) -and [string] $entry.qtAbi -ne $QtAbi) {
            continue
        }

        return $entry
    }

    return $null
}

function Assert-VideoTrimAllowedOriginal {
    param(
        [Parameter(Mandatory = $true)]
        [object] $Allowlist,

        [Parameter(Mandatory = $true)]
        [object] $Fingerprint,

        [string] $PluginVersion,

        [string] $QtAbi,

        [string] $Context = 'preview.dll'
    )

    $entry = Find-VideoTrimAllowedOriginal -Allowlist $Allowlist -Fingerprint $Fingerprint -PluginVersion $PluginVersion -QtAbi $QtAbi
    if ($null -eq $entry) {
        $hash = [string] $Fingerprint.sha256
        $size = [int64] $Fingerprint.size
        throw "$Context fingerprint is not allowlisted (SHA-256 $hash, size $size). Refusing to modify an unknown build."
    }

    return $entry
}

function Assert-VideoTrimDescriptor {
    param(
        [Parameter(Mandatory = $true)]
        [object] $Descriptor,

        [string] $Context = 'Prepared manifest'
    )

    $schemaVersion = [int] (Get-VideoTrimRequiredProperty -InputObject $Descriptor -Name 'schemaVersion' -Context $Context)
    if ($schemaVersion -ne $script:VideoTrimPatchSchemaVersion) {
        throw "$Context has unsupported schemaVersion '$schemaVersion'."
    }

    $patchId = [string] (Get-VideoTrimRequiredProperty -InputObject $Descriptor -Name 'patchId' -Context $Context)
    if ($patchId -ne $script:VideoTrimPatchId) {
        throw "$Context patchId must be '$script:VideoTrimPatchId'."
    }

    foreach ($metadataName in @('pluginVersion', 'qtAbi', 'createdUtc')) {
        $metadataValue = [string] (Get-VideoTrimRequiredProperty -InputObject $Descriptor -Name $metadataName -Context $Context)
        if ([string]::IsNullOrWhiteSpace($metadataValue)) {
            throw "$Context property '$metadataName' must not be empty."
        }
    }

    $createdUtcValue = [datetime]::MinValue
    if (-not [datetime]::TryParse([string] $Descriptor.createdUtc, [ref] $createdUtcValue)) {
        throw "$Context createdUtc is not a valid timestamp."
    }

    $target = Get-VideoTrimRequiredProperty -InputObject $Descriptor -Name 'target' -Context $Context
    $targetRelativePath = ConvertTo-VideoTrimSafeRelativePath -RelativePath ([string] (Get-VideoTrimRequiredProperty -InputObject $target -Name 'relativePath' -Context "$Context target")) -Context "$Context target.relativePath"
    if ($targetRelativePath -ne $script:VideoTrimTargetRelativePath) {
        throw "$Context target.relativePath must be '$script:VideoTrimTargetRelativePath'."
    }

    foreach ($fingerprintName in @('original', 'patched')) {
        $fingerprint = Get-VideoTrimRequiredProperty -InputObject $Descriptor -Name $fingerprintName -Context $Context
        [void] (ConvertTo-VideoTrimNormalizedHash -Hash ([string] (Get-VideoTrimRequiredProperty -InputObject $fingerprint -Name 'sha256' -Context "$Context $fingerprintName")) -Context "$Context $fingerprintName SHA-256")
        $size = [int64] (Get-VideoTrimRequiredProperty -InputObject $fingerprint -Name 'size' -Context "$Context $fingerprintName")
        if ($size -le 0) {
            throw "$Context $fingerprintName size must be greater than zero."
        }
    }

    if (Test-VideoTrimFingerprintEqual -Actual $Descriptor.original -Expected $Descriptor.patched) {
        throw "$Context original and patched fingerprints must differ."
    }

    $stockBackupRelativePath = ConvertTo-VideoTrimSafeRelativePath -RelativePath ([string] (Get-VideoTrimRequiredProperty -InputObject $Descriptor -Name 'stockBackupRelativePath' -Context $Context)) -Context "$Context stockBackupRelativePath"
    if ($stockBackupRelativePath -ne $script:VideoTrimRuntimeStockRelativePath) {
        throw "$Context stockBackupRelativePath must be '$script:VideoTrimRuntimeStockRelativePath'."
    }

    $recoveryBackupRelativePath = ConvertTo-VideoTrimSafeRelativePath -RelativePath ([string] (Get-VideoTrimRequiredProperty -InputObject $Descriptor -Name 'recoveryBackupRelativePath' -Context $Context)) -Context "$Context recoveryBackupRelativePath"
    if (-not $recoveryBackupRelativePath.StartsWith('backups\stock\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context recoveryBackupRelativePath must be under 'backups\stock'."
    }

    return $Descriptor
}

function Read-VideoTrimPreparedPackage {
    param(
        [Parameter(Mandatory = $true)]
        [string] $PatchRoot
    )

    $manifestPath = Resolve-VideoTrimContainedPath -Root $PatchRoot -RelativePath $script:VideoTrimPreparedManifestRelativePath -Context 'Prepared manifest path'
    $payloadPath = Resolve-VideoTrimContainedPath -Root $PatchRoot -RelativePath $script:VideoTrimPreparedPayloadRelativePath -Context 'Prepared payload path'
    $manifest = Read-VideoTrimJsonFile -LiteralPath $manifestPath -Context 'prepared manifest'
    [void] (Assert-VideoTrimDescriptor -Descriptor $manifest -Context 'Prepared manifest')

    $payloadFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $payloadPath
    if (-not (Test-VideoTrimFingerprintEqual -Actual $payloadFingerprint -Expected $manifest.patched)) {
        throw "Prepared payload fingerprint does not match manifest: $payloadPath"
    }

    $manifestFingerprint = Get-VideoTrimFileFingerprint -LiteralPath $manifestPath
    return [pscustomobject]@{
        manifest = $manifest
        manifestPath = $manifestPath
        manifestSha256 = $manifestFingerprint.sha256
        payloadPath = $payloadPath
    }
}

function Read-VideoTrimInstallationState {
    param(
        [Parameter(Mandatory = $true)]
        [string] $PatchRoot
    )

    $statePath = Resolve-VideoTrimContainedPath -Root $PatchRoot -RelativePath $script:VideoTrimInstallationStateRelativePath -Context 'Installation state path'
    $state = Read-VideoTrimJsonFile -LiteralPath $statePath -Context 'installation state'
    [void] (Assert-VideoTrimInstallationStateObject -State $state -Context 'Installation state')
    return $state
}

function Assert-VideoTrimInstallationStateObject {
    param(
        [Parameter(Mandatory = $true)]
        [object] $State,

        [string] $Context = 'Installation state'
    )

    [void] (Assert-VideoTrimDescriptor -Descriptor $State -Context $Context)

    foreach ($name in @('transactionId', 'installedUtc', 'preparedManifestSha256')) {
        $value = [string] (Get-VideoTrimRequiredProperty -InputObject $State -Name $name -Context $Context)
        if ([string]::IsNullOrWhiteSpace($value)) {
            throw "$Context property '$name' must not be empty."
        }
    }

    $transactionGuid = [guid]::Empty
    if (-not [guid]::TryParse([string] $State.transactionId, [ref] $transactionGuid)) {
        throw "$Context transactionId is not a valid GUID."
    }

    $installedUtcValue = [datetime]::MinValue
    if (-not [datetime]::TryParse([string] $State.installedUtc, [ref] $installedUtcValue)) {
        throw "$Context installedUtc is not a valid timestamp."
    }

    [void] (ConvertTo-VideoTrimNormalizedHash -Hash ([string] $State.preparedManifestSha256) -Context "$Context preparedManifestSha256")
    return $State
}

function Get-VideoTrimBillfishProcess {
    param(
        [Parameter(Mandatory = $true)]
        [string] $BillfishRoot
    )

    $expectedExecutables = @(
        'Billfish.exe',
        'Billfish.core.exe',
        'PluginWorker.exe',
        'PluginWorker64.exe'
    ) | ForEach-Object { [System.IO.Path]::GetFullPath((Join-Path -Path $BillfishRoot -ChildPath $_)) }
    $matchingProcesses = @()
    foreach ($process in @(Get-Process -Name 'Billfish','Billfish.core','PluginWorker','PluginWorker64' -ErrorAction SilentlyContinue)) {
        $processPath = $null
        try {
            $processPath = $process.Path
        }
        catch {
            $processPath = $null
        }

        if ([string]::IsNullOrWhiteSpace($processPath)) {
            $matchingProcesses += $process
            continue
        }

        $processFullPath = [System.IO.Path]::GetFullPath($processPath)
        if (@($expectedExecutables | Where-Object { $_.Equals($processFullPath, [System.StringComparison]::OrdinalIgnoreCase) }).Count -gt 0) {
            $matchingProcesses += $process
        }
    }

    return @($matchingProcesses)
}

function Assert-VideoTrimBillfishStopped {
    param(
        [Parameter(Mandatory = $true)]
        [string] $BillfishRoot
    )

    $running = @(Get-VideoTrimBillfishProcess -BillfishRoot $BillfishRoot)
    if ($running.Count -gt 0) {
        $processIds = ($running | ForEach-Object { $_.Id }) -join ', '
        throw "Billfish is running (PID: $processIds). Close it before installing or restoring the patch."
    }
}

function Enter-VideoTrimOperationLock {
    param(
        [Parameter(Mandatory = $true)]
        [string] $PatchRoot
    )

    $canonicalRoot = [System.IO.Path]::GetFullPath($PatchRoot).TrimEnd('\', '/').ToUpperInvariant()
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $rootBytes = [System.Text.Encoding]::UTF8.GetBytes($canonicalRoot)
        $nameHash = ([System.BitConverter]::ToString($sha256.ComputeHash($rootBytes))).Replace('-', '')
    }
    finally {
        $sha256.Dispose()
    }

    $mutexName = 'Local\BillfishVideoTrimPatch-' + $nameHash.Substring(0, 32)
    $mutex = New-Object System.Threading.Mutex($false, $mutexName)
    $acquired = $false
    try {
        $acquired = $mutex.WaitOne(0)
    }
    catch [System.Threading.AbandonedMutexException] {
        $acquired = $true
    }

    if (-not $acquired) {
        $mutex.Dispose()
        throw 'Another video trim patch operation is already running.'
    }

    return $mutex
}

function Exit-VideoTrimOperationLock {
    param(
        [Parameter(Mandatory = $true)]
        [System.Threading.Mutex] $Mutex
    )

    try {
        $Mutex.ReleaseMutex()
    }
    finally {
        $Mutex.Dispose()
    }
}

function Get-VideoTrimRuntimeInfo {
    param(
        [Parameter(Mandatory = $true)]
        [string] $BillfishRoot,

        [Parameter(Mandatory = $true)]
        [object] $AllowlistEntry
    )

    $pluginVersion = [string] $AllowlistEntry.pluginVersion
    $qtAbi = [string] $AllowlistEntry.qtAbi

    $billfishExecutable = Join-Path -Path $BillfishRoot -ChildPath 'Billfish.core.exe'
    if (Test-Path -LiteralPath $billfishExecutable -PathType Leaf) {
        $runtimeVersion = [string] (Get-Item -LiteralPath $billfishExecutable).VersionInfo.ProductVersion
        if (-not [string]::IsNullOrWhiteSpace($runtimeVersion) -and $runtimeVersion -ne $pluginVersion) {
            throw "Billfish.core.exe version '$runtimeVersion' does not match allowlist pluginVersion '$pluginVersion'."
        }
    }

    $qtCorePath = Join-Path -Path $BillfishRoot -ChildPath 'Qt5Core.dll'
    if (Test-Path -LiteralPath $qtCorePath -PathType Leaf) {
        $qtVersion = [string] (Get-Item -LiteralPath $qtCorePath).VersionInfo.ProductVersion
        if (-not [string]::IsNullOrWhiteSpace($qtVersion)) {
            $parsedQtVersion = [version] $qtVersion
            $expectedQtPrefix = 'Qt {0}.{1}.{2}' -f $parsedQtVersion.Major, $parsedQtVersion.Minor, $parsedQtVersion.Build
            if (-not $qtAbi.StartsWith($expectedQtPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Qt5Core.dll version '$qtVersion' does not match allowlist qtAbi '$qtAbi'."
            }
        }
    }

    return [pscustomobject]@{
        pluginVersion = $pluginVersion
        qtAbi = $qtAbi
    }
}
