[CmdletBinding(SupportsShouldProcess = $true)]
param()

. "$PSScriptRoot\BillfishLayoutDpiPatch.ps1"
Install-BillfishLayoutPatch
Test-BillfishLayoutPatchStatus | Format-List
