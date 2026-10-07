[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [string] $BillfishRoot,

    [switch] $DryRun
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
    $savedWhatIfPreference = $WhatIfPreference
    $WhatIfPreference = $false
    try {
        $plan = Restore-VideoTrimPatchCore -PatchRoot $patchRoot -BillfishRoot $BillfishRoot -DryRun
    }
    finally {
        $WhatIfPreference = $savedWhatIfPreference
    }

    if ($DryRun -or $plan.outcome -eq 'NotInstalled' -or $plan.outcome -eq 'BlockedByRunningBillfish') {
        $plan
        if ($plan.outcome -eq 'BlockedByRunningBillfish') {
            exit 2
        }

        exit 0
    }

    if (-not $PSCmdlet.ShouldProcess($plan.target, 'Atomically restore the stock preview.dll')) {
        $plan
        exit 0
    }

    $operationLock = Enter-VideoTrimOperationLock -PatchRoot $patchRoot
    try {
        Restore-VideoTrimPatchCore -PatchRoot $patchRoot -BillfishRoot $BillfishRoot
    }
    finally {
        Exit-VideoTrimOperationLock -Mutex $operationLock
    }
}
catch {
    Write-Error -ErrorRecord $_
    exit 1
}
