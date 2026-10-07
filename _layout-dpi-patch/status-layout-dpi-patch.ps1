. "$PSScriptRoot\BillfishLayoutDpiPatch.ps1"
$status = Test-BillfishLayoutPatchStatus
$status | Format-List
if (-not $status.PatchInstalled) {
    exit 1
}
