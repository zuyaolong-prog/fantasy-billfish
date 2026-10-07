[CmdletBinding()]
param(
    [string] $BillfishRoot,

    [switch] $AsJson
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$patchRoot = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BillfishRoot)) {
    $BillfishRoot = Split-Path -Parent $patchRoot
}

. (Join-Path -Path $patchRoot -ChildPath 'lib\VideoTrimPatch.Common.ps1')
. (Join-Path -Path $patchRoot -ChildPath 'lib\VideoTrimPatch.Operations.ps1')

try {
    $status = Get-VideoTrimPatchStatusCore -PatchRoot $patchRoot -BillfishRoot $BillfishRoot
    if ($AsJson) {
        $status | ConvertTo-Json -Depth 6
    }
    else {
        $status | Format-List targetState, preparedState, installationState, runtimeStockState, recoveryBackupState, billfishRunning, readyForInstall, readyForRestore, healthy, targetPath, runtimeStockPath, recoveryBackupPath, errors
    }

    if ($status.healthy) {
        exit 0
    }

    exit 2
}
catch {
    Write-Error -ErrorRecord $_
    exit 1
}
