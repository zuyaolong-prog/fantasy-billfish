[CmdletBinding()]
param()

$BillfishRoot = Split-Path -Parent $PSScriptRoot
$UnifiedLauncher = Join-Path $BillfishRoot 'start-billfish.ps1'

if (Test-Path -LiteralPath $UnifiedLauncher) {
    & $UnifiedLauncher
    exit $LASTEXITCODE
}

. "$PSScriptRoot\BillfishLayoutDpiPatch.ps1"
Start-BillfishNormalLauncher
