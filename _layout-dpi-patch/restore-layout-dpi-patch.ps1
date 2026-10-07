[CmdletBinding(SupportsShouldProcess = $true)]
param()

. "$PSScriptRoot\BillfishLayoutDpiPatch.ps1"
Restore-BillfishLayoutPatch
Test-BillfishLayoutPatchStatus | Format-List
