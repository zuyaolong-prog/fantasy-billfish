[CmdletBinding()]
param(
    [ValidateSet('Standard', 'DpiRoundPreferFloor', 'DpiUnaware', 'SoftwareRender', 'NoHighDpi')]
    [string]$Profile = 'Standard'
)

. "$PSScriptRoot\BillfishLayoutDpiPatch.ps1"
Start-BillfishLayoutPatchLauncher -Profile $Profile
