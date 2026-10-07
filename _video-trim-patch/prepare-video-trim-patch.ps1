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
    # Windows PowerShell 5.1 propagates WhatIfPreference into nested provider
    # reads. Disable only that ambient propagation during the read-only preflight;
    # the explicit ShouldProcess call below still honors the caller's -WhatIf.
    $savedWhatIfPreference = $WhatIfPreference
    $WhatIfPreference = $false
    try {
        $plan = Publish-VideoTrimPreparedPackageCore -PatchRoot $patchRoot -BillfishRoot $BillfishRoot -DryRun
    }
    finally {
        $WhatIfPreference = $savedWhatIfPreference
    }

    if ($DryRun) {
        $plan
        exit 0
    }

    if (-not $PSCmdlet.ShouldProcess($plan.destination, 'Publish prepared preview.dll and manifest atomically')) {
        $plan
        exit 0
    }

    $operationLock = Enter-VideoTrimOperationLock -PatchRoot $patchRoot
    try {
        Publish-VideoTrimPreparedPackageCore -PatchRoot $patchRoot -BillfishRoot $BillfishRoot
    }
    finally {
        Exit-VideoTrimOperationLock -Mutex $operationLock
    }
}
catch {
    Write-Error -ErrorRecord $_
    exit 1
}
