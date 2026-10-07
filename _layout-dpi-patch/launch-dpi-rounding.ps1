[CmdletBinding()]
param()

. "$PSScriptRoot\BillfishLayoutDpiPatch.ps1"
Start-BillfishLayoutPatchLauncher -Profile DpiRoundPreferFloor
